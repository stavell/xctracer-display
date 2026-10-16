#pragma once
// Mock hardware for the PC emulator: every port of the app, driven by scripts instead of pins.
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <Adafruit_SharpMem.h>      // host mock from emu/stubs
#include "platform/input/button_fsm.h"
#include "platform/sensors/mount.h"
#include "apps/vario/core/types.h"
#include "apps/vario/app/ports.h"
#include "apps/vario/app/app.h"
#include "apps/vario/app/vario_app.h"
#include "platform/app_host.h"

class MockPanel : public IPanel {
 public:
  MockPanel() : disp_(nullptr, 0, 400, 240, 0) { memset(snap_, 0xAA, sizeof snap_); }
  Adafruit_GFX& gfx() override { return disp_; }
  // same buffer the shots are rendered from (stub layout: row stride 50 bytes, LSB leftmost)
  Bitmap1 bitmap() override { return Bitmap1{disp_.getBuffer(), 400, 240, 50, disp_.getRotation()}; }
  void setRotation(uint8_t r) override { disp_.setRotation(r); }
  void clear() override { disp_.clearDisplay(); }
  void present() override {
    presents++;
    if (!dirty_ && memcmp(disp_.getBuffer(), snap_, sizeof snap_) == 0) return;
    refresh();
  }
  void invalidate() override { dirty_ = true; }
  void refresh() override { memcpy(snap_, disp_.getBuffer(), sizeof snap_); dirty_ = false; refreshes++; }
  void showRaw(const uint8_t* frame) override { memcpy(disp_.getBuffer(), frame, sizeof snap_); refresh(); }
  int rawPixel(int x, int y) { return (disp_.getBuffer()[(y * 400 + x) / 8] >> (x & 7)) & 1; }   // native layout, 1 = white
  void dump(const char* path) { disp_.dumpPgm(path); }
  const uint8_t* raw() { return disp_.getBuffer(); }   // native 400x240 buffer, 50 bytes per row
  int refreshes = 0;
  int presents = 0;        // every present() call, changed screen or not
 private:
  Adafruit_SharpMem disp_;
  uint8_t snap_[400 * 240 / 8];
  bool dirty_ = true;
};

class MockInput : public IInput {
 public:
  // The user switch: scripted presses (press) or a live level (setKey with KEY_MAIN), both through the real
  // ButtonFsm. The front buttons (setKey with another key) only produce edges, like the device.
  void press(uint32_t at, uint32_t durMs) { presses_.push_back({at, durMs}); }
  void setKey(Key k, bool down, uint32_t now) {
    if (k >= KEY_COUNT || level_[k] == down) return;
    level_[k] = down;
    if (k != KEY_MAIN) pushEdge(KeyEdge{k, down, now});
  }
  bool keyDown(Key k) const { return level_[k]; }
  ButtonEvent poll(uint32_t now) override {
    bool down = level_[KEY_MAIN];
    for (auto& p : presses_) if (now - p.at < p.dur) down = true;   // unsigned difference: correct across the 49.7-day millis() wrap
    ButtonEvent e = fsm_.feed(down, now);
    if (fsm_.down() != mainDown_) {                                  // the same debounced edge the device queues
      mainDown_ = fsm_.down();
      pushEdge(KeyEdge{KEY_MAIN, mainDown_, now});
    }
    return e;
  }
  uint32_t heldMs(uint32_t now) override { return fsm_.heldMs(now); }
  bool popEdge(KeyEdge& out) override {
    if (head_ == tail_) return false;
    out = edges_[tail_++ % EDGES];
    return true;
  }
 private:
  struct P { uint32_t at, dur; };
  std::vector<P> presses_;
  ButtonFsm fsm_;
  bool level_[KEY_COUNT] = {};
  bool mainDown_ = false;
  static const unsigned EDGES = 64;                    // a fixed ring: the soak test counts heap allocations
  void pushEdge(const KeyEdge& e) { if (head_ - tail_ < EDGES) edges_[head_++ % EDGES] = e; }
  KeyEdge edges_[EDGES];
  unsigned head_ = 0, tail_ = 0;
};

class MockKeyStore : public IKeyStore {
 public:
  uint32_t get(StoreSlot s) const override { return s < SLOT_COUNT ? v[s] : 0; }
  void set(StoreSlot s, uint32_t x) override { if (s < SLOT_COUNT && v[s] != x) { v[s] = x; writes++; } }
  uint32_t v[SLOT_COUNT] = {};
  int writes = 0;
};

