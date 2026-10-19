#!/usr/bin/env python3
"""Set the box's clock from this computer over USB serial: UTC seconds and this computer's UTC offset.

    python host/settime.py [--port /dev/cu.usbmodemXXXX]

The box keeps the time for 30 days (its 32 kHz crystal drifts under 2 s/day; a tracer GPS fix overrides it) and shows it
on the sleep clock. load_pack.py and logdl.py send the same command whenever they connect (time_line / send_time here).
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def time_line(now=None):
    """'TIME <utc epoch seconds> <minutes east of UTC>' for the computer's current time and local offset."""
    now = time.time() if now is None else now
    lt = time.localtime(now)
    return "TIME %d %d" % (int(now), int((lt.tm_gmtoff or 0) // 60))


def send_time(ser, wait=0.4):
    """Fire and forget (older images ignore it); returns the box's 'tm ...' answer line if one came back."""
    ser.write(time_line().encode("ascii") + b"\n")
    ser.flush()
    end = time.time() + wait
    buf = b""
    while time.time() < end:
        buf += ser.read(256)
        for raw in buf.split(b"\n"):
            t = raw.decode("ascii", "replace").strip()
            if t.startswith("tm "):
                return t
    return None


def main():
    from load_pack import find_port
    import serial
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port")
    a = ap.parse_args()
    ser = serial.Serial(a.port or find_port(), 115200, timeout=0.1)
    try:
        ans = None
        for _ in range(5):
            ans = send_time(ser, 1.0)
            if ans:
                break
        print(time_line())
        print(ans or "no answer (is this image the box with the TIME command?)")
        return 0 if ans and ans.startswith("tm ok") else 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
