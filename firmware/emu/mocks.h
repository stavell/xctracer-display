#pragma once
// Mock hardware for the PC emulator: every port of the app, driven by scripts instead of pins.
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <Adafruit_SharpMem.h>      // host mock from emu/stubs
#include "app/button_fsm.h"
#include "core/types.h"
#include "app/ports.h"

class MockPanel : public IPanel {
 public:
  MockPanel() : disp_(nullptr, 0, 400, 240, 0) { memset(snap_, 0xAA, sizeof snap_); }
  Adafruit_GFX& gfx() override { return disp_; }
  void setRotation(uint8_t r) override { disp_.setRotation(r); }
  void clear() override { disp_.clearDisplay(); }
  void present() override {
    if (!dirty_ && memcmp(disp_.getBuffer(), snap_, sizeof snap_) == 0) return;
    refresh();
  }
  void invalidate() override { dirty_ = true; }
  void refresh() override { memcpy(snap_, disp_.getBuffer(), sizeof snap_); dirty_ = false; refreshes++; }
  void dump(const char* path) { disp_.dumpPgm(path); }
  int refreshes = 0;
 private:
  Adafruit_SharpMem disp_;
  uint8_t snap_[400 * 240 / 8];
  bool dirty_ = true;
};

class MockInput : public IInput {
 public:
  void press(uint32_t at, uint32_t durMs) { presses_.push_back({at, durMs}); }
  ButtonEvent poll(uint32_t now) override {
    bool down = false;
    for (auto& p : presses_) if (now >= p.at && now < p.at + p.dur) down = true;
    return fsm_.feed(down, now);
  }
  uint32_t heldMs(uint32_t now) override { return fsm_.heldMs(now); }
 private:
  struct P { uint32_t at, dur; };
  std::vector<P> presses_;
  ButtonFsm fsm_;
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
  int tz = 0;
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
  bool read(Vec3& a, Vec3& m) override {
    if (!enabled) return false;
    reading(psi, pitch, roll, a, m);
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
  int enables = 0;
  double psi = 0, pitch = 0, roll = 0;
  double hardIron[3] = {0, 0, 0};
};

class MockPower : public IPower {
 public:
  void shutdown() override { off = true; }
  bool off = false;
};

// Bundles one of everything, wired to an App.
struct Rig {
  MockPanel panel;
  MockInput input;
  MockBattery battery;
  MockTracer tracer;
  MockLaptop laptop;
  MockSettings settings;
  MockCompass compass;
  MockPower power;
};
