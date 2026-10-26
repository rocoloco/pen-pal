#!/bin/sh
# Invoked by the native app in a separate process. No credentials in argv/logs.
# Usage: ink-worker.sh submit|get <job id> <space> <page title> <notebook> <intent>
set -eu
BASE=/mnt/us/hermes-native-live
SERVER=$(cat "$BASE/server.url" 2>/dev/null || echo http://127.0.0.1:8792)
MODE=$1
JOB=$2
SPACE=${3:-inbox}
# The page's context, percent-encoded by the app: what the page is titled, which notebook
# it came from, and what the writer is asking for.
TOPIC=${4:-}
NOTEBOOK=${5:-}
INTENT=${6:-ask}
case "$TOPIC$NOTEBOOK" in *[!A-Za-z0-9%._~-]*) exit 2;; esac
case "$INTENT" in ask|rescue) ;; *) INTENT=ask ;; esac
case "$JOB" in *[!0-9a-f]*|'') exit 2;; esac
[ "${#JOB}" -eq 32 ] || exit 2
case "$MODE" in submit|get) ;; *) exit 2;; esac
case "$SPACE" in inbox) ;; *) exit 2;; esac
umask 077
if [ ! -s "$BASE/auth.conf" ]; then
    # The server key lives in one device-local file (0600), never in a script.
    key=$(cat "$BASE/key.conf" 2>/dev/null || true)
    [ -n "$key" ] || exit 3
    printf 'header = "X-Diary-Remote-Key: %s"\n' "$key" > "$BASE/auth.conf"
    unset key
fi
# Optional: a tunnel starter you provide (see docs/setup.md). Not needed on a home network.
[ -f "$BASE/tunnel.sh" ] && sh "$BASE/tunnel.sh" >/dev/null 2>&1 || true
# A tunnel needs a moment to come up, and curl one step behind it gets "connection
# refused", which the app can only report as a dead connection. Wait for the server briefly.
tries=0
while [ "$tries" -lt 20 ]; do
    if curl -s -o /dev/null --max-time 2 "$SERVER/" 2>/dev/null; then break; fi
    tries=$((tries + 1))
    sleep 1
done
URL="$SERVER/ink/jobs/$JOB"
if [ "$MODE" = submit ]; then
    # No --max-filesize on the submit: it caps the REPLY, and a submit's reply is one
    # status line, while the body is the whole page (the service accepts up to
    # 720,020 bytes). The get below keeps the cap because there the reply is the
    # thing being read into the app's 60,000-byte buffer.
    curl --silent --show-error --fail --connect-timeout 4 --max-time 20 \
        --config "$BASE/auth.conf" --header 'Content-Type: application/octet-stream' \
        --header "X-Hermes-Space: $SPACE" \
        --header "X-Hermes-Topic: $TOPIC" \
        --header "X-Hermes-Notebook: $NOTEBOOK" \
        --header "X-Hermes-Intent: $INTENT" \
        --write-out '%{http_code}' \
        --data-binary "@$BASE/request.ink" --output "$BASE/worker.wire" "$URL" >"$BASE/worker.status" 2>/dev/null
    rc=$?
    [ "$rc" -eq 0 ] && exit 0
    # 429 is the service saying it is busy, not the link failing: exit 9 so the app can
    # say so instead of reporting a dead connection.
    if [ "$(cat "$BASE/worker.status" 2>/dev/null)" = "429" ]; then exit 9; fi
    exit "$rc"
else
    exec curl --silent --show-error --fail --connect-timeout 4 --max-time 20 --max-filesize 62000 \
        --config "$BASE/auth.conf" --output "$BASE/worker.wire" "$URL" 2>/dev/null
fi
