#pragma once
// The example app "Level" (apps/examples/level): game-mode cadence, tilt -> bubble, the hold toggle on the switch and on the six
// buttons, the system sheet over it, sensors only while open. Included by tests.cpp.
#include "level_rig.h"

namespace level_t {

static void run() {
  puts("example Level: 50 fps, tilt, hold toggle (switch and buttons), system sheet, sensors off when left");
  static LevelRig t;
  CHECK(t.app.frameIntervalMs() == 20 && t.host.gameMode());
  CHECK(t.r.compass.enabled);                                     // begin() started the motion service
  t.tilt(10, -6);
  t.run(2000);
  const FrameStats& st = t.host.stats();
  CHECK(st.frames >= 98 && st.frames <= 101);                     // 50 fps: one frame per 20 ms
  CHECK(st.fps >= 49 && st.fps <= 51 && st.dropped == 0);
  CHECK(t.app.haveTilt());
  NEAR(t.app.rollDeg(), 10, 1.5);
  NEAR(t.app.pitchDeg(), -6, 1.5);

  // The switch: a short press freezes the reading, the next one releases it.
  t.r.input.press(t.now + 5, 100);
  t.run(300);
  CHECK(t.app.holding());
  t.tilt(-8, 12);
  t.run(500);
  NEAR(t.app.rollDeg(), 10, 1.5);                                 // frozen
  const uint32_t frames = st.frames;
  t.run(200);
  CHECK(st.frames >= frames + 9);                                 // the screen is still drawn at 50 fps while held
  t.r.input.press(t.now + 5, 100);
  t.run(1000);
  CHECK(!t.app.holding());
  NEAR(t.app.rollDeg(), -8, 1.5);
  NEAR(t.app.pitchDeg(), 12, 1.5);

  // The six buttons: A toggles too (on the press edge only).
  t.r.input.setKey(KEY_ACTION, true, t.now);
  t.run(30);
  t.r.input.setKey(KEY_ACTION, false, t.now);
  t.run(30);
  CHECK(t.app.holding());
  t.r.input.setKey(KEY_ACTION, true, t.now);
  t.run(30);
  t.r.input.setKey(KEY_ACTION, false, t.now);
  t.run(30);
  CHECK(!t.app.holding());

  // The system sheet opens over the app after a 3 s hold, the app is not stepped meanwhile, B closes it.
  CHECK(!t.host.sheetOpen());
  t.r.input.press(t.now + 5, 3200);
  t.run(3300);
  CHECK(t.host.sheetOpen());
  const uint32_t steps = st.steps;
  t.run(300);
  CHECK(st.steps == steps);
  t.r.input.setKey(KEY_B, true, t.now);
  t.run(30);
  t.r.input.setKey(KEY_B, false, t.now);
  t.run(60);
  CHECK(!t.host.sheetOpen());
  t.run(200);
  CHECK(st.steps > steps);                                        // stepping again (the frame clock restarted)

  t.app.end();
  CHECK(!t.r.compass.enabled);                                    // leaving the app switched the sensors off
}

}  // namespace level_t
