#include "apps/intro/intro_app.h"
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include <stdio.h>
#include "apps/games/sim/takeoffs.h"
#include "platform/gfx/gray.h"
#include "platform/gfx/shapes.h"
#include "platform/version.h"

namespace intro {

using namespace sim;

namespace {

// The script, in ms of animation time.
constexpr uint32_t kClimbMs = 2300;                  // the glider reaches the cloud's level
constexpr uint32_t kWhiteFrom = 1450, kWhiteTo = 2250;   // the white-out
constexpr uint32_t kBankFrom = 800, kBankTo = 1900;        // the cloud front sweeps down
constexpr uint32_t kLogoFrom = 2150, kLogoTo = 2550;     // the name dissolves in
constexpr float kOmega = 0.75f;                      // rad/s around the thermal: a tight turn, 8.4 s a circle
constexpr float kSpeed = 12.0f;                      // m/s
constexpr float kRadius = kSpeed / kOmega;           // m (16 m: what 40 degrees of bank at this speed makes)
constexpr float kBelowStart = 150.0f, kAboveEnd = 40.0f;   // start and end altitude relative to the cloud base
constexpr int kPreferredSite = 1;                    // Kalofer: a high ridge close under the cloud base on the study pack
constexpr float kSkirtBelow = 300.0f;                // the flat ground beyond the window, below the takeoff

float phase(uint32_t ms, uint32_t from, uint32_t to) {
  if (ms <= from) return 0.0f;
  if (ms >= to) return 1.0f;
  return smoothstep((float)(ms - from) / (float)(to - from));
}

// The paraglider from behind, as a logo mark: a canopy arch, the lines and a pilot. roll turns it about its harness.
void glyph(const Bitmap1& bm, int cx, int cy, float W, float roll, DrawMode ink, int grow) {
  const float c = cosf(roll), s = sinf(roll);
  auto P = [&](float x, float y, int* ox, int* oy) {
    *ox = cx + (int)lroundf(x * c - y * s);
    *oy = cy + (int)lroundf(x * s + y * c);
  };
  const int N = 10;
  int ax[2 * N + 1], ay[2 * N + 1];
  for (int i = -N; i <= N; i++) {
    const float u = (float)i / N;
    P(W * u, -0.78f * W - 0.16f * W * (1.0f - u * u) + 0.26f * W * u * u * u * u, &ax[i + N], &ay[i + N]);
  }
  int hx, hy, bx, by, lx, ly, rx, ry;
  P(0, 0, &hx, &hy);
  P(0, 0.16f * W, &bx, &by);
  P(-0.10f * W, 0.40f * W, &lx, &ly);
  P(0.10f * W, 0.40f * W, &rx, &ry);
  for (int oy = -grow; oy <= grow; oy++)
    for (int ox = -grow; ox <= grow; ox++) {
      for (int i = 0; i < 2 * N; i++)                         // the canopy: three strokes thick
        for (int k = 0; k < 3; k++) line(bm, ax[i] + ox, ay[i] + oy + k, ax[i + 1] + ox, ay[i + 1] + oy + k, ink);
      for (int i = -N; i <= N; i += 5) line(bm, ax[i + N] + ox, ay[i + N] + 2 + oy, hx + ox, hy + oy, ink);
      line(bm, hx + ox, hy + oy, bx + ox, by + oy, ink);
      line(bm, bx + ox, by + oy, lx + ox, ly + oy, ink);
      line(bm, bx + ox, by + oy, rx + ox, ry + oy, ink);
    }
  fillDisc(bm, hx, hy, 2 + grow, ink);
}

}  // namespace

IntroApp::IntroApp(terrain::IBlockReader* pack) {
  terrainOk_ = pack && terrain_.open(pack) == terrain::Terrain::OK;
  for (int l = 0; l <= 64; l++)
    for (int x = 0; x < 8; x++) {
      uint8_t b = 0;
      for (int k = 0; k < 8; k++)
        if (bayer8(x, 7 - k) < l) b |= (uint8_t)(1u << k);
      white_[l][x] = b;
    }
  for (int x = 0; x < 240; x++) wob_[x] = (int8_t)(unit(hash3((uint32_t)(x >> 2), 0x1f, 0xC10D)) * 7.0f + 3.0f * sinf(x * 0.045f));
}

bool IntroApp::pickThermal() {
  const Air& air = world_.air();
  ColumnInfo* c = cand_;                                  // a member: the loop task has 4 kB of stack
  float best = 0;
  bool found = false;
  const float lim = kFillRadiusM - 2600.0f;                // the cloud and the view around it must be real terrain
  for (float tm = 360.0f; tm <= 2100.0f; tm += 120.0f) {
    const int n = air.columns(0, 0, tm, 4500.0f, c, Air::kMaxInfo);
    for (int i = 0; i < n; i++) {
      if (c[i].cloud < 0.9f || fabsf(c[i].cx) > lim || fabsf(c[i].cy) > lim) continue;
      const float d = sqrtf(c[i].cx * c[i].cx + c[i].cy * c[i].cy);
      if (d < 500.0f) continue;
      const float score = c[i].cloud * (c[i].strength + c[i].z0 / 400.0f);
      if (score > best) { best = score; col_ = c[i]; tScene_ = tm; found = true; }
    }
    if (best >= 3.0f) break;                                // good enough: stop searching
  }
  if (!found) return false;
  const float cb = air.cloudBase();
  zStart_ = cb - kBelowStart;
  zEnd_ = cb + kAboveEnd;
  // End the turn heading at the cumulus: it grows ahead and above and swallows the camera.
  const float tEnd = tScene_ + kClimbMs * 0.001f;
  float ex, ey;
  air.columnPoint(col_, zEnd_, tEnd, &ex, &ey);
  (void)ex; (void)ey;
  float dx, dy;
  air.wind(cb, &dx, &dy);                                    // the cloud drifts downwind of its column: end looking that way
  const float headEnd = atan2f(dx, dy);
  theta0_ = headEnd - kPi * 0.5f - kOmega * (kClimbMs * 0.001f);
  return true;
}

void IntroApp::startTerrainScene() {
  const uint32_t t0 = clk_ ? clk_() : 0;
  site_ = -1;
  for (int k = 0; k <= kTakeoffCount; k++) {                // the preferred site first (a ridge with relief), then the table's order
    const int i = k == 0 ? kPreferredSite : k - 1;
    src_.set(&terrain_, kTakeoffs[i].lat, kTakeoffs[i].lon);
    float h;
    if (src_.at(0, 0, &h)) { site_ = i; break; }
  }
  if (site_ < 0) return;
  float h0 = 0;
  src_.at(0, 0, &h0);
  win_.set(&src_, kFillRadiusM, h0 - kSkirtBelow);
  float from, speed;
  World::usualWind(&win_, kTakeoffs[site_], &from, &speed);
  world_.setLevel(1);
  world_.beginAt(&win_, kSeed, 0.0f, 0.0f, World::kLaunchAgl, from, from, speed, 180.0f);   // fills the window
  const uint32_t t1 = clk_ ? clk_() : 0;
  fillUs_ = t1 - t0;
  if (!pickThermal()) { site_ = -1; return; }
  searchUs_ = (clk_ ? clk_() : 0) - t1;
  mode_ = Mode::TERRAIN;
}

void IntroApp::begin(uint32_t) {
  const uint32_t t0 = clk_ ? clk_() : 0;
  steps_ = 0;
  done_ = skipped_ = false;
  mode_ = Mode::FLAT;
  site_ = -1;
  fillUs_ = searchUs_ = 0;
  if (terrainOk_) startTerrainScene();
  if (mode_ == Mode::TERRAIN) {
    pose(0);
    chase_.reset(g_);
  }
  name_.build(&FreeSansBold18pt7b, 1);
  beginUs_ = (clk_ ? clk_() : 0) - t0;
}

void IntroApp::end() {}

void IntroApp::finish(bool skipped) {
  if (done_) return;
  done_ = true;
  skipped_ = skipped;
}

void IntroApp::tick(uint32_t) {
  if (done_ && nav()) nav()->openApp(next_);
}

void IntroApp::onKey(Key key, bool down, uint32_t) {
  if (down && key != KEY_MAIN) finish(true);             // the switch's own press is recognised on release (onInput)
}

void IntroApp::onInput(Key key, ButtonEvent ev) {
  if (key == KEY_MAIN && ev != BTN_NONE) finish(true);
}

void IntroApp::pose(uint32_t ms) {
  const float t = ms * 0.001f;
  const float u = clampf(t / (kClimbMs * 0.001f), 0.0f, 1.0f);
  const float z = zStart_ + (zEnd_ - zStart_) * (0.45f * u + 0.55f * u * u);
  float cx, cy;
  world_.air().columnPoint(col_, z, tScene_ + t, &cx, &cy);
  const float bl = smoothstep(u * 1.1f);                   // the circle drifts into the cumulus over the climb
  cx += (col_.cloudX - cx) * bl;
  cy += (col_.cloudY - cy) * bl;
  const float th = theta0_ + kOmega * t;
  g_.x = cx + kRadius * sinf(th);
  g_.y = cy + kRadius * cosf(th);
  g_.z = z;
  float hd = th + kPi * 0.5f;
  while (hd < 0) hd += 2 * kPi;
  while (hd >= 2 * kPi) hd -= 2 * kPi;
  g_.heading = hd;
  g_.bank = atanf(kOmega * kSpeed / kG) * smoothstep(t / 0.4f);
  g_.airspeed = kSpeed;
  g_.stalled = false;
}

void IntroApp::step(uint32_t, uint32_t dtMs) {
  if (done_) return;
  steps_++;
  if (mode_ == Mode::TERRAIN) {
    pose((uint32_t)steps_ * kStepMs);
    chase_.update(g_, NAN, dtMs * 0.001f);
  }
  if (steps_ >= kSteps) finish(false);
}

// ---------------------------------------------------------------------------------------------------------------- drawing

void IntroApp::drawTerrainScene(const Bitmap1& bm) {
  const HeightCache& hc = world_.cache();
  const Camera cam = chase_.camera(g_, hc);
  voxel_.render(bm, hc, cam);
  sky_.draw(bm, voxel_, world_.air(), g_, cam, tScene_ + steps_ * kStepMs * 0.001f, false, SkyFx::Style::COLUMNS);
  chase_.drawGlider(bm, g_, hc, cam);
  cloudBank(bm, (uint32_t)steps_ * kStepMs);
}

// The cumulus the glider climbs into, closing over the picture from the top: a lumpy front of outlined puffs with white behind it. The sim's own
// cloud sprites stay where the thermal put them; this one is scripted so that the screen is certain to fill.
void IntroApp::cloudBank(const Bitmap1& bm, uint32_t ms) {
  const float k = phase(ms, kBankFrom, kBankTo);
  if (k <= 0.0f) return;
  const int edge = (int)lroundf(-80.0f + 440.0f * k);
  if (edge > 40) fillRect(bm, 0, 0, 240, edge - 48, Gray(16));
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < 6; i++) {            // few, large puffs: the cost of the front is its area of fills
      const int r = 40 + (int)(unit(hash3((uint32_t)i, 5, 0xC10D)) * 18.0f);
      const int cx = i * 46 - 8, cy = edge - r / 2 + (int)lroundf(10.0f * sinf(i * 1.7f));
      fillDisc(bm, cx, cy, pass == 0 ? r : r - 2, pass == 0 ? DRAW_CLEAR : DRAW_SET);
      if (pass == 1) fillRing(bm, cx, cy, r - 2, r - 8, Gray(12), ClipBox(0, cy + r / 4, 239, 399));   // the shaded underside
    }
}

