"""Build the proposed device terrain file (see README "Binary format").

usage: mkterrain.py OUT.bin [--mean 200] [--q 4] [--maxgrid 500] [--qmax 10] [--buffer 10] [--tile 32]
Also writes OUT.bin.ref.npz (quantised cropped mean grid + tile mask) for the C round-trip test.
"""
import argparse, struct
import numpy as np
import tlib, codec

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--mean", type=int, default=200)
ap.add_argument("--q", type=int, default=4)
ap.add_argument("--maxgrid", type=int, default=500, help="0 = no max layer")
ap.add_argument("--qmax", type=int, default=10)
ap.add_argument("--buffer", type=int, default=10)
ap.add_argument("--tile", type=int, default=32)
a = ap.parse_args()
T = a.tile
CODEC = 2  # MED + Rice(k), tile modes 0/1/3 only


def build_layer(S, kind, q):
    g = tlib.resample(S, "average" if kind == 0 else "max")
    if kind == 0:
        v = np.round(g / q)
    else:
        v = np.ceil(g / q)                      # round UP: never under-state the cell maximum
    v = np.clip(v, -32000, 32000).astype(np.int16)
    roi = tlib.roi_mask(S, a.buffer)
    tm = codec.tile_mask(roi, T)
    ys, xs = np.nonzero(tm)
    ty0, ty1, tx0, tx1 = ys.min(), ys.max(), xs.min(), xs.max()
    vp = codec.pad_to_tiles(v, T)
    vc = np.ascontiguousarray(vp[ty0 * T:(ty1 + 1) * T, tx0 * T:(tx1 + 1) * T])
    tmc = np.ascontiguousarray(tm[ty0:ty1 + 1, tx0:tx1 + 1])
    blob, offs = codec.layer_encode(vc, T, CODEC, tmc)
    dy, dx, nr, nc = tlib.grid_geom(S)
    return dict(S=S, kind=kind, q=q, vc=vc, tm=tmc, blob=blob, offs=offs, dy_e7=round(dy * 1e7), dx_e7=round(dx * 1e7),
                lat0_e7=round((tlib.LAT_N - ty0 * T * dy) * 1e7), lon0_e7=round((tlib.LON_W + tx0 * T * dx) * 1e7),
                rows=vc.shape[0], cols=vc.shape[1], ty=tmc.shape[0], tx=tmc.shape[1])


layers = [build_layer(a.mean, 0, a.q)]
if a.maxgrid:
    layers.append(build_layer(a.maxgrid, 1, a.qmax))

HDR = 16
LDESC = 48
pos = HDR + LDESC * 2
descs = []
chunks = []
for L in layers:
    idx_off = pos
    nidx = L["ty"] * L["tx"] + 1
    index = b"".join(int(o).to_bytes(3, "little") for o in L["offs"])
    assert L["offs"][-1] < (1 << 24)
    pos += len(index)
    data_off = pos
    pos += len(L["blob"])
    chunks += [index, L["blob"].tobytes()]
    descs.append(struct.pack("<iiiiHHHHBBBBII12x", L["lat0_e7"], L["lon0_e7"], L["dy_e7"], L["dx_e7"], L["rows"], L["cols"],
                             L["ty"], L["tx"], L["q"], int(np.log2(T)), L["kind"], 3, idx_off, data_off))
if len(layers) == 1:
    descs.append(bytes(LDESC))
assert all(len(d) == LDESC for d in descs), [len(d) for d in descs]
hdr = struct.pack("<4sBBHI4x", b"BGDM", 1, len(layers), 0, pos)
assert len(hdr) == HDR
blob = hdr + b"".join(descs) + b"".join(chunks)
open(a.out, "wb").write(blob)
L0 = layers[0]
np.savez(a.out + ".ref.npz", v=L0["vc"], tm=L0["tm"])
print(f"{a.out}: {len(blob)} bytes total")
for L in layers:
    print(f"  layer kind={L['kind']} S={L['S']} q={L['q']} grid {L['rows']}x{L['cols']} tiles {L['ty']}x{L['tx']} "
          f"(coded {int(L['tm'].sum())}) index {3*(L['ty']*L['tx']+1)} B data {len(L['blob'])} B")