class MockBattery : public IBattery {
 public:
  void update(uint32_t) override {}
  int percent() override { return pct; }
  int pct = 76;
};

class MockTracer : public ITracer {
 public:
  void update(uint32_t) override {}
  ViewModel::Link link() const override { return link_; }
  bool hasSaved() const override { return saved_; }
  const char* deviceName() const override { return name_.c_str(); }
  const char* filter() const override { return filter_.c_str(); }
  void setFilter(const char* g) override { filter_ = g; }
  void listOpen() override { opened = true; }
  void listClose() override { opened = false; }
  int listCount() override { return (int)items_.size(); }
  bool listItem(int i, ListItem& o) override { if (i < 0 || i >= (int)items_.size()) return false; o = items_[i]; return true; }
  bool listConnect(int i) override { connected = i; opened = false; return true; }
  bool popLine(char* buf, size_t n) override {
    if (lines.empty()) return false;
    strncpy(buf, lines.front().c_str(), n - 1);
    buf[n - 1] = 0;
    lines.pop_front();
    return true;
  }
  void describe(char* buf, size_t n) const override { snprintf(buf, n, "mock"); }
  void pause() override { paused = true; pauses++; lines.clear(); link_ = ViewModel::LINK_NONE; }
  void resume() override { if (paused) { lines.clear(); resumes++; link_ = saved_ ? ViewModel::LINK_CONNECTED : ViewModel::LINK_NONE; } paused = false; }
  bool paused = false;
  int pauses = 0, resumes = 0;
  void addItem(const char* name, int rssi) { ListItem it; memset(&it, 0, sizeof it); strncpy(it.name, name, 23); it.rssi = rssi; strcpy(it.id, "00:00"); items_.push_back(it); }

  std::deque<std::string> lines;
  ViewModel::Link link_ = ViewModel::LINK_CONNECTED;
  bool saved_ = true, opened = false;
  int connected = -1;
 private:
  std::string name_ = "XC-Tracer", filter_ = "*";
  std::vector<ListItem> items_;
};

class MockLaptop : public ILaptop {
 public:
  bool pop(Command& o) override { if (cmds.empty()) return false; o = cmds.front(); cmds.pop_front(); return true; }
  bool hostDrawing(uint32_t) override { return drawing; }
  void service(uint32_t) override {}
  void sim(bool on) { Command c; c.type = Command::SIM; c.value = on; cmds.push_back(c); }
  void inject(const std::string& line) { Command c; c.type = Command::XCTRC; strncpy(c.text, line.c_str(), sizeof c.text - 1); cmds.push_back(c); }
  std::deque<Command> cmds;
  bool drawing = false;
};

class MockSettings : public ISettings {
 public:
  int tzMinutes() const override { return tz; }
  void setTzMinutes(int m) override { tz = m; }
  bool loadMagCal(MagCal& out) override { if (!hasCal) return false; out = cal; return true; }
  void saveMagCal(const MagCal& c) override { cal = c; hasCal = true; saves++; }
  int mountIndex() const override { return mount; }
  void setMountIndex(int i) override { mount = i; }
  int tz = 0;
  int mount = 0;
  MagCal cal;
  bool hasCal = false;
  int saves = 0;
};

