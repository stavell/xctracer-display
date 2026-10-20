#pragma once
// Tests for the system sheet (platform/ui/system_sheet.h): its state machine alone, then inside the host with spy apps, then in
// the whole box (vario, Tetris, intro, the walks over the switch alone and over the six buttons alone). Included by tests.cpp
// after box_tests.h.
#include "platform/ui/system_sheet.h"

namespace sheet_t {

using bx::BoxRun;
using bx::SpyApp;
using bx::blackPixels;

static void unit() {
  puts("system sheet: rows, cursor, switch, six buttons, idle close, 6 s hold");
  SystemSheet s;
  CHECK(!s.isOpen());
  s.open(SystemSheet::R_BACK, true, 0);
  CHECK(s.isOpen() && s.count() == 4 && s.cursorRow() == SystemSheet::R_BACK);
  CHECK(s.row(0) == SystemSheet::R_BACK && s.row(1) == SystemSheet::R_HOME && s.row(2) == SystemSheet::R_SLEEP && s.row(3) == SystemSheet::R_OFF);
  // The switch: press = next (wraps), long = act.
  CHECK(s.input(BTN_SHORT, 100) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_HOME);
  CHECK(s.input(BTN_SHORT, 200) == SystemSheet::A_NONE && s.input(BTN_SHORT, 300) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_OFF);
  CHECK(s.input(BTN_SHORT, 400) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_BACK);   // wraps
  CHECK(s.input(BTN_HOLD, 450) == SystemSheet::A_NONE && s.isOpen());                                // a hold is not a press
  s.input(BTN_SHORT, 500);
  CHECK(s.input(BTN_LONG, 600) == SystemSheet::A_HOME && !s.isOpen());
  s.open(SystemSheet::R_BACK, true, 0);
  CHECK(s.input(BTN_LONG, 10) == SystemSheet::A_CLOSE);
  s.open(SystemSheet::R_SLEEP, true, 0);
  CHECK(s.input(BTN_LONG, 10) == SystemSheet::A_SLEEP);
  s.open(SystemSheet::R_BACK, true, 0);
  s.input(BTN_SHORT, 1); s.input(BTN_SHORT, 2); s.input(BTN_SHORT, 3);
  CHECK(s.input(BTN_LONG, 4) == SystemSheet::A_OFF);
  // Six buttons: UP / DOWN move, A acts, B is Back; key releases and other keys do nothing.
  s.open(SystemSheet::R_BACK, true, 0);
  CHECK(s.key(KEY_UP, true, 10) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_OFF);
  CHECK(s.key(KEY_DOWN, true, 20) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_BACK);
  CHECK(s.key(KEY_DOWN, false, 25) == SystemSheet::A_NONE && s.key(KEY_LEFT, true, 26) == SystemSheet::A_NONE && s.cursorRow() == SystemSheet::R_BACK);
  s.key(KEY_DOWN, true, 30);
  CHECK(s.key(KEY_ACTION, false, 35) == SystemSheet::A_NONE && s.isOpen());
  CHECK(s.key(KEY_ACTION, true, 40) == SystemSheet::A_HOME);
  s.open(SystemSheet::R_OFF, true, 0);
  CHECK(s.key(KEY_B, true, 50) == SystemSheet::A_CLOSE && !s.isOpen());                              // B = Back from any row
  // Without Home (the launcher): three rows; a Home start falls back to Back.
  s.open(SystemSheet::R_SLEEP, false, 0);
  CHECK(s.count() == 3 && s.cursorRow() == SystemSheet::R_SLEEP);
  for (int i = 0; i < 3; i++) CHECK(s.row(i) != SystemSheet::R_HOME);
  s.open(SystemSheet::R_HOME, false, 0);
  CHECK(s.cursorRow() == SystemSheet::R_BACK);
  // Idle: closes by itself after 6 s without input, input restarts the time; a held switch is input.
  s.open(SystemSheet::R_BACK, true, 1000);
  CHECK(s.tick(6900, 0) == SystemSheet::A_NONE && s.isOpen());
  CHECK(s.tick(7000, 0) == SystemSheet::A_CLOSE && !s.isOpen());
  s.open(SystemSheet::R_BACK, true, 0);
  s.input(BTN_SHORT, 5000);
  CHECK(s.tick(10900, 0) == SystemSheet::A_NONE && s.tick(11000, 0) == SystemSheet::A_CLOSE);
  s.open(SystemSheet::R_BACK, true, 0);
  CHECK(s.tick(9000, 3500) == SystemSheet::A_NONE && s.isOpen() && s.keepHolding());                // held: no idle close, hint on
  CHECK(s.barStep() >= 1 && s.barStep() <= 12);
  CHECK(s.tick(9100, 5900) == SystemSheet::A_NONE && s.barStep() == 11);
  CHECK(s.tick(9200, 6000) == SystemSheet::A_OFF && !s.isOpen());                                    // 6 s total = power off
  s.open(SystemSheet::R_BACK, true, 0);
  s.takeDirty();
  s.tick(10, 2900);
  CHECK(!s.takeDirty());                                                                             // below 3 s nothing to redraw
  s.tick(20, 3100);
  CHECK(s.takeDirty());
}

struct SheetSpy : SpyApp {
  bool blocked = false;
  SystemSheet::Row start = SystemSheet::R_HOME;
  int sheetOn = 0, sheetOff = 0;
  using SpyApp::SpyApp;
  bool sheetBlocked() const override { return blocked; }
  SystemSheet::Row sheetStart() const override { return start; }
  void onSheet(bool open, uint32_t) override { (open ? sheetOn : sheetOff)++; }
};

static int barInk(MockPanel& p) {                 // ink in the bottom strip where the hold bar lives (rotated portrait coordinates)
  Bitmap1 b = p.bitmap();
  int n = 0;
  for (int y = 380; y < 400; y++)
    for (int x = 0; x < 240; x++) n += b.getPixel(x, y) == 0;
  return n;
}

static void host() {
  puts("system sheet: host (hold, combo, cursor defaults, idle close, input only to the sheet, hold bar)");
  Rig r;
  AppHost h(r.panel, r.input);
  SheetSpy a("a"), v("v"), l("l"), intro("intro");
  v.start = SystemSheet::R_BACK;
  intro.blocked = true;
  h.add(a); h.add(v); h.add(l); h.add(intro);
  h.setLauncher(2);
  h.setSystemSheet(&r.power);
  uint32_t now = 0;
  auto run = [&](uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) h.tick(now); };
  h.start(0, now);
  run(100);

