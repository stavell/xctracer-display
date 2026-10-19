# Terrain and map data on the 2 MB QSPI flash (host study)

Question: the most detailed DEM of Bulgaria that fits the Feather's free 2 MB QSPI flash with random access, and
how accurate it is. Host-only study; nothing here touches `firmware/` or `case/`. All data (downloads, grids,
binaries, big PNGs) lives outside the repo in
`$TERRAIN_DATA` (`DATA` in `tlib.py`; default `~/terrain-data`). Python deps are in a separate venv there (numpy, scipy, rasterio, shapely, numba, pyosmium, pillow).

## Recommendation (short)

| | |
|---|---|
| Source | Copernicus GLO-90 (3 arcsec, DSM, EGM2008 heights), 28 tiles N41..44 x E022..028 |
| Grid | regular lat/lon grid, north-up, lon step = lat step / cos(42.7 deg): square 200 m cells, steps stored as integers in 1e-7 deg (dy = 17986, dx = 24474 (1e-7 deg)) |
| Cell value | area average (average resampling of the native 3" grid); heights quantised to **4 m** |
| Tile / codec | **32x32 cells**, independently decodable; **MED (LOCO-I) predictor + Rice(k), k per tile**, constant-tile and raw fallbacks; 3-byte tile index |
| Region | Bulgaria land border + **10 km buffer**, tile-level mask (unstored tiles have a zero-length index entry) |
| MAX layer | coarse **absolute cell maximum**, **500 m** cells, rounded up to **10 m**, same codec: 225 kB. (A per-cell offset layer at the full 200 m grid would cost 0.8-1.1 MB and not fit.) |
| File size | **1,659,004 B** (mean 1,434,808 incl. index + max 224,084 incl. index + 112 B header). Budget 1.9 MB: 240 kB spare, enough for the vector layers (~45 kB) |
| Error vs native (region) | RMS 6.6 m, p95 15 m, p99 24 m, max 80 m (flat 6.0 / mountain 10.9 RMS); on the two IGC tracks RMS 9.5 m, p99 32 m, max 49 m |
| Decode | ~1 ms per 32x32 tile on a Cortex-M4 at 64 MHz (estimate, see below); 3x3 cache = 18 kB RAM |

Without the MAX layer the finest grid that fits is 175 m (q4, 1.78 MB incl. index, buffer 10 km) or 200 m with q2
(1.82 MB); with buffer 0 and no max, 200 m q2 (1.62 MB) or 225 m q1. 150 m needs 2.3 MB at best (q4): does not fit.
Vertical quantisation hardly matters for accuracy (see table: q1/q2/q4 differ by <0.1 m RMS because the grid
resampling error dominates, and GLO-90 itself is only about 4 m LE90), so 4 m buys 20 % size for free. The cost is
terracing of flat areas in hillshades; it is invisible in the renders at 1 bit.

Honest caveats: (1) 200 m cells smooth peaks and ridges: mean-layer error in mountains is 11 m RMS and up to 80 m at
single peaks/gorges; use the MAX layer for any "terrain above me" safety logic, never the mean. (2) GLO-90 is a DSM: forest and
buildings are included (a few to 20 m); it is not a bare-earth model. (3) The MAX layer is conservative to the
sample (0.0-0.03 % of random points and 0 % of track points fall below it, worst shortfall 15 m, all at cell-edge
interpolation spots) but not a proof; add a margin for AGL warnings.

## Data and licences

* Copernicus GLO-90 from the AWS open data bucket `copernicus-dem-90m` (reachable, no login; no fallback needed).
  Licence: free and open; attribution required: "Contains modified Copernicus data (DEM GLO-90) (c) DLR e.V. 2010-2014
  and (c) Airbus Defence and Space GmbH 2014-2018, provided under COPERNICUS by the European Union and ESA; all rights
  reserved." Show it on an About screen. Heights are orthometric (EGM2008 geoid), i.e. comparable to MSL/GPS altitude
  from XCTrack, not ellipsoid heights.
* Border: Natural Earth based country polygons (`datasets/geo-countries`, derived from Natural Earth public domain data).
* OSM extract (vector section): Geofabrik `bulgaria-latest.osm.pbf`, ODbL, (c) OpenStreetMap contributors; derived
  databases stay ODbL and need attribution on the device.
* IGC files: `/Users/stavel/Downloads/2023-07-16-XCT-SVE-10.igc` and `2026-08-16-XCT-SVE-09.igc` (all that exist).

## Method

Scripts (run in this order): `fetch.py`, `study_sizes.py`, `study_acc.py`, `study_max.py`, `study_agl.py`,
`mkterrain.py`, `roundtrip.py`, `render.py`; tables via `show_sizes.py`, `show_acc.py`, `show_fit.py`.

* Projection (`tlib.py`): origin 45 N / 22 E, row = (45 - lat)/dy, col = (lon - 22)/dx: two subtractions, two
  multiplies, no trig. Chosen over UTM-like or per-latitude schemes because a fixed cos(42.7) makes cells square at the
  centre of Bulgaria and the cell aspect varies only +-2.5 % over 41.2-44.2 N (cos 0.753 to 0.717); because grid build
  and lookup use the same mapping there is no positional error, only that slight cell-shape change. It is also
  independent of any map rendering: map layers use the same linear mapping (x = (lon-22)*cos0*111195 m).
* Resampling: `rasterio.warp.reproject` with average (mean layer) and max (max layer) onto the target grid, cells
  at 90, 100, 125, 150, 175, 200, 225, 250, 300, 500 m (90 m is the same square-metre grid, not the anisotropic 92 x 68 m
  native cell).
* Region: Bulgaria polygon buffered by 0/10/20 km; sample counts (cells in region / tiles stored at T=32):

| grid | buffer 0 | buffer 10 km | buffer 20 km |
|---|---|---|---|
| 150 m | 5.02 M / 5147 | 5.86 M / 5959 | 6.67 M / 6736 |
| 200 m | 2.82 M / 2941 | 3.30 M / 3386 | 3.75 M / 3838 |
| 250 m | 1.81 M / 1912 | 2.11 M / 2187 | 2.40 M / 2472 |
| 500 m | 0.45 M / 516 | 0.53 M / 575 | 0.60 M / 651 |

* Accuracy: bilinear between cell centres of the (de-quantised) stored grid vs bilinear of the native 3" mosaic.
  Region = 600,000 uniform random points inside Bulgaria; tracks = all 19,761 B-records of both IGC files. Flat/mountain
  split at 20 deg native slope (8.5 % of the region and 25 % of the track points are >= 20 deg).

## Sizes (kB, mean layer only, buffer 10 km, tile 32, q2; tile index excluded; `show_sizes.py` has everything)

| grid | delta-row + Rice | **MED + Rice(k)** | MED + adaptive Rice | MED + Rice, T=64 | raw int16 (q1) |
|---|---|---|---|---|---|
| 90 m | 7910 | 6541 | 6767 | 6649 | ~33 MB |
| 100 m | 6626 | 5535 | 5721 | 5643 | |
| 125 m | 4538 | 3869 | 3990 | 3961 | |
| 150 m | 3327 | 2881 | 2964 | 2988 | 12210 |
| 175 m | 2549 | 2233 | 2296 | 2333 | |
| 200 m | 2023 | **1789** | 1837 | 1885 | 6938 |
| 225 m | 1647 | 1467 | 1505 | 1547 | |
| 250 m | 1369 | 1227 | 1257 | 1291 | 4481 |
| 300 m | 1003 | 909 | 928 | 969 | 3176 |
| 500 m | 407 | 375 | 382 | 422 | 1178 |

Findings: MED+Rice(k) beats row-delta by 11-18 % and beats the LOCO-style adaptive Rice (4 gradient contexts) by 2-4 %
(DEM residuals are smooth enough that a per-tile k is as good, and it is a much simpler decoder). 32x32 tiles beat
64x64 by 4-8 % (finer region mask and per-tile k adaptation; the larger index is only 3 B per tile). Quantisation:
q2 is 18 % smaller than q1, q4 is 35 % smaller. Raw int16 is 3.8x bigger. Average coded tile at 200 m / q4: 420 bytes
(3.3 bits per cell).

Totals including indexes (mean [+ max layer] with its own index; best codec = MED+Rice for all; `show_fit.py` lists all
combinations, "*" = fits 1.9 MB), buffer 10 km, T=32:

| config | 150 | 175 | 200 | 225 | 250 | 300 |
|---|---|---|---|---|---|---|
| q4, no max | 2288 | **1783\*** | 1435\* | 1181\* | 991\* | 738\* |
| q2, no max | 2906 | 2252 | 1803\* | 1478\* | 1236\* | 915\* |
| q2 + per-cell max offset 10 m | 4243 | 3291 | 2638 | 2171 | **1819\*** | 1355\* |
| q4 + per-cell max offset 10 m | 3657 | 2844 | 2287 | 1886\* | 1584\* | 1184\* |
| q4 + coarse 500 m abs max q10 (proposed) | | | **1659\*** | | | |

Finest fit by case (budget 1.9 MB incl. index; T=32, MED+Rice): buffer 10 km: no max q4 175 m, no max q2 200 m, no max q1
225 m; with per-cell max (offset 10 m) q2 250 m, q4 225 m; buffer 0: no max q4 175 m, q2 200 m, q1 225 m; per-cell max q2 250 m;
buffer 20 km: no max q2 225 m, q4 200 m, q1 250 m; per-cell max q2/q4 300/250 m. So per-cell MAX costs one grid step
(roughly 50 m); the coarse absolute max layer costs only 225 kB and keeps the 200 m mean grid.

MAX layer variants (`study_max.py`, T=32, buffer 10 km, nearest-cell lookup, size incl. index; margin = max layer minus native terrain):

| coarse max grid | q | kB | below native (region / tracks) | mean margin region / tracks | p95 margin |
|---|---|---|---|---|---|
| 300 m | 10 | 521 | 0.07 % / 0.07 % | 30 / 41 m | 94 / 121 m |
| **500 m** | **10** | **225** | 0.03 % / 0.02 % | 43 / 60 m | 138 / 177 m |
| 500 m | 20 | 172 | 0.01 % / 0.00 % | 48 / 66 m | 143 / 183 m |
| 750 m | 10 | 117 | 0.01 % / 0.03 % | 57 / 85 m | 183 / 253 m |
| 1000 m | 10 | 75 | 0.01 % / 0.01 % | 69 / 102 m | 221 / 284 m |

The per-cell offset layer (mean + offset, 200 m grid, offset q10) has a smaller margin (23 m mean) but costs ~820 kB.

## Accuracy vs native 90 m (metres; q2 rows, q1/q4 differ by < 0.1 m RMS; `show_acc.py` prints all)

Region (600 k random points), columns: RMS, p95, p99, max | flat RMS, p99 | mountain (>=20 deg) RMS, p99

| grid | RMS | p95 | p99 | max | flat RMS / p99 | mtn RMS / p99 |
|---|---|---|---|---|---|---|
| 90 | 1.9 | 4.1 | 7.4 | 24 | 1.7 / 6.9 | 3.1 / 9.7 |
| 100 | 2.2 | 5.0 | 8.7 | 30 | 2.1 / 8.2 | 3.7 / 11.3 |
| 125 | 3.3 | 7.4 | 12.4 | 33 | 3.0 / 11.7 | 5.4 / 15.7 |
| 150 | 4.4 | 9.9 | 16.4 | 52 | 4.0 / 15.5 | 7.2 / 20.7 |
| 175 | 5.5 | 12.5 | 20.4 | 57 | 5.0 / 19.3 | 9.0 / 25.7 |
| **200** | **6.6** | **15.0** | **24.3** | **80** | 6.0 / 23.0 | 10.9 / 30.5 |
| 225 | 7.6 | 17.5 | 28.3 | 88 | 7.0 / 26.7 | 12.8 / 35.8 |
| 250 | 8.7 | 19.9 | 32.2 | 85 | 7.9 / 30.3 | 14.6 / 40.5 |
| 300 | 10.8 | 24.6 | 39.8 | 129 | 9.8 / 37.5 | 18.2 / 49.7 |
| 500 | 18.0 | 41.2 | 66.5 | 182 | 16.2 / 61.7 | 31.4 / 84.8 |

Along the IGC tracks (19,761 fixes; terrain under actual flights; bias is slightly negative: smoothing lowers ridges):

| grid | RMS | p95 | p99 | max | flat RMS / p99 | mtn RMS / p99 | bias |
|---|---|---|---|---|---|---|---|
| 100 | 2.9 | 6.6 | 10.6 | 22 | 2.6 / 10.6 | 3.6 / 10.6 | -0.26 |
| 150 | 6.1 | 14.5 | 20.8 | 32 | 5.7 / 21.1 | 7.1 / 19.3 | -0.59 |
| 175 | 7.9 | 18.6 | 26.7 | 44 | 7.3 / 26.9 | 9.5 / 26.5 | -0.91 |
| **200** | **9.5** | 22.6 | 31.6 | 49 | 8.9 / 32.3 | 11.2 / 28.7 | -1.14 |
| 250 | 12.7 | 30.7 | 39.8 | 56 | 11.7 / 40.3 | 15.4 / 38.4 | -1.52 |
| 500 | 28.0 | 66.3 | 86.7 | 111 | 26.1 / 87.1 | 33.1 / 85.3 | -3.16 |

(Track points sit mostly over hill/valley terrain with mean slope 11.5 deg vs 7.4 deg for the country, hence larger errors.)
The 90 m row includes resampling to a square 90 m lattice (errors are not zero because the native cell is 92 x 68 m).
Per-cell MAX bound (q2 mean + ceil offset): below the native terrain at 0.00-0.05 % of points; worst shortfall 21 m (500 m grid).

## AGL sanity check on the IGC files (`study_agl.py`)

Terrain = native bilinear (stored 200 m in brackets). Speed < 5 km/h in the first/last 60 s. Both files record GNSS
altitude and a pressure altitude (ISA, no QNH correction, so the baro column is only meaningful as a difference).

| file | where | GNSS alt | baro alt | terrain native (stored 200, max500) | AGL GNSS | AGL baro (ISA) |
|---|---|---|---|---|---|---|
| 2026-08-16 | takeoff, 11 fixes | 465 | 382 | 462 (456, 470) | +3 (+9) | -80 |
| 2026-08-16 | landing, 18 fixes | 247 | 189 | 253 (256, 280) | **-6** (-9) | -64 |
| 2023-07-16 | landing, 45 fixes | 500 | 338 | 408 (407, 410) | +92 (+93) | -70 |

The 2026 flight (GNSS MSL) agrees with GLO-90 within 3-9 m at both takeoff and landing, i.e. the DEM and GNSS
altitude are consistent to GPS/DSM accuracy; the stored grid adds <= 6 m. The baro altitude is 64-80 m below the
GNSS on that day (QNH was ~8-9 hPa below ISA), so barometric AGL needs a takeoff/QNH reference. The 2023 file lands at a
flat site (terrain 406-408 m everywhere around) but its GNSS altitude is 93 m higher than terrain and 162 m above its baro:
that file's altitude has an offset (a phone/other source, unrelated to the DEM), so it is not a usable AGL check. Whole
flights: minimum GNSS-minus-terrain is -8 m (2026, at landing) / +91 m (2023); the stored grid never differs from native
by more than 49 m along either track.

## Binary format (`mkterrain.py` writes it, `tdec.h` reads it; all little endian)

```
header (16 B)   "BGDM" | u8 version=1 | u8 nlayers (1 or 2) | u16 0 | u32 file size | 4 B pad
layer desc x2 (48 B each; the second is zero if nlayers = 1)
    i32 lat0_e7, i32 lon0_e7     NW corner of the (tile-cropped) grid, degrees * 1e7
    i32 dy_e7,   i32 dx_e7       cell size in degrees * 1e7 (dx = dy / cos(42.7 deg))
    u16 rows, u16 cols           cells (multiple of the tile size)
    u16 ty, u16 tx               tile grid
    u8 quant (m), u8 log2(tile), u8 kind (0 mean, 1 abs max rounded up), u8 codec (3 = MED+Rice)
    u32 index_offset, u32 data_offset   (from file start)
    12 B pad
tile index      (ty*tx + 1) u24 offsets from data_offset, row-major; tile i occupies [off[i], off[i+1]);
                zero length = tile outside the region (no data)
tile data       byte0 = mode<<4 | k      mode 0 constant, 1 raw int16, 2 row-delta+Rice, 3 MED+Rice(k)
                int16 base = value of cell (0,0)                                  (mode 0: the whole tile)
                bit stream MSB first, T*T-1 residuals in raster order (mode 1: T*T raw int16 after byte0)
                residual = zigzag(v - pred); Rice(k): unary(u >> k) as 1s ended by 0, then k low bits;
                if u >> k >= 24: 24 ones, then u as 16 raw bits (escape)
                pred = left in row 0, above in column 0, else MED(a=left, b=above, c=upleft)
height (m)      = v * quant. Mean layer: sample at cell centres, bilinear. Max layer (kind 1): v = ceil(max/quant);
                nearest cell.
lookup          row_f = (lat0 - lat)/dy - 0.5, col_f = (lon - lon0)/dx - 0.5 (int32 differences, float divide)
```

The converter crops the grid to the bounding box of stored tiles, so the index is small (56 x 84 tiles at 200 m: 14 kB).
`mkterrain.py` also supports `--tile 64`, `--mean`, `--q`, `--maxgrid 0` for the other variants in the tables.
The Python encoder (`codec.py`, numba) implements adaptive Rice and row-delta too (modes 2 and 4); only the proposed
mode 3 is needed on the device (the C decoder also handles 0, 1, 2).

## Reference C decoder, round trip and decode cost

`tdec.h` (header only, C99, no heap, ~150 lines; `tdec_tile`, `tdec_cache_get`, `tdec_height`) and `tdec_test.c`.
`roundtrip.py bg_dem.bin` builds it and checks: all 3,467,264 cells of the stored region decode identically to the
encoder's integer grid (0 mismatches, unstored tiles empty), and 1,518 random bilinear queries match a Python
reference to 0.0095 m. Result: `ROUND TRIP OK`.

Measured on the Mac (Apple silicon, cc -O2): **8.86 us per 32x32 tile (8.6 ns per cell)**, 420 coded bytes per
tile on average. Walking a line with the 3x3 cache: 36 tile decodes for 20,000 height queries, 0.04 us/query
average (bilinear on cached cells).

Scaling to the nRF52840 (Cortex-M4F, 64 MHz): the 20-40x factor suggested is too optimistic for this code;
the Mac does ~28 cycles/cell at 3+ GHz with deep out-of-order issue, the M4 runs ~1 instruction/cycle. Counting
ops: 3.3 bits/cell, each bit read of the naive reader ~10 cycles (load, shift, mask, increment) = ~33 cycles, MED
predict + zigzag + store ~25 cycles: **~60 cycles/cell = ~61k cycles = ~1.0 ms per tile at 64 MHz** (Mac time x ~110).
A byte-wise bit reader with CLZ for the unary part would get this to ~35 cycles/cell (~0.55 ms). Not measured on
hardware; I would plan on 1 ms per tile.

* Redrawing a full 240x400 px screen at one px per 200 m cell (48 x 80 km window, 7.5 x 12.5 tiles = ~100 tiles):
  ~0.1 s of decoding (plus hillshade maths, about 100k pixels). Panning decodes a new edge row/column (~10 tiles): ~10 ms.
* RAM: 3x3 tile cache = 9 x 32 x 32 x 2 B = **18 kB** (plus ~2 kB read buffer for the worst-case raw tile and a few words
  of state); it must be a global/static (the loop task has a 4 KB stack). The 500 m max layer needs one more
  tile (2 kB) at a time. The nRF52840 has 256 kB RAM.
* QSPI: 4-bit reads at 32 MHz are 16 MB/s peak; an average tile of 420 B (worst case 2 kB raw) plus ~3 B index read
  and command/address overhead is about 30-60 us in quad mode, 0.1-0.2 ms in single-wire mode: below 10 % of the decode
  time either way. The index (14 kB) could be read per lookup or kept in RAM. nRF52840 QSPI also supports XIP
  (memory mapped at 0x12000000), which would let the decoder read straight from flash with no copy.
* Flash write: the file is programmed from the laptop (BLE or USB mass storage / flash tool); not studied here.

## Renders (look at them)

Copies in `img/` (small); the full-size grey Bulgaria (2688x1792) is `png/bulgaria_200m_gray.png` in the data directory.
Hillshade: light from NW (315 deg, 40 deg altitude), 2x vertical exaggeration, shade from the *stored* 200 m / q4 grid;
1-bit = 8x8 Bayer threshold, flat ground pushed towards white so only the relief is dotted.

* `img/bulgaria_200m_1bit.png` whole country, one px per cell (2688 x 1792, buffer tiles visible as blocks at the border).
* `img/zoom_flight_100mpp.png` 240x400 window over the 2026-08-16 flight (24 x 40 km; 1 px = 100 m, bilinear from the 200 m
  grid): [stored 1-bit | stored grey + track in red | native 90 m grey]. The stored grid is visually indistinguishable
  from native at this scale apart from slightly softer ridges. `img/zoom_flight_100mpp_1bit_x3.png` is the 1-bit panel at 3x.
* `img/zoom_rila_100mpp.png` same for the Rila mountains (Musala): ridges and valleys read well at 200 m; the
  1-bit panel is legible for ridge/valley structure and does not show terracing from the 4 m quantisation.

Judgement: grey is excellent; 1-bit works in mountains (strong shadows) and is fairly noisy over gently rolling hills
(like the flight area) - for those a lower exaggeration/contrast or contour lines may read better than Bayer-dotted
shade.

## Vector layers for a map page (towns and main roads only)

Scope (per request): place=city/town (no villages) and highway=motorway/trunk/primary; secondary only as a size line.
Skipped: rivers, lakes, coast, airspace, takeoffs.
`osm_extract.py` (pyosmium) -> `osm_study.py` (sizes) -> `osm_render.py` (mock-ups). Region = Bulgaria + 10 km.

Encoding that the sizes are exact bytes of: coordinates in 10 m units on the DEM projection
(x = (lon-22)*cos(42.7)*111195/10, y = (45-lat)*111195/10); 1024-unit (10.24 km) blocks with a 3-byte block index
(only blocks with data have a payload); per block `varint npoly`, per polyline `varint npts, u8 class`, first vertex as
zigzag-varint offset from the block origin, the rest as zigzag-varint deltas; polylines are cut where they change block
(the crossing segment stays with the block of its first vertex, so a renderer loads the window blocks plus one ring);
Douglas-Peucker (GEOS `simplify`, metric coordinates) before quantisation. OSM splits roads at every junction (12,210
ways) so ways are first merged end to end where exactly two ways of the same class meet (1,524 chains): without merging
the size is 108-116 kB and DP hardly helps (per-polyline overhead dominates).

| layer | DP 25 m | DP 50 m | DP 100 m | vertices (raw -> 50 m) |
|---|---|---|---|---|
| main roads (motorway+trunk+primary), merged | 40.4 kB | **34.7 kB** | 30.6 kB | 90,098 -> 8,382 |
| ... of which motorway / trunk / primary (data only) | 10.3 / 8.5 / 29.5 | 9.1 / 7.8 / 25.7 | 8.0 / 7.4 / (~23) | |
| secondary (optional), merged | 43.5 kB | 37.3 kB | 33.0 kB | 94,867 -> 9,855 |
| main roads without merging ways | 116.4 kB | 111.3 kB | 108.1 kB | |
| secondary without merging | 90.0 kB | 84.1 kB | 80.1 kB | |

(sizes include the ~4.9 kB / 4.6 kB block index; the split row is data only.)

Places in the region: 256 (27 city, 229 town), 170 with a `population` tag. Record = x u16, y u16 (10 m units), 1 B
class/population bucket, 1 B name length, name bytes. No index needed for 256 records (scan, or sort by population):

| names | bytes |
|---|---|
| Cyrillic UTF-8 (2 B/char) | 5,427 |
| Cyrillic as a 1-byte code (the font atlas holds only А-я) | 3,496 |
| Latin transliteration (name:en else Bulgarian "streamlined" system; ~7.7 -> 8.2 chars avg) | 3,627 |
| both UTF-8 + Latin | 7,518 |

**Total for the map page at DP 50 m: ~35 kB roads + ~3.5-5.4 kB places = ~40 kB (~45 kB with both name forms, ~80 kB
with secondary roads)**, i.e. 2-4 % of the DEM; the vector data is not a size problem, the glyph atlas for Cyrillic
is the real cost. Fits next to the 1.66 MB DEM within the 1.9 MB budget (1.70 MB; with secondary roads 1.74 MB).

Mock-ups (`osm_render.py`, 240x400, 1 bit, left Cyrillic, right Latin; Arial Bold TTF 14/11 px with a white halo as
a stand-in for the device font; roads from the merged ways simplified to pixel scale, widths 3/2/1 px with a white casing):
`img/map_sofia_100mpp.png` (24 x 40 km), `img/map_sofia_200mpp.png` (48 x 80 km), `img/map_plovdiv_300mpp.png`,
`img/map_shumen_200mpp.png`. Judgement: roads, ring road and town markers are clear; names at 11-14 px are readable
in both scripts; a collision/priority pass (population order) is needed because names overlap
(Kostinbrod / Novi Iskar), and the hillshade under the text should be lightened (or the text box cleared) near labels.
Latin is about 7 % longer than Cyrillic.

Licence: ODbL, "(c) OpenStreetMap contributors" visible in the About screen; the derived binary must be available
under ODbL on request. Not covered: airspace (OpenAIP terms: CC BY-NC 4.0 and key required) and ParaglidingEarth
sites were dropped from scope.

## Files

`mkmappack.py` + `FORMAT.md` (the device pack) · `tlib.py` (grid, mosaic, IGC reader, sampling) · `codec.py` (tile codecs, sizes, encoder) · `study_*.py`, `show_*.py`
(tables) · `mkterrain.py` (the file format) · `tdec.h`, `tdec_test.c`, `roundtrip.py` (C decoder + round trip) · `render.py`
(hillshade PNGs) · `osm_*.py` (vector layers) · `img/` (small renders).

## Building a map pack (`mkmappack.py`) - the format the firmware reads

`mkmappack.py` replaces `mkterrain.py` for anything that goes on the device: one file holding the 200 m mean DEM,
the 500 m MAX layer, towns/cities (Latin and Cyrillic names, population class), main roads and the attribution
strings, all CRC-protected. Format: `FORMAT.md`. Reader: `firmware/src/platform/terrain/` (`Terrain`, `MapVectors`).

    cd host/terrain
    TV=$TERRAIN_DATA
    $TV/venv/bin/python mkmappack.py ../../maps/bulgaria.pack           # the real pack, about 1 s from the cached grids
    $TV/venv/bin/python mkmappack.py OUT.pack --bbox 42.35 42.85 24.40 25.10 --ref OUT.ref.txt    # a test fixture

Options: `--bbox S N W E` restricts every section to a rectangle (fixtures); `--ref F` writes the reference values the C++
tests compare against (400 bilinear/cell-max queries from an independent Python decode of the quantised grids, every
town, every encoded polyline); `--dp M` road simplification (50 m); `--secondary` adds secondary roads (class 3, +37 kB);
`--no-roads`. It needs the study data in `tlib.DATA` (`grid_200_average.npy`, `grid_500_max.npy`, `roi_*.npy`,
`osm/bg.pkl`; `fetch.py`, `osm_extract.py` make them) and the venv (numpy, numba, shapely).

The test fixture `firmware/emu/fixtures/sopot.pack` (59 kB, Karlovo/Sopot/Kalofer and the Stara Planina ridge) and
its `sopot.ref.txt` are checked in; regenerate with the second command above if the format changes.
The emulator tests run on it; with `TERRAIN_PACK=<real pack> firmware/emu/out/tests` the real pack is opened too, and
`firmware/emu/out/terrain_demo PACK bench` prints decode timing; `terrain_demo PACK LAT LON HEADING M_PER_PX OUT.pgm`
renders a window (hillshade + contours + roads + towns).

### The real pack (Bulgaria + 10 km, `maps/bulgaria.pack` in the repo)

| | bytes |
|---|---|
| header | 192 |
| DEM mean 200 m (desc 48, index 14,115, tile CRCs 18,816, data 1,420,693 in 3,386 coded tiles) | 1,453,672 |
| DEM max 500 m (desc 48, index 2,349, tile CRCs 3,128, data 221,735 in 575 coded tiles) | 227,260 |
| towns (256, both names) | 8,546 |
| roads (2,067 polylines, 378 blocks) | 34,716 |
| strings | 266 |
| **total** | **1,724,652** (82.2 % of 2 MiB; 372,500 B spare) |

Renders from the pack through the firmware code (`img/map_pack_sopot_100mpp.png`, `img/map_pack_rila_100mpp.png`, 240x400, 1 bit,
2x enlarged): hillshade in the "light" style (flat ground white, steep faces down to level 10 of 16, 1.5x exaggeration,
quieter on gentle slopes) with contour lines every 200 m at 100 m/px (index contours every 5th, drawn 2 px), roads with a
white casing, towns with white-haloed Latin names.