// No terrain: contour lines of rolling hills fall away as the glider climbs, clouds swell around it.
void IntroApp::drawFlatScene(const Bitmap1& bm, uint32_t ms) {
  const float t = ms * 0.001f;
  for (int b = 0; b < 5; b++) fillRect(bm, 0, b * 52, 240, 52, Gray(11 + b));       // sky, whiter toward the horizon
  const int kLayers = 8;
  static const uint8_t level[kLayers] = {13, 11, 10, 8, 7, 5, 4, 3};
  for (int k = 0; k < kLayers; k++) {
    const float near = (float)k / (kLayers - 1);
    const float base = 262.0f + 15.0f * k + t * (14.0f + 46.0f * near);             // nearer layers fall away faster
    const float a1 = 7.0f + 5.0f * k, f1 = 0.017f + 0.004f * k, f2 = 0.043f + 0.006f * k;
    const float p1 = 1.7f * k + 0.35f * t * (1.0f - near), p2 = 0.9f * k + 2.1f;
    for (int x = 0; x < 240; x++) {
      const int y = (int)lroundf(base + a1 * sinf(f1 * x + p1) + 0.45f * a1 * sinf(f2 * x + p2));
      if (y >= 400) continue;
      const int y0 = y < 0 ? 0 : y;
      vline(bm, x, y0, 399, Gray(level[k]));
      bm.pixel(x, y, DRAW_CLEAR);
      bm.pixel(x, y + 1, DRAW_CLEAR);
    }
  }
  // Clouds: a few puffs that swell as the glider rises into them (outlines first, so overlapping discs make one cloud).
  const float grow = 1.0f + 2.4f * smoothstep(t / 2.0f);
  static const struct { int x, y, r; } puff[] = {
      {44, 104, 30}, {80, 94, 38}, {118, 104, 32}, {170, 66, 36}, {206, 78, 30}, {150, 82, 28}, {30, 40, 30}, {72, 32, 28}, {120, 30, 34}};
  for (int pass = 0; pass < 2; pass++)
    for (const auto& p : puff) {
      const int cx = 120 + (int)lroundf((p.x - 120) * grow), cy = 90 + (int)lroundf((p.y - 90) * grow), r = (int)lroundf(p.r * grow);
      fillDisc(bm, cx, cy, pass == 0 ? r : r - 2, pass == 0 ? DRAW_CLEAR : DRAW_SET);
    }
  const float a = kOmega * t * 1.2f;
  const int gx = 120 + (int)lroundf(52.0f * sinf(a)), gy = 292 - (int)lroundf(14.0f * t);
  glyph(bm, gx, gy, 30.0f, 0.55f * cosf(a), DRAW_SET, 2);
  glyph(bm, gx, gy, 30.0f, 0.55f * cosf(a), DRAW_CLEAR, 0);
}

