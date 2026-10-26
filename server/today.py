"""Today for the Scribe: a calendar feed and, optionally, TickTick, written as a HINKDAILY1 bundle.

Events come from any iCalendar (.ics) URL; Google Calendar's "secret address in iCal
format" works, so there is no OAuth and no database. Whatever you put in an event's
description appears on the Scribe as that event's notes. Tasks come from TickTick when a
key is set. With neither source Today is just the day's blank page.

    python today.py [--date YYYY-MM-DD]      write today's bundle now and print what it holds

Settings (environment): INK_ICS_URL (one or more URLs, space separated), INK_TIME_ZONE
(IANA name; defaults to the server's), INK_TICKTICK_API_KEY, PEN_PAL_HOME.
"""
import json
import os
import re
import sys
import time
import urllib.request
import uuid
from datetime import date, datetime, time as clock, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

NAMESPACE = uuid.UUID("0c5f7d2e-6f3a-5b1e-9a40-7d2b1f6c8e11")   # ids derive from this, so a recomposed day keeps its ink
MAX_ENTRIES = 128                  # the Scribe's bundle limit, including the capture entry
WIRE_LIMIT = 262144                # and its byte limit
STALE_DAYS = 3                     # an unfinished task this old is "stuck", not just carried
FEED_LIMIT = 8 * 1024 * 1024


def default_zone():
    name = os.environ.get("INK_TIME_ZONE") or os.environ.get("TZ") or ""
    if not name:
        try:
            name = Path("/etc/timezone").read_text().strip()
        except OSError:
            pass
    try:
        return ZoneInfo(name or "UTC")
    except Exception:  # noqa: BLE001 -- an unknown name must not stop Today
        return ZoneInfo("UTC")


def stable_id(*parts):
    return str(uuid.uuid5(NAMESPACE, "\0".join(parts)))


def field(value, limit):
    """One tab-free line, cut to `limit` bytes on a character boundary; '-' when empty."""
    clean = re.sub(r"[\x00-\x1f\x7f​-‏ -‮]+", " ", str(value or ""))
    clean = " ".join(clean.split())
    return clean.encode()[:limit].decode("utf-8", "ignore") or "-"


def clock_text(moment):
    return moment.strftime("%H:%M")


def local(moment, zone):
    """A floating time (no zone in the feed) is read in the owner's zone."""
    return moment.replace(tzinfo=zone) if moment.tzinfo is None else moment.astimezone(zone)


def meetings(feed, day, zone):
    """[(sort, id, title, when, prep)] for the events that touch `day`, all-day first."""
    from icalendar import Calendar
    import recurring_ical_events
    start = datetime.combine(day, clock.min, zone)
    end = datetime.combine(day + timedelta(days=1), clock.min, zone)
    found = []
    for event in recurring_ical_events.of(Calendar.from_ical(feed)).between(start, end):
        if str(event.get("STATUS", "")).upper() == "CANCELLED":
            continue
        first = event["DTSTART"].dt
        last = event["DTEND"].dt if "DTEND" in event else first
        title = str(event.get("SUMMARY", "")) or "(no title)"
        uid = str(event.get("UID", title))
        if not isinstance(first, datetime):                     # a date, not a time: all day
            found.append((0, "", stable_id(day.isoformat(), uid, first.isoformat()), title, "All day",
                          str(event.get("DESCRIPTION", ""))))
            continue
        first = local(first, zone)
        last = local(last, zone) if isinstance(last, datetime) else first
        begins = "From yesterday" if first < start else clock_text(first)
        ends = "into tomorrow" if last > end else clock_text(last)
        found.append((1, first.isoformat(), stable_id(day.isoformat(), uid, first.isoformat()), title,
                      f"{begins} - {ends}", str(event.get("DESCRIPTION", ""))))
    return sorted(found)


def tick_date(value):
    """TickTick writes 2019-11-14T03:00:00+0000; the instant, or None."""
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.strptime(value, "%Y-%m-%dT%H:%M:%S%z")
    except ValueError:
        return None


