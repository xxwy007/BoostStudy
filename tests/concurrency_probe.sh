#!/usr/bin/env bash

set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8080}"
REQUEST_COUNT="${2:-100}"
THREAD_LABEL="${3:-unknown}"
BASE_URL="${BASE_URL%/}"

fail() {
    echo "Probe failed: $1" >&2
    exit 1
}

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v awk >/dev/null 2>&1 || fail "awk is required"
command -v comm >/dev/null 2>&1 || fail "comm is required"
command -v grep >/dev/null 2>&1 || fail "grep is required"
[[ "$REQUEST_COUNT" =~ ^[1-9][0-9]*$ ]] || fail "request count must be a positive integer"

curl -fsS "$BASE_URL/health" >/dev/null || fail "server is not reachable"

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

for ((index = 1; index <= REQUEST_COUNT; ++index)); do
    printf '{"title":"concurrent-task-%d"}' "$index" \
        >"$TEMP_DIR/payload-$index.json"
done

echo "Sending $REQUEST_COUNT concurrent requests to $BASE_URL (threads=$THREAD_LABEL) ..."

START_NS="$(date +%s%N)"
[[ "$START_NS" =~ ^[0-9]+$ ]] || fail "date does not support nanosecond output"

pids=()
for ((index = 1; index <= REQUEST_COUNT; ++index)); do
    curl -fsS \
        --request POST \
        --header 'Content-Type: application/json' \
        --data-binary "@$TEMP_DIR/payload-$index.json" \
        --output "$TEMP_DIR/response-$index.json" \
        --write-out '%{time_total}' \
        "$BASE_URL/tasks" \
        >"$TEMP_DIR/time-$index.txt" \
        2>"$TEMP_DIR/error-$index.txt" &
    pids+=("$!")
done

failures=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        failures=$((failures + 1))
    fi
done

(( failures == 0 )) || fail "$failures request(s) failed"

END_NS="$(date +%s%N)"
[[ "$END_NS" =~ ^[0-9]+$ ]] || fail "date does not support nanosecond output"

CREATED_IDS="$TEMP_DIR/created-ids.txt"
: >"$CREATED_IDS"
for ((index = 1; index <= REQUEST_COUNT; ++index)); do
    response_file="$TEMP_DIR/response-$index.json"
    grep -Fq "\"title\":\"concurrent-task-$index\"" "$response_file" \
        || fail "response $index has an unexpected title: $(cat "$response_file")"
    grep -Fq '"completed":false' "$response_file" \
        || fail "response $index has an unexpected completed value: $(cat "$response_file")"

    task_id="$(grep -o '"id":[[:space:]]*[0-9][0-9]*' "$response_file" \
        | sed 's/.*://' \
        | tr -d ' ')"
    [[ -n "$task_id" ]] || fail "response $index has no task ID: $(cat "$response_file")"
    printf '%s\n' "$task_id" >>"$CREATED_IDS"
done

created_count="$(wc -l <"$CREATED_IDS" | tr -d ' ')"
sort -n -u "$CREATED_IDS" >"$TEMP_DIR/unique-created-ids.txt"
unique_count="$(wc -l <"$TEMP_DIR/unique-created-ids.txt" | tr -d ' ')"

[[ "$created_count" == "$REQUEST_COUNT" ]] \
    || fail "expected $REQUEST_COUNT responses, got $created_count"
[[ "$unique_count" == "$REQUEST_COUNT" ]] \
    || fail "task IDs are not unique"

curl -fsS "$BASE_URL/tasks" >"$TEMP_DIR/task-list.json" \
    || fail "could not fetch task list after concurrent creation"
grep -o '"id":[[:space:]]*[0-9][0-9]*' "$TEMP_DIR/task-list.json" \
    | sed 's/.*://' \
    | tr -d ' ' \
    | sort -n -u \
    >"$TEMP_DIR/listed-ids.txt"

comm -23 "$TEMP_DIR/unique-created-ids.txt" "$TEMP_DIR/listed-ids.txt" \
    >"$TEMP_DIR/missing-ids.txt"
[[ ! -s "$TEMP_DIR/missing-ids.txt" ]] \
    || fail "created task IDs are missing from the repository: $(tr '\n' ' ' <"$TEMP_DIR/missing-ids.txt")"

ELAPSED_MS=$(((END_NS - START_NS) / 1000000))
((ELAPSED_MS > 0)) || ELAPSED_MS=1
AVERAGE_LATENCY_MS="$(awk \
    '{ total += $1; count++ } END { printf "%.2f", (total * 1000) / count }' \
    "$TEMP_DIR"/time-*.txt)"
MAX_LATENCY_MS="$(awk \
    'BEGIN { max = 0 } { if ($1 > max) max = $1 } END { printf "%.2f", max * 1000 }' \
    "$TEMP_DIR"/time-*.txt)"
THROUGHPUT_RPS="$(awk \
    -v requests="$REQUEST_COUNT" \
    -v elapsed_ms="$ELAPSED_MS" \
    'BEGIN { printf "%.2f", requests * 1000 / elapsed_ms }')"

echo "Created $created_count tasks with $unique_count unique IDs; all are present in the repository."
echo "RESULT threads=$THREAD_LABEL requests=$REQUEST_COUNT elapsed_ms=$ELAPSED_MS average_latency_ms=$AVERAGE_LATENCY_MS max_latency_ms=$MAX_LATENCY_MS throughput_rps=$THROUGHPUT_RPS"

