"""Session table, state machine (spec 4), slots (spec 5), tool categories (spec 6), privacy filter (spec 9).

Pure code: time is passed in, the git lookup is injectable."""
import math
import os
import subprocess
import zlib

from .encoder import CTX_UNKNOWN, FLAG_BOWL_IS_LIMIT, FLAG_PAUSED, MAX_SLOTS, SlotRec, Snapshot

# State numbers = snapshot byte; a higher number is more urgent (rank 1 PERMISSION = 5 ... rank 6 IDLE = 0).
IDLE, WORKING, DONE, FAILED, QUESTION, PERMISSION = range(6)
STATE_NAMES = ["IDLE", "WORKING", "DONE", "FAILED", "QUESTION", "PERMISSION"]
THINK, READ, EDIT, SHELL, WEB, AGENT, MCP, OTHER = range(8)
CAT_NAMES = ["THINK", "READ", "EDIT", "SHELL", "WEB", "AGENT", "MCP", "OTHER"]

NSPECIES = 8
NVARIANTS = 4
DONE_TO_IDLE_S = 20 * 60
FAILED_TO_IDLE_S = 60 * 60
SUBAGENT_STALE_S = 30 * 60
STALE_IDLE_S = 2 * 3600
STALE_BUSY_S = 12 * 3600
CLEAR_WINDOW_S = 5.0
LIMIT_STALE_S = 3600

TOOL_EVENTS = ("PreToolUse", "PostToolUse", "PostToolUseFailure")
ELICIT_TYPES = ("elicitation_dialog", "elicitation_url_dialog", "agent_needs_input")


# ---------------------------------------------------------------- privacy

_STR_FIELDS = ("session_id", "agent_id", "hook_event_name", "tool_name", "notification_type",
               "cwd", "reason", "source")


def filter_hook(body):
    """Keep only the allowed hook fields (spec 9); everything else (prompts, tool input / output,
    messages, transcript paths) is dropped here, immediately. background_tasks becomes a count."""
    out = {}
    if not isinstance(body, dict):
        return out
    for k in _STR_FIELDS:
        v = body.get(k)
        if isinstance(v, str):
            out[k] = v[:512]
    bt = body.get("background_tasks")
    if isinstance(bt, list):
        out["background_tasks"] = len(bt)
    return out


def _num(d, *path):
    for p in path:
        d = d.get(p) if isinstance(d, dict) else None
    if isinstance(d, bool) or not isinstance(d, (int, float)) or not math.isfinite(d):
        return None
    return float(d)


def filter_statusline(body):
    """Statusline JSON -> session_id and three numbers (spec 8). Nothing else survives."""
    out = {}
    if not isinstance(body, dict):
        return out
    if isinstance(body.get("session_id"), str):
        out["session_id"] = body["session_id"][:512]
    for key, path in (("context_pct", ("context_window", "used_percentage")),
                      ("cost_usd", ("cost", "total_cost_usd")),
                      ("limit_pct", ("rate_limits", "five_hour", "used_percentage"))):
        v = _num(body, *path)
        if v is not None:
            out[key] = v
    return out


# ---------------------------------------------------------------- repo identity (4.4)

def git_resolver(cwd):
    """(repo_key, repo_name) of a cwd: the git common dir, so all worktrees of a repo agree."""
    try:
        r = subprocess.run(["git", "-C", cwd, "rev-parse", "--git-common-dir"],
                           capture_output=True, text=True, timeout=2)
        p = r.stdout.strip()
        if r.returncode == 0 and p:
            p = os.path.realpath(os.path.join(cwd, p))
            base = os.path.basename(p)
            if base == ".git":
                return p, os.path.basename(os.path.dirname(p))
            return p, base[:-4] if base.endswith(".git") else base
    except (OSError, subprocess.SubprocessError):
        pass
    return cwd, os.path.basename(cwd.rstrip("/")) or "/"


def make_label(name):
    out = ""
    for ch in name.upper()[:4]:
        out += ch if (ch.isascii() and (ch.isalnum() or ch in "-_.")) else "?"
    return out.ljust(4)


def tool_category(tool):
    if tool in ("Read", "Grep", "Glob", "LSP"):
        return READ
    if tool in ("Edit", "Write", "NotebookEdit"):
        return EDIT
    if tool == "Bash":
        return SHELL
    if tool in ("WebFetch", "WebSearch"):
        return WEB
    if tool in ("Agent", "Workflow") or tool.startswith("Task"):
        return AGENT
    if tool.startswith("mcp__"):
        return MCP
    return OTHER


# ---------------------------------------------------------------- model

