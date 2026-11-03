#!/bin/sh
# Runs ../install.sh against a sandbox instead of a Scribe: a stand-in `ssh` executes the remote
# commands locally with /mnt/us redirected into a temp folder. Usage: sh device/test-install.sh
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export SANDBOX=$T/sandbox PATH=$T/bin:$PATH
mkdir -p "$T/bin" "$SANDBOX/mnt/us/documents"

cat > "$T/bin/ssh" <<'EOF'
#!/bin/sh
while [ $# -gt 0 ]; do case $1 in -o|-i) shift 2 ;; root@*) shift; break ;; *) shift ;; esac; done
exec sh -c "$(printf '%s' "$*" | sed "s#/mnt/us#$SANDBOX/mnt/us#g")"
EOF
chmod +x "$T/bin/ssh"
printf 'fake-arm-binary' > "$T/hermes-native"
echo '#!/bin/sh' > "$T/fbink_xdamage"; chmod +x "$T/fbink_xdamage"
LIVE=$SANDBOX/mnt/us/hermes-native-live
KEY1=aaaaaaaaaaaaaaaaaaaa1111
KEY2=bbbbbbbbbbbbbbbbbbbb2222
run() { sh "$HERE/install.sh" 10.0.0.9 --server http://10.0.0.5:8792 --binary "$T/hermes-native" "$@" >"$T/out" 2>&1; }
ok() { echo "ok - $1"; }
fail() { echo "FAIL - $1"; cat "$T/out"; exit 1; }

run --key $KEY1 --dry-run || fail "dry run"
[ ! -e "$LIVE" ] || fail "dry run changed the Scribe"; ok "dry run changes nothing"

run --key $KEY1 --fbink-xdamage "$T/fbink_xdamage" || fail "fresh install"
[ "$(cat "$LIVE/key.conf")" = $KEY1 ] || fail "key.conf"
[ "$(cat "$LIVE/server.url")" = http://10.0.0.5:8792 ] || fail "server.url"
[ -f "$LIVE/hermes-native" ] && [ -f "$LIVE/fbink_xdamage" ] && [ -f "$LIVE/fonts/Caveat-Regular.ttf" ] && [ -x "$LIVE/ink-worker.sh" ] || fail "files and modes"
cmp -s "$HERE/device/Hermes-Ink.sh" "$SANDBOX/mnt/us/documents/Hermes-Ink.sh" || fail "launcher"
[ ! -e "$LIVE/launcher" ] || fail "staging file left behind"; ok "fresh install"

if run --key $KEY2; then fail "overwrote an existing install without --replace"; fi
[ "$(cat "$LIVE/key.conf")" = $KEY1 ] || fail "refused install still changed the key"; ok "refuses to overwrite"

run --key $KEY2 --replace || fail "replace"
[ "$(cat "$LIVE/key.conf")" = $KEY2 ] || fail "new key"
OLD=$(ls -d "$SANDBOX/mnt/us/hermes-native-live.backup-"*)
[ "$(cat "$OLD/key.conf")" = $KEY1 ] && [ -f "$OLD/Hermes-Ink.sh.documents-backup" ] || fail "backup"
[ -f "$LIVE/fbink_xdamage" ] || fail "display helper not carried over"; ok "replace keeps a backup and the display helper"

if run --key short; then fail "accepted a short key"; fi; ok "rejects a short key"
echo "all install checks passed"
