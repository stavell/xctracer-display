"""Hook receiver: stdlib threaded HTTP server on 127.0.0.1. Every POST is answered with 200 and an empty
body at once (an HTTP hook's response body is read as hook output); the work happens after the reply."""
import json
import queue
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .model import filter_hook, filter_statusline

PORT = 47800
MAX_BODY = 1 << 20


class Daemon:
    """Model + publisher behind one lock. Handler threads only enqueue filtered dicts; one worker
    thread (or step() in tests) applies them."""

    def __init__(self, model, publisher, clock=time.time):
        self.model, self.publisher, self.clock = model, publisher, clock
        self.q = queue.Queue(maxsize=10000)
        self.lock = threading.Lock()

    def submit(self, kind, ev):
        try:
            self.q.put_nowait((kind, ev))
        except queue.Full:
            pass                      # never block a hook; the next snapshot heals

    def step(self):
        now = self.clock()
        with self.lock:
            while True:
                try:
                    kind, ev = self.q.get_nowait()
                except queue.Empty:
                    break
                (self.model.statusline if kind == "statusline" else self.model.event)(ev, now)
            self.model.tick(now)
            self.publisher.poll(now)

    def run(self, stop):
        while not stop.is_set():
            self.step()
            stop.wait(0.05)

    def status(self):
        with self.lock:
            return self.model.table(self.clock())


def make_handler(daemon):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):         # never log: paths and bodies stay out of any log
            pass

        def _reply(self, code, body=b""):
            self.send_response(code)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()

        def do_POST(self):
            try:
                n = min(int(self.headers.get("Content-Length") or 0), MAX_BODY)
                raw = self.rfile.read(n) if n > 0 else b""
            except (ValueError, OSError):
                raw = b""
            kind = {"/hook": "hook", "/statusline": "statusline"}.get(self.path)
            self._reply(200 if kind else 404)
            if kind is None:
                return
            try:
                body = json.loads(raw)
                ev = (filter_hook if kind == "hook" else filter_statusline)(body)
            except ValueError:
                ev = {}
            raw = body = None                 # the rest of the body is gone here
            if ev:
                daemon.submit(kind, ev)

        def do_GET(self):
            if self.path == "/status":
                self._reply(200, json.dumps(daemon.status()).encode())
            else:
                self._reply(404)

    return Handler


def make_server(daemon, port=PORT):
    srv = ThreadingHTTPServer(("127.0.0.1", port), make_handler(daemon))
    srv.daemon_threads = True
    return srv