// Dithered white sweeping down from the top, ragged at its front: p 0 = none, 1 = all white.
void IntroApp::whiteout(const Bitmap1& bm, float p) {
  const int G = 46, maxWob = 10;
  const int top = (int)lroundf(p * (64 + G + maxWob));
  for (int x = 0; x < 240; x++) {
    uint8_t* row = bm.buf + (size_t)x * bm.stride;
    const int base = top - wob_[x];
    const uint8_t* pat = nullptr;
    int last = -1;
    for (int b = 49; b >= 0; b--) {                       // native byte 49 = the top of the screen
      const int l = base - (G * (49 - b)) / 49;
      if (l <= 0) break;
      if (l >= 64) { row[b] = 0xFF; continue; }
      if (l != last) { last = l; pat = white_[l]; }
      row[b] |= pat[x & 7];
    }
  }
}

// The name: black on the white, dissolving in through the same dither (level 0..64). The logo is drawn whole into a frame
// that is already white where it sits; the pixels the level has not reached yet are put back to white.
void IntroApp::drawLogo(const Bitmap1& bm, int level) {
  if (level <= 0) return;
  if (!name_.isBuilt(&FreeSansBold18pt7b, bm.rotation)) name_.build(&FreeSansBold18pt7b, bm.rotation);
  const char* name = "stavl.com";
  drawText(bm, 120 - textWidth(name, name_) / 2, 208, name, name_, DRAW_CLEAR);
  if (level >= 64) return;
  // Put back to white what the level does not show yet: screen columns 10..229, the bytes that cover y 170..230.
  const int b0 = (399 - 230) >> 3, b1 = (399 - 170) >> 3;
  for (int rx = 10; rx < 230; rx++) {
    uint8_t* row = bm.buf + (size_t)rx * bm.stride;
    const uint8_t shown = white_[level][rx & 7];           // the pixels already dissolved in
    for (int b = b0; b <= b1; b++) row[b] |= (uint8_t)~shown;
  }
}

void IntroApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t) {
  const uint32_t ms = (uint32_t)steps_ * kStepMs;
  if (mode_ == Mode::TERRAIN) drawTerrainScene(bm);
  else drawFlatScene(bm, ms);
  const float w = phase(ms, kWhiteFrom, kWhiteTo);
  if (w > 0.0f) whiteout(bm, w);
  const int lv = (int)lroundf(64.0f * phase(ms, kLogoFrom, kLogoTo));
  drawLogo(bm, lv);
}

int IntroApp::gameInfo(char* dst, size_t cap) {
  return snprintf(dst, cap, "intro=%s site=%d begin_us=%lu fill_us=%lu search_us=%lu steps=%d%s", mode_ == Mode::TERRAIN ? "3d" : "flat", site_,
                  (unsigned long)beginUs_, (unsigned long)fillUs_, (unsigned long)searchUs_, steps_, skipped_ ? " skipped" : "");
}

}  // namespace intro