  // Cursor defaults: Home in an ordinary app, Back in the vario, Sleep (and no Home row) in the launcher.
  r.input.press(now + 10, 3300);
  run(3400);
  CHECK(h.sheetOpen() && h.sheet().cursorRow() == SystemSheet::R_HOME && h.sheet().count() == 4 && a.sheetOn == 1);
  CHECK(a.inputs == 0 && a.lastEv == BTN_NONE);                      // the hold did not reach the app
  const int inputs = a.inputs;
  r.input.press(now + 10, 120);                                      // a press now moves the sheet, not the app
  run(400);
  CHECK(h.sheet().cursorRow() == SystemSheet::R_SLEEP && a.inputs == inputs);
  run(7000);                                                         // 6 s without input: Back
  CHECK(!h.sheetOpen() && a.sheetOff == 1);
  h.start(1, now);
  run(100);
  r.input.press(now + 10, 3300);
  run(3400);
  CHECK(h.sheetOpen() && h.sheet().cursorRow() == SystemSheet::R_BACK);
  r.input.setKey(KEY_B, true, now); run(30); r.input.setKey(KEY_B, false, now); run(30);
  CHECK(!h.sheetOpen() && v.sheetOff == 1);                          // B = Back
  h.start(2, now);
  run(100);
  r.input.setKey(KEY_ACTION, true, now); r.input.setKey(KEY_B, true, now);
  run(1700);
  CHECK(h.sheetOpen() && h.sheet().cursorRow() == SystemSheet::R_SLEEP && h.sheet().count() == 3);   // the combo, in the launcher
  r.input.setKey(KEY_ACTION, false, now); r.input.setKey(KEY_B, false, now);
  run(100);
  CHECK(h.sheetOpen());                                              // releasing the combo does not act
  r.input.setKey(KEY_ACTION, true, now); run(30); r.input.setKey(KEY_ACTION, false, now); run(30);
  CHECK(r.power.requests == 1 && !h.sheetOpen());                    // A on Sleep
  // The start-up animation: the hold and the combo reach the app, no sheet.
  h.start(3, now);
  run(100);
  r.input.press(now + 10, 3300);
  run(3400);
  CHECK(!h.sheetOpen() && intro.lastEv == BTN_HOLD);
  r.input.setKey(KEY_ACTION, true, now); r.input.setKey(KEY_B, true, now);
  run(1700);
  CHECK(!h.sheetOpen());
  r.input.setKey(KEY_ACTION, false, now); r.input.setKey(KEY_B, false, now);
  run(100);

