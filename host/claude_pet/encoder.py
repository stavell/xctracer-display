"""Snapshot encoder / decoder, version 1 (spec 7.3). Little-endian, opcode 0x30."""
from typing import NamedTuple, Tuple

OPCODE = 0x30
VERSION = 1
FLAG_BOWL_IS_LIMIT = 1
FLAG_PAUSED = 2
CTX_UNKNOWN = 255
MAX_SLOTS = 6


class SlotRec(NamedTuple):
    index: int
    state: int
    cat: int
    sub: int
    species: int
    variant: int
    ctx: int
    minutes: int
    label: str


class Snapshot(NamedTuple):
    flags: int
    bowl: int
    hidden: int
    hidden_waiting: int
    slots: Tuple[SlotRec, ...]

    def key(self):
        """Content without the minutes counters: a change worth a send. Minutes ride on the heartbeat."""
        return (self.flags, self.bowl, self.hidden, self.hidden_waiting,
                tuple(s[:7] + s[8:] for s in self.slots))


def _label(text):
    b = text.encode("ascii", "replace")[:4]
    return b.ljust(4, b" ")


def encode(snap, seq):
    if len(snap.slots) > MAX_SLOTS:
        raise ValueError("too many slots")
    out = bytearray([OPCODE, VERSION, seq & 0xFF, snap.flags & 0xFF, min(snap.bowl, 100),
                     min(snap.hidden, 99), min(snap.hidden_waiting, 99), len(snap.slots)])
    for s in snap.slots:
        out += bytes([s.index, s.state, s.cat, min(s.sub, 15), s.species, s.variant,
                      min(s.ctx, 255), min(s.minutes, 255)])
        out += _label(s.label)
    return bytes(out)


def decode(data):
    """Strict inverse of encode (the box's decoder rules); raises ValueError on a bad snapshot."""
    if len(data) < 8 or data[0] != OPCODE or data[1] != VERSION:
        raise ValueError("bad header")
    n = data[7]
    if n > MAX_SLOTS or len(data) != 8 + 12 * n:
        raise ValueError("bad length")
    slots = []
    for i in range(n):
        r = data[8 + 12 * i: 20 + 12 * i]
        if r[0] > 5 or r[1] > 5:
            raise ValueError("bad slot")
        slots.append(SlotRec(*r[:8], r[8:].decode("ascii")))
    return data[2], Snapshot(data[3], data[4], data[5], data[6], tuple(slots))
