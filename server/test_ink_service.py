import hashlib
import io
import json
import os
import struct
import tarfile
import tempfile
import threading
import time
import unittest
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import SimpleNamespace
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from unittest.mock import patch
import ink_service

# No test may read a real Today or reach a real TickTick.
ink_service.DAILY_DIR = tempfile.mkdtemp()
from ink_service import Jobs, build_ask_text, infer_scribe_intent, make_handler, page_meta, render_ink, wire


def ink():
    return b"HINK0001" + struct.pack("<III", 2, 0, 0) + struct.pack("<HHBHHB", 100, 200, 1, 3000, 4000, 0)


def ink_v2():
    flags = 1 | (1 << 5)
    return b"HINK0002" + struct.pack("<III", 2, 0, 0) + struct.pack("<HHBBHHBB", 100, 200, flags, 9, 3000, 4000, flags & ~1, 9)


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.calls = []
        self.db = Path(self.tmp.name) / "jobs.sqlite"
        self.jobs = Jobs(self.db, lambda image: "What is two plus two?", self.ask)
        self.id = "a" * 32

    def tearDown(self):
        self.tmp.cleanup()

    def ask(self, text, space, meta=None):
        self.calls.append((text, space))
        return {"ok": True, "text": "Four."}

    def test_render_and_validate(self):
        self.assertTrue(render_ink(ink()).startswith(b"\x89PNG"))
        for bad in (b"", ink()[:-1], ink() + b"x", b"x" * len(ink())):
            with self.assertRaises(ValueError):
                render_ink(bad)

    def test_duplicate_submission_is_answered_once(self):
        self.jobs.submit(self.id, ink())
        self.jobs.submit(self.id, ink())
        self.assertTrue(self.jobs.process_one())
        self.jobs.submit(self.id, ink())
        self.assertFalse(self.jobs.process_one())
        self.assertEqual(len(self.calls), 1)
        self.assertIn(b"done\nI read:", wire(self.jobs.get(self.id)))
        self.assertIn(b"Four.", wire(self.jobs.get(self.id)))

    def test_space_must_be_a_plain_name(self):
        for bad in ("Unknown!", "", "a" * 40, "../x", "UPPER"):
            with self.assertRaises(ValueError):
                self.jobs.submit(self.id, ink(), bad)
        for name in ("work", "projects", "work"):
            self.jobs.submit(os.urandom(16).hex(), ink(), name)
            self.jobs.process_one()
        self.assertEqual([call[1] for call in self.calls], ["work", "projects", "work"])

    def test_recent_context_is_confirmed_bounded_and_ordered(self):
        self.jobs.submit(self.id, ink(), "work")
        self.jobs.process_one()
        self.jobs.submit("b" * 32, ink(), "projects")
        self.jobs.process_one()
        self.jobs.submit("c" * 32, ink(), "home")
        context = self.jobs.recent_context(1)
        self.assertEqual(len(context), 1)
        self.assertEqual(context[0]["space"], "projects")
        self.assertEqual(context[0]["note"], "What is two plus two?")
        self.assertEqual(context[0]["hermes"], "Four.")

    def test_conflicting_id(self):
        self.jobs.submit(self.id, ink())
        with self.assertRaises(FileExistsError):
            self.jobs.submit(self.id, ink()[:-1] + b"\x02")

    def test_restart_does_not_resend(self):
        self.jobs.submit(self.id, ink())
        self.jobs.change(self.id, "thinking")
        recovered = Jobs(self.db, lambda x: "ignored", self.ask)
        self.assertEqual(recovered.get(self.id)["state"], "uncertain")
        self.assertFalse(recovered.process_one())
        self.assertEqual(self.calls, [])

    def test_failed_ocr_never_calls_the_model(self):
        self.jobs.transcribe = lambda x: "EMPTY_PAGE"
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["state"], "error")
        self.assertEqual(self.calls, [])

    def test_failed_backend_is_uncertain(self):
        self.jobs.ask = lambda *args: {"ok": False, "text": ""}
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["state"], "uncertain")
        self.assertFalse(self.jobs.process_one())

    def test_the_answer_is_stored_as_the_model_wrote_it(self):
        self.jobs.ask = lambda *args: {"ok": True, "text": "  HEARD: Keep Today small.\nNEXT: Test it on the Scribe.\n"}
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"], "HEARD: Keep Today small.\nNEXT: Test it on the Scribe.")

    def test_only_asks_and_rescues_exist(self):
        self.assertEqual(infer_scribe_intent("I'm stuck and don't know where to start."), "rescue")
        self.assertEqual(infer_scribe_intent("Where do I start with the taxes?"), "rescue")
        self.assertEqual(infer_scribe_intent("Just thinking out loud."), "ask")
        self.assertEqual(infer_scribe_intent("Confirm the prepared email."), "ask")      # no confirmations to give
        self.assertEqual(infer_scribe_intent("Remember this: milk."), "ask")             # no capture mode either
        self.assertEqual(infer_scribe_intent("Anything", "rescue"), "rescue")
        self.assertEqual(infer_scribe_intent("I'm stuck.", "capture"), "rescue")
        for sent in ("confirm", "cancel", "capture", "decide", "debrief", "nonsense"):   # an older client's labels
            job = os.urandom(16).hex()
            self.jobs.submit(job, ink(), "inbox", {"intent": sent})
            self.jobs.process_one()
            self.assertEqual(self.jobs.get(job)["intent"], "ask")

    def test_job_passes_inferred_intent_to_the_model(self):
        seen = {}
        self.jobs.transcribe = lambda image: "I'm stuck and don't know where to start."

        def ask(text, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "That sounds hard.\nNext: name one 15-minute step."}

        self.jobs.ask = ask
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(seen["intent"], "rescue")
        self.assertEqual(self.jobs.get(self.id)["intent"], "rescue")

    def test_page_ask_is_a_warm_note_with_page_context(self):
        text = build_ask_text("what soil mix for tomatos?", "inbox", {
            "notebook": "page|meeting|10:00 - 10:45", "topic": "Garden plans with Alex", "intent": "ask"})
        self.assertIn("notes page for their calendar event 'Garden plans with Alex' (10:00 - 10:45)", text)
        self.assertIn("thoughtful friend", text)
        self.assertIn("'Next:'", text)
        self.assertIn("never write a PREPARE line", text)
        self.assertNotIn("HEARD:", text)
        self.assertNotIn("notebook (bucket", text)
        self.assertIn("<<<NOTE\nwhat soil mix for tomatos?\nNOTE>>>", text)

    def test_today_page_and_plain_notebooks_keep_their_own_shapes(self):
        today = build_ask_text("call mum", "inbox", {"notebook": "page|today|Capture", "topic": "Today's page"})
        self.assertIn("today's page", today)
        self.assertNotIn("(Capture)", today)
        plain = build_ask_text("hello", "home", {"notebook": "HOME", "intent": "ask"})
        self.assertIn("writing in the HOME notebook", plain)
        self.assertNotIn("(Capture)", plain)
        self.assertEqual(page_meta("page|nonsense|x"), None)
        self.assertEqual(page_meta("page|task|Due 2026-10-01"), ("task", "Due 2026-10-01"))

    def test_the_prompt_makes_no_promises_the_model_cannot_keep(self):
        text = build_ask_text("note", "inbox", {})
        self.assertIn("You have no tools", text)
        self.assertNotIn("vault", text)
        self.assertNotIn("knowledge base", text)

    def test_page_headers_from_the_device_reach_the_model(self):
        seen = {}

        def ask(text, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "Equal parts compost and topsoil.\nNext: ask Alex about Saturday."}

        self.jobs.ask = ask
        self.jobs.submit(self.id, ink(), "inbox", {"notebook": "page|meeting|10:00 - 10:45",
                                                   "topic": "Garden plans with Alex", "intent": "ask"})
        self.jobs.process_one()
        self.assertEqual(seen["notebook"], "page|meeting|10:00 - 10:45")
        self.assertEqual(seen["topic"], "Garden plans with Alex")
        self.assertEqual(self.jobs.get(self.id)["answer"], "Equal parts compost and topsoil.\nNext: ask Alex about Saturday.")

    def test_page_archive_backs_up_every_page(self):
        def bundle(ink_bytes, answers=True):
            out = io.BytesIO()
            with tarfile.open(fileobj=out, mode="w") as tar:
                parts = {"p.ink": ink_bytes, "p.meta": b"daily\nToday's page \xc2\xb7 2\n2026-09-30\nsheet\n-\n"}
                if answers:  # one finished answer, one still waiting (not archived)
                    a, h = "Four.".encode(), "What is 2+2?".encode()
                    parts["p.hermes"] = (b"HERMESX1\n" + b"X\t" + b"a" * 32 + b"\t1\t2\td\t%d\t%d\n" % (len(a), len(h)) + a + h
                                         + b"\n" + b"X\t" + b"b" * 32 + b"\t3\t4\tw\t0\t0\n\n")
                for name, data in parts.items():
                    info = tarfile.TarInfo(name); info.size = len(data); tar.addfile(info, io.BytesIO(data))
            return out.getvalue()
        key = "daily/" + "1" * 8 + "-1111-1111-1111-" + "1" * 12 + "/" + "2" * 8 + "-2222-2222-2222-" + "2" * 12 + "-2"
        archive = Path(self.tmp.name) / "archive"
        with patch.object(ink_service, "ARCHIVE_PATH", str(archive)):
            data = bundle(ink())
            self.assertTrue(self.jobs.submit_page(key, hashlib.sha256(data).hexdigest(), data))
            self.assertFalse(self.jobs.submit_page(key, hashlib.sha256(data).hexdigest(), data))   # unchanged
            self.jobs.submit("c" * 32, ink())                     # an ask queued later still goes first
            self.assertTrue(self.jobs.process_one()); self.assertEqual(self.jobs.get("c" * 32)["state"], "done")
            self.assertTrue(self.jobs.process_one())
            [note] = list(archive.glob("2026-09-30/*.md"))
            text = note.read_text(encoding="utf-8")
            self.assertIn("What is two plus two?", text)            # the transcript
            self.assertIn("> What is 2+2?", text); self.assertIn("Four.", text)
            self.assertIn("type: page", text)
            self.assertTrue(note.with_suffix(".png").exists())
            self.assertEqual(note.with_suffix(".ink").read_bytes(), ink())   # original strokes, untouched
            changed = bundle(ink_v2(), answers=False)             # a new version keeps the same note
            self.assertTrue(self.jobs.submit_page(key, hashlib.sha256(changed).hexdigest(), changed))
            self.assertTrue(self.jobs.process_one())
            self.assertEqual(list(archive.glob("2026-09-30/*.md")), [note])
            self.assertNotIn("Four.", note.read_text(encoding="utf-8"))
        for bad_key, bad in (("../../etc", data), (key, b"not a tar"), (key, bundle(b"x" * (ink_service.MAX_BODY + 1)))):
            with self.assertRaises(ValueError):
                self.jobs.submit_page(bad_key, hashlib.sha256(bad).hexdigest(), bad)

    def test_daily_page_and_stats_routes_require_auth(self):
        class Base(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                pass
            def send_bytes(self, status, data, content_type):
                self.send_response(status); self.send_header("Content-Length", str(len(data))); self.end_headers()
                self.wfile.write(data)
            def send_json(self, status, payload):
                self.send_bytes(status, json.dumps(payload).encode(), "application/json")
        legacy = SimpleNamespace(Handler=Base, REMOTE_KEY="route-test-key")
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(legacy, self.jobs))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        base = "http://127.0.0.1:%d" % server.server_address[1]
        def call(path, data=None, key="route-test-key", headers=None):
            request = Request(base + path, data=data, headers={"X-Diary-Remote-Key": key, **(headers or {})})
            try:
                with urlopen(request, timeout=5) as response:
                    return response.status, response.read()
            except HTTPError as error:
                return error.code, b""
        daily = Path(self.tmp.name) / "today"; daily.mkdir()
        (daily / "2026-09-30.wire").write_bytes(b"ready\nFORMAT\tHINKDAILY1\n")
        out = io.BytesIO()
        with tarfile.open(fileobj=out, mode="w") as tar:
            info = tarfile.TarInfo("p.ink"); info.size = len(ink()); tar.addfile(info, io.BytesIO(ink()))
        bundle = out.getvalue(); digest = hashlib.sha256(bundle).hexdigest()
        page = "history/20260930-120000-abcdef12"
        try:
            with patch.object(ink_service, "DAILY_DIR", str(daily)):
                self.assertEqual(call("/ink/daily/2026-09-30", key="wrong")[0], 401)
                self.assertEqual(call("/ink/daily/2026-10-01")[0], 404)
                self.assertEqual(call("/ink/daily/2026-09-30"), (200, b"ready\nFORMAT\tHINKDAILY1\n"))
            self.assertEqual(call("/ink/page/" + digest, bundle, key="wrong", headers={"X-Hermes-Page": page})[0], 401)
            self.assertEqual(call("/ink/page/" + digest, bundle, headers={"X-Hermes-Page": page}), (202, b"archived\n"))
            self.assertEqual(call("/ink/page/" + digest, bundle, headers={"X-Hermes-Page": page}), (202, b"unchanged\n"))
            self.assertEqual(call("/ink/page/" + digest, bundle, headers={"X-Hermes-Page": "../x"})[0], 400)
            self.assertEqual(call("/ink/stats", key="wrong")[0], 401)
            status, body = call("/ink/stats")
            self.assertEqual((status, json.loads(body)["pages"]["waiting"]), (200, 1))
        finally:
            server.shutdown(); server.server_close()

    def test_task_done_completes_in_the_provider_once(self):
        daily = Path(self.tmp.name) / "today"; daily.mkdir()
        entry = "b6409e74-8edf-59c3-a833-61d9040861e5"
        (daily / "2026-09-30.tasks.json").write_text(json.dumps({entry: {"provider": "ticktick", "project": "p1", "task": "t1"}}))
        calls = []
        def flaky(project, task):
            calls.append((project, task))
            if len(calls) == 1:
                raise TimeoutError()              # the first try is lost; the device retries
            return 200
        with patch.object(ink_service, "DAILY_DIR", str(daily)):
            with self.assertRaises(OSError):
                self.jobs.complete_task("2026-09-30", entry, flaky)
            self.assertEqual(self.jobs.complete_task("2026-09-30", entry, flaky), "synced")
            self.assertEqual(self.jobs.complete_task("2026-09-30", entry, flaky), "synced")   # no second write
            self.assertEqual(calls, [("p1", "t1"), ("p1", "t1")])
            self.assertEqual(self.jobs.complete_task("2026-09-30", "0" * 8 + "-0000-0000-0000-" + "0" * 12, flaky), "unknown")
            self.assertEqual(self.jobs.complete_task("2026-10-01", entry, flaky), "unknown")   # another day's map

    def test_boxed_ink_lines_become_tasks_once(self):
        self.assertEqual(ink_service.ink_tasks("Notes\n[ ] Call dentist Fri\n[x] done already\n☐ pay rent tomorrow\n"
                                               "[ ] Fri\n[ ] buy milk.", "2026-09-30"),     # a Wednesday
                         [("Call dentist", "2026-10-02"), ("pay rent", "2026-10-01"), ("Fri", "2026-09-30"),
                          ("buy milk", "2026-09-30")])
        self.assertEqual(ink_service.ink_tasks("[ ] CHECK how I can submit the\nplumber's invoice to my insurer. Tomorrow.\n"
                                               "Then relax", "2026-09-30"),          # wrapped, as read from the Scribe
                         [("CHECK how I can submit the plumber's invoice to my insurer", "2026-10-01")])
        self.assertNotIn("TickTick", build_ask_text("[ ] x", "inbox", {}))       # only said when it is true
        with patch.dict(os.environ, {"INK_TICKTICK_API_KEY": "k"}):
            self.assertIn("'[ ]' is a task that Pen Pal adds to their TickTick", build_ask_text("[ ] x", "inbox", {}))
        now = time.mktime(time.strptime("2026-09-30 20:00", "%Y-%m-%d %H:%M"))
        def page(key, transcript, day="2026-09-30", age=900, digest="d1"):
            with self.jobs.db() as db:
                db.execute("INSERT OR REPLACE INTO pages(key,digest,bundle,state,day,transcript,created,updated)"
                           " VALUES(?,?,x'',?,?,?,?,?)", (key, digest, "done", day, transcript, now, now - age))
        made, publishes = [], []
        def create(title, due):
            made.append((title, due))
            if title == "flaky" and made.count(("flaky", due)) == 1:
                raise TimeoutError()
            return "t%d" % len(made)
        run = lambda: self.jobs.file_page_tasks(create, now, lambda: publishes.append(1))
        page("history/a", "[ ] call dentist fri", age=60)            # still being written: wait
        self.assertFalse(run()); self.assertEqual(made, [])
        page("history/a", "[ ] call dentist fri")
        page("history/old", "[ ] ancient task", day="2026-09-20")    # the backlog is never read
        self.assertTrue(run()); self.assertFalse(run())
        self.assertEqual(made, [("call dentist", "2026-10-02")]); self.assertEqual(publishes, [1])
        page("history/a", "[ ] Call dentist Fri.\n[ ] flaky", digest="d2")   # re-read + a new line, TickTick blips
        self.assertTrue(run()); self.assertFalse(run())                # retried only after another settle
        self.assertTrue(self.jobs.file_page_tasks(create, now + 900, lambda: publishes.append(1)))
        self.assertEqual(made, [("call dentist", "2026-10-02"), ("flaky", "2026-09-30"), ("flaky", "2026-09-30")])

    def test_hermes_breaks_down_each_task_once(self):
        daily = Path(self.tmp.name) / "today"; daily.mkdir()
        now = time.mktime(time.strptime("2026-09-30 20:00", "%Y-%m-%d %H:%M"))
        tasks = {"e%d" % i: {"provider": "ticktick", "project": "p", "task": t} for i, t in enumerate(("vague", "small", "noted", "flaky"))}
        (daily / "2026-09-30.tasks.json").write_text(json.dumps(tasks))
        remote = {"vague": {"title": "Sort out the insurance"}, "small": {"title": "Buy milk"},
                  "noted": {"title": "Taxes", "content": "my own notes"}, "flaky": {"title": "Plan trip"}}
        notes, publishes, asked = {}, [], []
        def get(project, task):
            if task == "flaky" and not asked.count("flaky-get"):
                asked.append("flaky-get"); raise TimeoutError()
            return remote[task]
        def steps(title):
            asked.append(title)
            return [] if title == "Buy milk" else ["Find the claim form", "Scan the bill"]
        run = lambda at: self.jobs.break_down_tasks(get, lambda p, t, n: notes.__setitem__(t, n), steps, at,
                                                    lambda: publishes.append(1))
        with patch.object(ink_service, "DAILY_DIR", str(daily)), patch.object(ink_service, "BREAKDOWN_PER_RUN", 10):
            self.assertTrue(run(now))
            self.assertFalse(run(now + 10))                    # throttled
            self.assertTrue(run(now + 400))                    # only the failed one is retried
            self.assertFalse(run(now + 800))                   # nothing left: each task is asked once
        steps_note = "Hermes' first steps:\n1. Find the claim form\n2. Scan the bill"
        self.assertEqual(notes, {"vague": steps_note, "flaky": steps_note})
        self.assertEqual(asked, ["Sort out the insurance", "Buy milk", "flaky-get", "Plan trip"])  # your own notes are never overwritten
        self.assertEqual(publishes, [1, 1])

    def test_breakdown_asks_whether_before_what(self):
        prompts = []
        judge = {"To-do item: Buy milk": '{"break": false}',
                 "To-do item: Pay rent": '{"break": false, "steps": ["Find the wallet"]}',   # a stray list is ignored
                 "To-do item: Taxes": '{"break": true}',
                 "To-do item: junk": '{"steps": []}'}                                       # no verdict: retried, not guessed
        def chat(body):
            prompts.append(body["messages"])
            system, item = body["messages"][0]["content"], body["messages"][1]["content"]
            if "You judge one to-do item" in system:
                return {"choices": [{"message": {"content": judge[item.splitlines()[0]]}}]}
            return {"choices": [{"message": {"content": '{"steps": [" Find last years return ", "Gather the receipts.", "",'
                                                       ' "Decide who files it.", "Call Ana.", "File it."]}'}}]}
        with patch.object(ink_service, "vision_chat", chat):
            self.assertEqual(ink_service.breakdown_steps("Buy milk"), [])       # one action: no steps written
            self.assertEqual(ink_service.breakdown_steps("Pay rent"), [])
            self.assertEqual(ink_service.breakdown_steps("Taxes"),
                             ["Find last years return", "Gather the receipts", "Decide who files it"])  # 2-4, tidied
            self.assertRaises(ValueError, ink_service.breakdown_steps, "junk")
        self.assertEqual(len(prompts), 5)     # judged first; only the vague one reaches the writing of steps
        self.assertIn("answer false", prompts[0][0]["content"])
        self.assertIn("Effort and dread are not reasons", prompts[0][0]["content"])
        self.assertIn('{"steps": ["..."]}', prompts[3][0]["content"])
        self.assertIn("would not obviously do it next anyway", prompts[3][0]["content"])

    def test_stats_measure_asks_speed_and_pages(self):
        self.jobs.submit(self.id, ink())
        self.assertTrue(self.jobs.process_one())
        self.jobs.ask = lambda *a, **k: (_ for _ in ()).throw(TimeoutError())   # a lost answer
        self.jobs.submit("d" * 32, ink())
        self.assertTrue(self.jobs.process_one())
        stats = self.jobs.stats()
        self.assertEqual((stats["asks"], stats["answered"], stats["failed"], stats["active_days"]), (2, 1, 1, 1))
        self.assertEqual(stats["pages"], {"changed": 0, "archived": 0, "failed": 0, "waiting": 0})
        self.assertNotIn("recall_hit_rate", stats)


if __name__ == "__main__":
    unittest.main()
