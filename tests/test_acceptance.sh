#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 /path/to/url-shortener /path/to/curl /path/to/python3" >&2
    exit 2
fi

binary=$1
curl_binary=$2
python_binary=$3
if [ ! -x "$binary" ]; then
    echo "url-shortener binary is not executable: $binary" >&2
    exit 1
fi

test_dir=$(mktemp -d)
server_pid=
slow_client_pid=

cleanup() {
    if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
        kill -TERM "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    if [ -n "$slow_client_pid" ] && kill -0 "$slow_client_pid" 2>/dev/null; then
        kill -TERM "$slow_client_pid" 2>/dev/null || true
        wait "$slow_client_pid" 2>/dev/null || true
    fi
    rm -rf "$test_dir"
}
trap cleanup EXIT HUP INT TERM

start_server() {
    (
        cd "$test_dir"
        exec "$binary" >>server.log 2>&1
    ) &
    server_pid=$!

    attempts=0
    while [ "$attempts" -lt 50 ]; do
        if ! kill -0 "$server_pid" 2>/dev/null; then
            echo "server exited during startup" >&2
            cat "$test_dir/server.log" >&2
            exit 1
        fi
        if "$curl_binary" --silent --show-error --fail --max-time 1 \
            http://127.0.0.1:8080/health >/dev/null 2>&1; then
            return
        fi
        attempts=$((attempts + 1))
        sleep 0.1
    done

    echo "server did not become ready" >&2
    cat "$test_dir/server.log" >&2
    exit 1
}

stop_server() {
    kill -TERM "$server_pid"
    set +e
    wait "$server_pid"
    status=$?
    set -e
    server_pid=
    if [ "$status" -ne 0 ]; then
        echo "server did not shut down cleanly (status $status)" >&2
        cat "$test_dir/server.log" >&2
        exit 1
    fi
}

start_server

