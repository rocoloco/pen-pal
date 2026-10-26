#!/bin/sh
# Name: Pen Pal
# Author: José Rosario
# Icon: /mnt/us/hermes-native-live/hermes-ink-cover.jpg
# The Icon line is read by SH Integration (the mod that runs .sh items from the library)
# when it builds the library tile; the mod's documentation places the extracted image at
# documents/Hermes-Ink.sh.sdr/icon.jpeg. The cover deliberately lives outside documents/
# so the image itself does not turn up as a second library item. Replacing the image
# does not refresh an indexed tile -- remove this file AND its .sdr folder, let the
# Kindle rescan, then copy it back, which is how the poster was installed on 2026-09-16.
BASE=/mnt/us/hermes-native-live
SERVER=$(cat "$BASE/server.url" 2>/dev/null || echo http://127.0.0.1:8792)
[ -x "$BASE/hermes-native" ] || exit 1
# Today (calendar events and tasks) is composed by the server and fetched to today.wire.
# Fetching never blocks launch for long: a quick try only when today's bundle is
# missing, then the background loop below keeps it fresh.
TODAY_FILE="$BASE/today.wire"
today_is_current() { grep -qx "DATE	$(date +%Y-%m-%d)" "$TODAY_FILE" 2>/dev/null; }
fetch_today() {
    [ -s "$BASE/auth.conf" ] || return 0
    [ -f "$BASE/tunnel.sh" ] && sh "$BASE/tunnel.sh" >/dev/null 2>&1 || true
    if curl -s --fail --connect-timeout 4 --max-time "${1:-15}" --config "$BASE/auth.conf" \
            -o "$TODAY_FILE.tmp" "$SERVER/ink/daily/$(date +%Y-%m-%d)" 2>/dev/null &&
       head -2 "$TODAY_FILE.tmp" | grep -qx "FORMAT	HINKDAILY1"; then
        # Replace only on a real change: the app reloads Today whenever the file changes.
        if [ -f "$TODAY_FILE" ] && [ "$(md5sum < "$TODAY_FILE.tmp")" = "$(md5sum < "$TODAY_FILE")" ]; then
            rm -f "$TODAY_FILE.tmp"
        else
            mv "$TODAY_FILE.tmp" "$TODAY_FILE"
        fi
    else
        rm -f "$TODAY_FILE.tmp"
    fi
    return 0
}
today_is_current || fetch_today 8
# The last bundle is used even when it is an earlier day's: the app says so on Today and
# reloads in place as soon as today's arrives. Before the first fetch ever works,
# offline-today.wire (a blank day, copied to the Scribe with the other files) stands in for
# today.wire, so the app is usable and every page written on it is kept.
[ -s "$TODAY_FILE" ] || cp "$BASE/offline-today.wire" "$TODAY_FILE" 2>/dev/null || exit 1
DAILY_FILE="$TODAY_FILE"
# Opening the launcher again should be harmless while the full-screen app is alive.
existing=$(pidof hermes-native 2>/dev/null || true)
if [ -n "$existing" ]; then
    kill -USR1 $existing 2>/dev/null || true
    while kill -0 "$existing" 2>/dev/null; do
        sleep 1
    done
    exit 0
fi
# Kindle's Home UI is itself an X11 client, so a managed window can end up beneath
# it. This launcher used to freeze the window manager (killall -STOP awesome) to
# hold the display -- which also froze the component that maps the SYSTEM's lock
# screen, so pressing power only dimmed the light. The app now declares its window
# override-redirect and raises it on every paint, so the WM can stay alive: it
# cannot stack Home over us, and it can still map the lock screen when powerd goes
# to the screensaver. Do not reintroduce the STOP.
#
# WATCHDOG (2026-09-19): when the app dies from a signal (a crash) the launcher now
# relaunches it, up to MAX_RELAUNCH times, and records the crash in watchdog.log and
# crash.marker so the next launch can tell the user. A clean close (exit 0) or a
# framework stop (SIGTERM/SIGKILL) is NOT a crash and still ends the session.
CVM_STOPPED=0
PILLOW_DISABLED=0
REFRESH_BRIDGE_PID=
RUN_SENTINEL="$BASE/.launcher-active"
WATCHDOG_LOG="$BASE/watchdog.log"
CRASH_MARKER="$BASE/crash.marker"
MAX_RELAUNCH=3
restore_kindle_ui() {
    rm -f "$RUN_SENTINEL"
    if [ "$CVM_STOPPED" = 1 ]; then
        killall -CONT cvm 2>/dev/null || true
    fi
    if [ "$PILLOW_DISABLED" = 1 ]; then
        lipc-set-prop com.lab126.pillow disableEnablePillow enable 2>/dev/null || true
    fi
    # ORDER MATTERS. fbink_xdamage is the thing that pushes framebuffer damage onto the
    # PANEL (it is why the app's own XClearArea loop is visible at all), and it does one
    # full refresh when it starts. The old order killed it and repainted afterwards, so
    # the repaint stayed in the framebuffer: the Kindle UI was alive underneath while the
    # panel sat blank until the next power cycle. Reported twice on the Scribe. Stop the
    # old instance, repaint, then let a fresh one do its startup full refresh.
    if [ -n "$REFRESH_BRIDGE_PID" ]; then
        kill "$REFRESH_BRIDGE_PID" 2>/dev/null || true
        wait "$REFRESH_BRIDGE_PID" 2>/dev/null || true
        REFRESH_BRIDGE_PID=
    fi
    /usr/bin/xrefresh -d :0.0 2>/dev/null || true
    if [ -x "$BASE/fbink_xdamage" ]; then
        env DISPLAY=:0 timeout 8 "$BASE/fbink_xdamage" >>"$BASE/fbink-xdamage.log" 2>&1 &
    fi
}
trap restore_kindle_ui EXIT HUP INT TERM

# Daily pages are backed up whenever they change: at launch, every ten minutes while
# the app is open, and on the way out. page-sweep.sh only sends pages whose bytes
# changed, so this is cheap, and it never blocks writing.
page_sweep() {
    [ -f "$BASE/page-sweep.sh" ] && ( sh "$BASE/page-sweep.sh" >>"$BASE/page-sweep.log" 2>&1 & )
    return 0
}
page_sweep

# The app's own log is per-session; crash copies are kept separately.
: >"$BASE/app.log"
: >"$RUN_SENTINEL"
# The loops below also stop when THIS launcher is gone: the next launch recreates the
# sentinel within seconds, so on its own it kept every earlier launch's loops alive
# (two dozen found on 2026-09-30, all fetching and sweeping in parallel).
LAUNCHER=$$
# While any app instance is open, back up pages even if you never restart it.
(
    while [ -e "$RUN_SENTINEL" ] && kill -0 "$LAUNCHER" 2>/dev/null; do
        sleep 600
        page_sweep
        fetch_today                       # ready for the next launch (and a new day)
    done
) &
# Tasks tapped done reach TickTick within about a minute of their undo window.
# sleep does not count suspended time, so a wall-clock jump means the Scribe just
# woke: back up pages then, so a task written before sleep goes up on wake.
(
    last=$(date +%s)
    while [ -e "$RUN_SENTINEL" ] && kill -0 "$LAUNCHER" 2>/dev/null; do
        sleep 60
        now=$(date +%s)
        [ $((now - last)) -gt 180 ] && page_sweep
        last=$now
        ls "$BASE"/daily-workspace/daily/*/done.list >/dev/null 2>&1 &&
            sh "$BASE/done-sweep.sh" >>"$BASE/done-sweep.log" 2>&1
        today_is_current || fetch_today 15     # a new day (or a wake after one): fetch until it lands
    done
) &

start_app() {
    env DISPLAY=:0 HERMES_INK_LIVE=1 HERMES_DAILY_FILE="$DAILY_FILE" HERMES_INK_STATE="$BASE/daily-demo-state.ink" "$BASE/hermes-native" >>"$BASE/app.log" 2>&1 &
    app_pid=$!
    i=0
    while kill -0 "$app_pid" 2>/dev/null && [ "$i" -lt 20 ]; do
        if DISPLAY=:0 /usr/bin/xwininfo -root -tree 2>/dev/null |
           grep -i 'org.hermes.inkprototype' |
           grep -q 'MapState=IsViewable'; then
            break
        fi
        i=$((i + 1))
        sleep 1
    done
    if kill -0 "$app_pid" 2>/dev/null; then
        if [ "$PILLOW_DISABLED" = 0 ]; then
            if lipc-set-prop com.lab126.pillow disableEnablePillow disable 2>/dev/null; then
                PILLOW_DISABLED=1
            fi
        fi
        if [ "$CVM_STOPPED" = 0 ] && pidof cvm >/dev/null 2>&1; then
            killall -STOP cvm 2>/dev/null || true
            CVM_STOPPED=1
        fi
        if [ -z "$REFRESH_BRIDGE_PID" ] || ! kill -0 "$REFRESH_BRIDGE_PID" 2>/dev/null; then
            if [ -x "$BASE/fbink_xdamage" ]; then
                env DISPLAY=:0 "$BASE/fbink_xdamage" >"$BASE/fbink-xdamage.log" 2>&1 &
                REFRESH_BRIDGE_PID=$!
            fi
        fi
        sleep 1
        kill -USR1 "$app_pid" 2>/dev/null || true
    fi
}

attempt=0
rc=0
while :; do
    start_app
    wait "$app_pid"
    rc=$?
    case "$rc" in
        0|130|137|143) break ;;   # clean close; SIGINT; framework SIGKILL/SIGTERM
        *) : ;;
    esac
    attempt=$((attempt + 1))
    stamp=$(date +%Y%m%d-%H%M%S)
    cp "$BASE/app.log" "$BASE/crash-$stamp.log" 2>/dev/null || true
    printf '%s rc=%s attempt=%s\n' "$stamp" "$rc" "$attempt" >>"$WATCHDOG_LOG"
    printf 'The app restarted after a crash on %s. Your ink was never at risk.\n' "$stamp" >"$CRASH_MARKER"
    if [ "$attempt" -ge "$MAX_RELAUNCH" ]; then
        printf 'giving up after %s relaunches\n' "$attempt" >>"$WATCHDOG_LOG"
        break
    fi
    sleep 2
done
page_sweep
[ -f "$BASE/done-sweep.sh" ] && ( sh "$BASE/done-sweep.sh" >>"$BASE/done-sweep.log" 2>&1 & )
exit "$rc"
