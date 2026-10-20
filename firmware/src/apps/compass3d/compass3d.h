#pragma once
// A 3D compass in a glass dome for the 1-bit portrait panel (240 x 400): the Compass & weather tool's page.
//
// A compass card (a disc with a raised, graduated rim wall) floats level in the real world and points north while the box
// is tilted and turned around it. The orientation comes from the AHRS quaternion; the card follows it through a damped
// spring (a liquid compass: it overshoots a little when the box is turned quickly), while the heading number is the exact
// AHRS reading. Everything is drawn straight into the panel's native bytes: convex polygons are filled in vertical runs
// (a native row of the portrait panel) with 8x8 Bayer dither, text is stroked polylines projected like the card, the
// static dome (bezel, liquid, vial, number window) is a cached 12 kB plate copied in word-wise.
//
// Pure code: no hardware, no clock (time is passed in), no libm in the per-frame work except sqrtf.
// Needs a Bitmap1 with rotation 1 and the 400 x 240 native size (the panel); draw() does nothing on any other.
#include <math.h>
#include <stdint.h>
#include "platform/gfx/atlas.h"
#include "platform/gfx/bitmap1.h"
#include "platform/gfx/ui.h"

namespace compass3d {

struct Input {
  bool valid = false;            // attitude known (the AHRS runs); false: the card rests and the number shows dashes
  float q[4] = {1, 0, 0, 0};     // device -> world (east, north, up), w x y z, as Ahrs::quaternion
  float headingDeg = NAN;        // the exact reading, azimuth of the top of the screen
  float trust = 1.0f;            // Ahrs::trust(): 1 = clean field, 0 = field rejected (magnetic disturbance)
  bool calibrated = true;        // a magnetometer calibration is stored
  float tempC = NAN, rhPct = NAN;
  int pressure10 = 0;            // hPa x 10 from the WeatherLog, 0 = no reading yet
  bool trendOk = false;          // WeatherLog::tendency(): needs 3 h of history
  int trendCls = 0;              // -2 falling fast .. +2 rising fast
};

class Compass3d {
 public:
  static const int W = 240, H = 400;
  static const int kSeg = 72;                       // card rim segments (5 degrees each)
  // Screen layout (pixels) and the camera.
  static const int DCX = 120, DCY = 140;            // dome centre
  static const int kVialX = 22, kVialY = 240, kVialR = 18;   // the bubble level, in the free bottom-left corner of the dome's square
  static const int RG = 102, RB = 116;              // glass and bezel radius
  static constexpr float kCardPx = 92.0f;           // card radius on the screen when seen from straight above
  static constexpr float kDist = 3.4f;              // camera distance in card radii
  static constexpr float kThick = 0.17f;            // rim wall height in card radii
  static constexpr float kTiltMax = 38.0f;          // degrees: the card is never drawn more tilted than this, so it never turns edge-on
  static constexpr float kBias = 0.35f;             // the dome is seen from this far below the screen's bottom edge (rad)
  // The spring of the card: natural frequency and damping ratio (overshoot about 16 % for 0.5).
  static constexpr float kOmega = 11.0f, kZeta = 0.5f;

  Compass3d();
  void reset();
  // One fixed step. simMs is the app's simulation clock (the wobble of a disturbed field runs on it).
  void step(const Input& in, uint32_t dtMs, uint32_t simMs);
  void draw(const Bitmap1& bm, uint32_t nowMs);            // the dome
  void drawFlat(const Bitmap1& bm, uint32_t nowMs);        // the flat page: numerals, heading tape, bubble, field quality, weather

  // --- for tests and tools ---
  const Input& input() const { return in_; }
  void card(float q[4]) const { for (int i = 0; i < 4; i++) q[i] = qc_[i]; }       // the damped card orientation
  // The device-frame (x right, y up the screen, z toward the viewer) vectors of world east, north and up as the dome
  // sees them (the view bias included).
  void axes(float e[3], float n[3], float u[3]) const;
  // Screen position of a point of the card's frame (u east, v north, z up, in card radii). False behind the camera.
  bool project(float u, float v, float z, int* sx, int* sy) const;
  bool topFacesViewer() const;
  float cardTiltDeg() const;                         // the drawn angle between the card's normal and the line of sight (never beyond kTiltMax)
  float tiltCue() const { return frame().cue; }
  int digitRamBytes() const { return (int)sizeof digitMem_; }
  int readoutTenths() const { return lastTenths_; }                       // the heading shown by the last draw, tenths 0..3599, -1 = dashes
  // Work of the last draw (the PC cannot time the device): vertical runs written, their bytes, projected points, filled polygons.
  struct Stats { uint32_t cols = 0, bytes = 0, proj = 0, polys = 0; };
  const Stats& stats() const { return stats_; }

