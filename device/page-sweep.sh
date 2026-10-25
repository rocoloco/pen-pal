#!/bin/sh
# Page sweep: back up every daily page that changed, so no ink lives only on the Scribe.
# Each page goes up as a small tar of its original strokes (.ink), Hermes' answers on it
# (.hermes) and its title/date/kind (.meta). The server keeps the latest version of each
# page in the vault -- strokes, a picture and a readable note -- which the nightly VPS
# backup carries off the machine. The tar's sha256 is its version id, and .page-sent
# remembers what the server already has, so an unchanged page costs nothing and a
# re-run can never duplicate one.
set -eu
BASE=/mnt/us/hermes-native-live
SERVER=$(cat "$BASE/server.url" 2>/dev/null || echo http://127.0.0.1:8792)
WS=$BASE/daily-workspace
SENT=$WS/.page-sent
BUNDLE=$BASE/.page-bundle.tar
umask 077
[ -d "$WS" ] || exit 0

# One sweep at a time: mkdir is busybox's atomic primitive. A lock whose owner is gone
# (the Scribe powered off mid-sweep) is stale; the cost of guessing wrong is one extra
# sweep, which .page-sent makes free.
LOCK="$BASE/.page-sweep.lock"
if ! mkdir "$LOCK" 2>/dev/null; then
    owner=$(cat "$LOCK/pid" 2>/dev/null || true)
    case "$owner" in ''|*[!0-9]*) owner=0 ;; esac
    if [ "$owner" -gt 0 ] && kill -0 "$owner" 2>/dev/null; then
        exit 0
    fi
    rm -rf "$LOCK"
    mkdir "$LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$LOCK/pid"
trap 'rm -rf "$LOCK" "$BUNDLE"' EXIT INT TERM
[ -s "$BASE/auth.conf" ] || exit 3
[ -f "$BASE/tunnel.sh" ] && sh "$BASE/tunnel.sh" >/dev/null 2>&1 || true
tries=0
while [ "$tries" -lt 25 ]; do
    if curl -s -o /dev/null --max-time 2 "$SERVER/" 2>/dev/null; then break; fi
    tries=$((tries + 1))
    sleep 1
done
touch "$SENT"

sent=0
unchanged=0
failed=0
for ink in "$WS"/daily/*/*.ink "$WS"/history/*.ink; do
    [ -f "$ink" ] || continue
    [ "$(wc -c < "$ink")" -gt 20 ] || continue    # a header-only page is an empty page
    stem=${ink%.ink}
    key=${stem#"$WS"/}
    name=$(basename "$stem")
    set -- "$name.ink"
    [ -f "$stem.hermes" ] && set -- "$@" "$name.hermes"
    [ -f "$stem.meta" ] && set -- "$@" "$name.meta"
    (cd "$(dirname "$ink")" && tar -cf "$BUNDLE" "$@") || { failed=$((failed + 1)); continue; }
    digest=$(sha256sum "$BUNDLE" | cut -d' ' -f1)
    if grep -qx "$key $digest" "$SENT"; then
        unchanged=$((unchanged + 1))
        continue
    fi
    if curl --silent --show-error --fail --connect-timeout 6 --max-time 60 \
            --config "$BASE/auth.conf" \
            --header 'Content-Type: application/x-tar' \
            --header "X-Hermes-Page: $key" \
            --data-binary "@$BUNDLE" --output /dev/null \
            "$SERVER/ink/page/$digest"; then
        { grep -v "^$key " "$SENT" || true; echo "$key $digest"; } > "$SENT.tmp"
        mv "$SENT.tmp" "$SENT"
        sent=$((sent + 1))
    else
        failed=$((failed + 1))                    # Wi-Fi asleep or host down: next sweep retries
    fi
done
printf '%s page sweep: %d sent, %d unchanged, %d failed\n' "$(date '+%F %T')" "$sent" "$unchanged" "$failed"
