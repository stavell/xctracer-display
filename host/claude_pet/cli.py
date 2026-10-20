"""Command line: serve, status, install, statusline."""
import argparse
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request

from .feed import FakeBleSink, Publisher
from .model import Model
from .server import PORT, Daemon, make_server

EVENTS = ["SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure",
          "PermissionRequest", "PermissionDenied", "Notification", "Elicitation", "ElicitationResult",
          "SubagentStart", "SubagentStop", "Stop", "StopFailure", "PostCompact"]


def serve(args):
    if not args.fake_ble:
        sys.exit("BLE is not built in yet (step 4): run with --fake-ble FILE")
    clock = time.time
    sink = FakeBleSink(args.fake_ble, clock)
    model = Model(daily_budget=args.daily_budget)
    daemon = Daemon(model, None, clock)
    daemon.publisher = Publisher(model.snapshot, sink)
    srv = make_server(daemon, args.port)
    stop = threading.Event()
    threading.Thread(target=daemon.run, args=(stop,), daemon=True).start()
    print("claude_pet: listening on 127.0.0.1:%d, feed -> %s" % (args.port, args.fake_ble), file=sys.stderr)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        sink.close()


def status(args):
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/status" % args.port, timeout=2) as r:
            rows = json.load(r)
    except OSError:
        sys.exit("claude_pet: daemon not running on port %d" % args.port)
    print("%-4s %-8s %-5s %-11s %-6s %3s %4s %4s %5s" %
          ("slot", "id", "label", "state", "tool", "sub", "min", "ctx", "idle"))
    for r in rows:
        print("%-4s %-8s %-5s %-11s %-6s %3d %4d %4s %4ds" % (
            "-" if r["slot"] is None else r["slot"], r["id"], r["label"], r["state"], r["tool"], r["sub"],
            r["minutes"], "?" if r["ctx"] is None else r["ctx"], r["idle_s"]))
    if not rows:
        print("(no sessions)")


def install(args):
    script = os.path.abspath(sys.argv[0])
    url = "http://127.0.0.1:%d/hook" % args.port
    hook = {"hooks": [{"type": "http", "url": url, "timeout": 1}]}
    cfg = {"hooks": {e: [hook] for e in EVENTS},
           "statusLine": {"type": "command", "command": "%s %s statusline --then YOUR_OWN_STATUSLINE_COMMAND"
                          % (sys.executable, script)}}
    print("# Merge into ~/.claude/settings.json (nothing is written for you).")
    print("# Leave out statusLine, or replace YOUR_OWN_STATUSLINE_COMMAND, if you have your own status line.")
    print(json.dumps(cfg, indent=2))
    print("\n# ~/Library/LaunchAgents/local.claude-pet.plist (launchctl load -w <file>)")
    print("""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>local.claude-pet</string>
  <key>ProgramArguments</key><array>
    <string>%s</string><string>%s</string><string>serve</string>
    <string>--fake-ble</string><string>/tmp/claude_pet_feed.txt</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
</dict></plist>""" % (sys.executable, script))


def statusline(args):
    """Wrapper: post the statusline JSON to the daemon in the background (200 ms), then run the
    user's own command with the same stdin and print its output."""
    data = sys.stdin.buffer.read()

    def post():
        try:
            req = urllib.request.Request("http://127.0.0.1:%d/statusline" % args.port, data=data, method="POST")
            urllib.request.urlopen(req, timeout=0.2).read()
        except OSError:
            pass
    t = threading.Thread(target=post, daemon=True)
    t.start()
    if args.then:
        r = subprocess.run(args.then, shell=True, input=data, capture_output=True)
        sys.stdout.buffer.write(r.stdout)
    t.join(0.25)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="claude_pet.py", description=__doc__)
    ap.add_argument("--port", type=int, default=PORT)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("serve", help="run the hook receiver")
    p.add_argument("--fake-ble", metavar="FILE", help="write snapshots to FILE (the emulator plays it)")
    p.add_argument("--daily-budget", type=float, default=0.0, help="USD per day for the cost bowl (0 = off)")
    p.set_defaults(fn=serve)
    sub.add_parser("status", help="print the session table of a running daemon").set_defaults(fn=status)
    sub.add_parser("install", help="print the hooks JSON and a launchd plist").set_defaults(fn=install)
    p = sub.add_parser("statusline", help="statusline wrapper")
    p.add_argument("--then", help="your own statusline command")
    p.set_defaults(fn=statusline)
    args = ap.parse_args(argv)
    args.fn(args)
