import os
import os
import struct
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
from ink_service import (Jobs, build_ask_text, page_meta,
                         extract_prepared_action, infer_scribe_intent,
                         make_handler, normalize_jarvis_answer,
                         render_ink, wire)


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

    def ask(self, text, session, job_id, space, meta=None):
        self.calls.append((text, session, job_id, space))
        return {"ok": True, "text": "Four.", "sessionId": "stable-session"}

    def test_render_and_validate(self):
        self.assertTrue(render_ink(ink()).startswith(b"\x89PNG"))
        for bad in (b"", ink()[:-1], ink()+b"x", b"x"*len(ink())):
            with self.assertRaises(ValueError):
                render_ink(bad)

    def test_duplicate_submission_and_session(self):
        self.jobs.submit(self.id, ink())
        self.jobs.submit(self.id, ink())
        self.assertTrue(self.jobs.process_one())
        self.jobs.submit(self.id, ink())
        self.assertFalse(self.jobs.process_one())
        self.assertEqual(len(self.calls), 1)
        self.assertIn(b"done\nI read:", wire(self.jobs.get(self.id)))
        self.assertIn(b"Four.", wire(self.jobs.get(self.id)))
        self.jobs.submit("b"*32, ink())
        self.jobs.process_one()
        self.assertEqual(self.calls[-1][1], "stable-session")
        # A new day starts a fresh Hermes session instead of resending weeks of history.
        with self.jobs.db() as db:
            db.execute("UPDATE settings SET value='2000-01-01' WHERE key='session-day:inbox'")
        self.jobs.submit("c"*32, ink())
        self.jobs.process_one()
        self.assertEqual(self.calls[-1][1], "")

    def test_space_validation_and_separate_sessions(self):
        with self.assertRaises(ValueError):
            self.jobs.submit(self.id, ink(), "unknown")
        self.jobs.submit(self.id, ink(), "work")
        self.jobs.process_one()
        self.jobs.submit("b"*32, ink(), "projects")
        self.jobs.process_one()
        self.jobs.submit("c"*32, ink(), "work")
        self.jobs.process_one()
        self.assertEqual([call[3] for call in self.calls], ["work", "projects", "work"])
        self.assertEqual(self.calls[1][1], "")
        self.assertEqual(self.calls[2][1], "stable-session")

    def test_recent_context_is_confirmed_bounded_and_ordered(self):
        self.jobs.submit(self.id, ink(), "work")
        self.jobs.process_one()
        self.jobs.submit("b"*32, ink(), "projects")
        self.jobs.process_one()
        self.jobs.submit("c"*32, ink(), "home")
        context = self.jobs.recent_context(1)
        self.assertEqual(len(context), 1)
        self.assertEqual(context[0]["space"], "projects")
        self.assertEqual(context[0]["note"], "What is two plus two?")
        self.assertEqual(context[0]["hermes"], "Four.")

    def test_conflicting_id(self):
        self.jobs.submit(self.id, ink())
        with self.assertRaises(FileExistsError):
            self.jobs.submit(self.id, ink()[:-1]+b"\x02")

    def test_restart_does_not_resend(self):
        self.jobs.submit(self.id, ink())
        self.jobs.change(self.id, "thinking")
        recovered = Jobs(self.db, lambda x: "ignored", self.ask)
        self.assertEqual(recovered.get(self.id)["state"], "uncertain")
        self.assertFalse(recovered.process_one())
        self.assertEqual(self.calls, [])

    def test_failed_ocr_never_calls_agent(self):
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

    def test_jarvis_reply_normalizes_legacy_labels(self):
        answer = ("WHAT I HEARD: I need to decide the smallest useful change.\n"
                  "- NEXT: Open the repo and write the contract.\n"
                  "QUESTION: Do you want the device or the browser first?")
        self.assertEqual(
            normalize_jarvis_answer(answer),
            "HEARD: I need to decide the smallest useful change.\n"
            "NEXT: Open the repo and write the contract.\n"
            "QUESTION: Do you want the device or the browser first?")

    def test_jarvis_reply_leaves_unstructured_legacy_text_untouched(self):
        answer = "Four. This is an older plain-text response."
        self.assertEqual(normalize_jarvis_answer(answer), answer)

    def test_jarvis_reply_is_applied_before_storage(self):
        self.jobs.ask = lambda *args: {
            "ok": True,
            "text": "WHAT I HEARD: Keep Today small.\nNEXT: Test it on the Scribe.",
        }
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"],
                         "HEARD: Keep Today small.\nNEXT: Test it on the Scribe.")

    def test_scribe_intent_is_inferred_from_plain_handwriting(self):
        self.assertEqual(infer_scribe_intent("Confirm the prepared email."), "confirm")
        self.assertEqual(infer_scribe_intent("CONFIRM"), "confirm")
        self.assertEqual(infer_scribe_intent("I can't confirm the date."), "ask")
        self.assertEqual(infer_scribe_intent("I don't want to do it."), "ask")
        self.assertEqual(infer_scribe_intent("Please don't cancel that."), "ask")
        self.assertEqual(infer_scribe_intent("Just a note.", "confirm"), "ask")
        self.assertEqual(infer_scribe_intent("Confirm", "confirm"), "confirm")
        self.assertEqual(infer_scribe_intent("Never mind, cancel that."), "cancel")
        self.assertEqual(infer_scribe_intent("I'm stuck and don't know where to start."), "rescue")
        self.assertEqual(infer_scribe_intent("Remember this: keep the morning brief."), "capture")
        self.assertEqual(infer_scribe_intent("We decided to ship the small version."), "decide")
        self.assertEqual(infer_scribe_intent("Just thinking out loud."), "ask")
        self.assertEqual(infer_scribe_intent("I'm stuck.", "capture"), "capture")

    def test_rescue_reply_uses_the_compact_contract(self):
        answer = "HEARD: I am circling the setup.\nNEXT: Write the first failing test.\nQUESTION: Which repo?"
        self.assertEqual(
            normalize_jarvis_answer(answer, "rescue"),
            "HEARD: I am circling the setup.\nNEXT: Write the first failing test.")

    def test_prepare_is_bounded_and_confirm_cannot_act_without_it(self):
        answer = "HEARD: You want to send the draft.\nPREPARE: Send the draft email to Alex."
        self.assertEqual(extract_prepared_action(answer), "Send the draft email to Alex.")
        self.jobs.transcribe = lambda image: "Confirm the prepared action."
        self.jobs.ask = lambda *args: self.fail("confirmation without a proposal must not call Hermes")
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"],
                         "QUESTION: Nothing is waiting for confirmation.")

    def test_confirm_passes_exact_pending_action_and_clears_on_done(self):
        seen = {}
        self.jobs.transcribe = lambda image: "Draft a message and prepare it."

        def prepare(text, session, job_id, space, meta=None):
            return {"ok": True, "text": "PREPARE: Send the draft to Alex."}

        self.jobs.ask = prepare
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        with self.jobs.db() as db:
            pending = db.execute("SELECT value FROM settings WHERE key=?", ("pending-action:inbox",)).fetchone()
        self.assertEqual(pending[0], "Send the draft to Alex.")

        self.jobs.transcribe = lambda image: "Confirm the prepared action."

        def confirm(text, session, job_id, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "DONE: The draft was sent."}

        self.jobs.ask = confirm
        self.jobs.submit("b" * 32, ink())
        self.jobs.process_one()
        self.assertEqual(seen["pending_action"], "Send the draft to Alex.")
        with self.jobs.db() as db:
            pending = db.execute("SELECT value FROM settings WHERE key=?", ("pending-action:inbox",)).fetchone()
            receipt = db.execute("SELECT space,action,job_id,source,result,undo_hint FROM action_receipts WHERE job_id=?",
                                 ("b" * 32,)).fetchone()
        self.assertIsNone(pending)
        self.assertEqual(receipt["space"], "inbox")
        self.assertEqual(receipt["action"], "Send the draft to Alex.")
        self.assertEqual(receipt["job_id"], "b" * 32)
        self.assertEqual(receipt["source"], "Confirm the prepared action.")
        self.assertEqual(receipt["result"], "DONE: The draft was sent.")
        self.assertIn("new confirmation", receipt["undo_hint"])

    def test_fixed_confirm_command_skips_ocr_and_uses_same_pending_action(self):
        with self.jobs.db() as db:
            db.execute("INSERT INTO settings(key,value) VALUES(?,?)",
                       ("pending-action:inbox", "Send the draft to Alex."))
        self.jobs.transcribe = lambda image: self.fail("a UI command must not invoke OCR")
        seen = {}

        def confirm(text, session, job_id, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "DONE: The draft was sent."}

        self.jobs.ask = confirm
        self.jobs.submit_command(self.id, "CONFIRM")
        self.jobs.process_one()
        self.assertEqual(seen["pending_action"], "Send the draft to Alex.")
        self.assertEqual(self.jobs.get(self.id)["intent"], "confirm")
        with self.assertRaises(ValueError):
            self.jobs.submit_command("b" * 32, "SEND")

    def test_not_filed_capture_falls_back_to_raw_vault_inbox(self):
        vault = Path(self.tmp.name) / "vault"
        self.jobs.transcribe = lambda image: "We decided to keep the morning brief gentle."
        self.jobs.ask = lambda *args: {"ok": True, "text": "NOT FILED: memory is unavailable."}
        with patch.object(ink_service, "VAULT_PATH", str(vault)):
            self.jobs.submit(self.id, ink())
            self.jobs.process_one()
        captures = list((vault / "00_inbox").glob("*-captures.md"))
        self.assertEqual(len(captures), 1)
        self.assertIn("keep the morning brief gentle", captures[0].read_text(encoding="utf-8"))
        self.assertIn("INBOX: Raw capture saved for triage.", self.jobs.get(self.id)["answer"])

    def test_capture_is_deterministic_and_does_not_call_hermes(self):
        vault = Path(self.tmp.name) / "vault"
        self.jobs.transcribe = lambda image: "Remember this: use the small morning brief."
        self.jobs.ask = lambda *args: self.fail("plain capture should not spend a reasoning turn")
        with patch.object(ink_service, "VAULT_PATH", str(vault)):
            self.jobs.submit(self.id, ink())
            self.jobs.process_one()
        answer = self.jobs.get(self.id)["answer"]
        self.assertEqual(answer, "FILED: Raw capture saved to Inbox for triage.")
        captures = list((vault / "00_inbox").glob("*-captures.md"))
        self.assertEqual(len(captures), 1)
        self.assertIn("use the small morning brief", captures[0].read_text(encoding="utf-8"))

    def test_command_route_requires_auth_and_accepts_only_fixed_commands(self):
        class Base(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                pass

            def send_bytes(self, status, data, content_type):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def send_json(self, status, payload):
                self.send_bytes(status, ("{}" if not payload else "{\"ok\":false}").encode(),
                                "application/json")

            def do_GET(self):
                self.send_json(404, {})

            def do_POST(self):
                self.send_json(404, {})

        legacy = SimpleNamespace(Handler=Base, REMOTE_KEY="route-test-key")
        handler = make_handler(legacy, self.jobs)
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = "http://127.0.0.1:%d/ink/commands/%s" % (server.server_address[1], self.id)
        try:
            with self.jobs.db() as db:
                db.execute("INSERT INTO settings(key,value) VALUES(?,?)",
                           ("pending-action:inbox", "Send the draft."))
            request = Request(base, data=b"CONFIRM", headers={
                "X-Diary-Remote-Key": "route-test-key", "X-Hermes-Space": "inbox"})
            with urlopen(request, timeout=3) as response:
                self.assertEqual(response.status, 202)
            unauthorized = Request(base, data=b"CONFIRM")
            with self.assertRaises(HTTPError) as error:
                urlopen(unauthorized, timeout=3)
            self.assertEqual(error.exception.code, 401)
            invalid = Request("http://127.0.0.1:%d/ink/commands/%s" %
                              (server.server_address[1], "b" * 32), data=b"DELETE", headers={
                "X-Diary-Remote-Key": "route-test-key", "X-Hermes-Space": "inbox"})
            with self.assertRaises(HTTPError) as error:
                urlopen(invalid, timeout=3)
            self.assertEqual(error.exception.code, 400)
        finally:
            server.shutdown()
            thread.join(timeout=3)
            server.server_close()

    def test_cancel_clears_pending_action_without_calling_hermes(self):
        with self.jobs.db() as db:
            db.execute("INSERT INTO settings(key,value) VALUES(?,?)",
                       ("pending-action:inbox", "Delete the draft."))
        self.jobs.transcribe = lambda image: "Never mind, cancel that."
        self.jobs.ask = lambda *args: self.fail("cancel must not call Hermes")
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"], "HEARD: The pending action is cancelled.")
        with self.jobs.db() as db:
            pending = db.execute("SELECT value FROM settings WHERE key=?", ("pending-action:inbox",)).fetchone()
        self.assertIsNone(pending)

    def test_confirmation_cannot_cross_notebook_boundaries(self):
        with self.jobs.db() as db:
            db.execute("INSERT INTO settings(key,value) VALUES(?,?)",
                       ("pending-action:work", "Publish the update."))
        self.jobs.transcribe = lambda image: "Confirm"
        self.jobs.ask = lambda *args: self.fail("a confirmation must not cross notebook boundaries")
        self.jobs.submit(self.id, ink(), "home")
        self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"],
                         "QUESTION: Nothing is waiting for confirmation.")
        with self.jobs.db() as db:
            pending = db.execute("SELECT value FROM settings WHERE key=?", ("pending-action:work",)).fetchone()
        self.assertEqual(pending[0], "Publish the update.")

    def test_job_passes_inferred_intent_to_agent(self):
        seen = {}
        self.jobs.transcribe = lambda image: "I'm stuck and don't know where to start."

        def ask(text, session, job_id, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "HEARD: The starting point is unclear.\nNEXT: Name one 15-minute step."}

        self.jobs.ask = ask
        self.jobs.submit(self.id, ink())
        self.jobs.process_one()
        self.assertEqual(seen["intent"], "rescue")
        self.assertEqual(self.jobs.get(self.id)["intent"], "rescue")

    def test_page_ask_is_a_warm_note_with_page_context(self):
        text = build_ask_text("what soil mix for tomatos?", "inbox", {
            "notebook": "page|meeting|10:00 - 10:45", "topic": "Garden plans with Alex", "intent": "ask"})
        self.assertIn("notes page for their meeting 'Garden plans with Alex' (10:00 - 10:45)", text)
        self.assertIn("thoughtful friend", text)
        self.assertIn("'Next:'", text)
        self.assertIn("never write a PREPARE line", text)
        self.assertNotIn("HEARD:", text)
        self.assertNotIn("notebook (bucket", text)
        self.assertIn("<<<NOTE\nwhat soil mix for tomatos?\nNOTE>>>", text)

    def test_today_page_and_legacy_notebooks_keep_their_own_shapes(self):
        today = build_ask_text("call mum", "inbox", {"notebook": "page|today|Capture", "topic": "Today's page"})
        self.assertIn("today's page", today)
        self.assertNotIn("(Capture)", today)
        legacy = build_ask_text("hello", "home", {"notebook": "HOME", "intent": "ask"})
        self.assertIn("writing in the HOME notebook", legacy)
        self.assertNotIn("(Capture)", legacy)
        self.assertEqual(page_meta("page|nonsense|x"), None)
        self.assertEqual(page_meta("page|task|Due 2026-10-01"), ("task", "Due 2026-10-01"))

    def test_page_capture_reply_is_warm_and_still_deterministic(self):
        vault = Path(self.tmp.name) / "vault"
        self.jobs.transcribe = lambda image: "Remember this: the shop closes at six."
        self.jobs.ask = lambda *args: self.fail("plain capture should not spend a reasoning turn")
        with patch.object(ink_service, "VAULT_PATH", str(vault)):
            self.jobs.submit(self.id, ink(), "inbox", {"notebook": "page|today|Capture", "topic": "Today's page"})
            self.jobs.process_one()
        self.assertEqual(self.jobs.get(self.id)["answer"], "Kept. It's in your inbox, so it won't get lost.")

    def test_page_headers_from_the_device_reach_the_agent(self):
        seen = {}

        def ask(text, session, job_id, space, meta=None):
            seen.update(meta or {})
            return {"ok": True, "text": "Equal parts compost and topsoil.\nNext: ask Alex about Saturday."}

        self.jobs.ask = ask
        self.jobs.submit(self.id, ink(), "inbox", {"notebook": "page|meeting|10:00 - 10:45",
                                                   "topic": "Garden plans with Alex", "intent": "ask"})
        self.jobs.process_one()
        self.assertEqual(seen["notebook"], "page|meeting|10:00 - 10:45")
        self.assertEqual(seen["topic"], "Garden plans with Alex")
        self.assertEqual(self.jobs.get(self.id)["answer"], "Equal parts compost and topsoil.\nNext: ask Alex about Saturday.")


    def test_page_archive_backs_up_every_page_to_the_vault(self):
        import hashlib, io, os, tarfile
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
        vault = Path(self.tmp.name) / "vault"
        with patch.object(ink_service, "VAULT_PATH", str(vault)):
            data = bundle(ink())
            self.assertTrue(self.jobs.submit_page(key, hashlib.sha256(data).hexdigest(), data))
            self.assertFalse(self.jobs.submit_page(key, hashlib.sha256(data).hexdigest(), data))   # unchanged
            self.jobs.submit("c" * 32, ink())                     # an ask queued later still goes first
            self.assertTrue(self.jobs.process_one()); self.assertEqual(self.jobs.get("c" * 32)["state"], "done")
            self.assertTrue(self.jobs.process_one())
            [note] = list(vault.glob("pages/2026-09-30/*.md"))
            text = note.read_text(encoding="utf-8")
            self.assertIn("What is two plus two?", text)            # the transcript
            self.assertIn("> What is 2+2?", text); self.assertIn("Four.", text)
            self.assertIn("type: page", text)
            self.assertTrue(note.with_suffix(".png").exists())
            self.assertEqual(note.with_suffix(".ink").read_bytes(), ink())   # original strokes, untouched
            changed = bundle(ink_v2(), answers=False)             # a new version keeps the same note
            self.assertTrue(self.jobs.submit_page(key, hashlib.sha256(changed).hexdigest(), changed))
            self.assertTrue(self.jobs.process_one())
            self.assertEqual(list(vault.glob("pages/2026-09-30/*.md")), [note])
            self.assertNotIn("Four.", note.read_text(encoding="utf-8"))
        for bad_key, bad in (("../../etc", data), (key, b"not a tar"), (key, bundle(b"x" * (ink_service.MAX_BODY + 1)))):
            with self.assertRaises(ValueError):
                self.jobs.submit_page(bad_key, hashlib.sha256(bad).hexdigest(), bad)

    def test_daily_page_and_stats_routes_require_auth(self):
        import hashlib, io, tarfile, json as _json
        class Base(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                pass
            def send_bytes(self, status, data, content_type):
                self.send_response(status); self.send_header("Content-Length", str(len(data))); self.end_headers()
                self.wfile.write(data)
            def send_json(self, status, payload):
                self.send_bytes(status, _json.dumps(payload).encode(), "application/json")
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
            self.assertEqual((status, _json.loads(body)["pages"]["waiting"]), (200, 1))
        finally:
            server.shutdown(); server.server_close()

    def test_task_done_completes_in_the_provider_once(self):
        import json as _json
        daily = Path(self.tmp.name) / "today"; daily.mkdir()
        entry = "b6409e74-8edf-59c3-a833-61d9040861e5"
        (daily / "2026-09-30.tasks.json").write_text(_json.dumps({entry: {"provider": "ticktick", "project": "p1", "task": "t1"}}))
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
        self.assertNotIn("TickTick", ink_service.build_ask_text("[ ] x", "inbox", {}))       # only said when it is true
        with patch.dict(os.environ, {"INK_TICKTICK_API_KEY": "k"}):
            self.assertIn("'[ ]' is a task that Pen Pal adds to their TickTick", ink_service.build_ask_text("[ ] x", "inbox", {}))
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
        import json as _json
        daily = Path(self.tmp.name) / "today"; daily.mkdir()
        now = time.mktime(time.strptime("2026-09-30 20:00", "%Y-%m-%d %H:%M"))
        tasks = {"e%d" % i: {"provider": "ticktick", "project": "p", "task": t} for i, t in enumerate(("vague", "small", "noted", "flaky"))}
        (daily / "2026-09-30.tasks.json").write_text(_json.dumps(tasks))
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
        self.assertEqual(asked, ["Sort out the insurance", "Buy milk", "flaky-get", "Plan trip"])  # his notes are never overwritten
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
        recall = lambda t: [("Insurance", "the excess is 200")] if t == "Taxes" else []
        with patch.object(ink_service, "vision_chat", chat):
            self.assertEqual(ink_service.breakdown_steps("Buy milk", recall), [])       # one action: no steps written
            self.assertEqual(ink_service.breakdown_steps("Pay rent", recall), [])
            self.assertEqual(ink_service.breakdown_steps("Taxes", recall),
                             ["Find last years return", "Gather the receipts", "Decide who files it"])  # 2-4, tidied
            self.assertRaises(ValueError, ink_service.breakdown_steps, "junk", recall)
        self.assertEqual(len(prompts), 5)     # judged first; only the vague one reaches the writing of steps
        self.assertIn("answer false", prompts[0][0]["content"])
        self.assertIn("Effort and dread are not reasons", prompts[0][0]["content"])
        self.assertIn("<<<NOTE Insurance\nthe excess is 200", prompts[2][1]["content"])   # his notes ground both questions
        self.assertIn('{"steps": ["..."]}', prompts[3][0]["content"])
        self.assertIn("would not obviously do it next anyway", prompts[3][0]["content"])

    def test_stats_measure_asks_speed_recall_and_pages(self):
        self.ask = lambda *a, **k: {"ok": True, "text": "Four.", "sessionId": "s", "recall": 2}
        self.jobs.ask = self.ask
        self.jobs.submit(self.id, ink())
        self.assertTrue(self.jobs.process_one())
        self.jobs.ask = lambda *a, **k: (_ for _ in ()).throw(TimeoutError())   # a lost answer
        self.jobs.submit("d" * 32, ink())
        self.assertTrue(self.jobs.process_one())
        stats = self.jobs.stats()
        self.assertEqual((stats["asks"], stats["answered"], stats["failed"], stats["active_days"]), (2, 1, 1, 1))
        self.assertEqual(stats["recall_hit_rate"], 1.0)
        self.assertIsNotNone(stats["seconds_to_answer"]["median"])
        self.assertEqual(stats["pages"]["waiting"], 0)


if __name__ == "__main__":
    unittest.main()
