"""Tile codecs for the terrain grid (numba; simple enough to port to C -- see tdec.c).

A tile is TxT int16 values (already quantised). Per-tile byte layout:
  byte0 = mode<<4 | k      mode 0 const, 1 raw int16, 2 delta-row+Rice(k), 3 MED+Rice(k), 4 MED+adaptive Rice
  int16 LE  base = v[0,0]  (modes 0,2,3,4; mode 1 has TxT raw int16 instead)
  bit stream (MSB first) of T*T-1 residuals in raster order, byte padded.
Residual: zigzag(v - pred). Rice(k): unary(u>>k) as 1s + 0, then k low bits; if u>>k >= 24: 24 ones, then u as 16 raw bits.
Predictors: delta-row = left (above in column 0). MED (LOCO-I): first row left, first col above, else
  med(a=left,b=above,c=upleft).  Adaptive Rice: LOCO-style A/N counters per context (NCTX contexts chosen from
  the local gradient act=|a-c|+|b-c|), k = smallest with (N<<k) >= A, N halves at 16.
"""
import numpy as np
from numba import njit

ESC = 24
NCTX = 4


@njit(cache=True)
def zz(e):
    return (e << 1) if e >= 0 else ((-e << 1) - 1)


@njit(cache=True)
def med(a, b, c):
    mx = max(a, b); mn = min(a, b)
    if c >= mx:
        return mn
    if c <= mn:
        return mx
    return a + b - c


@njit(cache=True)
def ctx_of(act):
    if act == 0:
        return 0
    if act <= 2:
        return 1
    if act <= 8:
        return 2
    return 3


@njit(cache=True)
def residuals(v, kind):
    """kind 0 delta-row, 1 MED. Returns zigzag residuals (n-1) and context ids."""
    T = v.shape[0]
    u = np.empty(T * T - 1, np.int64)
    cx = np.empty(T * T - 1, np.int64)
    n = 0
    for i in range(T):
        for j in range(T):
            if i == 0 and j == 0:
                continue
            if i == 0:
                p = v[i, j - 1]; act = 99
            elif j == 0:
                p = v[i - 1, j]; act = 99
            else:
                a = v[i, j - 1]; b = v[i - 1, j]; c = v[i - 1, j - 1]
                p = a if kind == 0 else med(a, b, c)
                act = abs(a - c) + abs(b - c)
            u[n] = zz(np.int64(v[i, j]) - p)
            cx[n] = ctx_of(act)
            n += 1
    return u, cx


@njit(cache=True)
def rice_bits(u, k):
    t = 0
    for x in u:
        q = x >> k
        t += (ESC + 16) if q >= ESC else q + 1 + k
    return t


@njit(cache=True)
def put(buf, pos, val, n):
    for i in range(n - 1, -1, -1):
        if (val >> i) & 1:
            buf[pos >> 3] |= np.uint8(0x80 >> (pos & 7))
        pos += 1
    return pos


@njit(cache=True)
def put_rice(buf, pos, x, k):
    q = x >> k
    if q >= ESC:
        for _ in range(ESC):
            pos = put(buf, pos, 1, 1)
        return put(buf, pos, x, 16)
    for _ in range(q):
        pos = put(buf, pos, 1, 1)
    pos = put(buf, pos, 0, 1)
    return put(buf, pos, x & ((1 << k) - 1), k)


@njit(cache=True)
def adaptive_k(A, N):
    k = 0
    while (N << k) < A:
        k += 1
    return k


@njit(cache=True)
def encode_adaptive(u, cx, buf, pos):
    A = np.full(NCTX, 16, np.int64)
    N = np.full(NCTX, 2, np.int64)
    for i in range(u.shape[0]):
        c = cx[i] if cx[i] < NCTX else 0
        k = adaptive_k(A[c], N[c])
        pos = put_rice(buf, pos, u[i], k)
        A[c] += u[i]; N[c] += 1
        if N[c] >= 16:
            A[c] >>= 1; N[c] >>= 1
    return pos


@njit(cache=True)
def adaptive_bits(u, cx):
    A = np.full(NCTX, 16, np.int64)
    N = np.full(NCTX, 2, np.int64)
    t = 0
    for i in range(u.shape[0]):
        c = cx[i]
        k = adaptive_k(A[c], N[c])
        q = u[i] >> k
        t += (ESC + 16) if q >= ESC else q + 1 + k
        A[c] += u[i]; N[c] += 1
        if N[c] >= 16:
            A[c] >>= 1; N[c] >>= 1
    return t