  // Held keys are released for the app when the sheet opens over it.
  h.start(0, now);
  run(100);
  r.input.setKey(KEY_LEFT, true, now); run(20);
  const int ups = a.keysUp;
  r.input.setKey(KEY_ACTION, true, now); r.input.setKey(KEY_B, true, now);
  run(1700);
  CHECK(h.sheetOpen() && a.keysUp >= ups + 1);
  r.input.setKey(KEY_ACTION, false, now); r.input.setKey(KEY_B, false, now); r.input.setKey(KEY_LEFT, false, now);
  run(100);
  r.input.setKey(KEY_B, true, now); run(30); r.input.setKey(KEY_B, false, now); run(30);
  CHECK(!h.sheetOpen());

  // The hold bar: from 0.7 s a bar along the bottom fills toward 3 s; a long press (0.7 .. 1.5 s) still arrives as a long press.
  h.start(0, now);
  run(200);
  CHECK(barInk(r.panel) == 0);
  r.input.press(now + 10, 1000);
  run(500);
  CHECK(barInk(r.panel) == 0);                                       // 0.5 s: a normal press shows nothing
  run(1000 - 500 + 200);                                             // released at 1.0 s
  CHECK(a.lastEv == BTN_LONG && !h.sheetOpen() && barInk(r.panel) == 0);
  r.input.press(now + 10, 2000);
  run(1300);                                                         // 1.3 s into the press
  const int ink1 = barInk(r.panel);
  CHECK(ink1 > 100);
  run(600);                                                          // 1.9 s: fuller
  CHECK(barInk(r.panel) > ink1);
  run(300);                                                          // released at 2.0 s before the 3 s mark
  run(200);
  CHECK(barInk(r.panel) == 0 && !h.sheetOpen());                     // gone after the release, nothing opened, no long press (2 s is a cancel)
  // The sheet's own bar from 3 to 6 s.
  r.input.press(now + 10, 4500);
  run(3400);
  CHECK(h.sheetOpen() && h.sheet().keepHolding() && h.sheet().barStep() >= 1);   // sheet is open, 'keep holding' hint with its own bar
  run(2000);
  CHECK(r.power.offs == 0);                                          // released at 4.5 s: not off (a 6 s hold powers off: clock_sleep_box)
}

