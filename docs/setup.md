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
| `PEN_PAL_HOST` / `PEN_PAL_PORT` | no | Default `127.0.0.1:8792`. Use `0.0.0.0` so the Scribe can reach it. |
| `PEN_PAL_HOME` | no | Where pages and the database live. Default `~/.pen-pal`. |
| `INK_OWNER_NAME` | no | Your first name, used in the model's prompt. |
| `INK_ICS_URL`, `INK_TIME_ZONE` | no | Calendar feed for Today, and your time zone. |
| `INK_TICKTICK_API_KEY` | no | Switches on the TickTick features. |

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

The result is `client/build/hermes-native`. The author built and ran this on a
first-generation Scribe. I checked the host build and its self-test, and that the ARM target compiles, before
publishing; I have not run this repo layout on a Scribe, and other Scribe models are untested.

## 3. Put files on the Scribe

Everything lives in one folder, `/mnt/us/hermes-native-live/`. Connect the Scribe over USB
and copy:

| From | To |
|---|---|
| `client/build/hermes-native` | `/mnt/us/hermes-native-live/hermes-native` |
| `client/fonts/Caveat-Regular.ttf`, `OFL.txt` | `/mnt/us/hermes-native-live/fonts/` |
| `device/ink-worker.sh`, `page-sweep.sh`, `done-sweep.sh` | `/mnt/us/hermes-native-live/` |
| `device/Hermes-Ink.sh` | `/mnt/us/documents/` (it then appears in your library as "Pen Pal") |

Then create two small files in `/mnt/us/hermes-native-live/`:

- `key.conf`: your `PEN_PAL_KEY`, on one line, nothing else.
- `server.url`: where the server is, for example `http://192.168.1.20:8792`.

Optionally add `owner.name` with your first name for the greeting.

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
(send the key in an `X-Diary-Remote-Key` header) show what it saw.

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
