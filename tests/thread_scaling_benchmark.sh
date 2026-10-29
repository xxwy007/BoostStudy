#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_EXE="${1:-$PROJECT_DIR/out/build/x64-Debug/task_server.exe}"
REQUEST_COUNT="${2:-100}"
PORT="${3:-18080}"
ADDRESS="127.0.0.1"
BASE_URL="http://$ADDRESS:$PORT"

fail() {
    echo "Benchmark failed: $1" >&2
    exit 1
}

[[ -f "$SERVER_EXE" ]] || fail "server executable not found: $SERVER_EXE"
[[ "$REQUEST_COUNT" =~ ^[1-9][0-9]*$ ]] \
    || fail "request count must be a positive integer"
[[ "$PORT" =~ ^[1-9][0-9]*$ ]] && ((PORT <= 65535)) \
    || fail "port must be between 1 and 65535"
command -v curl >/dev/null 2>&1 || fail "curl is required"

TEMP_DIR="$(mktemp -d)"
RESULTS_FILE="$TEMP_DIR/results.txt"
SERVER_PID=""

stop_server() {
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    SERVER_PID=""
}

cleanup() {
    stop_server
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT INT TERM

if curl --silent --fail --max-time 1 "$BASE_URL/health" >/dev/null 2>&1; then
    fail "port $PORT is already serving HTTP; choose another port"
fi

: >"$RESULTS_FILE"
for threads in 1 2 4; do
    stdout_file="$TEMP_DIR/server-$threads.out.log"
    stderr_file="$TEMP_DIR/server-$threads.err.log"

    echo
    echo "Starting server with --threads $threads ..."
    "$SERVER_EXE" \
        --address "$ADDRESS" \
        --port "$PORT" \
        --threads "$threads" \
        >"$stdout_file" \
        2>"$stderr_file" &
    SERVER_PID=$!

    ready=false
    for ((attempt = 1; attempt <= 50; ++attempt)); do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            fail "server exited for threads=$threads: $(cat "$stderr_file")"
        fi
        if curl --silent --fail --max-time 1 "$BASE_URL/health" >/dev/null 2>&1; then
            ready=true
            break
        fi
        sleep 0.1
    done
    [[ "$ready" == true ]] \
        || fail "server did not become ready for threads=$threads"

    set +e
    probe_output="$(bash "$SCRIPT_DIR/concurrency_probe.sh" \
        "$BASE_URL" "$REQUEST_COUNT" "$threads" 2>&1)"
    probe_exit_code=$?
    set -e
    echo "$probe_output"
    if ((probe_exit_code != 0)); then
        echo "Server output:" >&2
        cat "$stdout_file" >&2
        echo "Server errors:" >&2
        cat "$stderr_file" >&2
        fail "probe failed for threads=$threads"
    fi

    result_line="$(grep '^RESULT ' <<<"$probe_output")"
    [[ -n "$result_line" ]] || fail "probe did not produce a RESULT line"
    printf '%s\n' "$result_line" >>"$RESULTS_FILE"

    stop_server
    sleep 0.2
done

echo
echo "Thread scaling comparison"
awk '
    BEGIN {
        printf "%-8s %-10s %-12s %-18s %-14s %-15s\n", \
               "threads", "requests", "elapsed_ms", "avg_latency_ms", \
               "max_latency", "throughput_rps"
    }
    {
        delete values
        for (field_number = 2; field_number <= NF; field_number++) {
            split($field_number, pair, "=")
            values[pair[1]] = pair[2]
        }
        printf "%-8s %-10s %-12s %-18s %-14s %-15s\n", \
               values["threads"], values["requests"], values["elapsed_ms"], \
               values["average_latency_ms"], values["max_latency_ms"], \
               values["throughput_rps"]
    }
' "$RESULTS_FILE"

echo
echo "All thread configurations preserved unique IDs and complete repository data."
