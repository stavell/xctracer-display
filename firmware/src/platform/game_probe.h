#pragma once
// Frame timing of the game mode (AppHost with frameIntervalMs() > 0), summed over one second and formatted as one status line:
//
//   gm app=flightsim fps=49.0 frames=49 catchup=1 dropped=0 busy=0 step_avg/max=3.1/6.0 clear_avg=0.2 draw_avg/max=11.0/15.2
//      present_avg/max=0.9/1.4 wait_avg=0.0 tick_max=24.1 idle=18% <app part: view=3d scale=3 cache_decodes=0>
//
// All times in ms. step = one app step() call (a frame with catch-up runs several), draw = the app's draw(), present = the
// CPU time inside IPanel::present() (line diff, packing, waiting for the previous transfer; the SPI/DMA itself runs beside the
// loop), wait = the part of present() spent waiting for the previous transfer (hardware ports feed it with addWait()),
// tick_max = the slowest whole AppHost::tick(), idle = share of the second spent outside tick() (the loop's delay).
// catchup = extra steps run because a frame was late (its picture was skipped, the simulation kept real time);
// dropped = slots lost beyond kMaxCatchUp (the simulation fell behind real time); busy = host iterations in which a frame was due
// but the previous SPI transfer was still in flight. The app part comes from IApp::gameInfo().
// Pure: the clock (microseconds, wrapping) is injected, so the device (cycle counter) and the PC bench (steady_clock) share it.
#include <stdio.h>
#include <stdint.h>
#include "platform/app_host.h"

class GameProbe : public IFrameProbe {
 public:
  typedef uint32_t (*ClockUs)();
  explicit GameProbe(ClockUs clock, IFrameProbe* next = nullptr) : clock_(clock), next_(next) {}

  void clear(bool begin) override { if (next_) next_->clear(begin); if (on_) span(begin, &clearT_); }
  void present(bool begin) override {
    if (next_) next_->present(begin);
    if (!on_) return;
    span(begin, &presentT_);
    if (!begin) frames_++;
  }
  void step(bool begin) override { if (on_) span(begin, &stepT_); }
  void draw(bool begin) override { if (on_) span(begin, &drawT_); }

  void addWait(uint32_t us) { waitSum_ += us; }
  void noteTick(uint32_t us) { tickSum_ += us; if (us > tickMax_) tickMax_ = us; }

  // Start (on == true) / stop counting; the window restarts. Cheap enough to call every loop iteration.
  void enable(bool on, uint32_t nowMs) {
    if (on == on_) return;
    on_ = on;
    reset(nowMs);
  }
  bool enabled() const { return on_; }
  bool due(uint32_t nowMs) const { return on_ && (uint32_t)(nowMs - winStart_) >= 1000; }

  // Format the finished window into dst (and start the next one). stats: the host's running FrameStats; info: IApp::gameInfo text.
  int report(char* dst, size_t cap, uint32_t nowMs, const char* app, const FrameStats& st, const char* info) {
    if (st.catchUp < lastCatch_ || st.dropped < lastDropped_ || st.panelBusy < lastBusy_) lastCatch_ = lastDropped_ = lastBusy_ = 0;   // the host restarted its stats (app switch)
    const uint32_t winMs = nowMs - winStart_ ? nowMs - winStart_ : 1;
    const double f = frames_ ? 1.0 / frames_ : 0.0;
    const double steps = stepT_.n ? 1.0 / stepT_.n : 0.0;
    const double pc = presentT_.n ? 1.0 / presentT_.n : 0.0;
    const uint32_t idlePct = winMs * 1000ull > tickSum_ ? (uint32_t)((winMs * 1000ull - tickSum_) * 100ull / (winMs * 1000ull)) : 0;
    int n = snprintf(dst, cap,
                     "gm app=%s fps=%.1f frames=%lu catchup=%lu dropped=%lu busy=%lu step_avg/max=%.1f/%.1f clear_avg=%.1f draw_avg/max=%.1f/%.1f "
                     "present_avg/max=%.1f/%.1f wait_avg=%.1f tick_max=%.1f idle=%lu%%%s%s",
                     app, frames_ * 1000.0 / winMs, (unsigned long)frames_, (unsigned long)(st.catchUp - lastCatch_),
                     (unsigned long)(st.dropped - lastDropped_), (unsigned long)(st.panelBusy - lastBusy_), stepT_.sum * steps / 1000.0,
                     stepT_.max / 1000.0, clearT_.sum * f / 1000.0, drawT_.sum * f / 1000.0, drawT_.max / 1000.0, presentT_.sum * pc / 1000.0,
                     presentT_.max / 1000.0, waitSum_ * f / 1000.0, tickMax_ / 1000.0, (unsigned long)idlePct, info && info[0] ? " " : "",
                     info ? info : "");
    lastCatch_ = st.catchUp; lastDropped_ = st.dropped; lastBusy_ = st.panelBusy;
    reset(nowMs);
    return n;
  }

 private:
  struct Span { uint32_t t0 = 0, sum = 0, max = 0, n = 0; };
  void span(bool begin, Span* s) {
    if (begin) { s->t0 = clock_(); return; }
    const uint32_t d = clock_() - s->t0;
    s->sum += d; s->n++;
    if (d > s->max) s->max = d;
  }
  void reset(uint32_t nowMs) {
    stepT_ = clearT_ = drawT_ = presentT_ = Span();
    frames_ = 0; waitSum_ = 0; tickSum_ = 0; tickMax_ = 0;
    winStart_ = nowMs;
  }
  ClockUs clock_;
  IFrameProbe* next_;
  bool on_ = false;
  Span stepT_, clearT_, drawT_, presentT_;
  uint32_t frames_ = 0, waitSum_ = 0, tickSum_ = 0, tickMax_ = 0, winStart_ = 0;
  uint32_t lastCatch_ = 0, lastDropped_ = 0, lastBusy_ = 0;
};
