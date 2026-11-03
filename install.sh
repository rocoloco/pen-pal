#!/bin/sh
# Install Pen Pal on a jailbroken Kindle Scribe over SSH. Run it from a repo checkout, on the computer
# that will run the server, after building the app. See docs/setup.md for the whole picture.
#
#   ./install.sh SCRIBE_IP --server http://192.168.1.20:8792 [--key KEY] [--ssh-key FILE]
#                          [--fbink-xdamage FILE] [--binary FILE] [--replace] [--dry-run]
set -eu

die() { echo "error: $*" >&2; exit 1; }
usage() { sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

HERE=$(cd "$(dirname "$0")" && pwd)
HOST= SERVER= KEY= SSH_KEY= FBINK= BIN= REPLACE=0 DRY=0
while [ $# -gt 0 ]; do
    case $1 in
        --server) SERVER=${2:?}; shift 2 ;;
        --key) KEY=${2:?}; shift 2 ;;
        --ssh-key) SSH_KEY=${2:?}; shift 2 ;;
        --fbink-xdamage) FBINK=${2:?}; shift 2 ;;
        --binary) BIN=${2:?}; shift 2 ;;
        --replace) REPLACE=1; shift ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) usage ;;
        -*) usage 1 ;;
        *) [ -z "$HOST" ] || usage 1; HOST=$1; shift ;;
    esac
done
[ -n "$HOST" ] && [ -n "$SERVER" ] || usage 1

# A repo checkout has client/build, client/fonts and device/; a release bundle has them at the top.
first() { for p in "$@"; do [ -e "$p" ] && { echo "$p"; return; }; done; }
[ -n "$BIN" ] || BIN=$(first "$HERE/hermes-native" "$HERE/client/build/hermes-native" || true)
FONTS=$(first "$HERE/fonts" "$HERE/client/fonts" || true)
DEVICE=$HERE/device
[ -n "$BIN" ] && [ -f "$BIN" ] || die "no hermes-native binary. Build it (docs/setup.md, step 2) or pass --binary FILE."
[ -n "$FONTS" ] && [ -f "$DEVICE/Hermes-Ink.sh" ] || die "fonts/ or device/ missing next to install.sh"
[ -z "$FBINK" ] || [ -f "$FBINK" ] || die "--fbink-xdamage: no such file: $FBINK"
case $SERVER in http://*|https://*) ;; *) die "--server must start with http:// or https://" ;; esac

# The key is one line of URL-safe characters: the scripts paste it into a curl header unquoted.
[ -n "$KEY" ] || KEY=$(head -c 24 /dev/urandom | base64 | tr '+/' '-_' | tr -d '=\n')
[ "${#KEY}" -ge 16 ] || die "the key needs at least 16 characters"
case $KEY in *[!A-Za-z0-9_.~-]*) die "the key may only contain letters, digits and _ . ~ -" ;; esac

SSH_OPTS="-o ConnectTimeout=8 -o BatchMode=yes"
[ -z "$SSH_KEY" ] || SSH_OPTS="$SSH_OPTS -i $SSH_KEY"
remote() { ssh $SSH_OPTS "root@$HOST" "$@"; }

remote true || die "cannot SSH to root@$HOST. Is the Scribe awake, on this Wi-Fi, with SSH running? (--ssh-key FILE if needed)"
if remote 'pidof hermes-native >/dev/null 2>&1'; then die "Pen Pal is open on the Scribe. Close it (the x in the corner) and run this again."; fi

LIVE=/mnt/us/hermes-native-live
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP=$LIVE.backup-$STAMP
HAVE_OLD=0
if remote "[ -e $LIVE ]"; then
    HAVE_OLD=1
    [ "$REPLACE" = 1 ] || die "$LIVE already exists on the Scribe. Run again with --replace to move it to $BACKUP first."
fi

echo "Scribe:   root@$HOST"
echo "Binary:   $BIN"
echo "Server:   $SERVER"
[ "$HAVE_OLD" = 0 ] || echo "Existing: will be moved to $BACKUP (and the launcher copied into it)"
[ -n "$FBINK" ] || echo "Note:     no --fbink-xdamage given; the app cannot refresh the e-ink panel without it (docs/setup.md, section 4)"
[ "$DRY" = 0 ] || { echo "Dry run: nothing was changed."; exit 0; }

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir "$STAGE/fonts"
cp "$BIN" "$STAGE/hermes-native"
cp "$FONTS/Caveat-Regular.ttf" "$FONTS/OFL.txt" "$STAGE/fonts/"
cp "$DEVICE/ink-worker.sh" "$DEVICE/page-sweep.sh" "$DEVICE/done-sweep.sh" "$DEVICE/offline-today.wire" "$STAGE/"
cp "$DEVICE/Hermes-Ink.sh" "$STAGE/launcher"
[ -z "$FBINK" ] || cp "$FBINK" "$STAGE/fbink_xdamage"
printf '%s' "$KEY" > "$STAGE/key.conf"
printf '%s' "$SERVER" > "$STAGE/server.url"

if [ "$HAVE_OLD" = 1 ]; then
    remote "mv $LIVE $BACKUP && cp /mnt/us/documents/Hermes-Ink.sh $BACKUP/Hermes-Ink.sh.documents-backup 2>/dev/null || true"
    # Reuse the display helper from the old install when none was given.
    [ -n "$FBINK" ] || ! remote "[ -x $BACKUP/fbink_xdamage ]" || remote "mkdir -p $LIVE && cp $BACKUP/fbink_xdamage $LIVE/"
fi
tar cf - -C "$STAGE" . | remote "mkdir -p $LIVE && cd $LIVE && tar xf - && mv launcher /mnt/us/documents/Hermes-Ink.sh && chmod +x hermes-native *.sh /mnt/us/documents/Hermes-Ink.sh && { [ ! -f fbink_xdamage ] || chmod +x fbink_xdamage; }"

echo
echo "Installed. Now start the server with the same key, on the computer at $SERVER:"
echo
echo "  cd server && pip install -r requirements.txt"
echo "  PEN_PAL_KEY=$KEY PEN_PAL_HOST=0.0.0.0 INK_VISION_API_KEY=... INK_VISION_MODEL=... python ink_service.py"
echo
echo "Then open Pen Pal from the Scribe's library. To go back: close it, delete $LIVE,"
[ "$HAVE_OLD" = 0 ] || echo "and rename $BACKUP to $LIVE."
[ "$HAVE_OLD" = 1 ] || echo "and delete /mnt/us/documents/Hermes-Ink.sh."
