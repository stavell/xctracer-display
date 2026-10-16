"""Round-trip test: compile tdec_test.c, decode OUT.bin in C, compare with the encoder's quantised grid and with a
Python bilinear reference. usage: roundtrip.py OUT.bin"""
import subprocess, sys, os
import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
binf = os.path.abspath(sys.argv[1])
exe = binf + ".test"
subprocess.check_call(["cc", "-O2", "-std=c99", "-Wall", "-Wextra", "-o", exe, f"{here}/tdec_test.c"])
dump, qf = binf + ".dump", binf + ".queries"
print(subprocess.check_output([exe, binf, dump, qf]).decode())
ref = np.load(binf + ".ref.npz")
v, tm = ref["v"], ref["tm"]
d = np.fromfile(dump, np.int16).reshape(v.shape)
T = v.shape[0] // tm.shape[0]
m = np.kron(tm, np.ones((T, T), bool))
bad = int((d[m] != v[m]).sum())
print(f"cells compared {int(m.sum())}, mismatches {bad}; unstored cells all zero: {bool((d[~m] == 0).all())}")
assert bad == 0

# header values for the bilinear reference
import struct
f = open(binf, "rb").read()
lat0, lon0, dy, dx = struct.unpack_from("<iiii", f, 16)
q = f[16 + 24]
worst = 0
n = 0
for ln in open(qf):
    la, lo, h = ln.split(); la = int(la); lo = int(lo); h = float(h)
    rf = (lat0 - la) / dy - 0.5; cf = (lo - lon0) / dx - 0.5
    r, c = int(np.floor(rf)), int(np.floor(cf)); fr, fc = rf - r, cf - c
    hh = q * ((v[r, c] * (1 - fc) + v[r, c + 1] * fc) * (1 - fr) + (v[r + 1, c] * (1 - fc) + v[r + 1, c + 1] * fc) * fr)
    worst = max(worst, abs(hh - h)); n += 1
print(f"{n} bilinear queries: worst |C - python| = {worst:.4f} m")
assert worst < 0.05
print("ROUND TRIP OK")
