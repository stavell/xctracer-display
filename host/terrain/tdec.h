/* Reference decoder for the BGDM terrain file (see README.md "Binary format").
 * Pure C99, no heap, no float64 needed: lives in platform/ or apps/vario/core on the device.
 * The caller supplies the file bytes (on the device: read the index entry and the tile from QSPI into RAM first;
 * here we simply index into a memory image).
 */
#ifndef TDEC_H
#define TDEC_H
#include <stdint.h>
#include <string.h>

#define TDEC_MAXT 32
#define TDEC_ESC 24

typedef struct {
    int32_t lat0_e7, lon0_e7, dy_e7, dx_e7;
    uint16_t rows, cols, ty, tx;
    uint8_t q, log2t, kind, codec;
    uint32_t idx_off, data_off;
} tdec_layer;

static inline uint32_t tdec_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint16_t tdec_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* which = 0 mean layer, 1 max layer. Returns 0 on success. */
static inline int tdec_layer_open(const uint8_t *f, int which, tdec_layer *L) {
    if (memcmp(f, "BGDM", 4) != 0 || f[4] != 1 || which >= f[5]) return -1;
    const uint8_t *d = f + 16 + 48 * which;
    L->lat0_e7 = (int32_t)tdec_u32(d); L->lon0_e7 = (int32_t)tdec_u32(d + 4);
    L->dy_e7 = (int32_t)tdec_u32(d + 8); L->dx_e7 = (int32_t)tdec_u32(d + 12);
    L->rows = tdec_u16(d + 16); L->cols = tdec_u16(d + 18); L->ty = tdec_u16(d + 20); L->tx = tdec_u16(d + 22);
    L->q = d[24]; L->log2t = d[25]; L->kind = d[26]; L->codec = d[27];
    L->idx_off = tdec_u32(d + 28); L->data_off = tdec_u32(d + 32);
    return 0;
}

/* Byte range of tile `ti` (row-major in the tile grid) relative to the file start. len == 0: tile not stored. */
static inline void tdec_tile_range(const uint8_t *f, const tdec_layer *L, uint32_t ti, uint32_t *off, uint32_t *len) {
    const uint8_t *p = f + L->idx_off + 3 * ti;
    uint32_t a = p[0] | p[1] << 8 | p[2] << 16, b = p[3] | p[4] << 8 | p[5] << 16;
    *off = L->data_off + a;
    *len = b - a;
}

typedef struct { const uint8_t *p; uint32_t pos; } tdec_br;
static inline uint32_t tdec_bit(tdec_br *b) {
    uint32_t v = (b->p[b->pos >> 3] >> (7 - (b->pos & 7))) & 1;
    b->pos++;
    return v;
}
static inline uint32_t tdec_bits(tdec_br *b, int n) {
    uint32_t v = 0;
    while (n--) v = (v << 1) | tdec_bit(b);
    return v;
}

static inline int tdec_med(int a, int b, int c) {
    int mx = a > b ? a : b, mn = a < b ? a : b;
    if (c >= mx) return mn;
    if (c <= mn) return mx;
    return a + b - c;
}

/* Decode one tile (T x T int16, row-major) from `s` (len bytes). Returns 0 ok, -1 empty/unsupported. */
static inline int tdec_tile(const uint8_t *s, uint32_t len, int T, int16_t *out) {
    if (len == 0) return -1;
    int mode = s[0] >> 4, k = s[0] & 15;
    int n = T * T;
    if (mode == 1) {
        for (int i = 0; i < n; i++) out[i] = (int16_t)tdec_u16(s + 1 + 2 * i);
        return 0;
    }
    int16_t base = (int16_t)tdec_u16(s + 1);
    if (mode == 0) {
        for (int i = 0; i < n; i++) out[i] = base;
        return 0;
    }
    if (mode != 2 && mode != 3) return -1;
    tdec_br br = {s + 3, 0};
    out[0] = base;
    for (int i = 0; i < T; i++) {
        for (int j = 0; j < T; j++) {
            if (i == 0 && j == 0) continue;
            uint32_t q = 0;
            while (q < TDEC_ESC && tdec_bit(&br)) q++;
            uint32_t u = (q == TDEC_ESC) ? tdec_bits(&br, 16) : (q << k) | tdec_bits(&br, k);
            int e = (u & 1) ? -(int)((u + 1) >> 1) : (int)(u >> 1);
            int p;
            if (i == 0) p = out[j - 1];
            else if (j == 0) p = out[(i - 1) * T];
            else {
                int a = out[i * T + j - 1], b = out[(i - 1) * T + j], c = out[(i - 1) * T + j - 1];
                p = mode == 2 ? a : tdec_med(a, b, c);
            }
            out[i * T + j] = (int16_t)(p + e);
        }
    }
    return 0;
}

