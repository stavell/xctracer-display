#pragma once
// Mock hardware for the PC emulator: every port of the app, driven by scripts instead of pins.
#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include <Adafruit_SharpMem.h>      // host mock from emu/stubs
#include "app/button_fsm.h"
#include "app/ports.h"

class MockPanel : public IPanel {
 public:
  MockPanel() : disp_(nullptr, 0, 400, 240, 0) { memset(snap_, 0xAA, sizeof snap_); }
  Adafruit_GFX& gfx() override { return disp_; }
  void setRotation(uint8_t r) override { disp_.setRotation(r); }
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
  void addItem(const char* name, int rssi) { ListItem it; memset(&it, 0, sizeof it); strncpy(it.name, name, 23); it.rssi = rssi; items_.push_back(it); }

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
  int tz = 0;
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
  MockPower power;
};
