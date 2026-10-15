"""Record the box's raw compass sensor readings from its USB serial status line.
  python orient_log.py 75 orient.log      record for 75 seconds
The status line is only printed while the compass page is active and the tracer gives no GPS heading."""
import glob
import sys
import time

import serial

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 75
out = sys.argv[2] if len(sys.argv) > 2 else "orient.log"
s = serial.Serial(glob.glob("/dev/cu.usbmodem*")[0], 115200, timeout=1)
t0 = time.time()
n = 0
with open(out, "w") as f:
    while time.time() - t0 < secs:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("cmp"):
            f.write(f"{time.time() - t0:.2f} {line}\n")
            f.flush()
            n += 1
print(f"recorded {n} readings in {out}")
