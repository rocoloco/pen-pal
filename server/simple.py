"""The small HTTP base the service listens on: one shared key, JSON helpers, nothing else."""
import json
import os
from http.server import BaseHTTPRequestHandler

REMOTE_KEY = os.environ.get("PEN_PAL_KEY", "")
PORT = int(os.environ.get("PEN_PAL_PORT", "8792"))
HOST = os.environ.get("PEN_PAL_HOST", "127.0.0.1")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass  # a request path can carry an id; never log it

    def send_bytes(self, status, data, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, status, payload):
        self.send_bytes(status, json.dumps(payload, separators=(",", ":")).encode(), "application/json")

    def do_GET(self):
        self.send_json(404, {"ok": False})

    def do_POST(self):
        self.send_json(404, {"ok": False})
