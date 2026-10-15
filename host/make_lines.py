"""Turn recorded data into XCTRC line files for the PC emulator (firmware/emu).
  python make_lines.py igc  flight.igc --start 4185 --seconds 600 > lines.txt   one line per second
  python make_lines.py capture xct_capture.log > live.txt                        the real tracer's raw lines
"""
import argparse
import re
import sys
from igc import read_igc, xctrc_line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["igc", "capture"])
    ap.add_argument("path")
    ap.add_argument("--start", type=int, default=0)
    ap.add_argument("--seconds", type=int, default=10 ** 9)
    a = ap.parse_args()
    if a.kind == "igc":
        date, fx = read_igc(a.path)
        t0 = fx[0].t
        for f in fx:
            if f.t - t0 >= a.start and f.t - t0 < a.start + a.seconds:
                print(xctrc_line(date, f))
    else:
        for line in open(a.path, errors="replace"):
            m = re.search(r"b'(\$XCTRC[^']*?)\\r\\n'", line)
            if m:
                print(m.group(1))


main()