 private:
  struct Frame {
    float ex, ey, ez, nx, ny, nz, ux, uy, uz;       // east / north / up in the camera frame
    float lum;                                      // 0..1 brightness of the card (light, tilt, trust)
    float tx, ty, tz;                               // the true gravity-up in the device frame (no view bias, no clamp)
    float cue;                                      // 0..1 how far the real tilt is beyond what the dome shows
  };
  Frame frame(bool clampTilt = true) const;
  void build(const Bitmap1& bm);
  void buildPlate();
  void buildDigits();
  // raster
  void col(const Bitmap1& bm, int x, int y0, int y1, int level, int op) const;
  void fillPoly(const Bitmap1& bm, const int16_t* xs, const int16_t* ys, int n, int level, int op);
  void annulus(const Bitmap1& bm, int cx, int cy, int ro, int ri, int level, int op);
  void ring2d(const Bitmap1& bm, int cx, int cy, int r, int level, int op);
  void segment(const Bitmap1& bm, int x0, int y0, int x1, int y1, bool thick, DrawMode m) const;
  // scene
  bool proj(const Frame& f, float u, float v, float z, int16_t* sx, int16_t* sy) const;
  void ringPoints(const Frame& f, float r, float z, int16_t* xs, int16_t* ys) const;
  void scene(const Bitmap1& bm, const Frame& f);
  void cardTop(const Bitmap1& bm, const Frame& f);
  void glyphs(const Bitmap1& bm, const Frame& f, int seg, const char* text, float rc, float height, bool thick);
  void north(const Bitmap1& bm, const Frame& f);
  void glass(const Bitmap1& bm, const Frame& f);
  void lubber(const Bitmap1& bm) const;
  void readouts(const Bitmap1& bm, const Frame& f, uint32_t nowMs);
  void bubble(const Bitmap1& bm, const Frame& f);
  void badges(const Bitmap1& bm, uint32_t nowMs);
  void weatherStrip(const Bitmap1& bm);
  void heading(const Bitmap1& bm, int y, int dotX);
  void tape(const Bitmap1& bm);

  Input in_;
  mutable Stats stats_;
  float qc_[4] = {1, 0, 0, 0};                      // card orientation, device -> world
  float wc_[3] = {0, 0, 0};                         // card angular velocity (world frame, rad/s)
  float tapeHdg_ = 0;                               // the heading the flat page's tape shows (glides along the shortest arc)
  bool tapeInit_ = false;
  float yawWobble_ = 0;                             // rad, the disturbed field shakes the card
  float greyed_ = 0;                                // 0..1 how much the card is greyed (disturbance, no data)
  uint32_t simMs_ = 0;
  bool snap_ = true;
  bool built_ = false;
  UiFonts fonts_;
  // tables
  float sa_[kSeg + 1], ca_[kSeg + 1];               // sin / cos of the rim angles (bearing from north, clockwise)
  uint8_t pat_[17][8];                              // dither bytes of a native row by level and x & 7
  // scratch
  int16_t lo_[W], hi_[W];
  int16_t px_[kSeg], py_[kSeg], qx_[kSeg], qy_[kSeg], mx_[kSeg], my_[kSeg];
  // cached pictures
  uint8_t plate_[W * H / 8];
  static const int kDigitW = 33, kDigitH = 55;
  uint8_t digitMem_[12][((kDigitH + 7) / 8) * kDigitW];
  Atlas digit_[12];                                 // 0..9, '.', '-'
  uint8_t digitTmp_[((kDigitW + 7) / 8) * kDigitH];
  // text caches (snprintf is slow on the device): redone when the value changes
  int lastTenths_ = -2;
  char hdgText_[8] = "";
  int8_t hdgGlyph_[6] = {};
  int lastTemp10_ = -999999, lastRh_ = -999999;
  char tempText_[12] = "", rhText_[8] = "", pressText_[8] = "";
  int lastPress_ = -1;
};

}  // namespace compass3d
