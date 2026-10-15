"""Work out how the board sits in the case from a recorded session (see orient_log.py).

During the session: hold the box (1) flat with the screen facing up, (2) upright on its bottom edge with
the screen toward you, (3) on its left edge so the RIGHT edge points up (screen toward the right), about
6 s each, then (4) turn it slowly through every direction for about 40 s.

Output: the axis mapping for the accelerometer and the magnetometer, as code for src/hw/compass_lis.cpp.
  python orient_analyze.py orient.log
  python orient_analyze.py --selftest
"""
import itertools
import re
import sys

import numpy as np

PAT = re.compile(r"acc=([-\d.]+),([-\d.]+),([-\d.]+) mag=([-\d.]+),([-\d.]+),([-\d.]+)")


def load(path):
    rows = []
    for line in open(path):
        m = PAT.search(line)
        if m:
            rows.append([float(x) for x in m.groups()])
    a = np.array(rows)
    return a[:, :3], a[:, 3:]


def rotations():
    """The 24 proper signed axis permutations (rotation matrices with entries 0, +1, -1)."""
    out = []
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            m = np.zeros((3, 3))
            for r in range(3):
                m[r, perm[r]] = signs[r]
            if round(np.linalg.det(m)) == 1:
                out.append(m)
    return out


def stationary_segments(acc, min_len=12, tol=0.25):
    """Runs of readings with a steady gravity vector, merged if the same direction. Returns mean unit vectors."""
    segs, start = [], 0
    for i in range(1, len(acc) + 1):
        if i == len(acc) or np.linalg.norm(acc[i] - acc[start:i].mean(axis=0)) > tol:
            if i - start >= min_len:
                segs.append((start, i, acc[start:i].mean(axis=0)))
            start = i
    out = []
    for s0, s1, v in segs:
        u = v / np.linalg.norm(v)
        if out and np.dot(out[-1][2], u) > 0.97:
            continue                         # same pose as the previous one
        out.append((s0, s1, u))
    return out


def snap(m):
    """Snap an almost-signed-permutation matrix to an exact one."""
    out = np.zeros((3, 3))
    for r in range(3):
        c = int(np.argmax(np.abs(m[r])))
        out[r, c] = np.sign(m[r, c])
    return out


def sphere_center(m):
    A = np.hstack([2 * m, np.ones((len(m), 1))])
    b = (m ** 2).sum(axis=1)
    x, *_ = np.linalg.lstsq(A, b, rcond=None)
    return x[:3], float(np.sqrt(x[3] + (x[:3] ** 2).sum()))


def analyze(acc, mag, verbose=True):
    poses = stationary_segments(acc)
    if verbose:
        print(f"{len(acc)} readings, {len(poses)} steady poses found")
    if len(poses) < 3:
        raise SystemExit("need three steady poses (flat, upright, right edge up): hold each one still for ~6 s")
    u_flat, u_up, u_right = poses[0][2], poses[1][2], poses[2][2]
    # the up vector, seen in sensor axes, IS the device axis that points up in that pose
    ez, ey, ex = u_flat, u_up, u_right
    R = np.vstack([ex, ey, ez])                         # device = R @ sensor
    err = np.abs(R @ R.T - np.eye(3)).max()
    Ra = snap(R)
    if verbose:
        print(f"pose vectors orthogonality error {err:.2f} (0 = perfect); accelerometer mapping:\n{Ra}")
    # magnetometer: hard-iron centre from the whole session, then pick the mapping that keeps up . m constant
    centre, radius = sphere_center(mag)
    if verbose:
        print(f"magnetometer sphere fit: centre {np.round(centre, 1)} uT, field {radius:.1f} uT")
    up_dev = (Ra @ acc.T).T
    up_dev /= np.linalg.norm(up_dev, axis=1, keepdims=True)
    best = None
    for P in rotations():
        m_dev = (P @ (mag - centre).T).T
        d = (up_dev * m_dev).sum(axis=1)                # constant for the right mapping
        score = d.std()
        if best is None or score < best[0]:
            best = (score, P)
    score, Pm = best
    if verbose:
        print(f"magnetometer mapping (up . field spread {score:.2f} uT):\n{Pm}")
    return Ra, Pm, centre, radius, score


def code_for(R, name):
    axes = "xyz"
    lines = []
    for r in range(3):
        c = int(np.argmax(np.abs(R[r])))
        sign = "" if R[r, c] > 0 else "-"
        lines.append(f"v.{axes[r]} = {sign}{axes[c]};")
    return f"static Vec3 {name}(float x, float y, float z) {{ Vec3 v; " + " ".join(lines) + " return v; }"


def synth(Ra_true, Pm_true, hard_iron, seed=1):
    """A recorded session with a known mapping: three steady poses, then a sweep."""
    rng = np.random.default_rng(seed)
    B = np.array([0.0, 20.0, -40.0])                  # earth field in east/north/up
    up = np.array([0.0, 0.0, 1.0])

    def reading(R_dev_enu):                           # R rows = device axes in ENU
        a_dev = 9.81 * (R_dev_enu @ up)
        m_dev = R_dev_enu @ B
        return a_dev, m_dev

    def to_sensor(a_dev, m_dev):
        return Ra_true.T @ a_dev, Pm_true.T @ m_dev + hard_iron

    def basis(psi, pitch, roll):
        p, t, r = np.radians([psi, pitch, roll])
        x0 = np.array([np.cos(p), -np.sin(p), 0]); y0 = np.array([np.sin(p), np.cos(p), 0]); z0 = np.array([0, 0, 1.0])
        y1 = y0 * np.cos(t) + z0 * np.sin(t); z1 = -y0 * np.sin(t) + z0 * np.cos(t)
        x2 = x0 * np.cos(r) - z1 * np.sin(r); z2 = x0 * np.sin(r) + z1 * np.cos(r)
        return np.vstack([x2, y1, z2])

    accs, mags = [], []

    def add(R, n=30, noise=0.03):
        a, m = reading(R)
        for _ in range(n):
            sa, sm = to_sensor(a, m)
            accs.append(sa + rng.normal(0, noise, 3)); mags.append(sm + rng.normal(0, noise * 4, 3))

    add(basis(10, 0, 0))                              # flat, screen up
    add(basis(10, 90, 0))                             # upright, top up
    add(basis(10, 0, -90))                            # right edge up (roll about y so +x points up)
    for psi in range(0, 360, 20):
        for pitch in range(-90, 91, 30):
            for roll in range(-90, 91, 45):
                add(basis(psi, pitch, roll), n=1, noise=0.5)
    return np.array(accs), np.array(mags)


def selftest():
    ok = True
    for i, (Ra, Pm) in enumerate(itertools.product(rotations()[::5], rotations()[::7])):
        hi = np.array([12.0, -18.0, 7.0])
        acc, mag = synth(Ra, Pm, hi, seed=i)
        got_a, got_m, centre, radius, score = analyze(acc, mag, verbose=False)
        good = np.allclose(got_a, Ra) and np.allclose(got_m, Pm) and np.allclose(centre, hi, atol=1.5)
        ok &= good
        if not good and i < 2:
            print("FAIL case", i, "\n", Ra, "\n", got_a, "\n", Pm, "\n", got_m, centre)
    print("selftest", "PASSED" if ok else "FAILED")
    return ok


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(0 if selftest() else 1)
    acc, mag = load(sys.argv[1])
    Ra, Pm, centre, radius, score = analyze(acc, mag)
    print("\nPaste into firmware/src/hw/compass_lis.cpp:\n")
    print(code_for(Ra, "toDeviceAccel"))
    print(code_for(Pm, "toDeviceMag"))
