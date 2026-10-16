# Map pack format (version 1)

One file for the 2 MB QSPI flash: DEM mean layer, DEM MAX layer, towns, main roads, attribution. Written by
`mkmappack.py`, read by `firmware/src/platform/terrain/` (`Terrain`, `MapVectors`). Little endian, no alignment
requirements (every read goes through an `IBlockReader`). Offsets are bytes from the start of the file unless a
field says "section relative". Everything stored is covered by CRC-32 (IEEE, zlib polynomial).

## Header (192 bytes)

| offset | type | field |
|---|---|---|
| 0 | char[4] | magic `BGMP` |
| 4 | u16 | version = 1 |
| 6 | u16 | header size = 192 |
| 8 | u32 | file size |
| 12 | u32 | file CRC: CRC-32 of the whole file with these 4 bytes taken as zero |
| 16 | i32 x4 | bounding box of the stored mean grid: lat south, lat north, lon west, lon east (degrees * 1e7) |
| 32 | i32 x2 | projection origin lat, lon (degrees * 1e7): 45 N, 22 E |
| 40 | u32 | metres per degree = 111195 |
| 44 | f32 | cos0 = cos(42.7 deg), the fixed longitude scale of the grid and of the vector units |
| 48 | u16 | vector unit in metres = 10 |
| 50 | u16 | number of sections (at most 8) |
| 52 | u32 | header CRC: CRC-32 of the 192 header bytes with bytes 12..15 and 52..55 taken as zero |
| 56 | 8 B | zero |
| 64 | 8 x 16 B | section table: u16 id, u16 flags (0), u32 offset, u32 length, u32 CRC-32 of the section bytes; unused entries zero |

Section ids: 1 DEM mean, 2 DEM max, 3 towns, 4 roads, 5 strings. Sections follow the header in table order. A reader
must ignore unknown ids. Projection: row = (lat0 - lat) / dy, col = (lon - lon0) / dx on the DEM grids (two
subtractions, two divisions, no trig); vector units x = (lon - 22) * cos0 * 111195 / 10, y = (45 - lat) * 111195 / 10.

## DEM layer sections (ids 1 and 2)

```
desc (48 B)     i32 lat0_e7, i32 lon0_e7     NW corner of the (tile-cropped) grid, degrees * 1e7
                i32 dy_e7,   i32 dx_e7       cell size in degrees * 1e7 (dx = dy / cos0): square cells
                u16 rows, u16 cols           cells (multiples of the tile edge)
                u16 ty, u16 tx               tile grid
                u8 q (m per unit), u8 log2 tile edge (5 = 32), u8 kind (0 mean, 1 absolute max rounded up), u8 codec (3)
                u32 idx_off, u32 crc_off, u32 data_off      section relative: 48, 48 + 3(n+1), crc_off + 4n; n = ty*tx
                8 B zero
index           (n + 1) u24 offsets from data_off, row-major tiles; tile i occupies [off[i], off[i+1]); length 0 = tile
                not stored (outside the region)
tile CRCs       n x u32: CRC-32 of each coded tile (0 for unstored tiles)
tile data       byte0 = mode << 4 | k          mode 0 constant, 1 raw int16, 3 MED + Rice(k) (2 = row-delta + Rice, decodable)
                int16 base = value of cell (0,0)                                 (mode 0: the whole tile)
                bit stream MSB first, T*T - 1 residuals in raster order (mode 1: T*T raw int16 after byte0)
                residual = zigzag(v - pred); Rice(k): unary(u >> k) as ones ended by a zero, then the k low bits;
                if u >> k >= 24: 24 ones, then u as 16 raw bits
                pred = left in row 0, above in column 0, else MED(a = left, b = above, c = upleft)
```

Height in metres = value * q. Mean layer (200 m, q = 4): the cell value is the area average; a point is read by bilinear
interpolation between cell centres (`rf = (lat0 - lat) / dy - 0.5`, `cf = (lon - lon0) / dx - 0.5`, evaluated with
integer differences and an integer floor so float rounding does not grow with the row number); all four cells must be
stored. MAX layer (500 m, q = 10): value = ceil(max / q) of the native 90 m samples in the cell; read the cell that
contains the point (the reader also takes cells within 0.15 cell, and never returns less than the mean layer).
The grid is cropped to the bounding box of the stored tiles; the stored region is the Bulgaria land border plus
10 km, at tile granularity.

## Towns (id 3)

```
u16 count, u16 names_off (section relative)
count x 8 B   u16 x, u16 y (units), u8 cls (bits 0-2 population class 0..7, bit 3 city), u8 0, u16 name offset (from names_off)
names         per town: u8 latin_len, u8 cyr_len, latin bytes (transliteration, name:en when present), cyr bytes (UTF-8)
```

Sorted by population, largest first (unknown last), so iteration order is label priority. Population classes: 0 unknown,
1 below 5000, 2 below 10000, 3 below 20000, 4 below 50000, 5 below 100000, 6 below 250000, 7 above. place=city and
place=town only. Names at most 47 (Latin) and 95 (Cyrillic) bytes.

## Roads (id 4)

```
24 B header   u16 block (1024 units = 10.24 km), u16 bx0, by0 (first block), u16 nbx, nby, u16 max points (120),
              u32 index offset (24), u32 data offset (24 + 3 (nbx*nby + 1)), u32 polylines, all offsets section relative
index         (nbx*nby + 1) u24 offsets into the data, row-major blocks; equal offsets = empty block
block         varint polylines; per polyline: varint points, u8 class (0 motorway, 1 trunk, 2 primary, 3 secondary),
              zigzag varint (x - block x origin), (y - block y origin) of the first point, then zigzag varint deltas
```

motorway/trunk/primary ways (links dropped), ways merged end to end where exactly two of the same class meet, Douglas-Peucker
50 m, cut where they leave Bulgaria + 10 km, cut where they change block (the crossing segment stays with the block of its
first vertex, so a query reads the blocks of the window plus a one-block ring) and at 120 points. A block is at most
2 kB coded (largest in Bulgaria: 1055 B).

## Strings (id 5)

`u8 count`, then `count` NUL-terminated UTF-8 strings: 0 the Copernicus GLO-90 attribution (exact text from the study), 1
`© OpenStreetMap contributors` (UTF-8 copyright sign), 2 `(c) OpenStreetMap contributors (ODbL)` in ASCII for fonts without the sign. Show them on the About
screen.

## Integrity

* Open checks the header CRC and every section of up to 64 kB (towns, roads, strings) so vectors are known good.
* DEM tiles are verified lazily: each tile read from flash is checked against its CRC-32 before decoding, and a tile that
  fails (or does not decode, or whose index entry points outside the data) answers "no data" for every query. Cost
  about 12 cycles per coded byte (5k cycles for an average 420 B tile, against ~60k to decode).
* The file CRC covers everything, for after a transfer (`Terrain::verifyAll()`); too slow for every boot.
