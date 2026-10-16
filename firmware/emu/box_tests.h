#pragma once
// Tests for the platform frame loop (AppHost), the input helpers (ComboHold, KeyDebounce), the launcher and the
// whole box: switching apps, what stops and restarts, and that the vario behaves as it does alone.
// Included by tests.cpp, after CHECK and line() are defined.
#include "box.h"
#include "platform/input/combo_hold.h"
#include "platform/input/key_debounce.h"

namespace bx {

static void combo_and_debounce() {
  puts("combo hold and key debounce");
  ComboHold c(1500);
  CHECK(!c.feed(true, false, 0) && !c.feed(false, true, 100));       // one key alone never counts
  CHECK(!c.feed(true, true, 1000));
  CHECK(!c.feed(true, true, 2499));
  CHECK(c.feed(true, true, 2500));                                    // 1500 ms after both were down
  CHECK(!c.feed(true, true, 2600) && !c.feed(true, true, 9000));      // once per hold
  CHECK(!c.feed(true, false, 9100));                                  // releasing one re-arms
  CHECK(!c.feed(true, true, 9200) && !c.feed(true, true, 10699) && c.feed(true, true, 10700));
  CHECK(c.heldMs(10800) == 1600);
  c.reset();
  CHECK(c.heldMs(10800) == 0);
  ComboHold w(1500);                                                  // the 49.7-day wrap
  CHECK(!w.feed(true, true, 0xFFFFFF00u) && !w.feed(true, true, 0x2D0u) && w.feed(true, true, 0x5F0u));

  KeyDebounce d(15);
  CHECK(!d.feed(true, 0) && !d.down());
  CHECK(!d.feed(false, 5) && !d.down());                              // a 5 ms glitch is ignored
  CHECK(!d.feed(true, 10) && !d.feed(true, 24));
  CHECK(d.feed(true, 25) && d.down());                                // stable for 15 ms
  CHECK(!d.feed(true, 100));
  CHECK(!d.feed(false, 200) && d.feed(false, 215) && !d.down());
}

// An app that records what the host does to it.
struct SpyApp : IApp {
  const char* n;
  int begun = 0, ended = 0, ticks = 0, frames = 0, wants = 0, inputs = 0, keysDown = 0, keysUp = 0;
  bool want = false, owns = false, hold = false, combo = true;
  ButtonEvent lastEv = BTN_NONE;
  Key lastKey = KEY_COUNT;
  uint32_t lastHeld = 0, lastNow = 0;
  explicit SpyApp(const char* name) : n(name) {}
  const char* name() const override { return n; }
  void begin(uint32_t) override { begun++; }
  void end() override { ended++; }
  void tick(uint32_t now) override { ticks++; lastNow = now; }
  void onKey(Key k, bool down, uint32_t) override { lastKey = k; (down ? keysDown : keysUp)++; }
  void onInput(Key, ButtonEvent ev) override { inputs++; lastEv = ev; }
  void onHeld(uint32_t h) override { lastHeld = h; }
  bool wantsFrame(uint32_t) override { wants++; return want; }
  uint32_t nextWakeMs(uint32_t now) override { return now + 77; }
  void draw(Bitmap1& bm, Adafruit_GFX&, uint32_t) override { frames++; want = false; bm.fillRect(0, 0, 10, 10, DRAW_CLEAR); }
  bool ownsPanel(uint32_t) override { return owns; }
  bool handlesHold() const override { return hold; }
  bool comboExit() const override { return combo; }
};
struct SpyProbe : IFrameProbe {
  int clears = 0, presents = 0;
  void clear(bool b) override { if (b) clears++; }
  void present(bool b) override { if (b) presents++; }
};

static int blackPixels(MockPanel& p) {
  Bitmap1 b = p.bitmap();
  int n = 0;
  for (int y = 0; y < b.height(); y++)
    for (int x = 0; x < b.width(); x++) n += b.getPixel(x, y) == 0;
  return n;
}

static void host_frame_loop() {
  puts("host: frame loop, input delivery, app switching");
  Rig r;
  AppHost h(r.panel, r.input);
  SpyApp a("a"), b("b"), l("launcher");
  SpyProbe probe;
  h.setProbe(&probe);
  h.add(a); h.add(b); h.add(l);
  CHECK(h.start(0, 0));
  CHECK(!h.hasLauncher());
  uint32_t now = 0;
  for (; now < 100; now += 10) h.tick(now);
  CHECK(a.ticks == 10 && a.frames == 0 && r.panel.presents == 0);        // nothing wanted: no clear, no present
  CHECK(h.nextWakeMs(now) == now + 77);
  a.want = true;
  h.tick(now);
  CHECK(a.frames == 1 && r.panel.presents == 1 && probe.clears == 1 && probe.presents == 1);
  CHECK(blackPixels(r.panel) == 100);                                    // the panel was cleared before draw
  now += 10;
  a.want = true; a.owns = true;                                          // the laptop draws: hands off the panel
  int wants = a.wants;
  h.tick(now);
  CHECK(a.frames == 1 && r.panel.presents == 1 && a.wants == wants);
  a.owns = false; a.want = false;

  // Key edges and the held time reach the app before its tick.
  now += 10;
  r.input.setKey(KEY_UP, true, now);
  h.tick(now);
  CHECK(a.keysDown == 1 && a.lastKey == KEY_UP);
  r.input.setKey(KEY_UP, false, now + 10);
  h.tick(now + 10);
  CHECK(a.keysUp == 1);
  now += 20;
  r.input.press(now, 120);                                               // a short press of the switch
  for (uint32_t e = now + 400; now < e; now += 10) h.tick(now);
  CHECK(a.inputs == 1 && a.lastEv == BTN_SHORT);
  CHECK(a.keysDown == 2 && a.keysUp == 2 && a.lastKey == KEY_MAIN);      // the switch gives edges too

  // Without a launcher the 3 s hold goes to the app (the vario powers off) and A+B does nothing.
  r.input.press(now, 3300);
  for (uint32_t e = now + 3600; now < e; now += 10) h.tick(now);
  CHECK(a.lastEv == BTN_HOLD && h.active() == &a);
  r.input.setKey(KEY_ACTION, true, now); r.input.setKey(KEY_B, true, now);
  for (uint32_t e = now + 2000; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &a);
  r.input.setKey(KEY_ACTION, false, now); r.input.setKey(KEY_B, false, now);
  h.tick(now); now += 10;

  // With a launcher: the hold leaves apps that do not handle it, the combo leaves apps that allow it.
  h.setLauncher(2);
  CHECK(h.hasLauncher());
  a.hold = true;
  r.input.press(now, 3300);
  for (uint32_t e = now + 3600; now < e; now += 10) h.tick(now);
  CHECK(a.lastEv == BTN_HOLD && h.active() == &a);                       // handled by the app itself
  a.hold = false;
  h.tick(now); now += 10;
  r.input.press(now, 3300);
  for (uint32_t e = now + 3600; now < e && h.active() == &a; now += 10) h.tick(now);
  CHECK(h.active() == &l && a.ended == 1 && l.begun == 1);
  CHECK(!h.start(5, now) && !h.start(-1, now));
  for (uint32_t e = now + 600; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &l);                                               // the launcher stays (hold would not leave it)
  h.openApp(1);                                                          // a request is applied after the tick
  CHECK(h.active() == &l);
  h.tick(now); now += 10;
  CHECK(h.active() == &b && l.ended == 1 && b.begun == 1);
  CHECK(r.panel.presents >= 1);

  b.combo = false;
  r.input.setKey(KEY_ACTION, true, now); r.input.setKey(KEY_B, true, now);
  for (uint32_t e = now + 2000; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &b);                                               // this app refuses the combo
  b.combo = true;
  for (uint32_t e = now + 100; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &b);                                               // still held, but already counted: re-arm first
  r.input.setKey(KEY_ACTION, false, now); h.tick(now); now += 10;
  r.input.setKey(KEY_ACTION, true, now);
  for (uint32_t e = now + 1400; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &b);                                               // 1.4 s: not yet
  for (uint32_t e = now + 300; now < e; now += 10) h.tick(now);
  CHECK(h.active() == &l);                                               // 1.5 s of A + B
  r.input.setKey(KEY_ACTION, false, now); r.input.setKey(KEY_B, false, now);
  h.tick(now); now += 10;

  // openLauncher from inside the launcher does nothing; an unknown index is ignored.
  h.openLauncher(); h.openApp(9); h.tick(now); now += 10;
  CHECK(h.active() == &l);
  // Switching starts the new app with a full refresh: the panel is invalidated and set to the host's rotation.
  h.setRotation(3);
  CHECK(h.start(0, now));
  CHECK(r.panel.bitmap().rotation == 3);
}

static std::string xline(int sec, float alt, float vario = 1.0f) {
  char body[200];
  snprintf(body, sizeof body, "XCTRC,2026,8,16,10,%d,%d,0,43.252650,26.927967,%.2f,30.00,250.0,%.2f,,,,900.00,85", sec / 60, sec % 60, alt, vario);
  return line(body);
}

struct BoxRun {
  Box& b;
  uint32_t now;
  explicit BoxRun(Box& box, uint32_t t0 = 0) : b(box), now(t0) {}
  void run(uint32_t ms) { for (uint32_t e = now + ms; now < e; now += 10) b.tick(now); }
  void press(uint32_t dur) { b.r.input.press(now + 10, dur); run(dur + 300); }
  void shortp() { press(120); }
  void longp() { press(900); }
  void off() { press(3300); }
};

static void box_flow() {
  puts("box: vario -> launcher -> Tetris -> launcher -> vario");
  Box b;
  BoxRun u(b);
  b.boot(0);
  CHECK(b.host.active() == &b.vario && !b.r.tracer.paused);
  u.run(500);
  CHECK(b.r.compass.enabled);
  for (int i = 0; i < 5; i++) { b.r.tracer.lines.push_back(xline(i, 1000 + i)); u.run(1000); }
  CHECK(b.varioCore.linesOk() == 5);
  const int vf = b.r.panel.presents;
  CHECK(vf > 10);

  // Leave through the device list: long press, short, short (to "Apps..."), long.
  u.longp();
  CHECK(b.varioCore.listOpen() && b.r.tracer.opened);
  u.shortp(); u.shortp();
  u.longp();
  CHECK(b.host.active() == &b.launcher);
  CHECK(b.r.tracer.paused && b.r.tracer.pauses == 1 && !b.r.tracer.opened);   // link and scan stopped
  CHECK(!b.r.compass.enabled);                                               // sensors off
  u.run(300);
  CHECK(blackPixels(b.r.panel) > 2500);                                       // the launcher drew itself
  const int lf = b.r.panel.presents;
  u.run(1000);
  CHECK(b.r.panel.presents == lf);                                            // an idle launcher draws nothing
  // Lines that come while away are not collected.
  b.r.tracer.lines.push_back(xline(10, 1010));
  u.run(200);
  CHECK(b.varioCore.linesOk() == 5);

  // Start Tetris: down one row, long press.
  CHECK(b.launcher.selected() == 0);
  u.shortp();
  CHECK(b.launcher.selected() == 1);
  u.longp();
  CHECK(b.host.active() == &b.tetris);
  CHECK(b.tetris.game().state() == tetris::ST_TITLE);
  u.run(300);
  CHECK(blackPixels(b.r.panel) > 1500);
  // Play a moment: the switch starts the game (acted on at press).
  u.shortp();
  CHECK(b.tetris.game().state() == tetris::ST_PLAYING);
  u.run(1500);
  CHECK(b.varioCore.linesOk() == 5 && !b.r.compass.enabled && b.r.tracer.paused);   // the vario sleeps

  // Back: hold the switch 3 s.
  u.off();
  CHECK(b.host.active() == &b.launcher);
  CHECK(!b.r.power.off);
  CHECK(b.launcher.selected() == 1);
  // Select the vario again: next, next, next (wraps through the Start-up row), long.
  u.shortp(); u.shortp(); u.shortp();
  CHECK(b.launcher.selected() == 0);
  u.longp();
  CHECK(b.host.active() == &b.vario);
  CHECK(b.r.tracer.resumes == 1 && !b.r.tracer.paused);
  u.run(300);
  CHECK(b.r.compass.enabled);
  b.r.tracer.lines.push_back(xline(20, 1020));
  u.run(500);
  CHECK(b.varioCore.linesOk() == 6);                                          // data flows again
  CHECK(blackPixels(b.r.panel) > 2500);
}

// The vario must behave the same inside the box as alone: same frames at the same times, same data.
static void box_vario_matches_alone() {
  puts("box: the vario draws exactly what it draws alone");
  Rig ra;
  Driven alone(ra.panel, ra.input, ra.battery, ra.tracer, ra.laptop, ra.settings, ra.compass, ra.thermo, ra.power);
  Box b;
  alone.begin(0);
  b.boot(0);
  uint64_t da = 1469598103934665603ull, db = da;
  auto fold = [](uint64_t& d, MockPanel& p, uint32_t now) {
    const uint8_t* q = p.raw();
    d ^= now; d *= 1099511628211ull;
    for (int i = 0; i < 50 * 240; i++) { d ^= q[i]; d *= 1099511628211ull; }
  };
  int pa = 0, pb = 0;
  for (uint32_t now = 0; now < 40000; now += 10) {
    if (now % 1000 == 0) {
      std::string l = xline((int)(now / 1000), 1000 + (float)(now / 100), (now / 1000) % 20 < 10 ? 2.5f : -1.0f);
      ra.tracer.lines.push_back(l);
      b.r.tracer.lines.push_back(l);
    }
    if (now == 5000) { ra.input.press(now, 150); b.r.input.press(now, 150); }       // page
    if (now == 9000) { ra.input.press(now, 150); b.r.input.press(now, 150); }
    if (now == 15000) { ra.input.press(now, 150); b.r.input.press(now, 150); }
    // (the device list differs by design: the box adds its "Apps..." row; the proof compares it with the old code)
    alone.tick(now);
    b.tick(now);
    if (ra.panel.presents != pa) { pa = ra.panel.presents; fold(da, ra.panel, now); }
    if (b.r.panel.presents != pb) { pb = b.r.panel.presents; fold(db, b.r.panel, now); }
  }
  CHECK(pa == pb && pa > 100);
  CHECK(da == db);
  CHECK(alone.linesOk() == b.varioCore.linesOk());
  // And a game that was never chosen has not run.
  CHECK(b.tetris.game().state() == tetris::ST_TITLE && b.tetris.game().score() == 0);
  CHECK(b.host.active() == &b.vario);
}

static void box_gestures() {
  puts("box: vario power-off hold, A+B combo, start-up app, compass tool");
  {
    Box b;                                          // the vario keeps its 3 s power-off
    BoxRun u(b);
    b.boot(0);
    u.run(300);
    u.off();
    CHECK(b.r.power.off && b.host.active() == &b.vario);
  }
  {
    Box b;                                          // A + B for 1.5 s from a game opens the launcher
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_TETRIS, 0);
    u.run(200);
    b.r.input.setKey(KEY_ACTION, true, u.now);
    u.run(500);
    b.r.input.setKey(KEY_B, true, u.now);
    u.run(1400);
    CHECK(b.host.active() == &b.tetris);
    u.run(200);
    CHECK(b.host.active() == &b.launcher);
    b.r.input.setKey(KEY_ACTION, false, u.now);
    b.r.input.setKey(KEY_B, false, u.now);
    u.run(100);
    // D-pad in the launcher: DOWN moves, A starts, UP moves back.
    b.r.input.setKey(KEY_DOWN, true, u.now); u.run(50); b.r.input.setKey(KEY_DOWN, false, u.now); u.run(50);
    b.r.input.setKey(KEY_DOWN, true, u.now); u.run(50); b.r.input.setKey(KEY_DOWN, false, u.now); u.run(50);
    CHECK(b.launcher.selected() == 2);
    b.r.input.setKey(KEY_UP, true, u.now); u.run(50); b.r.input.setKey(KEY_UP, false, u.now); u.run(50);
    CHECK(b.launcher.selected() == 1);
    b.r.input.setKey(KEY_ACTION, true, u.now); u.run(50); b.r.input.setKey(KEY_ACTION, false, u.now); u.run(50);
    CHECK(b.host.active() == &b.tetris);
  }
  {
    Box b;                                          // the start-up app is stored and chosen in the launcher
    BoxRun u(b);
    CHECK(b.launcher.startupApp() == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = 99;                   // garbage: the vario
    CHECK(b.launcher.startupApp() == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = Box::APP_LAUNCHER;    // the launcher itself is not a start-up app
    CHECK(b.launcher.startupApp() == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = 0;
    b.boot(0);
    b.host.start(Box::APP_LAUNCHER, 0);
    u.run(100);
    u.shortp(); u.shortp(); u.shortp();             // to the "Start-up" row
    CHECK(b.launcher.selected() == 3);
    u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_TETRIS && b.keys.writes == 1);
    CHECK(b.host.active() == &b.launcher);
    u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_COMPASS);
    u.longp(); u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_TETRIS);
    // A new power-on starts that app, and the vario's radios stay quiet until it is opened.
    Box c;
    c.keys.v[SLOT_BOOT_APP] = Box::APP_TETRIS;
    c.boot(0);
    CHECK(c.host.active() == &c.tetris && c.r.tracer.paused);
    BoxRun w(c);
    w.run(1000);
    CHECK(!c.r.compass.enabled);
    c.host.start(Box::APP_VARIO, w.now);
    w.run(100);
    CHECK(!c.r.tracer.paused && c.r.compass.enabled);
  }
  {
    Box b;                                          // the compass tool: no page switching, menu, leave by hold
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_COMPASS, 0);
    u.run(1000);
    CHECK(b.r.compass.enabled);
    u.shortp();
    CHECK(b.toolCore.buildViewModel(u.now).page == ViewModel::PAGE_COMPASS);   // a short press does not change pages
    u.longp();
    CHECK(b.toolCore.listOpen());
    u.shortp(); u.shortp();                          // back, calibrate, Apps...
    u.longp();
    CHECK(b.host.active() == &b.launcher && !b.r.compass.enabled);
    b.host.start(Box::APP_COMPASS, u.now);
    u.run(300);
    u.off();                                         // 3 s hold leaves the tool (it does not power off)
    CHECK(b.host.active() == &b.launcher && !b.r.power.off);
  }
  {
    Box b;                                          // laptop host drawing still takes the panel from the vario
    BoxRun u(b);
    b.boot(0);
    u.run(1000);
    b.r.laptop.drawing = true;
    u.run(100);
    const int p = b.r.panel.presents;
    u.run(2000);
    CHECK(b.r.panel.presents == p);
    b.r.laptop.drawing = false;
    u.run(1000);
    CHECK(b.r.panel.presents > p);
  }
}

static void test_box() {
  combo_and_debounce();
  host_frame_loop();
  box_flow();
  box_vario_matches_alone();
  box_gestures();
}

}  // namespace bx