def tasks(get, day, zone):
    """[(due, id, title, when, notes, project, task)] for open TickTick tasks due on or before `day`."""
    end = datetime.combine(day + timedelta(days=1), clock.min, zone)
    projects = [p["id"] for p in get("/project") or []
                if isinstance(p, dict) and p.get("kind") == "TASK" and not p.get("closed") and p.get("id")]
    rows = []
    for project in projects + ["inbox"]:                    # the Inbox is not in the project list
        for task in (get(f"/project/{project}/data") or {}).get("tasks") or []:
            if task.get("status") == 2 or not task.get("id"):
                continue
            due = tick_date(task.get("dueDate"))
            if not due:
                continue
            try:
                task_zone = ZoneInfo(task.get("timeZone") or "")
            except Exception:  # noqa: BLE001
                task_zone = None
            if task.get("isAllDay"):
                due_on = due.astimezone(task_zone).date() if task_zone else date.fromisoformat(task["dueDate"][:10])
                if due_on > day:
                    continue
                at = None
            else:
                if due >= end:
                    continue
                due_on, at = due.astimezone(zone).date(), due.astimezone(zone)
            late = (day - due_on).days
            when = (f"Carried {late} days" if late >= STALE_DAYS
                    else f"Carried from {due_on.strftime('%a')}" if late > 0
                    else f"Due {due_on.isoformat()}{' ' + clock_text(at) if at else ''}")
            rows.append((at or datetime.combine(due_on, clock.min, zone), task["id"], task.get("title") or "(no title)", when,
                         (task.get("content") or task.get("desc") or "")[:1500], task.get("projectId") or project, task["id"]))
    return sorted(rows, key=lambda row: (row[0], row[1]))


def compose(day, zone, feeds=(), get=None):
    """(wire bytes, {entry id: {provider, project, task}}) for one day."""
    document = stable_id("document", day.isoformat())
    entries, links = [], {}
    for _, _, ident, title, when, prep in feeds_meetings(feeds, day, zone):
        entries.append(("meeting", ident, title, when, prep))
    for _, _, title, when, notes, project, task in (tasks(get, day, zone) if get else []):
        ident = stable_id(document, "task", task)
        entries.append(("task", ident, title, when, notes))
        links[ident] = {"provider": "ticktick", "project": project, "task": task}
    entries = entries[:MAX_ENTRIES - 1]
    entries.append(("capture", stable_id(document, "capture", "today"), "Open notebook", "Capture", ""))
    for prep_limit in (8191, 1500, 300, 0):               # a very busy day sheds notes before it sheds events
        lines = ["ready", "FORMAT\tHINKDAILY1", f"DOCUMENT\t{document}", f"DATE\t{day.isoformat()}",
                 f"ZONE\t{field(getattr(zone, 'key', 'UTC'), 63)}"]
        lines += ["\t".join(["ENTRY", kind, ident, field(title, 511), field(when, 95), field(context, prep_limit)])
                  for kind, ident, title, when, context in entries]
        wire = ("\n".join(lines) + "\n").encode()
        if len(wire) < WIRE_LIMIT:
            break
    else:
        raise ValueError("Today is too large for the Scribe")
    return wire, {ident: links[ident] for _, ident, *_ in entries if ident in links}


def feeds_meetings(feeds, day, zone):
    return sorted(m for feed in feeds for m in meetings(feed, day, zone))


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "pen-pal/1.0"})
    with urllib.request.urlopen(request, timeout=20) as response:
        return response.read(FEED_LIMIT)


def write_if_changed(path, data):
    path = Path(path)
    if path.exists() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def refresh(out, day=None, get=None, urls=None, zone=None):
    """Rebuild one day's bundle into `out`. A source that fails keeps yesterday's good file."""
    zone = zone or default_zone()
    day = day or datetime.now(zone).date()
    urls = os.environ.get("INK_ICS_URL", "").split() if urls is None else urls
    wire, links = compose(day, zone, [fetch(url) for url in urls], get)
    out = Path(out)
    changed = write_if_changed(out / f"{day.isoformat()}.tasks.json", json.dumps(links).encode())
    changed = write_if_changed(out / f"{day.isoformat()}.wire", wire) or changed
    return {"date": day.isoformat(), "entries": wire.count(b"\nENTRY\t"), "tasks": len(links), "changed": changed}


def prune(out, keep_days=7, now=None):
    """Old bundles are only clutter: nothing reads a day once it is over."""
    cutoff = (now or time.time()) - keep_days * 86400
    for old in Path(out).glob("*-*-*.*"):
        if old.stat().st_mtime < cutoff:
            old.unlink(missing_ok=True)


if __name__ == "__main__":
    home = Path(os.environ.get("PEN_PAL_HOME") or Path.home() / ".pen-pal")
    chosen = date.fromisoformat(sys.argv[sys.argv.index("--date") + 1]) if "--date" in sys.argv else None
    from ink_service import ticktick_call, ticktick_enabled
    print(refresh(home / "today", chosen, ticktick_call if ticktick_enabled() else None))
