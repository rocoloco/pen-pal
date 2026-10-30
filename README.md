# Pen Pal

A pen-first assistant for a jailbroken Kindle Scribe. You write on the page with the
pen, tap the star, and the answer is written back on the same page, under your own
handwriting, in a handwriting face. No keyboard, no chat window, no screen of buttons.

It is a native app, not a web page: it draws straight to the e-ink screen, so the pen,
the eraser, the palm rejection and the saved ink behave like a notebook.

> **Status: a hobby project, shared as-is.** It was built and used on a first-generation
> Kindle Scribe (firmware 5.19.6). The second-generation Scribe uses the same jailbreak
> but has not been tried. Expect rough edges, and expect to read some code. I do not
> offer support, but I like hearing that it worked.

## A note on names

Pen Pal is the app. **Hermes** is the name of the assistant that writes the answers: you will see
it on the Scribe screen, in the model prompt and in the code. A few folders and files keep
the name from before Pen Pal (`hermes-native-live`, `hermes-native`, `Hermes-Ink.sh`). They
are only names; leave them as the setup guide has them.

## What you get

- **Native ink.** Pen, highlighter, eraser, undo, large controls. Every stroke is saved
  the moment the pen lifts, and nothing waits for Wi-Fi.
- **The star.** Write anything, tap the star, and the answer appears on the page under
  what you asked. If you are offline the question queues and is answered later.
- **Today.** One screen for the day: your calendar events, with each event's description
  shown as notes, and a blank page under them. Pages turn like a notebook.
- **A backup of every page** on your own server: the original strokes, a picture and a
  searchable note with the transcript and the answers. Nothing leaves your machines
  except what you send to your own model provider.
- **TickTick, if you want it.** Boxes you draw with the pen (`[ ] call plumber fri`)
  become tasks, tasks due today show up on Today, and tapping one done completes it.

## How it works

```
  Kindle Scribe                          your computer or small server
  ─────────────                          ─────────────────────────────
  client/   native app (C, X11)  ──HTTP──▶  server/  ink_service.py
  device/   launcher + sync scripts         reads the ink with a vision model,
                                            answers with a chat model, builds Today
                                            └─▶ one OpenAI-compatible API key
```

You run the small Python server yourself and give it one API key. The Scribe only ever
holds a shared key for talking to your server, never your model provider's key.

## Quick start

1. **Server** (Python 3.9+):

   ```sh
   cd server
   pip install -r requirements.txt
   export PEN_PAL_KEY="$(python -c 'import secrets; print(secrets.token_urlsafe(24))')"
   export INK_VISION_API_KEY=...        # your key for any OpenAI-compatible provider
   export INK_VISION_MODEL=...          # a model that can read images and return JSON
   export PEN_PAL_HOST=0.0.0.0          # only so the Scribe can reach it on your home network
   python ink_service.py
   ```

2. **Scribe**: build the app, copy the files across and point it at your server.
   [docs/setup.md](docs/setup.md) walks through it, including what you need first
   (a jailbroken Scribe).

3. **Optional**: add your calendar and TickTick (see below).

## Your calendar and tasks

Set `INK_ICS_URL` to any calendar feed in `.ics` format. For Google Calendar, use
*Settings → your calendar → Integrate calendar → Secret address in iCal format*. Recurring
events work. Whatever you write in an event's description shows up on the Scribe as the
notes for that event. Set `INK_TIME_ZONE` (for example `Europe/Madrid`) if the server is
not in your time zone.

Set `INK_TICKTICK_API_KEY` to a TickTick Open API token to switch on tasks. Without it
none of the TickTick code runs.

Check what Today will look like without touching the Scribe:

```sh
python today.py
```

## Your data

Your handwriting goes to the model provider you configure, and nowhere else. That covers
the pages you ask about and also every page the backup saves, because the backup reads
each page to write a searchable transcript. If you switch on TickTick, task titles go to
TickTick, and the calendar feed is fetched from wherever you point `INK_ICS_URL`. Pages
are stored in `~/.pen-pal/` on your server (override with `PEN_PAL_HOME`).

The server speaks plain HTTP with a shared key. Keep it on your home network, or put it
behind a VPN or a TLS proxy. Do not expose it to the internet as it is.

## Not included

- **The jailbreak.** Pen Pal does not contain or distribute one. The Kindle modding
  community keeps good guides at [kindlemodding.org](https://kindlemodding.org). Check
  the rules where you live.
- **`fbink_xdamage`**, the small helper that pushes the app's drawing onto the e-ink
  panel. It is a separate project; see [docs/setup.md](docs/setup.md).

## Layout

| Folder | What is in it |
|---|---|
| `client/` | The native app (C, X11). `build.sh` builds a host test binary and the ARM one; `check-daily.sh` replays a day against a fake server and saves screenshots. |
| `device/` | Scripts that run on the Scribe: the launcher, the sender and the page backup. |
| `server/` | The Python server, the Today builder and the tests (`python -m unittest`). |
| `docs/` | Setup guide and notes on driving an e-ink panel from X11. |

## Credits

Built by José Rosario with Claude Code. Handwriting font: Caveat (SIL Open Font
License). The Kindle modding community made all of this possible.

[MIT license](LICENSE).
