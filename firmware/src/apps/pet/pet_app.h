#pragma once
// "Agents": a desk display of up to 6 Claude Code sessions as pets (docs/claude_pet.md). The laptop daemon writes snapshots into the
// PetMailbox (platform/pet); this app only shows them. No input: the system sheet (3 s hold / A + B) is the way out. Change-driven
// (frameIntervalMs() == 0, so the laptop link stays on) with the frame cadence of spec section 10:
//   a slot waits (PERMISSION / QUESTION) or an egg / goodbye plays: 6 fps, the waiting cell inverts at 1 Hz
//   something works, nothing waits: 2 fps
//   only DONE / IDLE / FAILED: one frame every 3 s
//   no snapshot for 30 s: the herd sleeps, one frame, then none until a snapshot arrives
// The app never requests the clock sleep and nothing times out under it.
#include "apps/pet/pet_art.h"
#include "platform/app_host.h"
#include "platform/gfx/ui.h"
#include "platform/pet/pet_snapshot.h"

namespace pet_app {

const uint32_t kStaleMs = 30000;
const uint32_t kTransitionMs = 2000;
const int kBarH = 40;

class PetApp : public IApp {
 public:
  explicit PetApp(IPetFeed& feed) : feed_(feed) {}

  const char* name() const override { return "agents"; }
  void begin(uint32_t nowMs) override;
  void end() override {}
  void tick(uint32_t nowMs) override;
  bool wantsFrame(uint32_t nowMs) override { return dirty_ || (int32_t)(nowMs - nextFrame_) >= 0; }
  uint32_t nextWakeMs(uint32_t nowMs) override;
  void draw(Bitmap1& bm, Adafruit_GFX& gfx, uint32_t nowMs) override;

  // For tests: what the app thinks.
  bool stale() const { return stale_; }
  bool anyWaiting() const;
  uint32_t framesDrawn() const { return frames_; }
  int hatching() const;
  int leaving() const;

 private:
  struct Cell {
    bool present = false, hatching = false, leaving = false;
    uint32_t since = 0;               // start of the egg / goodbye
    pet::Slot slot;                   // kept while the pet says goodbye
  };
  enum Mode { M_STALE, M_SLOW, M_WORK, M_FAST };

  void onSnapshot(uint32_t nowMs);
  Mode mode(uint32_t nowMs) const;
  void drawTopBar(const Bitmap1& bm, uint32_t nowMs);
  void drawCell(const Bitmap1& bm, int i, uint32_t nowMs);
  pet_art::Look lookOf(const pet::Slot& s, uint32_t nowMs) const;

  IPetFeed& feed_;
  UiFonts fonts_;
  pet::Snapshot cur_;
  Cell cell_[pet::MAX_SLOTS];
  uint32_t seq_ = 0, rxMs_ = 0, nextFrame_ = 0, frames_ = 0;
  bool have_ = false, stale_ = true, dirty_ = true;
};

}  // namespace pet_app
