#!/usr/bin/env bash

set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8080}"
BODY_LIMIT="${2:-65536}"
BASE_URL="${BASE_URL%/}"

fail() {
    echo "Test failed: $1" >&2
    exit 1
}

command -v curl >/dev/null 2>&1 || fail "curl is required"
command -v grep >/dev/null 2>&1 || fail "grep is required"
[[ "$BODY_LIMIT" =~ ^[1-9][0-9]*$ ]] \
    || fail "body limit must be a positive integer"

if [[ "$BASE_URL" =~ ^http://([^/:]+)(:([0-9]+))?$ ]]; then
    SERVER_HOST="${BASH_REMATCH[1]}"
    SERVER_PORT="${BASH_REMATCH[3]:-80}"
    SERVER_AUTHORITY="${SERVER_HOST}:${SERVER_PORT}"
else
    fail "base URL must look like http://127.0.0.1:8080"
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

PASSED_CASES=0
CASE_INDEX=0

assert_api_error() {
    local name="$1"
    local method="$2"
    local path="$3"
    local expected_status="$4"
    local expected_code="$5"
    local content_type="${6:-}"
    local body="${7:-}"

    CASE_INDEX=$((CASE_INDEX + 1))
    local response_file="$TEMP_DIR/response-$CASE_INDEX.json"
    local error_file="$TEMP_DIR/error-$CASE_INDEX.txt"
    local payload_file="$TEMP_DIR/payload-$CASE_INDEX.json"
    local -a curl_args=(
        --silent
        --show-error
        --output "$response_file"
        --write-out '%{http_code}'
        --request "$method"
        "$BASE_URL$path"
    )

    if [[ -n "$content_type" ]]; then
        curl_args+=(--header "Content-Type: $content_type")
    fi
    if [[ -n "$body" ]]; then
        printf '%s' "$body" >"$payload_file"
        curl_args+=(--data-binary "@$payload_file")
    fi

    local status
    local exit_code
    set +e
    status="$(curl "${curl_args[@]}" 2>"$error_file")"
    exit_code=$?
    set -e

    if ((exit_code != 0)); then
        fail "$name curl failed: $(cat "$error_file")"
    fi
    if [[ "$status" != "$expected_status" ]]; then
        fail "$name expected HTTP $expected_status, got $status; body=$(cat "$response_file")"
    fi
    if ! grep -Eq \
        "\"error\"[[:space:]]*:[[:space:]]*\"${expected_code}\"" \
        "$response_file"; then
        fail "$name expected error '$expected_code'; body=$(cat "$response_file")"
    fi

    PASSED_CASES=$((PASSED_CASES + 1))
    echo "PASS  $name"
}

assert_api_error \
    "invalid JSON" "POST" "/tasks" "400" "invalid_json" \
    "application/json" '{"title":'

assert_api_error \
    "empty title" "POST" "/tasks" "400" "invalid_title" \
    "application/json" '{"title":""}'

printf -v long_title '%*s' 201 ''
long_title="${long_title// /x}"
assert_api_error \
    "title longer than 200 characters" "POST" "/tasks" "400" "invalid_title" \
    "application/json" "{\"title\":\"$long_title\"}"

assert_api_error \
    "title with wrong type" "POST" "/tasks" "400" "invalid_title" \
    "application/json" '{"title":123}'

assert_api_error \
    "completed with wrong type" "PUT" "/tasks/999999999" "400" \
    "invalid_completed" "application/json" '{"completed":"yes"}'

assert_api_error \
    "empty update" "PUT" "/tasks/999999999" "400" "empty_update" \
    "application/json" '{}'

for invalid_id in 0 abc 1abc; do
    assert_api_error \
        "invalid task id: $invalid_id" "GET" "/tasks/$invalid_id" "400" \
        "invalid_task_id"
done

MISSING_ID="999999999999999999"
for method in GET DELETE; do
    assert_api_error \
        "$method missing task" "$method" "/tasks/$MISSING_ID" "404" \
        "task_not_found"
done

assert_api_error \
    "PUT missing task" "PUT" "/tasks/$MISSING_ID" "404" "task_not_found" \
    "application/json" '{"completed":true}'

assert_api_error \
    "unsupported Content-Type" "POST" "/tasks" "415" \
    "unsupported_media_type" "text/plain" '{"title":"wrong content type"}'

assert_api_error \
    "method not allowed" "PATCH" "/tasks" "405" "method_not_allowed"

assert_api_error \
    "unknown route" "GET" "/unknown" "404" "route_not_found"

printf -v large_title '%*s' "$((BODY_LIMIT + 1024))" ''
large_title="${large_title// /x}"
large_payload_file="$TEMP_DIR/large-payload.json"
large_response_file="$TEMP_DIR/large-response.txt"
large_error_file="$TEMP_DIR/large-error.txt"
printf '{"title":"%s"}' "$large_title" >"$large_payload_file"
unset large_title

set +e
large_status="$(curl \
    --silent \
    --show-error \
    --output "$large_response_file" \
    --write-out '%{http_code}' \
    --request POST \
    --header 'Content-Type: application/json' \
    --data-binary "@$large_payload_file" \
    "$BASE_URL/tasks" \
    2>"$large_error_file")"
large_exit_code=$?
set -e

if [[ "$large_status" != "413" \
      && ("$large_status" != "000" || "$large_exit_code" -eq 0) ]]; then
    fail "oversized body should return 413 or close the connection; status=$large_status, exit=$large_exit_code"
fi
PASSED_CASES=$((PASSED_CASES + 1))
echo "PASS  request body larger than BodyLimit"

# 声明较大的 Content-Length，只发送部分正文，然后立即断开连接。
if exec 3<>"/dev/tcp/$SERVER_HOST/$SERVER_PORT"; then
    printf \
        'POST /tasks HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: 100\r\nConnection: close\r\n\r\n{"title":"partial' \
        "$SERVER_AUTHORITY" >&3
    exec 3>&-
else
    fail "could not open raw TCP connection to $SERVER_AUTHORITY"
fi

sleep 0.2
health="$(curl -fsS "$BASE_URL/health")" \
    || fail "server stopped after the client disconnected mid-request"
[[ "$health" == *'"status":"ok"'* ]] \
    || fail "unexpected health response after disconnect: $health"

PASSED_CASES=$((PASSED_CASES + 1))
echo "PASS  client disconnect during request"
echo "$PASSED_CASES error and boundary cases passed."
