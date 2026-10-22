"""Software renderer (numpy z-buffer, flat shading) for the case STL files. No OpenGL needed.
usage: python render.py out.png [--view front|back|iso|isoback] [--size N] [--zoom Z] file.stl@r,g,b [file2.stl@r,g,b ...]
Front view = looking at the front face with the device upright (case +X appears on the left)."""
import sys
import numpy as np
import trimesh
from PIL import Image

FRONT = np.array([(-1, 0, 0), (0, 1, 0), (0, 0, -1)], float)   # world -> view (+z toward camera)
BACK = np.array([(1, 0, 0), (0, 1, 0), (0, 0, 1)], float)

def rot(view):
    if view == "front": return FRONT
    if view == "back": return BACK
    if view == "side": return np.array([(0, 0, -1), (0, 1, 0), (1, 0, 0)], float)    # camera at +X
    yaw, pitch = np.radians(30 if view == "iso" else -30), np.radians(28)
    Ry = np.array([[np.cos(yaw), 0, np.sin(yaw)], [0, 1, 0], [-np.sin(yaw), 0, np.cos(yaw)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(pitch), -np.sin(pitch)], [0, np.sin(pitch), np.cos(pitch)]])
    return Rx @ Ry @ (FRONT if view == "iso" else BACK)

def render(meshes, colors, view, size, path, zoom=1.0, focus=None):
    """focus = (x, y) in view coordinates: centre of a close-up (default: the bounding box centre)"""
    R = rot(view)
    tris, cols = [], []
    for m, c in zip(meshes, colors):
        tris.append((m.vertices @ R.T)[m.faces]); cols.append(np.tile(np.array(c, float), (len(m.faces), 1)))
    T = np.concatenate(tris); C = np.concatenate(cols)
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    light = np.array([0.35, 0.55, 0.75]); light /= np.linalg.norm(light)
    shade = 0.30 + 0.70 * np.abs(n @ light)
    lo, hi = T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0)
    ctr = (lo + hi) / 2
    if focus is not None: ctr = np.array([focus[0], focus[1], ctr[2]])
    S = 2; W = H = size * S
    scale = zoom * W * 0.92 / max(hi[0] - lo[0], hi[1] - lo[1])
    img = np.full((H, W, 3), 255, np.uint8); zb = np.full((H, W), -1e9)
    P = np.empty_like(T)
    P[..., 0] = (T[..., 0] - ctr[0]) * scale + W / 2; P[..., 1] = H / 2 - (T[..., 1] - ctr[1]) * scale; P[..., 2] = T[..., 2]
    for i in range(len(P)):
        a, b, c = P[i]
        x0, x1 = int(max(min(a[0], b[0], c[0]), 0)), int(min(max(a[0], b[0], c[0]) + 1, W))
        y0, y1 = int(max(min(a[1], b[1], c[1]), 0)), int(min(max(a[1], b[1], c[1]) + 1, H))
        if x1 <= x0 or y1 <= y0: continue
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-9: continue
        xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
        w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
        w2 = 1 - w0 - w1
        m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not m.any(): continue
        z = w0 * a[2] + w1 * b[2] + w2 * c[2]
        sub = zb[y0:y1, x0:x1]; upd = m & (z > sub)
        sub[upd] = z[upd]
        img[y0:y1, x0:x1][upd] = (C[i] * shade[i]).clip(0, 255).astype(np.uint8)
    # depth cue + outlines at depth discontinuities (flat faces seen head-on would otherwise look identical)
    zr = np.where(zb > -1e8, zb, lo[2]); t = (zr - lo[2]) / max(hi[2] - lo[2], 1e-9)
    out = img.astype(float) * (0.55 + 0.45 * t)[..., None]
    gy, gx = np.gradient(zr)
    edge = (np.hypot(gx, gy) > 0.6)
    out[edge] *= 0.35
    out[zb < -1e8] = 255
    Image.fromarray(out.clip(0, 255).astype(np.uint8)).resize((size, size), Image.LANCZOS).save(path)

if __name__ == "__main__":
    a = sys.argv[1:]; out = a[0]; files, cols = [], []
    view, size, zoom = "front", 900, 1.0
    i = 1
    while i < len(a):
        if a[i] == "--view": view = a[i + 1]; i += 2
        elif a[i] == "--size": size = int(a[i + 1]); i += 2
        elif a[i] == "--zoom": zoom = float(a[i + 1]); i += 2
        else:
            name, _, col = a[i].partition("@"); files.append(name)
            cols.append(tuple(int(x) for x in col.split(",")) if col else (200, 200, 205)); i += 1
    render([trimesh.load(f) for f in files], cols, view, size, out, zoom)
