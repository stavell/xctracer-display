#include "apps/pet/pet_app.h"
#include <stdio.h>
#include <string.h>
#include "platform/gfx/gray.h"

namespace pet_app {

using namespace pet;

void PetApp::begin(uint32_t nowMs) {
  for (Cell& c : cell_) c = Cell();
  have_ = false;
  seq_ = 0;
  stale_ = true;
  dirty_ = true;
  nextFrame_ = nowMs;
  tick(nowMs);
}

bool PetApp::anyWaiting() const {
  for (const Cell& c : cell_) if (c.present && !c.leaving && waiting(c.slot.state)) return true;
  return false;
}
int PetApp::hatching() const { int n = 0; for (const Cell& c : cell_) n += c.hatching; return n; }
int PetApp::leaving() const { int n = 0; for (const Cell& c : cell_) n += c.leaving; return n; }

void PetApp::onSnapshot(uint32_t nowMs) {
  const Snapshot& s = cur_;
  for (int i = 0; i < MAX_SLOTS; i++) {
    const Slot* f = nullptr;
    for (int k = 0; k < s.n; k++) if (s.slot[k].index == i) f = &s.slot[k];
    Cell& c = cell_[i];
    if (f) {
      const bool same = c.present && !c.leaving && c.slot.species == f->species && c.slot.variant == f->variant && !strcmp(c.slot.label, f->label);
      if (!same) { c.hatching = have_; c.since = nowMs; }   // the first snapshot after opening the app just shows the herd
      c.present = true;
      c.leaving = false;
      c.slot = *f;
    } else if (c.present && !c.leaving) {
      if (have_) { c.leaving = true; c.hatching = false; c.since = nowMs; }
      else c.present = false;
    }
  }
  rxMs_ = nowMs;
  have_ = true;
  dirty_ = true;
}

void PetApp::tick(uint32_t nowMs) {
  Snapshot s;
  uint32_t seq = 0;
  if (feed_.latest(s, seq) && seq != seq_) {
    seq_ = seq;
    cur_ = s;
    onSnapshot(nowMs);
  }
  const bool st = !have_ || nowMs - feed_.lastWriteMs() > kStaleMs;
  if (st != stale_) { stale_ = st; dirty_ = true; }
  if (stale_) {                                           // the transitions end where they are
    for (Cell& c : cell_) { c.hatching = false; if (c.leaving) { c.leaving = false; c.present = false; } }
    return;
  }
  for (Cell& c : cell_) {
    if (c.hatching && nowMs - c.since >= kTransitionMs) { c.hatching = false; dirty_ = true; }
    if (c.leaving && nowMs - c.since >= kTransitionMs) { c.leaving = false; c.present = false; dirty_ = true; }
  }
}

PetApp::Mode PetApp::mode(uint32_t) const {
  if (stale_) return M_STALE;
  bool work = false;
  for (const Cell& c : cell_) {
    if (!c.present) continue;
    if (c.hatching || c.leaving || waiting(c.slot.state)) return M_FAST;
    if (c.slot.state == S_WORKING) work = true;
  }
  return work ? M_WORK : M_SLOW;
}

uint32_t PetApp::nextWakeMs(uint32_t nowMs) {
  if (dirty_) return nowMs;
  if (mode(nowMs) == M_STALE) return 0xFFFFFFFFu;
  uint32_t w = nextFrame_;
  if (!stale_) {                                          // the staleness itself is a change to draw
    const uint32_t at = feed_.lastWriteMs() + kStaleMs + 1;
    if ((int32_t)(at - w) < 0) w = at;
  }
  return w;
}

pet_art::Look PetApp::lookOf(const Slot& sl, uint32_t nowMs) const {
  pet_art::Look l;
  l.species = sl.species & 7;
  l.variant = sl.variant & 3;
  l.state = sl.state;
  l.tool = sl.tool;
  l.context = sl.context;
  l.followers = sl.subagents;
  l.fat = cur_.bowl < 34 ? 0 : cur_.bowl < 67 ? 1 : 2;
  l.asleep = stale_;
  if (waiting(sl.state)) {                                // a long wait grows the jump, then holds a sign (no sound)
    const uint32_t m = sl.minutes + (nowMs - rxMs_) / 60000;
    l.escalation = m >= 10 ? 2 : m >= 2 ? 1 : 0;
  }
  return l;
}

void PetApp::drawTopBar(const Bitmap1& bm, uint32_t) {
  pet_art::drawBowl(bm, 6, 6, cur_.bowl, cur_.bowlIsLimit());
  char b[8];
  snprintf(b, sizeof b, "+%d", cur_.hidden);
  drawText(bm, 62, 28, b, fonts_.mid, DRAW_CLEAR);
  if (cur_.hiddenWaiting > 0) {                           // inverted: hidden sessions that wait for you
    ui::roundFill(bm, 118, 5, 50, 30, 6, DRAW_CLEAR);
    snprintf(b, sizeof b, "%d", cur_.hiddenWaiting);
    drawText(bm, 143 - textWidth(b, fonts_.mid) / 2, 28, b, fonts_.mid, DRAW_SET);
  }
  if (stale_ || cur_.paused()) pet_art::drawUnplugged(bm, 200, 6);
  bm.hline(0, ui::W - 1, kBarH - 1, DRAW_CLEAR);
  bm.hline(0, ui::W - 1, kBarH - 2, DRAW_CLEAR);
}

void PetApp::drawCell(const Bitmap1& bm, int i, uint32_t nowMs) {
  const Cell& c = cell_[i];
  if (!c.present) return;
  const int ox = (i % 2) * pet_art::CELL, oy = kBarH + (i / 2) * pet_art::CELL;
  const pet_art::Look l = lookOf(c.slot, nowMs);
  bool invert = false;
  if (c.hatching) {
    pet_art::drawHatch(bm, ox, oy, l, nowMs - c.since);
  } else if (c.leaving) {
    pet_art::drawWave(bm, ox, oy, l, nowMs - c.since);
  } else {
    pet_art::drawPet(bm, ox, oy, l, nowMs);
    pet_art::drawFollowers(bm, ox, oy, c.slot.subagents, nowMs, !stale_);
    invert = !stale_ && waiting(c.slot.state) && ((nowMs / 500) & 1) == 0;   // 1 Hz blink
  }
  if (!c.hatching) {
    char lab[5];
    memcpy(lab, c.slot.label, 5);
    for (int k = 3; k >= 0 && lab[k] == ' '; k--) lab[k] = 0;
    drawText(bm, ox + 60 - textWidth(lab, fonts_.mid) / 2, oy + 116, lab, fonts_.mid, DRAW_CLEAR);
  }
  if (c.slot.state == S_FAILED && !stale_ && !c.leaving) {                    // dashed border
    for (int x = 3; x < pet_art::CELL - 3; x += 8) {
      bm.hspan(ox + x, oy + 3, 5, DRAW_CLEAR); bm.hspan(ox + x, oy + 4, 5, DRAW_CLEAR);
      bm.hspan(ox + x, oy + pet_art::CELL - 5, 5, DRAW_CLEAR); bm.hspan(ox + x, oy + pet_art::CELL - 4, 5, DRAW_CLEAR);
    }
    for (int y = 3; y < pet_art::CELL - 3; y += 8) {
      bm.vspan(ox + 3, oy + y, 5, DRAW_CLEAR); bm.vspan(ox + 4, oy + y, 5, DRAW_CLEAR);
      bm.vspan(ox + pet_art::CELL - 5, oy + y, 5, DRAW_CLEAR); bm.vspan(ox + pet_art::CELL - 4, oy + y, 5, DRAW_CLEAR);
    }
  }
  if (invert) bm.fillRect(ox + 1, oy + 1, pet_art::CELL - 2, pet_art::CELL - 2, DRAW_INVERT);
}

void PetApp::draw(Bitmap1& bm, Adafruit_GFX&, uint32_t nowMs) {
  fonts_.ensure(bm.rotation);
  drawTopBar(bm, nowMs);
  vline(bm, pet_art::CELL - 1, kBarH, ui::H - 1, Gray(8));
  for (int r = 1; r < 3; r++) hline(bm, 0, ui::W - 1, kBarH + r * pet_art::CELL - 1, Gray(8));
  for (int i = 0; i < MAX_SLOTS; i++) drawCell(bm, i, nowMs);
  const Mode m = mode(nowMs);
  nextFrame_ = nowMs + (m == M_FAST ? 167 : m == M_WORK ? 500 : m == M_SLOW ? 3000 : 0x40000000u);
  dirty_ = false;
  frames_++;
  if (have_) feed_.markShown(cur_.seq);
}

}  // namespace pet_app
