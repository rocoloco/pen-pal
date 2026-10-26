#!/bin/sh
# Build the daily-mode probe and run it against a synthetic day with a fake server.
# It drives the real renderer through Today, a calendar-event page, asks and answers,
# offline queueing, long answers, Pages, sheets, task ticks and a new day, and writes a
# screenshot at each stop. Needs gcc, the X11/Xi/Xft headers and an X display: set
# DISPLAY, or run it under Xvfb (Xvfb :97 -screen 0 1860x2480x24 &  DISPLAY=:97 ...).
# Usage: sh check-daily.sh [output directory]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build/daily-check}
mkdir -p "$here/build" "$out" "$here/build/strokes"
python3 "$here/probe/make-wire.py" "$out/daily.wire"
python3 "$here/probe/make-strokes.py" "$here/build/strokes"
gcc -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -I/usr/include/freetype2 \
  "$here/daily-probe.c" -o "$here/build/daily-probe" -lX11 -lXi -lXft -lfontconfig
HERMES_REPLY_FONT=${HERMES_REPLY_FONT:-$here/fonts/Caveat-Regular.ttf}; export HERMES_REPLY_FONT
HERMES_OWNER_NAME=${HERMES_OWNER_NAME:-Sam}; export HERMES_OWNER_NAME
PRODUCT_EVENING=${PRODUCT_EVENING:-0}; export PRODUCT_EVENING
cd "$here"
build/daily-probe "$out/daily.wire" "$out" "$here/build/strokes"