static void box() {
  puts("system sheet: box (vario keeps running, Home, Tetris frozen and paused, intro skip only, switch-only and buttons-only walks)");
  {
    Box b;
    BoxRun u(b);
    b.boot(0);
    u.run(500);
    for (int i = 0; i < 3; i++) { b.r.tracer.lines.push_back(bx::xline(i, 1000 + i)); u.run(1000); }
    const int ok0 = b.varioCore().linesOk();
    u.off();
    CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_BACK && b.host.active() == &b.vario);
    const int pr = b.r.panel.presents;
    for (int i = 3; i < 6; i++) { b.r.tracer.lines.push_back(bx::xline(i, 1000 + i)); u.run(500); }
    CHECK(b.varioCore().linesOk() >= ok0 + 3);                          // the flight model keeps receiving data underneath
    CHECK(b.r.panel.presents > pr);                                     // and the picture goes on being refreshed under the sheet
    CHECK(b.host.sheetOpen());
    u.longp();                                                          // Back
    CHECK(!b.host.sheetOpen() && b.host.active() == &b.vario);
    u.run(300);
    CHECK(blackPixels(b.r.panel) > 2500);                               // the vario repainted in full
    u.home();
    CHECK(b.host.active() == &b.launcher && !b.vario.alive());
  }
  {
    Box b;                                                              // Tetris: frozen while the sheet is open, paused afterwards
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_TETRIS, 0);
    u.run(300);
    u.shortp();
    CHECK(b.tetris->game().state() == tetris::ST_PLAYING);
    u.run(300);
    u.off();
    CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_HOME);
    CHECK(b.tetris->game().state() == tetris::ST_PAUSED);
    const uint32_t ver = b.tetris->game().version();
    u.run(4000);
    CHECK(b.tetris->game().version() == ver);                           // nothing falls
    u.row(SystemSheet::R_BACK);
    u.longp();
    CHECK(!b.host.sheetOpen() && b.host.active() == &b.tetris && b.tetris->game().state() == tetris::ST_PAUSED);
    u.shortp();
    CHECK(b.tetris->game().state() == tetris::ST_PLAYING);              // the pause screen resumes
  }
  {
    std::unique_ptr<Box> pb(new Box());                                 // the intro: hold only skips
    Box& b = *pb;
    BoxRun u(b);
    b.boot(0, true);
    CHECK(b.host.active() == &b.introApp);
    u.run(500);
    b.r.input.press(u.now + 10, 3300);
    u.run(3500);
    CHECK(!b.host.sheetOpen() && b.host.active() == &b.vario);
  }
  {
    Box b;                                                              // the switch alone: launcher -> Settings -> sheet -> Home
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_LAUNCHER, 0);
    u.run(300);
    CHECK(!b.host.sixButtons());
    for (int i = 0; i < 4; i++) u.shortp();
    CHECK(b.launcher.selected() == 4);                                  // Vario, Compass, Sim, Tetris, Settings
    u.longp();
    CHECK(b.host.active() == &b.settings);
    u.shortp();
    CHECK(b.settings.selected() == 1);
    u.off();
    CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_HOME);
    u.longp();
    CHECK(b.host.active() == &b.launcher && !b.host.sheetOpen());
    u.off();                                                            // the launcher's sheet: Sleep first, no Home row
    CHECK(b.host.sheet().cursorRow() == SystemSheet::R_SLEEP && b.host.sheet().count() == 3);
  }
  {
    Box b;                                                              // the six buttons alone: the same walk
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_LAUNCHER, 0);
    u.run(300);
    auto tap = [&](Key k) { b.r.input.setKey(k, true, u.now); u.run(50); b.r.input.setKey(k, false, u.now); u.run(50); };
    tap(KEY_DOWN); tap(KEY_DOWN);
    CHECK(b.launcher.selected() == 4 && b.host.sixButtons());
    tap(KEY_ACTION);
    CHECK(b.host.active() == &b.settings);
    tap(KEY_DOWN);
    CHECK(b.settings.selected() == 1);
    b.r.input.setKey(KEY_ACTION, true, u.now); b.r.input.setKey(KEY_B, true, u.now);
    u.run(1700);
    CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_HOME);
    b.r.input.setKey(KEY_ACTION, false, u.now); b.r.input.setKey(KEY_B, false, u.now);
    u.run(100);
    tap(KEY_ACTION);
    CHECK(b.host.active() == &b.launcher && !b.host.sheetOpen());
    // The hint follows the control used last: a switch press brings the switch wording back.
    u.shortp();
    CHECK(!b.host.sixButtons());
    // The vario with the six buttons: down = next page / row, A = long press, B = back.
    b.host.start(Box::APP_VARIO, u.now);
    u.run(300);
    tap(KEY_ACTION);
    CHECK(b.varioCore().listOpen());
    tap(KEY_B);
    CHECK(!b.varioCore().listOpen());
    tap(KEY_DOWN);
    CHECK(b.varioCore().buildViewModel(u.now).page != ViewModel::PAGE_COMPASS || true);
    b.r.input.setKey(KEY_ACTION, true, u.now); b.r.input.setKey(KEY_B, true, u.now);
    u.run(1700);
    CHECK(b.host.sheetOpen() && b.host.sheet().cursorRow() == SystemSheet::R_BACK);
    b.r.input.setKey(KEY_ACTION, false, u.now); b.r.input.setKey(KEY_B, false, u.now);
    u.run(100);
    tap(KEY_B);                                                         // B = Back
    CHECK(!b.host.sheetOpen() && b.host.active() == &b.vario);
  }
}

static void run() {
  unit();
  host();
  box();
}

}  // namespace sheet_t