health_status=$("$curl_binary" --silent --show-error --max-time 2 \
    --dump-header "$test_dir/health.headers" \
    --output "$test_dir/health.body" --write-out '%{http_code}' \
    http://127.0.0.1:8080/health)
health_body=$(sed -n '1p' "$test_dir/health.body")
[ "$health_status" = "200" ] || {
    echo "unexpected health status: $health_status" >&2
    exit 1
}
[ "$health_body" = "OK" ] || {
    echo "unexpected health body: $health_body" >&2
    exit 1
}
tr -d '\r' <"$test_dir/health.headers" >"$test_dir/health.clean"
grep -qi '^Content-Type: text/plain$' "$test_dir/health.clean"
grep -qi '^Content-Length: 2$' "$test_dir/health.clean"
grep -qi '^Connection: close$' "$test_dir/health.clean"

root_status=$("$curl_binary" --silent --show-error --max-time 2 \
    --dump-header "$test_dir/root.headers" \
    --output "$test_dir/root.body" --write-out '%{http_code}' \
    http://127.0.0.1:8080/)
[ "$root_status" = "200" ] || {
    echo "unexpected root status: $root_status" >&2
    exit 1
}
tr -d '\r' <"$test_dir/root.headers" >"$test_dir/root.clean"
root_bytes=$(wc -c <"$test_dir/root.body" | tr -d ' ')
grep -qi '^Content-Type: text/html; charset=utf-8$' "$test_dir/root.clean"
grep -qi "^Content-Length: $root_bytes$" "$test_dir/root.clean"
grep -qi '^Connection: close$' "$test_dir/root.clean"
grep -q '<form id="shorten-form"' "$test_dir/root.body"
grep -q '<label for="original-url">Original URL</label>' "$test_dir/root.body"
grep -q 'aria-live="polite"' "$test_dir/root.body"
grep -q "fetch('/shorten'" "$test_dir/root.body"

root_query_status=$("$curl_binary" --silent --show-error --max-time 2 \
    --output "$test_dir/root-query.body" --write-out '%{http_code}' \
    'http://127.0.0.1:8080/?source=acceptance')
[ "$root_query_status" = "200" ] || {
    echo "unexpected root query status: $root_query_status" >&2
    exit 1
}
cmp "$test_dir/root.body" "$test_dir/root-query.body"

root_post_status=$("$curl_binary" --silent --show-error --max-time 2 \
    --request POST --dump-header "$test_dir/root-post.headers" \
    --output "$test_dir/root-post.body" --write-out '%{http_code}' \
    http://127.0.0.1:8080/)
[ "$root_post_status" = "405" ] || {
    echo "unexpected root POST status: $root_post_status" >&2
    exit 1
}
tr -d '\r' <"$test_dir/root-post.headers" >"$test_dir/root-post.clean"
grep -qi '^Allow: GET$' "$test_dir/root-post.clean"

create_status=$("$curl_binary" --silent --show-error --max-time 2 \
    -H 'Content-Type: application/json' \
    --data '{"url":"https://example.com/persisted"}' \
    --dump-header "$test_dir/create.headers" \
    --output "$test_dir/create.body" --write-out '%{http_code}' \
    http://127.0.0.1:8080/shorten)
[ "$create_status" = "201" ] || {
    echo "unexpected create status: $create_status" >&2
    exit 1
}
create_response=$(sed -n '1p' "$test_dir/create.body")
short_code=$(printf '%s' "$create_response" |
    sed -n 's/.*"code":"\([^"]*\)".*/\1/p')
short_url=$(printf '%s' "$create_response" |
    sed -n 's/.*"short_url":"\([^"]*\)".*/\1/p')
[ -n "$short_code" ] || {
    echo "create response has no code: $create_response" >&2
    exit 1
}
tr -d '\r' <"$test_dir/create.headers" >"$test_dir/create.clean"
create_bytes=$(wc -c <"$test_dir/create.body" | tr -d ' ')
grep -qi '^Content-Type: application/json$' "$test_dir/create.clean"
grep -qi "^Content-Length: $create_bytes$" "$test_dir/create.clean"
grep -qi '^Connection: close$' "$test_dir/create.clean"
[ "$short_url" = "http://localhost:8080/$short_code" ] || {
    echo "unexpected short URL: $short_url" >&2
    exit 1
}
[ -f "$test_dir/url_shortener.db" ] || {
    echo "database was not created in the working directory" >&2
    exit 1
}

"$curl_binary" --silent --show-error --max-time 2 --output /dev/null \
    --dump-header "$test_dir/redirect.headers" \
    "http://127.0.0.1:8080/$short_code"
tr -d '\r' <"$test_dir/redirect.headers" >"$test_dir/redirect.clean"
grep -q '^HTTP/1\.1 302 ' "$test_dir/redirect.clean"
grep -q '^Location: https://example\.com/persisted$' \
    "$test_dir/redirect.clean"

"$python_binary" -c '
import socket
import time
s = socket.create_connection(("127.0.0.1", 8080), timeout=2)
payload = b"GET /health HTTP/1.1\r\nHost: localhost\r\n"
try:
    for byte in payload:
        s.sendall(bytes([byte]))
        time.sleep(0.4)
except (BrokenPipeError, ConnectionResetError):
    pass
' &
slow_client_pid=$!
sleep 0.2
"$curl_binary" --silent --show-error --fail --max-time 4 \
    http://127.0.0.1:8080/health >/dev/null
kill -TERM "$slow_client_pid" 2>/dev/null || true
wait "$slow_client_pid" 2>/dev/null || true
slow_client_pid=

"$python_binary" -c '
import socket
import time

def exchange(parts):
    sock = socket.create_connection(("127.0.0.1", 8080), timeout=2)
    for part in parts:
        sock.sendall(part)
        time.sleep(0.05)
    sock.shutdown(socket.SHUT_WR)
    response = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        response += chunk
    sock.close()
    return response

partial = exchange([
    b"GET /health HTTP/1.1\r\nHo",
    b"st: localhost\r\n\r\n",
])
assert partial.startswith(b"HTTP/1.1 200 "), partial

wrong_method = exchange([
    b"POST /health HTTP/1.1\r\nHost: localhost\r\n\r\n",
])
assert wrong_method.startswith(b"HTTP/1.1 405 "), wrong_method
assert b"\r\nAllow: GET\r\n" in wrong_method, wrong_method

transfer = exchange([
    b"POST /shorten HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
])
assert transfer.startswith(b"HTTP/1.1 501 "), transfer
'

stop_server
start_server

"$curl_binary" --silent --show-error --max-time 2 --output /dev/null \
    --dump-header "$test_dir/restarted.headers" \
    "http://127.0.0.1:8080/$short_code"
tr -d '\r' <"$test_dir/restarted.headers" >"$test_dir/restarted.clean"
grep -q '^HTTP/1\.1 302 ' "$test_dir/restarted.clean"
grep -q '^Location: https://example\.com/persisted$' \
    "$test_dir/restarted.clean"

stop_server
echo "URL shortener acceptance tests passed"
