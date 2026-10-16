#pragma once
// The seam between the platform and an application. An app is one self-contained program (the flight
// display, later games and tools); the host owns the active one and forwards time and input to it.
// Pure C++: compiles on the PC. For now the app still owns its screen cadence (panel present/redraw);
// moving that into the platform is a later step.
#include <stddef.h>
#include <stdint.h>
#include "platform/ports.h"

// Physical inputs. Today only KEY_MAIN exists (the Feather user switch); the others are reserved for the
// six-button console and are not wired to anything yet.
enum Key : uint8_t { KEY_MAIN, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ACTION, KEY_COUNT };

class IApp {
 public:
  virtual ~IApp() {}
  virtual const char* name() const = 0;
  virtual void begin(uint32_t nowMs) = 0;   // become the active app
  virtual void end() = 0;                   // stop being the active app: release what begin() took
  virtual void tick(uint32_t nowMs) = 0;    // called every loop iteration (about every 10 ms)
  // A button event, forwarded by the host. Apps that poll their own IInput port (the vario) may ignore it.
  virtual void onInput(Key key, ButtonEvent ev) { (void)key; (void)ev; }
};

// Holds the registered apps and the active one. No launcher yet: start() picks one by index.
class AppHost {
 public:
  static const int MAX_APPS = 4;
  bool add(IApp& app) {
    if (n_ >= MAX_APPS) return false;
    apps_[n_++] = &app;
    return true;
  }
  int count() const { return n_; }
  IApp* active() const { return active_; }
  bool start(int index, uint32_t nowMs) {
    if (index < 0 || index >= n_) return false;
    if (active_) active_->end();
    active_ = apps_[index];
    active_->begin(nowMs);
    return true;
  }
  void tick(uint32_t nowMs) {
    if (active_) active_->tick(nowMs);
  }
  void input(Key key, ButtonEvent ev) {
    if (active_) active_->onInput(key, ev);
  }

 private:
  IApp* apps_[MAX_APPS] = {};
  int n_ = 0;
  IApp* active_ = nullptr;
};
