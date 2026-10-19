#!/usr/bin/env python3
"""Download the box's flight log over USB serial and convert it to CSV.

    python host/logdl.py --info                       # state and size of the log
    python host/logdl.py logs/ride1                   # download -> logs/ride1.bin, ride1_lines.csv, ride1_yaw.csv
    python host/logdl.py logs/ride1 --erase           # ... and then erase the log (needed before the next recording)
    python host/logdl.py --erase                      # only erase (also formats a log that was never formatted)
    python host/logdl.py --decode logs/ride1.bin      # convert a file that was downloaded earlier

Then evaluate it against the dead-reckoning code:  firmware/emu/build.sh logeval logs/ride1.bin [--latency MS] [--wind]

The log is the last 256 kB of the box's QSPI flash (firmware/src/platform/storage/flash_log.h). The box records only while
the vario app is open and receives tracer lines. A full log stops recording; it is never overwritten by itself.

Protocol: text lines to the box (LOGINFO, LOGREAD off len, LOGERASE), answers "lg ..." lines; the box prints its status
lines too, which are ignored. Every read is checked with a CRC-32 and repeated if it fails. The serial port is always
closed on exit (the bootloader uses the same port for uploads); the program never keeps a reader running.
"""
import argparse
import csv
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from load_pack import ExecPort, find_port  # noqa: E402

PAGE = 256
PAGE_MARK = 0xA5
REC_SESSION, REC_LINE, REC_YAW, REC_PAD = 0x53, 0x4C, 0x59, 0xFF
YAW_GRID_MS = 40
YAW_UNIT = 0.05
YAW_STEP_UNITS = 5


def decode(data):
    """Mirror of firmware/emu/flightlog_io.h decode(); format in firmware/src/apps/vario/core/flight_log.h.
    Returns (sessions, lines, yaw, bad_pages); lines and yaw are lists of dicts with a 'session' index."""
    sessions, lines, yaw, bad = [], [], [], 0
    session = -1
    for pg in range(0, len(data) - PAGE + 1, PAGE):
        p = data[pg:pg + PAGE]
        if p[0] != PAGE_MARK:
            bad += 1
            continue
        i = 1
        while i < PAGE:
            t, room = p[i], PAGE - i
            if t == REC_PAD:
                break
            if t == REC_SESSION and room >= 7:
                sessions.append(struct.unpack_from("<I", p, i + 1)[0])
                session = len(sessions) - 1
                i += 7
            elif t == REC_LINE and room >= 23:
                ms, hh, mm, ss, flags, lat, lon, sp, co, va = struct.unpack_from("<IBBBBiiHHh", p, i + 1)
                lines.append(dict(session=max(session, 0), rx_ms=ms, utc="%02d:%02d:%02d" % (hh, mm, ss), fix=flags & 1,
                                  lat=lat * 1e-7, lon=lon * 1e-7,
                                  speed_kmh=None if sp == 0xFFFF else sp * 0.1,
                                  course_deg=None if co == 0xFFFF else co * 0.1,
                                  vario_ms=None if va == -32768 else va * 0.01))
                i += 23
            elif t == REC_YAW and room >= 8 and room >= 8 + p[i + 7]:
                ms, first, n = struct.unpack_from("<IhB", p, i + 1)
                v = first
                s = max(session, 0)
                yaw.append(dict(session=s, ms=ms, yaw_dps=v * YAW_UNIT))
                for k in range(n):
                    v += struct.unpack_from("b", p, i + 8 + k)[0] * YAW_STEP_UNITS
                    yaw.append(dict(session=s, ms=ms + YAW_GRID_MS * (k + 1), yaw_dps=v * YAW_UNIT))
                i += 8 + n
            else:
                bad += 1
                break
    return sessions, lines, yaw, bad


def write_csv(base, sessions, lines, yaw):
    out = []
    for name, rows in (("lines", lines), ("yaw", yaw)):
        path = "%s_%s.csv" % (base, name)
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            if rows:
                w.writerow(list(rows[0].keys()))
                for r in rows:
                    w.writerow(["" if v is None else (("%.7f" % v) if k in ("lat", "lon") else (("%.3f" % v) if isinstance(v, float) else v))
                                for k, v in r.items()])
        out.append(path)
    return out


