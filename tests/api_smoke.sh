#!/usr/bin/env bash

set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8080}"

fail() {
    echo "Test failed: $1" >&2
    exit 1
}

command -v curl >/dev/null 2>&1 || fail "curl is required"

health="$(curl -fsS "$BASE_URL/health")"
[[ "$health" == *'"status":"ok"'* ]] || fail "health endpoint"

created="$(curl -fsS \
    -X POST "$BASE_URL/tasks" \
    -H 'Content-Type: application/json' \
    -d '{"title":"Learn Boost.Beast"}')"

task_id="$(printf '%s' "$created" | sed -n \
    's/.*"id":[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
[[ -n "$task_id" ]] || fail "created task has no id: $created"
[[ "$created" == *'"completed":false'* ]] || fail "new task should be incomplete"

task="$(curl -fsS "$BASE_URL/tasks/$task_id")"
[[ "$task" == *'"title":"Learn Boost.Beast"'* ]] || fail "task cannot be fetched"

updated="$(curl -fsS \
    -X PUT "$BASE_URL/tasks/$task_id" \
    -H 'Content-Type: application/json' \
    -d '{"completed":true}')"
[[ "$updated" == *'"completed":true'* ]] || fail "task was not completed"

tasks="$(curl -fsS "$BASE_URL/tasks")"
[[ "$tasks" == *"\"id\":$task_id"* ]] || fail "task is missing from task list"

delete_status="$(curl -sS \
    -o /dev/null \
    -w '%{http_code}' \
    -X DELETE "$BASE_URL/tasks/$task_id")"
[[ "$delete_status" == "204" ]] || fail "delete returned HTTP $delete_status"

missing_status="$(curl -sS \
    -o /dev/null \
    -w '%{http_code}' \
    "$BASE_URL/tasks/$task_id")"
[[ "$missing_status" == "404" ]] || fail "deleted task returned HTTP $missing_status"

stats="$(curl -fsS "$BASE_URL/stats")"
request_count="$(printf '%s' "$stats" | sed -n \
    's/.*"total_requests":[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
[[ -n "$request_count" ]] || fail "stats response has no request count: $stats"
(( request_count >= 8 )) || fail "request count is unexpectedly low: $request_count"

echo "All API smoke tests passed."

