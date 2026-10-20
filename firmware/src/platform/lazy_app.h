#pragma once
// Apps that take RAM only while they run. The box image holds several big apps (the flight sim ~93 kB, the compass tool
// ~44 kB, Tetris) but only one runs at a time, and a statically allocated app costs its full size in .bss for good, which
// once left the box image with 2 kB of heap and a dead board (FreeRTOS takes its task stacks from that heap).
// Instead the rarely used apps share ONE arena, sized for the largest of them: LazyApp<T> constructs its T in the arena
// in begin() and destroys it in end(). The app's state therefore starts afresh each time it is opened (what a launcher
// start does anyway); anything worth keeping goes through a port (high scores: IScoreStore, settings: the key store).
//
// The host sees an ordinary IApp. name() works while the app does not exist (the home screen asks). Pure C++.
#include <new>
#include <stddef.h>
#include <stdint.h>
#include "platform/app_host.h"

constexpr size_t maxSize(size_t a, size_t b) { return a > b ? a : b; }   // to size an arena for the biggest app

// A block of memory that holds at most one app object at a time.
class AppArena {
 public:
  AppArena(void* mem, size_t capacity) : mem_(mem), cap_(capacity) {}
  AppArena(const AppArena&) = delete;
  AppArena& operator=(const AppArena&) = delete;
  // The block for an object of `size` / `align` bytes, or nullptr when it does not fit or is in use.
  void* claim(size_t size, size_t align, const void* owner) {
    if (owner_ || size > cap_ || align > 16) return nullptr;
    owner_ = owner;
    return mem_;
  }
  void release(const void* owner) { if (owner_ == owner) owner_ = nullptr; }
  bool busy() const { return owner_ != nullptr; }
  size_t capacity() const { return cap_; }

 private:
  void* mem_;
  size_t cap_;
  const void* owner_ = nullptr;
};

template <size_t N>
class AppArenaStore : public AppArena {
 public:
  AppArenaStore() : AppArena(buf_, N) {}

 private:
  alignas(16) uint8_t buf_[N];
};

template <class T>
class LazyApp : public IApp {
 public:
  // make(mem, ctx) constructs the T in `mem` (placement new) and does any wiring the app needs before begin().
  typedef T* (*Make)(void* mem, void* ctx);
  LazyApp(AppArena& arena, const char* name, Make make, void* ctx = nullptr)
      : arena_(arena), name_(name), make_(make), ctx_(ctx) {}
  ~LazyApp() { destroy(); }

  T* get() { return app_; }                    // nullptr while the app is not the active one
  T* operator->() { return app_; }
  bool alive() const { return app_ != nullptr; }

  const char* name() const override { return name_; }
  void begin(uint32_t nowMs) override {
    if (app_) return;
    void* mem = arena_.claim(sizeof(T), alignof(T), this);
    if (!mem) return;                          // cannot happen when the arena was sized for the apps it hosts
    app_ = make_(mem, ctx_);
    app_->attach(nav());
    app_->begin(nowMs);
  }
  void end() override {
    if (!app_) return;
    app_->end();
    destroy();
  }
  void tick(uint32_t nowMs) override { if (app_) app_->tick(nowMs); }
  void onKey(Key key, bool down, uint32_t nowMs) override { if (app_) app_->onKey(key, down, nowMs); }
  void onInput(Key key, ButtonEvent ev) override { if (app_) app_->onInput(key, ev); }
  void onHeld(uint32_t mainHeldMs) override { if (app_) app_->onHeld(mainHeldMs); }
  bool wantsFrame(uint32_t nowMs) override { return app_ && app_->wantsFrame(nowMs); }
  uint32_t nextWakeMs(uint32_t nowMs) override { return app_ ? app_->nextWakeMs(nowMs) : 0xFFFFFFFFu; }
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override { if (app_) app_->draw(bm, gfx, nowMs); }
  bool ownsPanel(uint32_t nowMs) override { return app_ && app_->ownsPanel(nowMs); }
  uint32_t frameIntervalMs() const override { return app_ ? app_->frameIntervalMs() : 0; }
  void step(uint32_t simMs, uint32_t dtMs) override { if (app_) app_->step(simMs, dtMs); }
  bool handlesHold() const override { return app_ && app_->handlesHold(); }
  bool comboExit() const override { return app_ ? app_->comboExit() : true; }
  bool sheetBlocked() const override { return app_ && app_->sheetBlocked(); }
  SystemSheet::Row sheetStart() const override { return app_ ? app_->sheetStart() : SystemSheet::R_HOME; }
  void onSheet(bool open, uint32_t nowMs) override { if (app_) app_->onSheet(open, nowMs); }
  int gameInfo(char* dst, size_t cap) override { return app_ ? app_->gameInfo(dst, cap) : 0; }

 private:
  void destroy() {
    if (!app_) return;
    app_->~T();
    app_ = nullptr;
    arena_.release(this);
  }
  AppArena& arena_;
  const char* name_;
  Make make_;
  void* ctx_;
  T* app_ = nullptr;
};
