# Setting up Pen Pal

You need a **jailbroken Kindle Scribe** and a computer or small server that stays on.
Pen Pal does not include a jailbreak; follow the community guides at
[kindlemodding.org](https://kindlemodding.org). The launcher script was written for the
"run `.sh` files from the library" mod, and the app expects a Scribe whose X server and
`/mnt/us` storage look like the stock firmware's.

## 1. Run the server

```sh
cd server
pip install -r requirements.txt
```

| Variable | Needed | What it is |
|---|---|---|
| `PEN_PAL_KEY` | yes | A shared secret, 16+ characters. The Scribe sends it with every request. |
| `INK_VISION_API_KEY` | yes | Your key for an OpenAI-compatible provider. |
| `INK_VISION_MODEL` | yes | A model that reads images and can return JSON. |
| `INK_VISION_ENDPOINT` | no | Chat completions URL. Defaults to OpenAI's. |
| `INK_VISION_REASONING_EFFORT` | no | Set (for example `low`) only if your model reasons and you want it faster. |
| `INK_VISION_SESSION` | no | Any fixed string. Sent as the `x-opencode-session` header, which OpenCode Go requires on every request. Other providers do not need it. |
| `PEN_PAL_HOST` / `PEN_PAL_PORT` | no | Default `127.0.0.1:8792`. Use `0.0.0.0` so the Scribe can reach it. |
| `PEN_PAL_HOME` | no | Where pages and the database live. Default `~/.pen-pal`. |
| `INK_OWNER_NAME` | no | Your first name, used in the model's prompt. |
| `INK_ICS_URL`, `INK_TIME_ZONE` | no | Calendar feed for Today, and your time zone. |
| `INK_TICKTICK_API_KEY` | no | Switches on the TickTick features. |
| `INK_TODAY_REFRESH_S` | no | Seconds between Today rebuilds. Default `1800`. |
| `INK_DAILY_DIR` | no | Where the Today files are written. Default `<PEN_PAL_HOME>/today`. |
| `INK_ARCHIVE_PATH` | no | Where the page backup is kept. Default `<PEN_PAL_HOME>/archive`. |

The `HERMES_*` variables you may see in the scripts and in `client/check-daily.sh` are set by
the launcher or the test script. You do not need to set them.

Then `python ink_service.py`. Use the same model for reading and answering, or any
OpenAI-compatible endpoint you like; the server does not care which.

Run the tests any time with `python -m unittest` in `server/`.

## 2. Build the app

`client/build.sh` builds a host binary (and runs its self-test) and the ARM binary for
the Scribe. It expects a Debian trixie container with the ARM cross toolchain;
`build-isolated.sh` installs everything it needs:

```sh
docker run --rm -v "$PWD/client:/work" debian:trixie sh /work/build-isolated.sh
```

The result is `client/build/hermes-native`. To see the app work without a Scribe, `client/check-daily.sh` replays a
whole day (Today, an event page, asks and answers, offline, sheets, a new day) against a fake server and saves a
screenshot at each stop. It needs gcc, the X11 headers and an X display (`Xvfb` works). The author built and ran this on a
first-generation Scribe (firmware 5.19.6). This repo's layout was installed on one fresh from the docs and answered a
handwritten question end to end, with the server reached through an SSH tunnel; a direct connection over home Wi-Fi was not
part of that test. Other Scribe models are untested.

## 3. Put files on the Scribe

**The quick way: `install.sh`.** With SSH working on the Scribe (your jailbreak guide covers
it), run this on the computer that will host the server, from a repo checkout where you have
built the app (step 2; or pass `--binary FILE`):

```sh
./install.sh SCRIBE_IP --server http://YOUR_COMPUTER_IP:8792 --fbink-xdamage path/to/fbink_xdamage
```

It copies everything below, writes `key.conf` and `server.url`, makes up a key if you do not
pass `--key`, and prints the command to start the server with that key. Close Pen Pal on the
Scribe first. If an install is already there it stops; add `--replace` to move the old one
aside to `hermes-native-live.backup-<date>` (nothing is deleted). `--dry-run` shows the plan
without changing anything. You need to have built or downloaded `fbink_xdamage` yourself
(step 4); without it the script still installs, but the screen will not refresh.

**By hand.** Everything lives in one folder, `/mnt/us/hermes-native-live/`. Connect the
Scribe over USB and copy:

| From | To |
|---|---|
| `client/build/hermes-native` | `/mnt/us/hermes-native-live/hermes-native` |
| `client/fonts/Caveat-Regular.ttf`, `OFL.txt` | `/mnt/us/hermes-native-live/fonts/` |
| `device/ink-worker.sh`, `page-sweep.sh`, `done-sweep.sh`, `offline-today.wire` | `/mnt/us/hermes-native-live/` |
| `device/Hermes-Ink.sh` | `/mnt/us/documents/` (it then appears in your library as "Pen Pal") |

Then create two small files in `/mnt/us/hermes-native-live/`:

- `key.conf`: your `PEN_PAL_KEY`, on one line, nothing else.
- `server.url`: where the server is, for example `http://192.168.1.20:8792`.

Optionally add `owner.name` with your first name for the greeting.

Line endings do not matter: the scripts drop a trailing carriage return, so files saved by a Windows editor work. Leave the key and URL alone on a single line with no quotes.

If your server is not reachable directly (for example you run it away from home), put an
executable `tunnel.sh` in the same folder. The scripts run it before they talk to the
server, so it can bring up a VPN or an SSH tunnel.

## 4. The display helper

The app draws to X; getting those pixels onto the e-ink panel needs
[`fbink_xdamage`](https://github.com/schuhumi/fbink-xdamage), a small client that watches
for X damage and refreshes the panel through FBInk. It is a separate project and is not
bundled here. Build it for the Scribe following its README and put the binary at
`/mnt/us/hermes-native-live/fbink_xdamage`. The launcher starts it for you.

## 5. First run

Open **Pen Pal** from the library. Today opens first. Write on the blank page, tap the
star, and the answer should appear under your writing in a few seconds. If it does not,
the page says why; on the server, `python ink_service.py --stats` and the `/health` route
(send the key in an `X-Pen-Pal-Key` header) show what it saw.

## Troubleshooting

- **"Handwriting vision is not configured on the server."** `INK_VISION_API_KEY` or
  `INK_VISION_MODEL` is missing in the server's environment.
- **The model returns an error about `response_format` or `reasoning_effort`.** Your
  provider does not support a parameter the app sends. Leave `INK_VISION_REASONING_EFFORT`
  unset; for `response_format`, pick a model or endpoint that supports JSON output.
- **Nothing connects.** Check that `server.url` is reachable from the Scribe's Wi-Fi and
  that the server was started with `PEN_PAL_HOST=0.0.0.0`.
- **Today is empty.** Without `INK_ICS_URL` Today is only the blank page. With it, run
  `python today.py` on the server and read what it prints.
- **After a Kindle software update the app stopped.** Updates can remove a jailbreak.
  Turn on airplane mode before an update arrives if you want to avoid that, and check the
  jailbreak project for your firmware first.
