#!/usr/bin/env python3
"""Daemon tests (unittest, no BLE, no network beyond 127.0.0.1): the 11 fixture scenarios of the spec,
encoder layout, change coalescing + heartbeat, fake-ble file format, HTTP handler, privacy filter."""
import http.client
import json
import os
import re
import sys
import tempfile
import threading
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from claude_pet import encoder, model as M  # noqa: E402
from claude_pet.feed import FakeBleSink, Publisher  # noqa: E402
from claude_pet.server import Daemon, make_server  # noqa: E402


def make_model(repos=None):
    repos = repos or {}
    return M.Model(resolver=lambda cwd: tuple(repos.get(cwd, (cwd, os.path.basename(cwd)))))


def check(tc, model, now, exp, where):
    snap = model.snapshot(now)
    seq, dec = encoder.decode(encoder.encode(snap, 7))          # what the box would see
    tc.assertEqual(seq, 7)
    tc.assertEqual(dec, snap, where)
    rows = {s.sid: s for s in model.sessions.values()}
    for sid, want in exp.get("sessions", {}).items():
        s = rows.get(sid)
        tc.assertIsNotNone(s, "%s: %s missing" % (where, sid))
        got = {"state": M.STATE_NAMES[s.state], "slot": s.slot, "sub": len(s.subagents),
               "tool": M.CAT_NAMES[s.cat], "variant": s.variant, "label": s.label.strip()}
        for k, v in want.items():
            tc.assertEqual(got[k], v, "%s: %s.%s" % (where, sid, k))
    for sid in exp.get("absent", []):
        tc.assertNotIn(sid, rows, where)
    if "hidden" in exp:
        tc.assertEqual(dec.hidden, exp["hidden"], where)
    if "hidden_waiting" in exp:
        tc.assertEqual(dec.hidden_waiting, exp["hidden_waiting"], where)
    if "slots" in exp:
        tc.assertEqual(len(dec.slots), exp["slots"], where)
    if "same_species" in exp:
        tc.assertEqual(len({rows[i].species for i in exp["same_species"]}), 1, where)
    if "distinct_variants" in exp:
        tc.assertEqual(len({rows[i].variant for i in exp["distinct_variants"]}), len(exp["distinct_variants"]), where)
    slots = [s.slot for s in rows.values() if s.slot is not None]
    tc.assertEqual(len(slots), len(set(slots)), where + ": slot used twice")


class Fixtures(unittest.TestCase):
    def test_fixtures(self):
        names = sorted(f for f in os.listdir(os.path.join(HERE, "fixtures")) if f.endswith(".json"))
        self.assertEqual(len(names), 11)
        for name in names:
            with self.subTest(name):
                with open(os.path.join(HERE, "fixtures", name)) as f:
                    fx = json.load(f)
                model = make_model(fx["repos"])
                for i, st in enumerate(fx["steps"]):
                    if st.get("tick"):
                        model.tick(st["t"])
                    else:
                        model.event(M.filter_hook(st["ev"]), st["t"])
                    check(self, model, st["t"], st.get("expect", {}), "%s step %d" % (name, i))

    def test_repo_lookup_cached_per_cwd(self):
        calls = []
        m = M.Model(resolver=lambda cwd: calls.append(cwd) or (cwd, "x"))
        for i in range(3):
            m.event({"session_id": "s%d" % i, "hook_event_name": "SessionStart", "cwd": "/a"}, i)
        self.assertEqual(calls, ["/a"])

    def test_real_git_worktrees_share_a_key(self):
        import shutil
        import subprocess
        if not shutil.which("git"):
            self.skipTest("no git")
        with tempfile.TemporaryDirectory() as d:
            d = os.path.realpath(d)
            repo = os.path.join(d, "mything")
            git = ["git", "-c", "user.email=t@example.com", "-c", "user.name=t", "-C", repo]
            subprocess.run(["git", "init", "-q", repo], check=True)
            subprocess.run(git + ["commit", "-q", "--allow-empty", "-m", "x"], check=True)
            subprocess.run(git + ["worktree", "add", "-q", os.path.join(d, "wt")], check=True)
            k1, n1 = M.git_resolver(repo)
            k2, n2 = M.git_resolver(os.path.join(d, "wt"))
            self.assertEqual((k1, n1), (k2, n2))
            self.assertEqual(n1, "mything")
            self.assertEqual(M.make_label(n1), "MYTH")

    def test_hidden_session_never_hides_a_waiting_one(self):
        m = make_model()
        for i in range(10):
            m.event({"session_id": "s%d" % i, "hook_event_name": "UserPromptSubmit", "cwd": "/p%d" % i}, i)
        m.event({"session_id": "s9", "hook_event_name": "PermissionRequest", "tool_name": "Bash"}, 20)
        self.assertEqual(m.sessions["s9"].slot is not None, True)
        self.assertEqual(m.snapshot(20).hidden, 4)

    def test_tool_categories(self):
        want = {"Read": M.READ, "Grep": M.READ, "Glob": M.READ, "LSP": M.READ, "Edit": M.EDIT,
                "Write": M.EDIT, "NotebookEdit": M.EDIT, "Bash": M.SHELL, "WebFetch": M.WEB,
                "WebSearch": M.WEB, "Agent": M.AGENT, "TaskCreate": M.AGENT, "Workflow": M.AGENT,
                "mcp__srv__tool": M.MCP, "Whatever": M.OTHER}
        for tool, cat in want.items():
            self.assertEqual(M.tool_category(tool), cat, tool)