class Session:
    def __init__(self, sid, cwd, now):
        self.sid = sid
        self.cwd = cwd            # kept only for /clear continuity and the git lookup
        self.state = IDLE
        self.since = now
        self.last_event = now
        self.cat = THINK
        self.subagents = {}       # agent_id -> time last seen
        self.repo_key = ""
        self.species = 0
        self.variant = 0
        self.label = "    "
        self.ctx = None           # percent 0..100, None = unknown
        self.cost = 0.0
        self.slot = None
        self.cleared_at = None    # SessionEnd(clear) seen: keeps the slot for CLEAR_WINDOW_S


class Model:
    def __init__(self, resolver=git_resolver, daily_budget=0.0):
        self.resolver = resolver
        self.daily_budget = daily_budget
        self.sessions = {}
        self.paused = False
        self._repo_cache = {}
        self._limit = None        # (percent, time)

    # -- events
    def event(self, ev, now):
        sid, name = ev.get("session_id"), ev.get("hook_event_name")
        if not sid or not name:
            return
        if name == "SessionEnd":
            return self._end(sid, ev, now)
        s = self.sessions.get(sid)
        if s is None and name == "SessionStart" and ev.get("source") == "clear":
            s = self._adopt_cleared(sid, ev.get("cwd"), now)
        if s is None:
            s = self._create(sid, ev.get("cwd"), now)
        s.last_event = now
        s.cleared_at = None
        if s.cwd is None and ev.get("cwd"):
            s.cwd = ev["cwd"]
        self._apply(s, name, ev, now)
        self._rebalance()

    def _set(self, s, state, now):
        if s.state != state:
            s.state, s.since = state, now

    def _apply(self, s, name, ev, now):
        agent = ev.get("agent_id")
        tool = ev.get("tool_name", "")
        ntype = ev.get("notification_type", "")
        wait = None               # the state this event asks for when it needs me
        if name == "PermissionRequest":
            wait = QUESTION if tool == "AskUserQuestion" else PERMISSION
        elif name == "PreToolUse" and tool == "AskUserQuestion":
            wait = QUESTION
        elif name == "Notification" and ntype == "permission_prompt":
            wait = PERMISSION
        elif (name == "Notification" and ntype in ELICIT_TYPES) or name == "Elicitation":
            wait = QUESTION
        # subagents: set bookkeeping; they never move the session out of a wait (rule of 4.2)
        if name == "SubagentStart" and agent:
            s.subagents[agent] = now
            if s.state in (IDLE, DONE):
                self._set(s, WORKING, now)
            return
        if name == "SubagentStop":
            s.subagents.pop(agent, None)
            return
        if agent:
            if agent in s.subagents:
                s.subagents[agent] = now
            if wait is not None:
                self._set(s, wait, now)
            return
        if wait is not None:
            self._set(s, wait, now)
            return
        if name == "SessionStart":
            if ev.get("source") != "compact":     # a compaction mid-turn is not a new session
                s.subagents.clear()
                self._set(s, IDLE, now)
        elif name == "UserPromptSubmit":
            s.cat = THINK
            self._set(s, WORKING, now)
        elif name in TOOL_EVENTS:
            s.cat = tool_category(tool)
            self._set(s, WORKING, now)
        elif name in ("PermissionDenied", "ElicitationResult"):
            self._set(s, WORKING, now)
        elif name == "Notification" and ntype == "agent_completed":
            self._set(s, DONE, now)
        elif name == "Stop":
            self._set(s, WORKING if ev.get("background_tasks", 0) else DONE, now)
        elif name == "StopFailure":
            self._set(s, FAILED, now)
        elif name == "PostCompact":
            s.ctx = 0.0

    def _end(self, sid, ev, now):
        s = self.sessions.get(sid)
        if s is None:
            return
        if ev.get("reason") == "clear":
            s.cleared_at = now
            s.subagents.clear()
            s.last_event = now
            self._set(s, IDLE, now)
            return
        del self.sessions[sid]
        self._rebalance()

    def statusline(self, ev, now):
        if "limit_pct" in ev:
            self._limit = (ev["limit_pct"], now)
        s = self.sessions.get(ev.get("session_id"))
        if s is None:
            return
        if "context_pct" in ev:
            s.ctx = max(0.0, min(100.0, ev["context_pct"]))
        if "cost_usd" in ev:
            s.cost = max(0.0, ev["cost_usd"])

    # -- time
    def tick(self, now):
        for sid, s in list(self.sessions.items()):
            for a, t in list(s.subagents.items()):
                if now - t >= SUBAGENT_STALE_S:
                    del s.subagents[a]
            idle_for = now - s.last_event
            if s.cleared_at is not None and now - s.cleared_at >= CLEAR_WINDOW_S:
                del self.sessions[sid]
                continue
            if s.state == DONE and idle_for >= DONE_TO_IDLE_S:
                self._set(s, IDLE, now)
            elif s.state == FAILED and idle_for >= FAILED_TO_IDLE_S:
                self._set(s, IDLE, now)
            if idle_for >= (STALE_IDLE_S if s.state in (IDLE, DONE) else STALE_BUSY_S):
                del self.sessions[sid]
        self._rebalance()

    # -- sessions and slots
    def _repo(self, cwd):
        if cwd is None:
            return "", "?"
        if cwd not in self._repo_cache:
            try:
                self._repo_cache[cwd] = self.resolver(cwd)
            except Exception:
                self._repo_cache[cwd] = (cwd, os.path.basename(cwd.rstrip("/")))
        return self._repo_cache[cwd]

    def _create(self, sid, cwd, now):
        s = Session(sid, cwd, now)
        key, name = self._repo(cwd)
        s.repo_key, s.label = key, make_label(name)
        s.species = zlib.crc32(key.encode("utf-8", "replace")) % NSPECIES
        mates = [m.variant for m in self.sessions.values() if m.repo_key == key]
        free = [v for v in range(NVARIANTS) if v not in mates]
        s.variant = free[0] if free else min(range(NVARIANTS), key=lambda v: (mates.count(v), v))
        free_slots = self._free_slots()
        s.slot = free_slots[0] if free_slots else None
        self.sessions[sid] = s
        return s

    def _adopt_cleared(self, sid, cwd, now):
        for old_id, s in list(self.sessions.items()):
            if s.cleared_at is not None and now - s.cleared_at <= CLEAR_WINDOW_S and cwd and s.cwd == cwd:
                del self.sessions[old_id]
                s.sid = sid
                self.sessions[sid] = s
                return s
        return None

    def _free_slots(self):
        used = {s.slot for s in self.sessions.values() if s.slot is not None}
        return [i for i in range(MAX_SLOTS) if i not in used]

    def _rebalance(self):
        live = list(self.sessions.values())
        while True:
            hidden = [s for s in live if s.slot is None]
            free = self._free_slots()
            if free and hidden:
                max(hidden, key=lambda s: (s.state, s.last_event)).slot = free[0]
                continue
            waiting = [s for s in hidden if s.state >= QUESTION]
            visible = [s for s in live if s.slot is not None and s.state < QUESTION]
            if not waiting or not visible:
                return
            w = max(waiting, key=lambda s: (s.state, s.last_event))
            v = min(visible, key=lambda s: (s.state, s.last_event))
            w.slot, v.slot = v.slot, None

    # -- output
    def bowl(self, now):
        if self._limit and now - self._limit[1] < LIMIT_STALE_S:
            return int(round(min(100.0, self._limit[0]))), True
        if self.daily_budget > 0:
            total = sum(s.cost for s in self.sessions.values())
            return int(round(min(100.0, 100.0 * total / self.daily_budget))), False
        return 0, False

    def snapshot(self, now):
        bowl, is_limit = self.bowl(now)
        vis = sorted((s for s in self.sessions.values() if s.slot is not None), key=lambda s: s.slot)
        hid = [s for s in self.sessions.values() if s.slot is None]
        slots = tuple(SlotRec(s.slot, s.state, s.cat, len(s.subagents), s.species, s.variant,
                              CTX_UNKNOWN if s.ctx is None else int(round(s.ctx)),
                              min(255, max(0, int((now - s.since) // 60))), s.label) for s in vis)
        flags = (FLAG_BOWL_IS_LIMIT if is_limit else 0) | (FLAG_PAUSED if self.paused else 0)
        return Snapshot(flags, bowl, len(hid), sum(1 for s in hid if s.state >= QUESTION), slots)

    def table(self, now):
        """Rows for `status`: no text of the sessions, only ids (shortened), label and numbers."""
        return [{"id": s.sid[:8], "label": s.label.strip(), "slot": s.slot, "state": STATE_NAMES[s.state],
                 "tool": CAT_NAMES[s.cat], "sub": len(s.subagents), "species": s.species,
                 "variant": s.variant, "minutes": int((now - s.since) // 60),
                 "idle_s": int(now - s.last_event),
                 "ctx": None if s.ctx is None else int(round(s.ctx))}
                for s in sorted(self.sessions.values(), key=lambda s: (s.slot is None, s.slot or 0))]