/* Small tile cache (3x3 = 9 tiles for the mean layer). */
#ifndef TDEC_CACHE_N
#define TDEC_CACHE_N 9
#endif
typedef struct {
    int32_t key[TDEC_CACHE_N];
    int16_t v[TDEC_CACHE_N][TDEC_MAXT * TDEC_MAXT];
    uint8_t ok[TDEC_CACHE_N];
    int next;
    uint32_t decodes;
} tdec_cache;

static inline void tdec_cache_init(tdec_cache *c) {
    for (int i = 0; i < TDEC_CACHE_N; i++) c->key[i] = -1;
    c->next = 0; c->decodes = 0;
}

/* Returns the decoded tile or NULL if the tile is not stored. */
static inline const int16_t *tdec_cache_get(tdec_cache *c, const uint8_t *f, const tdec_layer *L, uint32_t ti) {
    for (int i = 0; i < TDEC_CACHE_N; i++)
        if (c->key[i] == (int32_t)ti) return c->ok[i] ? c->v[i] : 0;
    uint32_t off, len;
    tdec_tile_range(f, L, ti, &off, &len);
    int i = c->next;
    c->next = (c->next + 1) % TDEC_CACHE_N;
    c->key[i] = (int32_t)ti;
    c->ok[i] = tdec_tile(f + off, len, 1 << L->log2t, c->v[i]) == 0;
    c->decodes++;
    return c->ok[i] ? c->v[i] : 0;
}

/* Cell value (quantised units) at integer row/col; returns 0 ok, -1 outside / not stored. */
static inline int tdec_cell(tdec_cache *c, const uint8_t *f, const tdec_layer *L, int r, int col, int *val) {
    if (r < 0 || col < 0 || r >= L->rows || col >= L->cols) return -1;
    int sh = L->log2t;
    uint32_t ti = (uint32_t)(r >> sh) * L->tx + (uint32_t)(col >> sh);
    const int16_t *t = tdec_cache_get(c, f, L, ti);
    if (!t) return -1;
    *val = t[(r & ((1 << sh) - 1)) * (1 << sh) + (col & ((1 << sh) - 1))];
    return 0;
}

/* Bilinear height in metres at lat/lon (degrees * 1e7). Returns 0 ok, -1 outside / not stored. */
static inline int tdec_height(tdec_cache *c, const uint8_t *f, const tdec_layer *L, int32_t lat_e7, int32_t lon_e7, float *h) {
    float rf = (float)(L->lat0_e7 - lat_e7) / (float)L->dy_e7 - 0.5f;
    float cf = (float)(lon_e7 - L->lon0_e7) / (float)L->dx_e7 - 0.5f;
    if (rf < 0 || cf < 0) return -1;
    int r = (int)rf, col = (int)cf;
    float fr = rf - r, fc = cf - col;
    int a, b, d, e;
    if (tdec_cell(c, f, L, r, col, &a) || tdec_cell(c, f, L, r, col + 1, &b) ||
        tdec_cell(c, f, L, r + 1, col, &d) || tdec_cell(c, f, L, r + 1, col + 1, &e))
        return -1;
    *h = L->q * ((a * (1 - fc) + b * fc) * (1 - fr) + (d * (1 - fc) + e * fc) * fr);
    return 0;
}
#endif
