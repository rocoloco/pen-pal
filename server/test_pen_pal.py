import http.client
import json
import os
import struct
import tempfile
import threading
import time
import unittest
from datetime import date
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

import ink_service
import simple
import today

MADRID = ZoneInfo("Europe/Madrid")
MONDAY = date(2026, 10, 26)          # the clocks went back on the 25th: Madrid is UTC+1

CALENDAR = b"""BEGIN:VCALENDAR
VERSION:2.0
BEGIN:VEVENT
UID:standup
DTSTART;TZID=Europe/Madrid:20261026T093000
DTEND;TZID=Europe/Madrid:20261026T101500
SUMMARY:Kitchen walkthrough
DESCRIPTION:Bring the cabinet measurements.\\nAnd the tile samples.
END:VEVENT
BEGIN:VEVENT
UID:weekly
DTSTART;TZID=Europe/Madrid:20261012T140000
DTEND;TZID=Europe/Madrid:20261012T144500
RRULE:FREQ=WEEKLY;BYDAY=MO
SUMMARY:Weekly review
END:VEVENT
BEGIN:VEVENT
UID:holiday
DTSTART;VALUE=DATE:20261026
DTEND;VALUE=DATE:20261027
SUMMARY:Bank holiday
END:VEVENT
BEGIN:VEVENT
UID:gone
DTSTART;TZID=Europe/Madrid:20261026T110000
DTEND;TZID=Europe/Madrid:20261026T120000
STATUS:CANCELLED
SUMMARY:Cancelled thing
END:VEVENT
BEGIN:VEVENT
UID:utc
DTSTART:20261026T070000Z
DTEND:20261026T073000Z
SUMMARY:Early call
END:VEVENT
BEGIN:VEVENT
UID:night
DTSTART;TZID=Europe/Madrid:20261025T230000
DTEND;TZID=Europe/Madrid:20261026T010000
SUMMARY:Night shift
END:VEVENT
BEGIN:VEVENT
UID:floating
DTSTART:20261026T180000
DTEND:20261026T190000
SUMMARY:Dinner
END:VEVENT
END:VCALENDAR
"""


def entries(wire):
    return [line.split("\t") for line in wire.decode().splitlines() if line.startswith("ENTRY\t")]


def tick(task_id, title, due=None, all_day=True, status=0, zone="Europe/Madrid", project="p1", content=""):
    task = {"id": task_id, "title": title, "status": status, "isAllDay": all_day, "timeZone": zone,
            "projectId": project, "content": content}
    if due:
        task["dueDate"] = due
    return task