class Encoder(unittest.TestCase):
    def slot(self, i):
        return encoder.SlotRec(i, 5, 3, 2, 6, 1, 42, 9, "ab")

    def test_header_and_slot_layout(self):
        snap = encoder.Snapshot(3, 77, 5, 2, (self.slot(0),))
        b = encoder.encode(snap, 300)
        self.assertEqual(list(b[:8]), [0x30, 1, 300 & 255, 3, 77, 5, 2, 1])
        self.assertEqual(list(b[8:16]), [0, 5, 3, 2, 6, 1, 42, 9])
        self.assertEqual(b[16:20], b"ab  ")
        self.assertEqual(len(b), 20)

    def test_six_slots_are_80_bytes(self):
        snap = encoder.Snapshot(0, 0, 0, 0, tuple(self.slot(i) for i in range(6)))
        self.assertEqual(len(encoder.encode(snap, 0)), 80)

    def test_caps_and_unknown_context(self):
        s = encoder.SlotRec(0, 1, 0, 40, 0, 0, 255, 999, "x")
        b = encoder.encode(encoder.Snapshot(0, 250, 200, 150, (s,)), 0)
        self.assertEqual((b[4], b[5], b[6]), (100, 99, 99))
        self.assertEqual((b[11], b[14], b[15]), (15, 255, 255))

    def test_decoder_rejects_bad_snapshots(self):
        good = encoder.encode(encoder.Snapshot(0, 0, 0, 0, (self.slot(0),)), 0)
        encoder.decode(good)
        for bad in (b"", good[:-1], good + b"\0", bytes([0x31]) + good[1:], good[:1] + b"\2" + good[2:],
                    good[:7] + b"\7" + good[8:], good[:8] + b"\6" + good[9:], good[:9] + b"\6" + good[10:]):
            with self.assertRaises(ValueError):
                encoder.decode(bad)


class Feed(unittest.TestCase):
    def setUp(self):
        self.m = make_model()
        self.sent = []

        class Sink:
            def send(_, data, now):
                self.sent.append((now, data))
        self.pub = Publisher(self.m.snapshot, Sink())

    def ev(self, name, now, sid="a"):
        self.m.event({"session_id": sid, "hook_event_name": name, "cwd": "/x"}, now)

    def test_first_poll_sends_then_quiet(self):
        self.pub.poll(0)
        self.pub.poll(1)
        self.assertEqual(len(self.sent), 1)

    def test_changes_within_200ms_merge(self):
        self.pub.poll(0)
        self.ev("UserPromptSubmit", 1.00)
        self.pub.poll(1.00)
        self.ev("SessionStart", 1.10, "b")
        self.pub.poll(1.10)
        self.pub.poll(1.19)
        self.assertEqual(len(self.sent), 1)
        self.pub.poll(1.20)
        self.assertEqual(len(self.sent), 2)
        sent = encoder.decode(self.sent[1][1])[1]
        self.assertEqual(len(sent.slots), 2)            # both changes in one snapshot
        self.assertEqual(encoder.decode(self.sent[1][1])[0], 1)   # seq counts

    def test_at_most_5_per_second(self):
        self.pub.poll(0)
        for i in range(100):
            now = 1 + i * 0.01
            self.ev("PreToolUse" if i % 2 else "Stop", now)
            self.m.sessions["a"].cat = i % 8
            self.pub.poll(now)
        self.assertLessEqual(len(self.sent) - 1, 6)

    def test_heartbeat_every_10s(self):
        self.pub.poll(0)
        self.pub.poll(9.9)
        self.assertEqual(len(self.sent), 1)
        self.pub.poll(10.0)
        self.assertEqual(len(self.sent), 2)
        self.pub.poll(19.9)
        self.pub.poll(20.0)
        self.assertEqual(len(self.sent), 3)

    def test_fake_ble_file_format(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "feed.txt")
            t = [100.0]
            sink = FakeBleSink(path, lambda: t[0])
            snap = self.m.snapshot(0)
            sink.send(encoder.encode(snap, 0), 100.0)
            sink.send(encoder.encode(snap, 1), 100.25)
            sink.close()
            with open(path) as f:
                lines = f.read().splitlines()
        self.assertTrue(lines[0].startswith("#"))
        self.assertEqual(lines[1], "0 3001000000000000")
        self.assertEqual(lines[2], "250 3001010000000000")
        for ln in lines[1:]:
            self.assertRegex(ln, r"^\d+ 30[0-9a-f]*$")