// A magnetometer + accelerometer for a unit with a known orientation. Earth's field: 20 uT north and
// 40 uT down (a mid-latitude inclination); `hardIron` is added to the magnetometer like a magnetised
// part in the case would. Orientation: heading psi (azimuth of the top of the screen), pitch (top up),
// roll (right edge down), degrees.
class MockCompass : public ICompass {
 public:
  void enable(bool on) override { enabled = on; enables += on ? 1 : 0; }
  bool read(Vec3& a, Vec3& m, Vec3& g, bool& haveGyro) override {
    if (!enabled) return false;
    reading(psi, pitch, roll, a, m);
    // board-frame data: the inverse of the case mounting the app will apply
    if (boardMount) { a = mountInverse(boardMount, a); m = mountInverse(boardMount, m); }
    haveGyro = gyro;
    if (gyro) {                                   // still unit: a few counts of noise (a frozen gyro is a failure)
      g = Vec3();
      if (gyroNoise > 0) {
        seed = seed * 1664525u + 1013904223u; g.x = gyroNoise * ((seed >> 8) / 8388608.0f - 1.0f);
        seed = seed * 1664525u + 1013904223u; g.y = gyroNoise * ((seed >> 8) / 8388608.0f - 1.0f);
        seed = seed * 1664525u + 1013904223u; g.z = gyroNoise * ((seed >> 8) / 8388608.0f - 1.0f);
      }
      if (boardMount) g = mountInverse(boardMount, g);
    }
    return true;
  }
  void reading(double psiDeg, double pitchDeg, double rollDeg, Vec3& a, Vec3& m) const {
    const double D2R = 0.017453292519943295;
    double p = psiDeg * D2R, t = pitchDeg * D2R, r = rollDeg * D2R;
    // level device axes in east/north/up, then pitch about x, then roll about y
    double x0[3] = {cos(p), -sin(p), 0}, y0[3] = {sin(p), cos(p), 0}, z0[3] = {0, 0, 1};
    double x1[3], y1[3], z1[3], x2[3], z2[3];
    for (int i = 0; i < 3; i++) {
      x1[i] = x0[i];
      y1[i] = y0[i] * cos(t) + z0[i] * sin(t);
      z1[i] = -y0[i] * sin(t) + z0[i] * cos(t);
      x2[i] = x1[i] * cos(r) - z1[i] * sin(r);
      z2[i] = x1[i] * sin(r) + z1[i] * cos(r);
    }
    const double *ax[3] = {x2, y1, z2};
    const double up[3] = {0, 0, 1}, field[3] = {0, 20, -40};
    double av[3], mv[3];
    for (int k = 0; k < 3; k++) {
      av[k] = 9.81 * (up[0] * ax[k][0] + up[1] * ax[k][1] + up[2] * ax[k][2]);
      mv[k] = field[0] * ax[k][0] + field[1] * ax[k][1] + field[2] * ax[k][2];
    }
    a.x = (float)av[0]; a.y = (float)av[1]; a.z = (float)av[2];
    m.x = (float)(mv[0] + hardIron[0]); m.y = (float)(mv[1] + hardIron[1]); m.z = (float)(mv[2] + hardIron[2]);
  }
  bool enabled = false;
  bool gyro = false;                // has a gyro (zero rates plus gyroNoise rad/s of noise)
  float gyroNoise = 0.002f;
  int boardMount = 0;               // data is delivered in the board frame of this mounting index
  uint32_t seed = 12345;
  int enables = 0;
  double psi = 0, pitch = 0, roll = 0;
  double hardIron[3] = {0, 0, 0};
};

class MockThermo : public IThermo {
 public:
  bool read(float& t, float& rh) override {
    if (!present || !fresh) return false;
    fresh = false;
    t = tempC;
    rh = rhPct;
    return true;
  }
  void set(float t, float rh = 50) { tempC = t; rhPct = rh; fresh = true; }   // a new reading becomes available
  float tempC = 15, rhPct = 50;
  bool present = true, fresh = false;
};

class MockPower : public IPower {
 public:
  void shutdown() override { off = true; }
  bool off = false;
};

// What the device's main loop does around the vario: an AppHost running a VarioApp over the mock panel and
// switch. Tests and replays drive the App through this, so they exercise the real host frame loop (clear,
// draw, present) and the real redraw cadence (App::wantsFrame). begin() / tick() are the host's.
class Driven : public App {
 public:
  Driven(IPanel& panel, IInput& input, IBattery& battery, ITracer& tracer, ILaptop& laptop,
         ISettings& settings, ICompass& compass, IThermo& thermo, IPower& power, AppConfig cfg = AppConfig())
      : App(panel, battery, tracer, laptop, settings, compass, thermo, power, cfg),
        host_(panel, input), va_(*this) {
    host_.add(va_);
    host_.setProbe(&probe_);
  }
  void begin(uint32_t now) { host_.start(0, now); }
  void tick(uint32_t now) { host_.tick(now); }
  AppHost& host() { return host_; }

 private:
  AppHost host_;
  VarioApp va_;
  VarioProbe probe_;
};

// A canvas for apps that draw through the Bitmap1 only (the games ignore the GFX argument).
inline Adafruit_GFX& dummyGfx() {
  static Adafruit_SharpMem d(nullptr, 0, 400, 240, 0);
  return d;
}

// Bundles one of everything, wired to an App.
struct Rig {
  MockPanel panel;
  MockInput input;
  MockBattery battery;
  MockTracer tracer;
  MockLaptop laptop;
  MockSettings settings;
  MockCompass compass;
  MockThermo thermo;
  MockPower power;
};
