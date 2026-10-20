"""Change coalescing + heartbeat (spec 7.3) and the --fake-ble file sink. Time is passed in."""
from .encoder import encode


class Publisher:
    """poll(now) sends a snapshot when the content changed (merged over `merge` s, so at most 1/merge
    per second) or when `heartbeat` s passed since the last send."""

    def __init__(self, snapshot_fn, sink, merge=0.2, heartbeat=10.0):
        self.snapshot_fn, self.sink, self.merge, self.heartbeat = snapshot_fn, sink, merge, heartbeat
        self.seq = 0
        self.last_key = None
        self.last_send = None
        self.pending_since = None

    def poll(self, now):
        snap = self.snapshot_fn(now)
        key = snap.key()
        due = self.last_send is None or now - self.last_send >= self.heartbeat
        if key != self.last_key and not due:
            if self.pending_since is None:
                self.pending_since = now
            eps = 1e-9                    # 1.2 - 1.0 < 0.2 in floating point
            due = now - self.pending_since >= self.merge - eps and now - self.last_send >= self.merge - eps
        elif key == self.last_key:
            self.pending_since = None
        if not due:
            return False
        self.sink.send(encode(snap, self.seq), now)
        self.seq = (self.seq + 1) & 0xFF
        self.last_key, self.last_send, self.pending_since = key, now, None
        return True


class FakeBleSink:
    """Shared feed format (the box emulator plays it): one line per snapshot `<ms> <hex>`,
    ms since the file was opened, lowercase hex without spaces; `#` lines are comments."""

    def __init__(self, path, clock):
        self.t0 = clock()
        self.f = open(path, "w", encoding="ascii")
        self.f.write("# claude_pet feed: <ms since start> <snapshot hex>\n")
        self.f.flush()

    def send(self, data, now):
        ms = max(0, int((now - self.t0) * 1000))
        self.f.write("%d %s\n" % (ms, data.hex()))
        self.f.flush()

    def close(self):
        self.f.close()