class Http(unittest.TestCase):
    def setUp(self):
        self.d = Daemon(make_model(), Publisher(lambda now: None, None), clock=lambda: 1000.0)
        self.srv = make_server(self.d, 0)
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()

    def post(self, path, body):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        c.request("POST", path, body=body, headers={"Content-Type": "application/json"})
        r = c.getresponse()
        out = (r.status, r.read())
        c.close()
        return out

    def test_200_empty_body_and_body_dropped(self):
        body = json.dumps({"session_id": "s1", "hook_event_name": "UserPromptSubmit", "cwd": "/w",
                           "prompt": "TOPSECRET prompt", "tool_input": {"command": "TOPSECRET"},
                           "transcript_path": "/home/x/TOPSECRET.jsonl"})
        self.assertEqual(self.post("/hook", body), (200, b""))
        kind, ev = self.d.q.get(timeout=2)
        self.assertEqual(kind, "hook")
        self.assertNotIn("TOPSECRET", json.dumps(ev))
        self.assertEqual(set(ev), {"session_id", "hook_event_name", "cwd"})

    def test_garbage_still_200(self):
        self.assertEqual(self.post("/hook", "not json {{"), (200, b""))
        self.assertEqual(self.post("/hook", ""), (200, b""))
        self.assertEqual(self.post("/hook", "[1,2]"), (200, b""))
        self.assertTrue(self.d.q.empty())

    def test_statusline_and_unknown_path(self):
        body = json.dumps({"session_id": "s1", "model": {"display_name": "TOPSECRET"},
                           "context_window": {"used_percentage": 12.5}, "cost": {"total_cost_usd": 0.5},
                           "rate_limits": {"five_hour": {"used_percentage": 30}}})
        self.assertEqual(self.post("/statusline", body), (200, b""))
        kind, ev = self.d.q.get(timeout=2)
        self.assertEqual(ev, {"session_id": "s1", "context_pct": 12.5, "cost_usd": 0.5, "limit_pct": 30.0})
        self.assertEqual(self.post("/other", "{}")[1], b"")
        self.assertTrue(self.d.q.empty())

    def test_status_has_no_session_text(self):
        self.d.model.event({"session_id": "abcdefghij", "hook_event_name": "UserPromptSubmit", "cwd": "/w/demo"}, 1000.0)
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=3)
        c.request("GET", "/status")
        rows = json.loads(c.getresponse().read())
        self.assertEqual(rows[0]["id"], "abcdefgh")
        self.assertEqual(rows[0]["state"], "WORKING")
        self.assertNotIn("/w/demo", json.dumps(rows))


class Privacy(unittest.TestCase):
    def test_only_allowed_fields_survive(self):
        body = {"session_id": "s", "agent_id": "a", "hook_event_name": "Stop", "tool_name": "Bash",
                "notification_type": "idle_prompt", "cwd": "/w", "reason": "clear", "source": "clear",
                "background_tasks": [{"id": 1, "description": "SECRET", "command": "SECRET"}, {}],
                "prompt": "SECRET", "tool_input": {"x": "SECRET"}, "tool_response": "SECRET",
                "message": "SECRET", "last_assistant_message": "SECRET", "transcript_path": "SECRET",
                "title": "SECRET", "error_details": "SECRET", "permission_suggestions": ["SECRET"]}
        out = M.filter_hook(body)
        self.assertEqual(out, {"session_id": "s", "agent_id": "a", "hook_event_name": "Stop", "tool_name": "Bash",
                               "notification_type": "idle_prompt", "cwd": "/w", "reason": "clear",
                               "source": "clear", "background_tasks": 2})
        self.assertNotIn("SECRET", json.dumps(out))

    def test_wrong_types_dropped(self):
        self.assertEqual(M.filter_hook({"session_id": 5, "cwd": ["x"], "background_tasks": "many"}), {})
        self.assertEqual(M.filter_hook("x"), {})
        self.assertEqual(M.filter_statusline({"cost": {"total_cost_usd": True},
                                              "context_window": {"used_percentage": float("nan")}}), {})

    def test_model_never_stores_free_text(self):
        m = make_model()
        ev = M.filter_hook({"session_id": "s", "hook_event_name": "PreToolUse", "tool_name": "mcp__x__SECRET",
                            "cwd": "/w", "prompt": "SECRET"})
        m.event(ev, 0)
        self.assertNotIn("SECRET", repr(m.table(0)) + repr(m.snapshot(0)))


if __name__ == "__main__":
    unittest.main()