@njit(cache=True)
def tile_size(v, codec):
    """Byte size of the best encoding of one tile with the allowed codec.
    codec: 0 raw, 1 delta-row+Rice, 2 MED+Rice(k), 3 MED+adaptive.  const/raw fallback always allowed (1,2)."""
    T = v.shape[0]
    flat = True
    for i in range(T):
        for j in range(T):
            if v[i, j] != v[0, 0]:
                flat = False
    if flat and codec > 0:
        return 3
    raw = 1 + 2 * T * T
    if codec == 0:
        return raw
    best = raw
    if codec == 1:
        u, cx = residuals(v, 0)
        for k in range(0, 15):
            b = 3 + (rice_bits(u, k) + 7) // 8
            best = min(best, b)
    elif codec == 2:
        u, cx = residuals(v, 1)
        for k in range(0, 15):
            b = 3 + (rice_bits(u, k) + 7) // 8
            best = min(best, b)
    else:
        u, cx = residuals(v, 1)
        best = min(best, 3 + (adaptive_bits(u, cx) + 7) // 8)
    return best


@njit(cache=True)
def encode_tile(v, codec):
    """Returns the tile bytes (uint8 array)."""
    T = v.shape[0]
    flat = True
    for i in range(T):
        for j in range(T):
            if v[i, j] != v[0, 0]:
                flat = False
    if flat and codec > 0:
        out = np.zeros(3, np.uint8)
        b = np.int64(v[0, 0]) & 0xFFFF
        out[1] = b & 255; out[2] = b >> 8
        return out
    raw = 1 + 2 * T * T
    if codec == 0:
        out = np.zeros(raw, np.uint8)
        out[0] = 1 << 4
        n = 1
        for i in range(T):
            for j in range(T):
                b = np.int64(v[i, j]) & 0xFFFF
                out[n] = b & 255; out[n + 1] = b >> 8
                n += 2
        return out
    kind = 0 if codec == 1 else 1
    u, cx = residuals(v, kind)
    if codec == 3:
        bits = adaptive_bits(u, cx); bk = 0; mode = 4
    else:
        bits = 1 << 60; bk = 0
        for k in range(0, 15):
            b = rice_bits(u, k)
            if b < bits:
                bits = b; bk = k
        mode = 2 if codec == 1 else 3
    if 3 + (bits + 7) // 8 >= raw:
        out = np.zeros(raw, np.uint8)
        out[0] = 1 << 4
        n = 1
        for i in range(T):
            for j in range(T):
                b = np.int64(v[i, j]) & 0xFFFF
                out[n] = b & 255; out[n + 1] = b >> 8
                n += 2
        return out
    out = np.zeros(3 + (bits + 7) // 8, np.uint8)
    out[0] = (mode << 4) | bk
    b = np.int64(v[0, 0]) & 0xFFFF
    out[1] = b & 255; out[2] = b >> 8
    pos = 3 * 8
    if codec == 3:
        pos = encode_adaptive(u, cx, out, pos)
    else:
        for i in range(u.shape[0]):
            pos = put_rice(out, pos, u[i], bk)
    return out


@njit(cache=True)
def layer_tile_sizes(q, T, codec, tmask):
    """q: int16 grid padded to multiples of T; tmask: bool (ty,tx) tiles to encode. Returns int32 (ty,tx) sizes."""
    ty, tx = tmask.shape
    s = np.zeros((ty, tx), np.int32)
    for a in range(ty):
        for b in range(tx):
            if tmask[a, b]:
                s[a, b] = tile_size(q[a * T:(a + 1) * T, b * T:(b + 1) * T], codec)
    return s


@njit(cache=True)
def layer_encode(q, T, codec, tmask):
    """Encode all masked tiles; returns (blob uint8, offsets int64 (ntiles+1))."""
    ty, tx = tmask.shape
    offs = np.zeros(ty * tx + 1, np.int64)
    parts = []
    tot = 0
    for a in range(ty):
        for b in range(tx):
            if tmask[a, b]:
                t = encode_tile(q[a * T:(a + 1) * T, b * T:(b + 1) * T], codec)
                parts.append(t)
                tot += t.shape[0]
            offs[a * tx + b + 1] = tot
    blob = np.empty(tot, np.uint8)
    p = 0
    for t in parts:
        blob[p:p + t.shape[0]] = t
        p += t.shape[0]
    return blob, offs


def pad_to_tiles(a, T, fill_edge=True):
    nr, nc = a.shape
    pr = (-nr) % T; pc = (-nc) % T
    return np.pad(a, ((0, pr), (0, pc)), mode="edge")


def tile_mask(roi, T):
    nr, nc = roi.shape
    r = pad_to_tiles(roi, T)
    ty, tx = r.shape[0] // T, r.shape[1] // T
    return r.reshape(ty, T, tx, T).any(axis=(1, 3))
