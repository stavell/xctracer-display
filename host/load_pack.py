#!/usr/bin/env python3
"""Load a map pack into the box's QSPI flash over USB serial.

    python host/load_pack.py <pack> [--port /dev/cu.usbmodemXXXX] [--chunk 512] [--no-reboot] [--status]

    python host/load_pack.py maps/bulgaria.pack          # the one command: finds the box, erases, streams, checks, reboots it
    python host/load_pack.py --status               # ask the box about the pack it holds (header and whole-pack CRC)

Needs a box image with the map loader (feather_sense, feather_sense_diag or feather_sense_box). The box keeps flying/running
while it is loaded; from the first command the map is not drawn (the flash is being rewritten) and the box restarts when
the load is done.

Protocol (firmware/src/platform/storage/pack_proto.h): frames A5 5A | cmd | len u16le | payload | crc32 u32le (zlib crc of
cmd, len, payload) go to the box; it answers with text lines "pk ok ..." / "pk err ...", and also prints its normal status
lines, which are ignored here. Stop-and-wait: every chunk is acknowledged with its offset, a failed or lost chunk is sent
again. The header that makes the pack valid is written last, by the box, after it has checked the CRC of the bytes it stored;
an interrupted load therefore leaves "no pack", never half a pack.

The serial port is always closed on exit (the bootloader uses the same port for uploads).
"""
import argparse
import os
import select
import struct
import subprocess
import sys
import time
import zlib

CMD_HELLO, CMD_ERASE, CMD_WRITE, CMD_FINISH, CMD_STATUS, CMD_EXIT = 1, 2, 3, 4, 5, 6
ADAFRUIT_VID = 0x239A


def frame(cmd, payload=b""):
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    return b"\xa5\x5a" + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def check_pack(data):
    """The pack's own header (host/terrain/FORMAT.md): magic, version, size and the file CRC with bytes 12..15 zeroed."""
    if len(data) < 192 or data[:4] != b"BGMP":
        raise SystemExit("not a map pack (no BGMP header)")
    version, hsize, size, crc = struct.unpack_from("<HHII", data, 4)
    if version != 1 or hsize != 192:
        raise SystemExit("unsupported pack version %d" % version)
    if size != len(data):
        raise SystemExit("pack header says %d bytes, the file has %d" % (size, len(data)))
    zeroed = data[:12] + b"\0\0\0\0" + data[16:]
    if zlib.crc32(zeroed) & 0xFFFFFFFF != crc:
        raise SystemExit("pack file CRC is wrong: the file is damaged")


class ExecPort:
    """A pipe to a program instead of a serial port (the emulator's loader, for the end-to-end test): --port exec:<command>."""

    def __init__(self, command):
        self.p = subprocess.Popen(command, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.timeout = 0.1

    def write(self, b):
        self.p.stdin.write(b)
        self.p.stdin.flush()

    def flush(self):
        pass

    def read(self, n):
        r, _, _ = select.select([self.p.stdout], [], [], self.timeout)
        return os.read(self.p.stdout.fileno(), n) if r else b""

    def reset_input_buffer(self):
        pass

    def close(self):
        try:
            self.p.stdin.close()
        except OSError:
            pass
        try:
            self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.p.kill()


def find_port():
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == ADAFRUIT_VID]
    if len(ports) == 1:
        return ports[0].device
    names = ", ".join(p.device for p in ports) or "none found"
    raise SystemExit("cannot pick the box's serial port (Adafruit USB devices: %s); give --port" % names)


class Link:
    def __init__(self, ser):
        self.ser = ser
        self.buf = b""

    def request(self, cmd, payload=b"", timeout=5.0, match=None):
        """Send a frame, return the first matching 'pk ...' line. match: substring the line must contain."""
        self.ser.write(frame(cmd, payload))
        self.ser.flush()
        return self.wait_line(timeout, match)

    def wait_line(self, timeout, match=None):
        end = time.time() + timeout
        while time.time() < end:
            self.buf += self.ser.read(4096)
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                text = line.decode("ascii", "replace").strip()
                if text.startswith("pk ") and (match is None or match in text or text.startswith("pk err")):
                    return text
        return None


