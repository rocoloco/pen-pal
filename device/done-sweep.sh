#!/bin/sh
# Done sweep: send tasks tapped done on Today to the server, which completes them in
# TickTick. The app writes daily/<doc>/done.list ("<entry> <epoch> <date>"); this script
# only reads it, sends a tap once its undo window (60 s) has passed, and appends the
# answer to done.sent ("<entry> synced|failed|unknown <epoch>", last line wins), which
# the app shows on the row. Completing an already completed task is harmless, so a
# retry after a lost response is safe; a synced entry is never sent again.
set -eu
BASE=/mnt/us/hermes-native-live
SERVER=$(tr -d '\r\n' 2>/dev/null <"$BASE/server.url" || echo http://127.0.0.1:8792)
WS=$BASE/daily-workspace
UNDO=60
umask 077
LOCK="$BASE/.done-sweep.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
    owner=$(cat "$LOCK/pid" 2>/dev/null || true)
    case "$owner" in ''|*[!0-9]*) owner=0 ;; esac
    if [ "$owner" -gt 0 ] && kill -0 "$owner" 2>/dev/null; then exit 0; fi
    rm -rf "$LOCK"; mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$LOCK/pid"
trap 'rm -rf "$LOCK"' EXIT INT TERM
[ -s "$BASE/auth.conf" ] || exit 3
now=$(date +%s)
pending=0
for list in "$WS"/daily/*/done.list; do
    [ -f "$list" ] || continue
    sent_file=${list%.list}.sent
    while read -r entry at day; do
        [ -n "$entry" ] || continue
        case "$at" in ''|*[!0-9]*) continue ;; esac
        [ $((now - at)) -ge "$UNDO" ] || { pending=1; continue; }
        state=$(grep "^$entry " "$sent_file" 2>/dev/null | tail -1 | cut -d' ' -f2 || true)
        case "$state" in synced|unknown) continue ;; esac
        [ "$pending" = 2 ] || { [ -f "$BASE/tunnel.sh" ] && sh "$BASE/tunnel.sh" >/dev/null 2>&1 || true; pending=2; }
        code=$(curl -s -o /dev/null -w '%{http_code}' --connect-timeout 6 --max-time 30 \
                   --config "$BASE/auth.conf" -X POST "$SERVER/ink/done/$day/$entry" || true)
        case "$code" in
            202) echo "$entry synced $now" >> "$sent_file" ;;
            404) echo "$entry unknown $now" >> "$sent_file" ;;
            *)   echo "$entry failed $now" >> "$sent_file" ;;
        esac
        printf '%s done %s -> %s\n' "$(date '+%F %T')" "$entry" "$code"
    done < "$list"
done
