#include "apps/pet/pet_art.h"
#include <math.h>
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"

namespace pet_art {
namespace {

using namespace pet;

const int BLACK = 0, WHITE = 16;

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
inline int iabs(int a) { return a < 0 ? -a : a; }

void span(const Bitmap1& bm, int x0, int x1, int y, int g) { if (x1 >= x0) hline(bm, x0, x1, y, Gray(g)); }
void disc(const Bitmap1& bm, int cx, int cy, int r, int g) { fillDisc(bm, cx, cy, r, Gray(g)); }
void ring(const Bitmap1& bm, int cx, int cy, int r, int w) { fillRing(bm, cx, cy, r, r - w, Gray(BLACK)); }
void box(const Bitmap1& bm, int x, int y, int w, int h, int g) { fillRect(bm, x, y, w, h, Gray(g)); }
void thick(const Bitmap1& bm, int x0, int y0, int x1, int y1, int grow = 1, DrawMode m = DRAW_CLEAR) { lineGrown(bm, x0, y0, x1, y1, grow, m); }
void frame(const Bitmap1& bm, int x, int y, int w, int h, int t, int inner) {   // black frame of thickness t around a filled inside
  box(bm, x, y, w, h, BLACK);
  box(bm, x + t, y + t, w - 2 * t, h - 2 * t, inner);
}

enum Kind : uint8_t { K_ELL, K_RECT, K_TRI, K_DIA };
struct Species { Kind kind; int8_t rx, ry, eyeDx, eyeDy, eyeR; uint8_t gray; };
const Species kSpecies[8] = {
    {K_ELL, 28, 26, 9, -4, 6, 12},    // blob
    {K_RECT, 25, 25, 10, -5, 6, 9},   // box
    {K_TRI, 30, 28, 8, 8, 5, 14},     // triangle
    {K_ELL, 19, 30, 7, -10, 5, 11},   // bean
    {K_DIA, 29, 30, 9, -2, 6, 13},    // diamond
    {K_ELL, 32, 21, 17, -22, 8, 10},  // frog: the eyes sit on bumps
    {K_ELL, 27, 24, 10, -2, 6, 15},   // cat
    {K_RECT, 22, 28, 10, -8, 8, 7},   // owl
};

// Half width of the shape at dy from its centre (ry = half height); -1 outside.
int halfAt(Kind k, int rx, int ry, int dy) {
  if (dy < -ry || dy > ry || ry <= 0) return -1;
  switch (k) {
    case K_ELL: { const float f = (float)dy / ry; return (int)(rx * sqrtf(1.0f - f * f) + 0.5f); }
    case K_RECT: { const int c = 7, e = iabs(dy) - (ry - c); return e > 0 ? rx - (e * e + c - 1) / c : rx; }
    case K_TRI: return rx * (dy + ry) / (2 * ry);
    case K_DIA: return rx * (ry - iabs(dy)) / ry;
  }
  return -1;
}

void fillShape(const Bitmap1& bm, Kind k, int cx, int cy, int rx, int ry, int g) {
  for (int dy = -ry; dy <= ry; dy++) {
    const int h = halfAt(k, rx, ry, dy);
    if (h >= 0) span(bm, cx - h, cx + h, cy + dy, g);
  }
}

void tri(const Bitmap1& bm, int apexX, int apexY, int baseX0, int baseX1, int baseY, int g) {   // filled triangle, base horizontal
  const int h = baseY - apexY;
  if (h <= 0) return;
  for (int y = apexY; y <= baseY; y++) {
    const int a = apexX + (baseX0 - apexX) * (y - apexY) / h, b = apexX + (baseX1 - apexX) * (y - apexY) / h;
    span(bm, imin(a, b), imax(a, b), y, g);
  }
}

// ---- face ----
enum Eye { E_OPEN, E_WIDE, E_CLOSED, E_HAPPY, E_X };

void eye(const Bitmap1& bm, int x, int y, int r, Eye k) {
  switch (k) {
    case E_OPEN: case E_WIDE: {
      const int R = k == E_WIDE ? r + 2 : r;
      disc(bm, x, y, R, WHITE);
      ring(bm, x, y, R, 2);
      disc(bm, x + (k == E_WIDE ? 0 : 1), y + (k == E_WIDE ? 0 : 1), imax(2, R / 2), BLACK);
      break;
    }
    case E_CLOSED: thick(bm, x - r, y, x, y + 2); thick(bm, x, y + 2, x + r, y); break;
    case E_HAPPY: thick(bm, x - r, y + 3, x, y - 3); thick(bm, x, y - 3, x + r, y + 3); break;
    case E_X: thick(bm, x - r + 1, y - r + 1, x + r - 1, y + r - 1); thick(bm, x - r + 1, y + r - 1, x + r - 1, y - r + 1); break;
  }
}

enum Mouth { M_FLAT, M_SMILE, M_O, M_SICK };

void mouth(const Bitmap1& bm, int x, int y, Mouth k) {
  switch (k) {
    case M_FLAT: thick(bm, x - 5, y, x + 5, y, 1); break;
    case M_SMILE: thick(bm, x - 8, y - 2, x - 4, y + 3, 1); thick(bm, x - 4, y + 3, x + 4, y + 3, 1); thick(bm, x + 4, y + 3, x + 8, y - 2, 1); break;
    case M_O: disc(bm, x, y + 1, 4, WHITE); ring(bm, x, y + 1, 4, 2); break;
    case M_SICK: thick(bm, x - 8, y + 2, x - 4, y - 1, 1); thick(bm, x - 4, y - 1, x, y + 3, 1); thick(bm, x, y + 3, x + 4, y - 1, 1); thick(bm, x + 4, y - 1, x + 8, y + 2, 1); break;
  }
}

// ---- glyphs: no font, so a sign reads from afar ----
void questionMark(const Bitmap1& bm, int cx, int cy) {
  static const int8_t p[][2] = {{-6, -6}, {-3, -10}, {3, -10}, {6, -6}, {6, -2}, {0, 2}, {0, 5}};
  for (int i = 0; i < 6; i++) thick(bm, cx + p[i][0], cy + p[i][1], cx + p[i + 1][0], cy + p[i + 1][1], 1);
  disc(bm, cx, cy + 10, 2, BLACK);
}
void exclamation(const Bitmap1& bm, int cx, int cy) {
  thick(bm, cx, cy - 10, cx, cy + 3, 1);
  disc(bm, cx, cy + 10, 2, BLACK);
}
void sign(const Bitmap1& bm, int cx, int cy, bool question, bool heavy) {   // a board on a stick, held up, centred on cx
  const int w = 30, h = 28;
  thick(bm, cx, cy + h / 2, cx, cy + h / 2 + 14, 1);
  frame(bm, cx - w / 2, cy - h / 2, w, h, heavy ? 4 : 2, WHITE);
  if (question) questionMark(bm, cx, cy); else exclamation(bm, cx, cy);
}
void zed(const Bitmap1& bm, int x, int y, int s) {   // a "z" s wide, 2 px strokes
  thick(bm, x, y, x + s, y, 0); thick(bm, x, y + 1, x + s, y + 1, 0);
  thick(bm, x + s, y, x, y + s, 1);
  thick(bm, x, y + s, x + s, y + s, 0); thick(bm, x, y + s + 1, x + s, y + s + 1, 0);
}
void sparkle(const Bitmap1& bm, int x, int y, int r) { thick(bm, x - r, y, x + r, y, 0); thick(bm, x, y - r, x, y + r, 0); }

}  // namespace

// Everything that depends on the species and the pose, in one place.
struct Pose {
  int cx, cy;          // body centre
  int rx, ry;          // body half sizes (fat and lying applied)
  int jump;            // vertical offset already in cy
  Eye eyes;
  Mouth mouth;
  int groundY;
};

namespace {

void ears(const Bitmap1& bm, const Species& s, const Pose& p) {
  switch (&s - kSpecies) {
    case 0:  // blob: two stubby ears
      disc(bm, p.cx - p.rx * 2 / 3, p.cy - p.ry + 2, 7, BLACK);
      disc(bm, p.cx + p.rx * 2 / 3, p.cy - p.ry + 2, 7, BLACK);
      break;
    case 1:  // box: an antenna
      thick(bm, p.cx, p.cy - p.ry, p.cx, p.cy - p.ry - 12, 1);
      disc(bm, p.cx, p.cy - p.ry - 14, 5, BLACK);
      break;
    case 3:  // bean: two little feet
      disc(bm, p.cx - 8, p.cy + p.ry + 1, 5, BLACK);
      disc(bm, p.cx + 8, p.cy + p.ry + 1, 5, BLACK);
      break;
    case 5:  // frog: the eye bumps
      disc(bm, p.cx - s.eyeDx, p.cy + s.eyeDy * p.ry / s.ry, 12, BLACK);
      disc(bm, p.cx + s.eyeDx, p.cy + s.eyeDy * p.ry / s.ry, 12, BLACK);
      disc(bm, p.cx - s.eyeDx, p.cy + s.eyeDy * p.ry / s.ry, 10, imax(s.gray - 1, 0));
      disc(bm, p.cx + s.eyeDx, p.cy + s.eyeDy * p.ry / s.ry, 10, imax(s.gray - 1, 0));
      break;
    case 6:  // cat: pointed ears
      tri(bm, p.cx - p.rx + 2, p.cy - p.ry - 14, p.cx - p.rx + 2, p.cx - p.rx / 4, p.cy - p.ry + 6, BLACK);
      tri(bm, p.cx + p.rx - 2, p.cy - p.ry - 14, p.cx + p.rx / 4, p.cx + p.rx - 2, p.cy - p.ry + 6, BLACK);
      break;
    case 7:  // owl: ear tufts
      tri(bm, p.cx - p.rx + 2, p.cy - p.ry - 12, p.cx - p.rx - 2, p.cx - p.rx / 3, p.cy - p.ry + 4, BLACK);
      tri(bm, p.cx + p.rx - 2, p.cy - p.ry - 12, p.cx + p.rx / 3, p.cx + p.rx + 2, p.cy - p.ry + 4, BLACK);
      break;
  }
}

void variantOverlay(const Bitmap1& bm, const Species& s, const Pose& p, int variant) {
  const int top = p.cy - p.ry;
  if (variant == 1) {          // party hat
    const int hy = s.kind == K_TRI ? top + 4 : top + 2;
    tri(bm, p.cx + 2, hy - 20, p.cx - 11, p.cx + 15, hy, BLACK);
    tri(bm, p.cx + 2, hy - 15, p.cx - 6, p.cx + 10, hy - 3, 11);
    disc(bm, p.cx + 2, hy - 22, 3, BLACK);
    box(bm, p.cx - 12, hy, 28, 3, BLACK);
  } else if (variant == 2) {   // scarf: a band round the lower body with a tail
    const int y = p.cy + p.ry / 3;
    const int h = halfAt(s.kind, p.rx, p.ry, y - p.cy);
    if (h > 0) {
      box(bm, p.cx - h, y, 2 * h + 1, 7, BLACK);
      for (int x = p.cx - h + 3; x < p.cx + h - 1; x += 8) box(bm, x, y + 2, 4, 3, 16);
      box(bm, p.cx + h / 2, y + 7, 8, 14, BLACK);
      box(bm, p.cx + h / 2 + 2, y + 9, 4, 2, 16);
    }
  } else if (variant == 3) {   // glasses
    const int ex = s.eyeDx, ey = p.cy + s.eyeDy * p.ry / s.ry, r = s.eyeR + 4;
    ring(bm, p.cx - ex, ey, r, 3);
    ring(bm, p.cx + ex, ey, r, 3);
    thick(bm, p.cx - ex + r - 1, ey, p.cx + ex - r + 1, ey, 0);
    thick(bm, p.cx - ex - r + 1, ey, p.cx - ex - r - 5, ey - 2, 0);
    thick(bm, p.cx + ex + r - 1, ey, p.cx + ex + r + 5, ey - 2, 0);
  }
}

// The belly is a row of five buttons on the lower body: each black one is a fifth of the context window used (unknown: all grey dots).
void belly(const Bitmap1& bm, const Species& s, const Pose& p, int ctx) {
  const int y = s.kind == K_DIA ? p.cy + p.ry / 3 : s.kind == K_TRI ? p.cy + p.ry - 10 : p.cy + p.ry - 9;
  const int pitch = 7, w = 5 * pitch - 2;
  if (2 * halfAt(s.kind, p.rx, p.ry, y - p.cy) < w + 6) return;
  const int x0 = p.cx - w / 2;
  const int full = ctx == CONTEXT_UNKNOWN ? 0 : (ctx * 5 + 50) / 100;
  for (int i = 0; i < 5; i++) {
    const int x = x0 + i * pitch;
    if (ctx == CONTEXT_UNKNOWN) { box(bm, x + 1, y + 1, 3, 3, BLACK); continue; }
    box(bm, x, y, 5, 5, BLACK);
    if (i >= full) box(bm, x + 1, y + 1, 3, 3, WHITE);
  }
}

}  // namespace

void drawPet(const Bitmap1& bm, int ox, int oy, const Look& look, uint32_t t) {
  const Species& s = kSpecies[look.species & 7];
  const uint8_t state = look.asleep ? S_IDLE : look.state;
  const bool lying = state == S_FAILED;
  Pose p;
  p.cx = ox + CELL / 2;
  const int baseCy = oy + 61;
  p.rx = s.rx + look.fat * 3;
  p.ry = s.ry;
  if (lying) { p.rx = p.rx * 7 / 6; p.ry = p.ry * 11 / 20; }
  p.groundY = baseCy + s.ry + 5;
  int dy = 0, dx = 0;
  p.eyes = E_OPEN; p.mouth = M_FLAT;
  uint32_t f = 0;                                         // animation frame
  switch (state) {
    case S_PERMISSION: case S_QUESTION: {
      f = (t / 167) % 4;
      static const int8_t j1[4] = {0, -3, -6, -3}, j2[4] = {0, -6, -12, -6};
      dy = look.escalation ? j2[f] : j1[f];
      p.eyes = E_WIDE; p.mouth = M_O;
      break;
    }
    case S_FAILED: p.eyes = E_X; p.mouth = M_SICK; dy = (t / 3000) & 1; break;
    case S_DONE: p.eyes = E_HAPPY; p.mouth = M_SMILE; f = (t / 3000) & 1; dy = f ? -9 : 0; break;
    case S_WORKING: {
      f = (t / 500) % 4;
      if (look.tool == T_OTHER) { static const int8_t w[4] = {-9, 0, 9, 0}; dx = w[f]; dy = (f & 1) ? -2 : 0; }
      if (look.tool == T_THINK) p.eyes = E_OPEN;
      if (look.tool == T_AGENT) p.mouth = M_SMILE;
      break;
    }
    default: p.eyes = E_CLOSED; dy = 3; f = (t / 3000) % 3; break;   // IDLE: asleep
  }
  p.cx += dx;
  p.cy = baseCy + dy + (lying ? s.ry - p.ry : 0);
  p.jump = dy;
  const int cx = p.cx, cy = p.cy, rx = p.rx, ry = p.ry;

  // shadow on the ground, so a jump reads as a jump
  {
    const int sw = imax(10, rx - 4 + dy / 3);
    fillShape(bm, K_ELL, ox + CELL / 2 + dx, p.groundY, sw, 3, 8);
  }
  const bool wave = state == S_PERMISSION || state == S_QUESTION;
  // arms (behind the body)
  if (wave) {
    const int w = (f & 1) ? 6 : -6;
    thick(bm, cx - rx + 2, cy, cx - rx - 12, cy - 22 + w);
    thick(bm, cx + rx - 2, cy, cx + rx + 12, cy - 22 - w);
    disc(bm, cx - rx - 12, cy - 22 + w, 4, BLACK);
    disc(bm, cx + rx + 12, cy - 22 - w, 4, BLACK);
  }
  ears(bm, s, p);
  fillShape(bm, s.kind, cx, cy, rx + 2, ry + 2, BLACK);
  fillShape(bm, s.kind, cx, cy, rx, ry, s.gray);

  const int eyeY = cy + s.eyeDy * ry / s.ry;
  const int eyeR = s.eyeR;
  if (state == S_FAILED) {                                // a bandage on the forehead
    box(bm, cx - 9, cy - ry / 2 - 4, 18, 8, WHITE);
    box(bm, cx - 9, cy - ry / 2 - 4, 18, 1, BLACK); box(bm, cx - 9, cy - ry / 2 + 3, 18, 1, BLACK);
    thick(bm, cx - 4, cy - ry / 2 - 3, cx - 4, cy - ry / 2 + 2, 0); thick(bm, cx + 4, cy - ry / 2 - 3, cx + 4, cy - ry / 2 + 2, 0);
  }
  eye(bm, cx - s.eyeDx, eyeY, eyeR, p.eyes);
  eye(bm, cx + s.eyeDx, eyeY, eyeR, p.eyes);
  const int mouthY = s.kind == K_TRI ? cy + ry / 3 : eyeY + eyeR + 5;
  if (look.species != 5) mouth(bm, cx, mouthY, p.mouth);
  else mouth(bm, cx, cy + ry / 5, p.mouth);
  if (look.species == 6) {                                // whiskers
    thick(bm, cx - 14, mouthY - 1, cx - 26, mouthY - 4, 0); thick(bm, cx - 14, mouthY + 2, cx - 26, mouthY + 5, 0);
    thick(bm, cx + 14, mouthY - 1, cx + 26, mouthY - 4, 0); thick(bm, cx + 14, mouthY + 2, cx + 26, mouthY + 5, 0);
  }
  if (!lying) belly(bm, s, p, look.context);
  variantOverlay(bm, s, p, look.variant & 3);

  // ---- props of the state ----
  const int rxs = s.rx + look.fat * 3;
  if (state == S_QUESTION) {
    sign(bm, cx + rxs + 2 > ox + 98 ? ox + 98 : cx + rxs + 2, cy - ry - 8, true, look.escalation == 2);
  } else if (state == S_PERMISSION && look.escalation == 2) {
    sign(bm, imax(cx - rxs - 2, ox + 22), cy - ry - 8, false, true);
  } else if (state == S_IDLE) {
    const int zx = cx + rx - 6, zy = cy - ry - 4;
    if (f >= 0) zed(bm, zx, zy, 8);
    if (f >= 1) zed(bm, zx + 10, zy - 14, 10);
    if (f >= 2) zed(bm, zx + 22 > ox + 104 ? ox + 104 : zx + 22, zy - 30 < oy + 2 ? oy + 2 : zy - 30, 12);
  } else if (state == S_DONE && f) {
    sparkle(bm, cx - rx - 6, cy - ry + 2, 5);
    sparkle(bm, cx + rx + 6, cy - ry - 6, 4);
  } else if (state == S_WORKING) {
    switch (look.tool) {
      case T_THINK: {
        const int bx = imin(cx + 40, ox + 100), by = cy - ry - 8;
        disc(bm, cx + rx - 2, cy - ry + 4, 2, BLACK);
        disc(bm, cx + rx + 6, cy - ry - 4, 3, BLACK);
        fillShape(bm, K_ELL, bx, by, 21, 14, BLACK);
        fillShape(bm, K_ELL, bx, by, 19, 12, WHITE);
        for (int i = 0; i < 3; i++) disc(bm, bx - 10 + 10 * i, by, (int)f % 3 == i || f == 3 ? 4 : 2, BLACK);
        break;
      }
      case T_READ: {
        const int bx = cx - 18, by = cy + ry / 3 + 2;
        frame(bm, bx, by, 36, 24, 2, WHITE);
        box(bm, bx + 17, by, 2, 24, BLACK);
        for (int i = 0; i < 3; i++) { box(bm, bx + 5, by + 5 + 6 * i, 9, 1, BLACK); box(bm, bx + 23, by + 5 + 6 * i, f & 1 ? 9 : 6, 1, BLACK); }
        if (look.variant != 3) {                          // reading glasses
          ring(bm, cx - s.eyeDx, eyeY, eyeR + 4, 3);
          ring(bm, cx + s.eyeDx, eyeY, eyeR + 4, 3);
          thick(bm, cx - s.eyeDx + eyeR + 3, eyeY, cx + s.eyeDx - eyeR - 3, eyeY, 0);
        }
        break;
      }
      case T_EDIT: {
        const int px = cx + rxs - 4, py = cy + 8;
        frame(bm, imin(px, ox + 90), py, 26, 30, 2, WHITE);
        const int x0 = imin(px, ox + 90) + 5;
        for (int i = 0; i < 3; i++) box(bm, x0, py + 6 + 7 * i, i < (int)f ? 14 : 4 + (int)f * 2, 1, BLACK);
        const int tipx = x0 + 4 + (int)f * 4, tipy = py + 6 + 7 * imin((int)f, 2);
        thick(bm, tipx, tipy, tipx + 14, tipy - 16, 1);
        break;
      }
      case T_SHELL: {
        const bool down = f & 1;
        const int hx = cx + rxs + (down ? 4 : 0), hy = down ? cy + 6 : cy - ry - 4;
        thick(bm, cx + rxs - 6, cy + 2, hx, hy + 4, 1);
        box(bm, hx - 9, hy - 3, 20, 10, BLACK);
        if (down) { sparkle(bm, hx + 4, hy + 14, 5); }
        break;
      }
      case T_WEB: {
        const int tx = imin(cx + rxs + 16, ox + 114), ty = cy - ry - 4;
        thick(bm, cx + rxs - 4, cy + 4, tx, ty, 0);
        const int by = cy + 14 + ((f & 1) ? 3 : 0);
        thick(bm, tx, ty, tx, by, 0);
        disc(bm, tx, by + 3, 3, WHITE); ring(bm, tx, by + 3, 3, 2);
        thick(bm, tx - 5, by + 15, tx + 5, by + 15, 0);   // the water
        thick(bm, tx - 8, by + 18, tx + 8, by + 18, 0);
        break;
      }
      case T_AGENT: {
        const int bx = imax(cx - 40, ox + 24), by = cy - ry - 8;
        frame(bm, bx - 20, by - 12, 40, 24, 2, WHITE);
        tri(bm, bx - 8, by + 18, bx - 8, bx + 2, by + 11, BLACK);
        for (int i = 0; i < 3; i++) box(bm, bx - 13, by - 6 + 6 * i, (int)((f + i) % 3) * 5 + 8, 2, BLACK);
        break;
      }
      case T_MCP: {
        const int px = imin(cx + rxs - 2, ox + 94), py = cy - 22;
        frame(bm, px, py, 14, 26, 2, 12);
        box(bm, px + 5, py + 21, 4, 2, BLACK);
        for (int i = 0; i < 3; i++) if ((int)f % 4 > i) thick(bm, px + 17 + 4 * i, py + 8 - 4 * i, px + 17 + 4 * i, py + 8 + 4 * i, 0);
        break;
      }
      case T_OTHER: {                                     // walking: feet
        const int a = (f & 1) ? 4 : -4;
        fillShape(bm, K_ELL, cx - 9 + a, cy + ry + 2, 6, 3, BLACK);
        fillShape(bm, K_ELL, cx + 9 - a, cy + ry + 2, 6, 3, BLACK);
        break;
      }
    }
  }
}

void drawHatch(const Bitmap1& bm, int ox, int oy, const Look& l, uint32_t t) {
  const int cx = ox + CELL / 2, cy = oy + 64 + 10;
  if (t >= 1500) {                                        // out of the egg: a small hop
    Look h = l;
    h.state = S_DONE;
    drawPet(bm, ox, oy, h, (t - 1500) < 250 ? 3000 : 0);
    return;
  }
  const int wob = t < 1000 ? (int)((t / 120) & 1) * 4 - 2 : 0;
  const int ex = cx + wob;
  fillShape(bm, K_ELL, ex, cy, 24, 31, BLACK);
  fillShape(bm, K_ELL, ex, cy, 22, 29, 14);
  box(bm, ex - 8, cy - 14, 6, 6, 9); box(bm, ex + 6, cy + 2, 7, 7, 9);
  if (t >= 800) {                                         // a crack
    const int ys[] = {-4, 2, -2, 5, 0};
    int px = ex - 22;
    for (int i = 0; i < 4; i++) { thick(bm, px, cy + ys[i], px + 11, cy + ys[i + 1], 0); px += 11; }
  }
  if (t >= 1000) {                                        // eyes peek out of the top half
    box(bm, ex - 22, cy - 31, 44, 14, WHITE);
    fillShape(bm, K_ELL, ex, cy + 8, 22, 22, 14);
    eye(bm, ex - 8, cy - 14, 5, E_WIDE);
    eye(bm, ex + 8, cy - 14, 5, E_WIDE);
    tri(bm, ex - 22, cy - 6, ex - 22, ex - 10, cy + 6, BLACK);
    tri(bm, ex + 22, cy - 6, ex + 10, ex + 22, cy + 6, BLACK);
  }
}

void drawWave(const Bitmap1& bm, int ox, int oy, const Look& l, uint32_t t) {
  Look h = l;
  h.state = S_PERMISSION;                                 // arms up and swaying: the pet waves
  h.escalation = 0;
  drawPet(bm, ox, oy, h, t);
}

void drawFollowers(const Bitmap1& bm, int ox, int oy, int n, uint32_t t, bool walking) {
  static const int8_t px[4] = {10, 24, 96, 110};
  if (n > 4) n = 5;
  for (int i = 0; i < n && i < 4; i++) {
    const int bob = walking && (((t / 500) + i) & 1) ? 3 : 0;
    const int x = ox + px[i], y = oy + 100 - bob;
    if (n > 4 && i == 3) { thick(bm, x - 5, oy + 98, x + 5, oy + 98, 1); thick(bm, x, oy + 93, x, oy + 103, 1); continue; }
    disc(bm, x, y, 6, BLACK);
    disc(bm, x, y, 4, 13);
    disc(bm, x + 1, y - 1, 1, BLACK);
  }
}

void drawBowl(const Bitmap1& bm, int x, int y, int pct, bool isLimit) {
  // a half-disc bowl: the fill rises with pct; a limit bowl has a notch on the rim
  const int cx = x + 18, top = y + 8, R = 17;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  for (int dy = 0; dy <= R; dy++) {                       // bottom half of a disc
    const int h = (int)(sqrtf((float)(R * R - dy * dy)) + 0.5f);
    span(bm, cx - h, cx + h, top + dy, 16);
  }
  const int fillH = R * pct / 100;
  for (int dy = R - fillH + 1; dy <= R; dy++) {
    const int h = (int)(sqrtf((float)(R * R - dy * dy)) + 0.5f);
    span(bm, cx - h, cx + h, top + dy, 0);
  }
  for (int dy = 0; dy <= R; dy++) {                       // outline
    const int h = (int)(sqrtf((float)(R * R - dy * dy)) + 0.5f);
    const int h2 = dy + 1 <= R ? (int)(sqrtf((float)(R * R - (dy + 1) * (dy + 1))) + 0.5f) : 0;
    span(bm, cx - h, cx - h2, top + dy, 0);
    span(bm, cx + h2, cx + h, top + dy, 0);
  }
  box(bm, cx - R - 2, top - 2, 2 * R + 5, 3, 0);          // rim
  if (isLimit) box(bm, cx - 2, top - 7, 5, 5, 0);         // a knob: this bowl is a limit, not a cost
}

void drawUnplugged(const Bitmap1& bm, int x, int y) {
  // a plug with its cord cut: two prongs, the body, and a slash across
  box(bm, x + 6, y + 2, 4, 8, 0); box(bm, x + 14, y + 2, 4, 8, 0);
  box(bm, x + 3, y + 10, 18, 9, 0);
  thick(bm, x + 12, y + 19, x + 12, y + 24, 0);
  thick(bm, x + 1, y + 26, x + 25, y + 1, 3, DRAW_SET);       // a white edge, then the slash
  thick(bm, x + 1, y + 26, x + 25, y + 1, 1, DRAW_CLEAR);
}

}  // namespace pet_art