def fail(msg, line=None):
    raise SystemExit("%s%s" % (msg, (": " + line) if line else " (no answer from the box)"))


def hello(link):
    for attempt in range(8):
        line = link.request(CMD_HELLO, b"LOADPACK", timeout=1.5)
        if line and line.startswith("pk ok hello"):
            return line
    fail("the box does not answer the loader (flash an image with the map loader; close other programs using the port)")


def field(line, name, default=None):
    for tok in line.split():
        if tok.startswith(name + "="):
            return tok[len(name) + 1:]
    return default


def load(link, data, chunk, reboot):
    crc = zlib.crc32(data) & 0xFFFFFFFF
    line = hello(link)
    print(line)
    maxchunk = int(field(line, "chunk", "512"))
    chunk = min(chunk, maxchunk)
    cap = int(field(line, "cap", "0")) - int(field(line, "base", "4096"))
    if len(data) > cap:
        fail("the pack (%d bytes) does not fit the flash (%d bytes)" % (len(data), cap))

    print("erasing %d kB ..." % ((len(data) + 4095) // 4096 * 4))
    t0 = time.time()
    line = link.request(CMD_ERASE, struct.pack("<I", len(data)), timeout=180)
    if not line or not line.startswith("pk ok erase"):
        fail("erase failed", line)
    print("  erased in %.1f s" % (time.time() - t0))

    t0 = time.time()
    retries = 0
    off = 0
    last_print = 0
    while off < len(data):
        part = data[off:off + chunk]
        payload = struct.pack("<I", off) + part
        for attempt in range(6):
            line = link.request(CMD_WRITE, payload, timeout=3, match="off=%d " % off)
            if line and line.startswith("pk ok w ") and ("off=%d " % off) in line + " ":
                break
            retries += 1
            time.sleep(0.05)
        else:
            fail("chunk at %d failed 6 times" % off, line)
        off += len(part)
        if time.time() - last_print > 1.0 or off == len(data):
            last_print = time.time()
            rate = off / max(time.time() - t0, 1e-3) / 1024
            print("\r  written %7d / %d bytes (%3d%%)  %.0f kB/s  retries %d" % (off, len(data), off * 100 // len(data), rate, retries), end="", flush=True)
    print()

    print("checking the stored bytes (CRC-32 %08x) and writing the header ..." % crc)
    line = link.request(CMD_FINISH, struct.pack("<II", len(data), crc), timeout=60)
    if not line or not line.startswith("pk ok finish"):
        fail("the box did not accept the pack", line)
    print(" ", line)

    line = link.request(CMD_STATUS, timeout=60)
    print(" ", line)
    if not line or "match=1" not in line:
        fail("verification after the load failed", line)

    line = link.request(CMD_EXIT, bytes([1 if reboot else 0]), timeout=5)
    print(" ", line)
    print("done: %d bytes loaded in %.1f s%s" % (len(data), time.time() - t0, "; the box restarts" if reboot else ""))


def status(link):
    line = hello(link)
    print(line)
    line = link.request(CMD_STATUS, timeout=60)
    print(line)
    ok = bool(line and ("hdr=ok" in line and "match=1" in line))
    link.request(CMD_EXIT, bytes([0]), timeout=5)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pack", nargs="?")
    ap.add_argument("--port")
    ap.add_argument("--chunk", type=int, default=512, help="data bytes per frame (the box allows up to 512)")
    ap.add_argument("--no-reboot", action="store_true", help="end the session without asking the box to restart (it then keeps the old map in RAM only until its next boot)")
    ap.add_argument("--status", action="store_true", help="only ask the box about the stored pack")
    a = ap.parse_args()
    if not a.status and not a.pack:
        ap.error("give the pack file, or --status")
    data = b""
    if a.pack:
        with open(a.pack, "rb") as f:
            data = f.read()
        if not a.status:
            check_pack(data)
    port = a.port
    if port and port.startswith("exec:"):
        ser = ExecPort(port[5:])
    else:
        import serial
        ser = serial.Serial(port or find_port(), 115200, timeout=0.1)
    try:
        link = Link(ser)
        if a.status:
            return 0 if status(link) else 1
        load(link, data, a.chunk, not a.no_reboot)
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
