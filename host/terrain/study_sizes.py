"""Size table: grid x tile x codec x quant x buffer x (max layer). Writes DATA/sizes.json."""
import json, sys, time
import numpy as np
import tlib, codec

GRIDS = [90, 100, 125, 150, 175, 200, 225, 250, 300, 500]
TILES = [32, 64]
CODECS = {0: "raw16", 1: "delta+rice", 2: "med+rice", 3: "med+adaptive"}
QS = [1, 2, 4]
QMAX = [5, 10]
BUFS = [0, 10, 20]
out = {}
t0 = time.time()
for S in GRIDS:
    mean = tlib.resample(S, "average")
    mx = tlib.resample(S, "max")
    rois = {b: tlib.roi_mask(S, b) for b in BUFS}
    cells = {b: int(rois[b].sum()) for b in BUFS}
    for T in TILES:
        tm = {b: codec.tile_mask(rois[b], T) for b in BUFS}
        # tile count and index
        for b in BUFS:
            ys, xs = np.where(tm[b])
            bbox = (ys.max() - ys.min() + 1) * (xs.max() - xs.min() + 1)
            out[f"{S}|{T}|buf{b}|meta"] = dict(cells_roi=cells[b], tiles=int(tm[b].sum()), tile_cells=int(tm[b].sum()) * T * T,
                                               index_bytes=int(3 * (bbox + 1)))
        for qm in QS:
            q = codec.pad_to_tiles(np.clip(np.round(mean / qm), -32000, 32000).astype(np.int16), T)
            for cid, cn in CODECS.items():
                if cid == 0 and qm != 1:
                    continue
                sz = codec.layer_tile_sizes(q, T, cid, tm[20])
                for b in BUFS:
                    out[f"{S}|{T}|{cn}|q{qm}|buf{b}|mean"] = int(sz[tm[b]].sum())
            for qx in QMAX:
                rec = q.astype(np.float32) * qm
                m2 = codec.pad_to_tiles(mx, T)
                off = np.clip(np.ceil((m2 - rec) / qx), 0, 32000).astype(np.int16)
                for cid, cn in CODECS.items():
                    if cid == 0:
                        continue
                    sz = codec.layer_tile_sizes(off, T, cid, tm[20])
                    for b in BUFS:
                        out[f"{S}|{T}|{cn}|q{qm}|buf{b}|max{qx}"] = int(sz[tm[b]].sum())
        print(S, T, f"{time.time()-t0:.0f}s", flush=True)
    json.dump(out, open(f"{tlib.DATA}/sizes.json", "w"))