class TodayTests(unittest.TestCase):
    def test_calendar_feed_becomes_meetings_in_order(self):
        wire, links = today.compose(MONDAY, MADRID, [CALENDAR])
        lines = wire.decode().splitlines()
        self.assertEqual(lines[:2], ["ready", "FORMAT\tHINKDAILY1"])
        self.assertIn("DATE\t2026-10-26", lines)
        self.assertIn("ZONE\tEurope/Madrid", lines)
        rows = entries(wire)
        self.assertEqual([(r[1], r[3], r[4]) for r in rows], [
            ("meeting", "Bank holiday", "All day"),
            ("meeting", "Night shift", "From yesterday - 01:00"),
            ("meeting", "Early call", "08:00 - 08:30"),                 # 07:00Z is 08:00 here
            ("meeting", "Kitchen walkthrough", "09:30 - 10:15"),
            ("meeting", "Weekly review", "14:00 - 14:45"),              # a recurring event, expanded
            ("meeting", "Dinner", "18:00 - 19:00"),                     # a floating time is read in the owner's zone
            ("capture", "Open notebook", "Capture")])
        self.assertEqual(rows[3][5], "Bring the cabinet measurements. And the tile samples.")   # the description is the prep
        self.assertEqual(links, {})

    def test_ids_are_stable_so_ink_survives_a_rebuild(self):
        first = [r[2] for r in entries(today.compose(MONDAY, MADRID, [CALENDAR])[0])]
        again = [r[2] for r in entries(today.compose(MONDAY, MADRID, [CALENDAR])[0])]
        other_day = [r[2] for r in entries(today.compose(date(2026, 10, 27), MADRID, [CALENDAR])[0])]
        self.assertEqual(first, again)
        self.assertEqual(len(set(first)), len(first))
        self.assertFalse(set(first) & set(other_day))

    def test_no_sources_still_gives_the_day_a_blank_page(self):
        rows = entries(today.compose(MONDAY, MADRID)[0])
        self.assertEqual([r[1] for r in rows], ["capture"])

    def test_text_cannot_break_the_wire(self):
        feed = CALENDAR.replace(b"SUMMARY:Dinner", b"SUMMARY:Tab\\there\\nENTRY\\tmeeting")
        wire, _ = today.compose(MONDAY, MADRID, [feed])
        for row in entries(wire):
            self.assertEqual(len(row), 6)                                   # kind, id and four fields: nothing smuggled in
        self.assertEqual(today.field("é" * 600, 511).encode().decode(), "é" * 255)   # cut on a character, not a byte
        self.assertEqual(today.field("  \t ", 10), "-")

    def test_a_very_busy_day_sheds_prep_text_before_it_sheds_meetings(self):
        events = "".join(f"BEGIN:VEVENT\nUID:e{i}\nDTSTART;TZID=Europe/Madrid:20261026T{8 + i % 10:02d}{i % 60:02d}00\n"
                         f"SUMMARY:Event {i}\nDESCRIPTION:{'Notes ' * 2500}\nEND:VEVENT\n" for i in range(300))
        wire, _ = today.compose(MONDAY, MADRID, [("BEGIN:VCALENDAR\nVERSION:2.0\n" + events + "END:VCALENDAR\n").encode()])
        rows = entries(wire)
        self.assertLess(len(wire), today.WIRE_LIMIT)
        self.assertEqual((len(rows), rows[-1][1]), (128, "capture"))        # the Scribe's own limit, blank page last
        self.assertTrue(all(len(r) == 6 and r[5] for r in rows))           # no empty field: the Scribe's parser would skip it

    def test_ticktick_tasks_are_the_open_ones_due_by_today(self):
        data = {"/project": [{"id": "p1", "kind": "TASK"}, {"id": "n", "kind": "NOTE"}, {"id": "x", "kind": "TASK", "closed": True}],
                "/project/p1/data": {"tasks": [
                    tick("a", "Order hinges", "2026-10-25T23:00:00+0000"),                      # all day, due today here
                    tick("b", "Pay invoice", "2026-10-21T22:00:00+0000", content="Account 12"),  # due the 22nd: stuck
                    tick("c", "Call plumber", "2026-10-23T22:00:00+0000"),                      # the 24th
                    tick("d", "Next week", "2026-10-30T23:00:00+0000"),
                    tick("e", "Done already", "2026-10-25T23:00:00+0000", status=2),
                    tick("f", "No date")]},
                "/project/inbox/data": {"tasks": [
                    tick("g", "Sign form", "2026-10-26T14:30:00+0000", all_day=False, project="inbox42")]}}
        seen = []

        def get(path):
            seen.append(path)
            return data[path]

        wire, links = today.compose(MONDAY, MADRID, [], get)
        self.assertEqual([r[3] for r in entries(wire) if r[1] == "task"],
                         ["Pay invoice", "Call plumber", "Order hinges", "Sign form"])
        self.assertEqual([r[4] for r in entries(wire) if r[1] == "task"],
                         ["Carried 4 days", "Carried from Sat", "Due 2026-10-26", "Due 2026-10-26 15:30"])
        self.assertEqual(entries(wire)[0][5], "Account 12")
        self.assertNotIn("/project/n/data", seen)                           # note lists and closed lists are never read
        self.assertNotIn("/project/x/data", seen)
        self.assertEqual({v["task"]: v["project"] for v in links.values()},
                         {"a": "p1", "b": "p1", "c": "p1", "g": "inbox42"})  # what a tap on "done" needs, ids only
        self.assertTrue(all(v["provider"] == "ticktick" for v in links.values()))

    def test_refresh_writes_once_and_keeps_the_last_good_bundle_when_a_feed_fails(self):
        with tempfile.TemporaryDirectory() as out, patch.object(today, "fetch", lambda url: CALENDAR):
            first = today.refresh(out, MONDAY, urls=["http://feed"], zone=MADRID)
            self.assertEqual((first["entries"], first["changed"]), (7, True))
            self.assertFalse(today.refresh(out, MONDAY, urls=["http://feed"], zone=MADRID)["changed"])
            good = (Path(out) / "2026-10-26.wire").read_bytes()
            with patch.object(today, "fetch", side_effect=OSError("offline")):
                with self.assertRaises(OSError):
                    today.refresh(out, MONDAY, urls=["http://feed"], zone=MADRID)
            self.assertEqual((Path(out) / "2026-10-26.wire").read_bytes(), good)
            self.assertEqual(json.loads((Path(out) / "2026-10-26.tasks.json").read_text()), {})

    def test_prune_drops_only_old_days(self):
        with tempfile.TemporaryDirectory() as out:
            old, new = Path(out, "2026-10-01.wire"), Path(out, "2026-10-26.wire")
            old.write_text("x"); new.write_text("y")
            os.utime(old, (time.time() - 30 * 86400,) * 2)
            today.prune(out)
            self.assertEqual([p.name for p in Path(out).iterdir()], ["2026-10-26.wire"])


def ink():
    return b"HINK0001" + struct.pack("<III", 2, 0, 0) + struct.pack("<HHBHHB", 100, 200, 1, 3000, 4000, 0)


class OwnKeyModeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.sent = []

        def chat(payload):
            self.sent.append(payload)
            return {"choices": [{"message": {"content": "  Equal parts compost and topsoil.\nNext: ask Alex.  "}}]}

        self.jobs = ink_service.Jobs(Path(self.tmp.name) / "jobs.sqlite", lambda image: "what soil mix for tomatos?", None)
        self.jobs.ask = ink_service.make_ask(self.jobs, chat)

    def submit(self, job_id, notebook="page|meeting|10:00 - 10:45"):
        self.jobs.submit(job_id, ink(), "inbox", {"notebook": notebook, "topic": "Garden plans", "intent": "ask"})
        self.assertTrue(self.jobs.process_one())

    def test_a_star_ask_is_one_model_call_and_the_answer_is_stored(self):
        self.submit("a" * 32)
        job = self.jobs.get("a" * 32)
        self.assertEqual((job["state"], job["answer"]), ("done", "Equal parts compost and topsoil.\nNext: ask Alex."))
        messages = self.sent[0]["messages"]
        self.assertEqual([m["role"] for m in messages], ["system", "user"])
        self.assertIn("their calendar event 'Garden plans' (10:00 - 10:45)", messages[1]["content"])
        self.assertIn("<<<NOTE\nwhat soil mix for tomatos?\nNOTE>>>", messages[1]["content"])

    def test_the_next_page_sees_the_last_exchange_and_an_old_one_is_forgotten(self):
        self.submit("a" * 32)
        self.submit("b" * 32)
        self.assertEqual([m["role"] for m in self.sent[1]["messages"]], ["system", "user", "assistant", "user"])
        with self.jobs.db() as db:
            db.execute("UPDATE jobs SET updated=?", (time.time() - 7 * 3600,))
        self.submit("c" * 32)
        self.assertEqual([m["role"] for m in self.sent[2]["messages"]], ["system", "user"])

    def test_an_empty_model_reply_is_an_error_not_a_blank_answer(self):
        self.jobs.ask = ink_service.make_ask(self.jobs, lambda payload: {"choices": [{"message": {"content": " "}}]})
        self.submit("a" * 32)
        self.assertEqual(self.jobs.get("a" * 32)["state"], "uncertain")

    def test_tickTick_work_is_off_without_a_key(self):
        def boom(*args, **kwargs):
            raise AssertionError("TickTick must not be touched without a key")

        with patch.dict(os.environ, {"INK_TICKTICK_API_KEY": ""}), \
             patch.object(ink_service.Jobs, "file_page_tasks", boom), patch.object(ink_service.Jobs, "break_down_tasks", boom):
            self.assertFalse(self.jobs.process_one())
        with patch.dict(os.environ, {"INK_TICKTICK_API_KEY": "k"}), \
             patch.object(ink_service.Jobs, "file_page_tasks", lambda self: True):
            self.assertTrue(self.jobs.process_one())

class HandlerTests(unittest.TestCase):
    """The real HTTP base, not a stand-in: auth, the health report and Today."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        daily = Path(self.tmp.name) / "today"
        daily.mkdir()
        (daily / "2026-10-26.wire").write_bytes(today.compose(MONDAY, MADRID)[0])
        patcher = patch.object(ink_service, "DAILY_DIR", str(daily))
        patcher.start()
        self.addCleanup(patcher.stop)
        key = patch.object(simple, "REMOTE_KEY", "k" * 20)
        key.start()
        self.addCleanup(key.stop)
        jobs = ink_service.Jobs(Path(self.tmp.name) / "jobs.sqlite", None, None)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), ink_service.make_handler(simple, jobs))
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = f"http://127.0.0.1:{self.server.server_address[1]}"

    def get(self, path, key=None):
        return urlopen(Request(self.base + path, headers={"X-Pen-Pal-Key": key} if key else {}), timeout=5)

    def test_every_route_wants_the_key(self):
        for path in ("/health", "/ink/daily/2026-10-26", "/ink/stats"):
            with self.assertRaises(HTTPError) as caught:
                self.get(path)
            self.assertEqual(caught.exception.code, 401)
            with self.assertRaises(HTTPError) as caught:
                self.get(path, "wrong" * 5)
            self.assertEqual(caught.exception.code, 401)

    def test_a_non_ascii_key_is_refused_not_a_crash(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.server.server_address[1], timeout=5)
        conn.request("GET", "/health", headers={"X-Pen-Pal-Key": "ké" * 10})
        self.assertEqual(conn.getresponse().status, 401)

    def test_the_old_command_and_sweep_routes_are_gone(self):
        for path in ("/ink/commands/" + "a" * 32, "/ink/sweep/inbox/" + "b" * 32, "/ink/today/2026-10-26"):
            with self.assertRaises(HTTPError) as caught:
                urlopen(Request(self.base + path, data=b"CONFIRM", headers={"X-Pen-Pal-Key": "k" * 20}), timeout=5)
            self.assertEqual(caught.exception.code, 404)

    def test_today_and_health_with_the_key(self):
        body = self.get("/ink/daily/2026-10-26", "k" * 20).read().decode()
        self.assertTrue(body.startswith("ready\nFORMAT\tHINKDAILY1\n"))
        self.assertTrue(json.loads(self.get("/health", "k" * 20).read())["ok"])
        with self.assertRaises(HTTPError) as caught:
            self.get("/ink/daily/2026-10-27", "k" * 20)
        self.assertEqual(caught.exception.code, 404)
        with self.assertRaises(HTTPError) as caught:
            self.get("/nothing", "k" * 20)
        self.assertEqual(caught.exception.code, 404)


if __name__ == "__main__":
    unittest.main()