class Box:
    def __init__(self, ser):
        self.ser = ser
        self.buf = b""

    def send(self, text):
        self.ser.write(text.encode("ascii") + b"\n")
        self.ser.flush()

    def lines_until(self, done, timeout):
        """Collect 'lg ...' lines until done(line) is true; None on timeout."""
        got = []
        end = time.time() + timeout
        while time.time() < end:
            self.buf += self.ser.read(4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                t = raw.decode("ascii", "replace").strip()
                if t.startswith("lg "):
                    got.append(t)
                    if done(t):
                        return got
        return None

    def command(self, text, done, timeout=3.0, tries=4):
        for _ in range(tries):
            self.send(text)
            r = self.lines_until(done, timeout)
            if r is not None:
                return r
        raise SystemExit("the box does not answer %r (is the vario/box image with the flight log flashed, and the port free?)" % text)


def field(line, name):
    for tok in line.split():
        if tok.startswith(name + "="):
            return tok[len(name) + 1:]
    return None


def info(box):
    r = box.command("LOGINFO", lambda t: t.startswith("lg info"))
    line = r[-1]
    return line, field(line, "state"), int(field(line, "used")), int(field(line, "cap")), int(field(line, "dropped"))


def download(box, used, chunk=2048):
    data = bytearray()
    while len(data) < used:
        n = min(chunk, used - len(data))
        off = len(data)
        for attempt in range(5):
            r = box.command("LOGREAD %d %d" % (off, n), lambda t: t.startswith("lg ok read") or t.startswith("lg err"), timeout=5.0)
            if r[-1].startswith("lg err"):
                raise SystemExit("box refused: " + r[-1])
            part = bytearray()
            for t in r:
                if t.startswith("lg d "):
                    _, _, o, hx = t.split(" ", 3)
                    if int(o) == off + len(part):
                        part += bytes.fromhex(hx)
            want = int(field(r[-1], "crc"))
            if len(part) == n and zlib.crc32(bytes(part)) & 0xFFFFFFFF == want:
                data += part
                break
        else:
            raise SystemExit("read at %d failed 5 times (CRC)" % off)
        print("\r%d / %d bytes" % (len(data), used), end="", file=sys.stderr)
    print(file=sys.stderr)
    return bytes(data)


def erase(box):
    r = box.command("LOGERASE", lambda t: t.startswith("lg ok erase") or t.startswith("lg err"))
    if r[-1].startswith("lg err"):
        raise SystemExit("erase refused: " + r[-1])
    print("erasing (a few seconds, the map is off meanwhile)...")
    done = box.lines_until(lambda t: t.startswith("lg ok erased") or t.startswith("lg err"), 60.0)
    if not done or not done[-1].startswith("lg ok erased"):
        raise SystemExit("erase did not finish: %s" % (done[-1] if done else "no answer"))
    print("log erased; recording starts at the next vario session")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base", nargs="?", help="output path without extension")
    ap.add_argument("--port")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--erase", action="store_true", help="erase the log (after the download if a path is given)")
    ap.add_argument("--decode", metavar="BIN", help="only convert a downloaded .bin to CSV")
    a = ap.parse_args()

    if a.decode:
        data = open(a.decode, "rb").read()
        s, l, y, bad = decode(data)
        base = a.base or os.path.splitext(a.decode)[0]
        print("%d sessions, %d lines, %d yaw samples, %d bad pages" % (len(s), len(l), len(y), bad))
        print("\n".join(write_csv(base, s, l, y)))
        return

    if a.port and a.port.startswith("exec:"):
        ser = ExecPort(a.port[5:])
    else:
        import serial
        ser = serial.Serial(a.port or find_port(), 115200, timeout=0.1)
    try:
        box = Box(ser)
        if not (a.port or "").startswith("exec:"):
            try:
                from settime import send_time
                send_time(ser, 0.3)                       # free: the box learns the PC's time whenever we connect
            except Exception:
                pass
        line, state, used, cap, dropped = info(box)
        print(line)
        if state == "unformatted" and not a.erase:
            print("the log area was never formatted: run with --erase once (it takes a few seconds)")
        if a.info or (not a.base and not a.erase):
            return
        if a.base:
            if used == 0:
                print("the log is empty")
            else:
                data = download(box, used)
                os.makedirs(os.path.dirname(os.path.abspath(a.base)), exist_ok=True)
                with open(a.base + ".bin", "wb") as f:
                    f.write(data)
                s, l, y, bad = decode(data)
                print("%s.bin: %d bytes, %d sessions, %d lines, %d yaw samples, %d bad pages, %d records dropped on the box" %
                      (a.base, len(data), len(s), len(l), len(y), bad, dropped))
                print("\n".join(write_csv(a.base, s, l, y)))
        if a.erase:
            erase(box)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
