/* Round-trip test + timing for tdec.h.
 * usage: tdec_test file.bin dump.i16 queries.txt
 *   dump.i16   : mean layer fully decoded (rows padded to tiles, zeros where the tile is not stored)
 *   queries.txt: "lat_e7 lon_e7 height" for 2000 pseudo-random points (skipping unstored tiles)
 * Prints decode timing (all tiles, repeated).
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "tdec.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    FILE *fp = fopen(argv[1], "rb");
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *f = malloc(n);
    if (fread(f, 1, n, fp) != (size_t)n) return 2;
    fclose(fp);
    tdec_layer L[2];
    int nl = f[5];
    for (int i = 0; i < nl; i++) if (tdec_layer_open(f, i, &L[i])) return 3;
    int T = 1 << L[0].log2t;
    int W = L[0].tx * T, H = L[0].ty * T;
    int16_t *dump = calloc((size_t)W * H, 2);
    static int16_t tile[TDEC_MAXT * TDEC_MAXT];
    double t0 = now();
    int reps = 20, tiles = 0;
    uint32_t bytes = 0;
    for (int rep = 0; rep < reps; rep++)
        for (uint32_t ti = 0; ti < (uint32_t)L[0].ty * L[0].tx; ti++) {
            uint32_t off, len;
            tdec_tile_range(f, &L[0], ti, &off, &len);
            if (tdec_tile(f + off, len, T, tile)) continue;
            tiles++; bytes += len;
            if (rep == 0) {
                int ty = ti / L[0].tx, tx = ti % L[0].tx;
                for (int r = 0; r < T; r++) memcpy(dump + (size_t)(ty * T + r) * W + tx * T, tile + r * T, 2 * T);
            }
        }
    double dt = now() - t0;
    printf("decoded %d tiles (%.1f kB coded per pass): %.2f us/tile, %.1f ns/cell, avg %.0f coded bytes/tile\n", tiles / reps,
           bytes / reps / 1e3, dt / tiles * 1e6, dt / tiles / (T * T) * 1e9, (double)bytes / tiles);
    fp = fopen(argv[2], "wb"); fwrite(dump, 2, (size_t)W * H, fp); fclose(fp);

    tdec_cache c; tdec_cache_init(&c);
    FILE *q = fopen(argv[3], "w");
    uint32_t s = 12345; int ok = 0;
    for (int i = 0; i < 2000; i++) {
        s = s * 1664525u + 1013904223u; double u = (s >> 8) / 16777216.0;
        s = s * 1664525u + 1013904223u; double v = (s >> 8) / 16777216.0;
        int32_t lat = (int32_t)(L[0].lat0_e7 - (0.01 + 0.98 * u) * (double)L[0].rows * L[0].dy_e7);
        int32_t lon = (int32_t)(L[0].lon0_e7 + (0.01 + 0.98 * v) * (double)L[0].cols * L[0].dx_e7);
        float h;
        if (tdec_height(&c, f, &L[0], lat, lon, &h)) continue;
        fprintf(q, "%d %d %.4f\n", lat, lon, h); ok++;
    }
    fclose(q);
    printf("%d height queries ok, %u tile decodes via cache\n", ok, c.decodes);

    /* timing of a realistic access pattern: walk a straight line (3x3 cache), 20k queries */
    tdec_cache_init(&c);
    t0 = now(); int hit = 0;
    for (int i = 0; i < 20000; i++) {
        float h; int32_t lat = L[0].lat0_e7 - (int32_t)((0.3 + 0.2 * i / 20000.0) * L[0].rows * L[0].dy_e7);
        int32_t lon = L[0].lon0_e7 + (int32_t)((0.3 + 0.2 * i / 20000.0) * L[0].cols * L[0].dx_e7);
        hit += tdec_height(&c, f, &L[0], lat, lon, &h) == 0;
    }
    printf("line walk: %d ok, %u tile decodes, %.2f us/query avg\n", hit, c.decodes, (now() - t0) / 20000 * 1e6);
    return 0;
}
