#pragma once
// The apps the home screen lists, shared by the launcher (the grid) and Settings (the start-up choice). An app
// registers with a name, one line of text and an icon; `app` is its index in the AppHost. Apps that are tools
// of the box itself (Settings) are listed but are not offered as the start-up app.
#include <stddef.h>
#include "platform/gfx/icons.h"
#include "platform/ports.h"

struct AppEntry {
  const char* title;
  const char* sub;         // a few words under the name on the tile
  const char* about;       // one line under the grid while the tile is chosen
  int app;                 // index in the AppHost
  IconId icon;
  bool startable;          // may be the app the box starts with
};

class AppRegistry {
 public:
  static const int MAX = 8;
  bool add(const char* title, const char* sub, const char* about, int appIndex, IconId icon, bool startable = true) {
    if (n_ >= MAX) return false;
    e_[n_++] = AppEntry{title, sub, about, appIndex, icon, startable};
    return true;
  }
  int count() const { return n_; }
  const AppEntry& at(int i) const { return e_[i]; }
  const AppEntry* find(int app) const {
    for (int i = 0; i < n_; i++) if (e_[i].app == app) return &e_[i];
    return nullptr;
  }

  // The app the box starts with at power-on: the stored choice if it is a startable entry, else the first
  // startable entry (the vario). Flight safety: anything unknown or damaged lands there.
  int startupApp(const IKeyStore& store) const {
    const uint32_t v = store.get(SLOT_BOOT_APP);
    int first = -1;
    for (int i = 0; i < n_; i++) {
      if (!e_[i].startable) continue;
      if (first < 0) first = e_[i].app;
      if ((uint32_t)e_[i].app == v) return e_[i].app;
    }
    return first < 0 ? 0 : first;
  }
  // The next startable app after `current`, wrapping.
  int nextStartup(int current) const {
    int idx = -1;
    for (int i = 0; i < n_; i++) if (e_[i].app == current) idx = i;
    for (int k = 1; k <= n_; k++) {
      const AppEntry& e = e_[(idx + k + n_) % n_];
      if (e.startable) return e.app;
    }
    return current;
  }

 private:
  AppEntry e_[MAX] = {};
  int n_ = 0;
};
