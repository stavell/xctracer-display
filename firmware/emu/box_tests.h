#pragma once
// Tests for the platform frame loop (AppHost), the input helpers (ComboHold, KeyDebounce), the launcher and the
// whole box: switching apps, what stops and restarts, and that the vario behaves as it does alone.
// Included by tests.cpp, after CHECK and line() are defined.
#include "box.h"
#include "platform/lazy_app.h"
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
  void off() { press(3300); }                                  // the 3 s hold: opens the system sheet
  void row(SystemSheet::Row r) { for (int i = 0; i < 5 && b.host.sheet().cursorRow() != r; i++) shortp(); }
  void sheetAct(SystemSheet::Row r) { row(r); longp(); }       // press to the row, long press does it
  void home() { off(); sheetAct(SystemSheet::R_HOME); }       // the way to the home screen
  void sleep() { off(); sheetAct(SystemSheet::R_SLEEP); }     // the way into the clock sleep
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
  CHECK(b.varioCore().linesOk() == 5);
  const int vf = b.r.panel.presents;
  CHECK(vf > 10);

  // The device list has "< back" and the tracers only; the way home is the system sheet.
  u.longp();
  CHECK(b.varioCore().listOpen() && b.r.tracer.opened);
  u.longp();                                                                  // "< back"
  CHECK(!b.varioCore().listOpen() && b.host.active() == &b.vario);
  u.home();
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
  CHECK(!b.vario.alive());                                                    // the vario is gone: its RAM is the arena's
  CHECK(b.r.tracer.lines.size() <= 1);

  // Start Tetris: three rows down (Vario, Compass, Sim, Tetris), long press.
  CHECK(b.launcher.selected() == 0);
  u.shortp(); u.shortp(); u.shortp();
  CHECK(b.launcher.selected() == 3);
  u.longp();
  CHECK(b.host.active() == &b.tetris);
  CHECK(b.tetris->game().state() == tetris::ST_TITLE);
  u.run(300);
  CHECK(blackPixels(b.r.panel) > 1500);
  // Play a moment: the switch starts the game (acted on at press).
  u.shortp();
  CHECK(b.tetris->game().state() == tetris::ST_PLAYING);
  u.run(1500);
  CHECK(!b.vario.alive() && !b.r.compass.enabled && b.r.tracer.paused);   // the vario does not exist

  // Back: hold the switch 3 s, the sheet's Home.
  u.home();
  CHECK(b.host.active() == &b.launcher);
  CHECK(!b.r.power.off);
  CHECK(b.launcher.selected() == 3);
  // Select the vario again: next x2 (Settings, wraps to the Vario), long.
  u.shortp(); u.shortp();
  CHECK(b.launcher.selected() == 0);
  u.longp();
  CHECK(b.host.active() == &b.vario);
  CHECK(b.r.tracer.resumes == 1 && !b.r.tracer.paused);
  u.run(300);
  CHECK(b.r.compass.enabled);
  b.r.tracer.lines.push_back(xline(20, 1020));
  u.run(500);
  CHECK(b.varioCore().linesOk() == 1);                                        // data flows again, into a fresh vario
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
  CHECK(alone.linesOk() == b.varioCore().linesOk());
  // And a game that was never chosen has not run.
  CHECK(!b.tetris.alive());
  CHECK(b.host.active() == &b.vario);
}

// The vario is an arena app like the others: gone when left, built afresh (flight state lost) when reopened.
static void box_vario_lazy() {
  puts("box: leaving the vario destroys it and frees the arena, reopening starts fresh and reconnects the tracer");
  Box b;
  BoxRun u(b);
  b.boot(0);
  u.run(300);
  CHECK(b.vario.alive() && b.arena.busy());
  for (int i = 0; i < 4; i++) { b.r.tracer.lines.push_back(xline(i, 1000 + i)); u.run(1000); }
  CHECK(b.varioCore().linesOk() == 4);
  b.host.start(Box::APP_LAUNCHER, u.now);
  u.run(100);
  CHECK(!b.vario.alive() && !b.arena.busy());                                 // arena released
  CHECK(b.r.tracer.paused && !b.r.compass.enabled);
  b.host.start(Box::APP_TETRIS, u.now);                                       // another app takes the block
  u.run(100);
  CHECK(b.tetris.alive() && !b.vario.alive());
  b.host.start(Box::APP_VARIO, u.now);
  u.run(300);
  CHECK(!b.tetris.alive() && b.vario.alive());
  CHECK(b.r.tracer.resumes >= 1 && !b.r.tracer.paused && b.r.compass.enabled);   // tracer link resumed by the new vario
  CHECK(b.varioCore().linesOk() == 0);   // flight state starts fresh
  b.r.tracer.lines.push_back(xline(9, 1009));
  u.run(600);
  CHECK(b.varioCore().linesOk() == 1);
  CHECK(blackPixels(b.r.panel) > 2500);
}

static void box_gestures() {
  puts("box: hold and A+B open the sheet, start-up app, compass tool");
  {
    Box b;                                          // the vario: the 3 s hold opens the sheet, it does not power off
    BoxRun u(b);
    b.boot(0);
    u.run(300);
    u.off();
    CHECK(!b.r.power.off && b.host.active() == &b.vario && b.host.sheetOpen());
  }
  {
    Box b;                                          // A + B for 1.5 s from a game opens the sheet (Home first), A on it goes home
    BoxRun u(b);
    b.boot(0);
    b.host.start(Box::APP_TETRIS, 0);
    u.run(200);
    b.r.input.setKey(KEY_ACTION, true, u.now);
    u.run(500);
    b.r.input.setKey(KEY_B, true, u.now);
    u.run(1400);
    CHECK(b.host.active() == &b.tetris && !b.host.sheetOpen());
    u.run(200);
    CHECK(b.host.active() == &b.tetris && b.host.sheetOpen());
    b.r.input.setKey(KEY_ACTION, false, u.now);
    b.r.input.setKey(KEY_B, false, u.now);
    u.run(100);
    auto tap = [&](Key k) { b.r.input.setKey(k, true, u.now); u.run(50); b.r.input.setKey(k, false, u.now); u.run(50); };
    CHECK(b.host.sheet().cursorRow() == SystemSheet::R_HOME);
    tap(KEY_ACTION);
    CHECK(b.host.active() == &b.launcher);
    // D-pad on the home grid (2 columns): DOWN / UP change the row, LEFT / RIGHT the column, A opens.
    CHECK(b.launcher.selected() == 0);
    tap(KEY_DOWN);  CHECK(b.launcher.selected() == 2);
    tap(KEY_DOWN);  CHECK(b.launcher.selected() == 4);
    tap(KEY_DOWN);  CHECK(b.launcher.selected() == 4);          // no fourth row with five apps
    tap(KEY_UP);    CHECK(b.launcher.selected() == 2);
    tap(KEY_RIGHT); CHECK(b.launcher.selected() == 3);
    tap(KEY_LEFT);  CHECK(b.launcher.selected() == 2);
    tap(KEY_UP);    CHECK(b.launcher.selected() == 0);
    tap(KEY_RIGHT); CHECK(b.launcher.selected() == 1);
    tap(KEY_ACTION);
    CHECK(b.host.active() == &b.compassTool);
  }
  {
    Box b;                                          // the start-up app is stored and chosen in Settings
    BoxRun u(b);
    CHECK(b.registry.startupApp(b.keys) == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = 99;                   // garbage: the vario
    CHECK(b.registry.startupApp(b.keys) == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = Box::APP_LAUNCHER;    // the home screen itself is not a start-up app
    CHECK(b.registry.startupApp(b.keys) == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = Box::APP_SETTINGS;    // nor is Settings: a box never boots into a tool
    CHECK(b.registry.startupApp(b.keys) == Box::APP_VARIO);
    b.keys.v[SLOT_BOOT_APP] = 0;
    b.boot(0);
    b.host.start(Box::APP_SETTINGS, 0);
    u.run(100);
    u.shortp(); u.shortp();                         // to the "Start-up app" row
    CHECK(b.settings.selected() == SettingsApp::ROW_STARTUP);
    u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_COMPASS && b.keys.writes == 1);
    CHECK(b.host.active() == &b.settings);
    u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_SIM);
    u.longp();
    CHECK(b.keys.v[SLOT_BOOT_APP] == Box::APP_TETRIS);
    u.longp(); u.longp();                           // Settings is skipped: vario, then Compass
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
    CHECK(b.compassTool->core().buildViewModel(u.now).page == ViewModel::PAGE_COMPASS);   // a short press does not change pages
    u.longp();
    CHECK(b.compassTool->core().listOpen());
    u.longp();                                       // "< back" (no calibration here: that is Settings')
    CHECK(!b.compassTool->core().listOpen() && b.host.active() == &b.compassTool);
    u.home();                                        // the sheet leaves the tool (it does not power off)
    CHECK(b.host.active() == &b.launcher && !b.r.power.off && !b.r.compass.enabled);
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

static void settings_store() {
  puts("box settings: storage, mounting move, migration, batching");
  MockKeyStore k;
  BoxSettings s(k);
  MagCal m;
  CHECK(s.tzMinutes() == 0 && s.mountIndex() == 0 && !s.loadMagCal(m));
  s.setTzMinutes(-210);
  CHECK(s.tzMinutes() == -210);
  s.setTzMinutes(5000);
  CHECK(s.tzMinutes() == 1440);                                      // kept within a day
  MagCal c;
  c.valid = true; c.off[0] = 10; c.off[1] = -20; c.off[2] = 30; c.radius = 45;
  const int w0 = k.writes;
  s.saveMagCal(c);
  CHECK(k.writes == w0 + 1);                                         // five slots, one flash write
  CHECK(s.loadMagCal(m) && m.valid && m.off[0] == 10 && m.off[1] == -20 && m.off[2] == 30 && m.radius == 45);
  const int w1 = k.writes;
  CHECK(s.setMountIndex(7) && s.mountIndex() == 7);
  CHECK(k.writes == w1 + 1);                                         // offsets and index together
  Vec3 o; o.x = 10; o.y = -20; o.z = 30;
  o = mountRemap(0, 7, o);                                           // the offsets live in the device frame: they move along
  CHECK(s.loadMagCal(m));
  NEAR(m.off[0], o.x, 1e-4); NEAR(m.off[1], o.y, 1e-4); NEAR(m.off[2], o.z, 1e-4);
  CHECK(!s.setMountIndex(24) && !s.setMountIndex(-1) && s.mountIndex() == 7);
  CHECK(s.setMountIndex(7) && k.writes == w1 + 1);                   // no change, no write
  k.v[SLOT_MAG_X] = 0x7fc00000u;                                     // a damaged slot (NaN): as if there were no calibration
  CHECK(!s.loadMagCal(m));
  k.v[SLOT_MOUNT] = 99;
  CHECK(s.mountIndex() == 0);                                        // garbage mounting: the identity

  // One-time import of what older firmware kept in the vario's files.
  MockKeyStore k2;
  BoxSettings s2(k2);
  BoxSettings::Legacy l;
  l.haveCal = true; l.cal = c; l.haveMount = true; l.mount = 13; l.haveTz = true; l.tz = 180;
  CHECK(!s2.migrated());
  s2.importLegacy(l);
  CHECK(s2.migrated() && k2.writes == 1);
  CHECK(s2.tzMinutes() == 180 && s2.mountIndex() == 13 && s2.loadMagCal(m) && m.off[1] == -20 && m.radius == 45);
  s2.setTzMinutes(-60);
  BoxSettings::Legacy stale;
  stale.haveTz = true; stale.tz = 999; stale.haveMount = true; stale.mount = 2;
  s2.importLegacy(stale);                                            // never again: a changed value is not overwritten
  CHECK(s2.tzMinutes() == -60 && s2.mountIndex() == 13);
  MockKeyStore k3;
  BoxSettings s3(k3);
  BoxSettings::Legacy bad;
  bad.haveMount = true; bad.mount = 77;
  s3.importLegacy(bad);
  CHECK(s3.migrated() && s3.mountIndex() == 0 && !s3.loadMagCal(m));

  // The wall clock: the tracer's GPS time of day plus the time since, never invented.
  WallClock wc;
  CHECK(wc.localMinutes(0, 1000) == -1);
  wc.set(10 * 3600 + 38 * 60 + 30, 5000);
  CHECK(wc.localMinutes(0, 5000) == 10 * 60 + 38);
  CHECK(wc.localMinutes(0, 5000 + 31000) == 10 * 60 + 39);           // 31 s later, past the minute
  CHECK(wc.localMinutes(180, 5000) == 13 * 60 + 38);
  CHECK(wc.localMinutes(-11 * 60, 5000) == 23 * 60 + 38);            // wraps over midnight
  CHECK(wc.localMinutes(0, 5000 + 13u * 3600u * 1000u) == 23 * 60 + 38);   // still valid after 13 h (was stale)
  CHECK(wc.localMinutes(0, 5000 + 31u * 86400u * 1000u) == -1);      // stale after 30 days
  wc.set(86400, 0);                                                  // out of range: ignored
  CHECK(wc.localMinutes(0, 5000) == 10 * 60 + 38);
}

static void settings_app() {
  puts("box: Settings app (calibration, mounting, start-up, time zone, About)");
  Box b;
  BoxRun u(b);
  b.boot(0);
  b.host.start(Box::APP_SETTINGS, 0);
  u.run(300);
  CHECK(!b.r.compass.enabled);                                       // the sensors run only on the calibration screen
  CHECK(blackPixels(b.r.panel) > 2500);
  // Compass calibration: long press opens it, turn the unit, long press keeps it.
  b.r.compass.hardIron[0] = 12; b.r.compass.hardIron[1] = -18; b.r.compass.hardIron[2] = 7;
  u.longp();
  CHECK(b.settings.screen() == SettingsApp::S_CAL && b.settings.calibrating());
  u.run(100);
  CHECK(b.r.compass.enabled);
  for (int psi = 0; psi < 360; psi += 20)
    for (int pitch = -90; pitch <= 90; pitch += 30)
      for (int roll = -90; roll <= 90; roll += 45) {
        b.r.compass.psi = psi; b.r.compass.pitch = pitch; b.r.compass.roll = roll;
        u.run(20);
      }
  const int w0 = b.keys.writes;
  u.longp();
  MagCal m;
  CHECK(!b.settings.calibrating() && b.settings.screen() == SettingsApp::S_LIST && !b.r.compass.enabled);
  CHECK(b.r.settings.loadMagCal(m) && b.keys.writes == w0 + 1);
  NEAR(m.off[0], 12, 3); NEAR(m.off[1], -18, 3); NEAR(m.off[2], 7, 3);
  // A cancelled calibration changes nothing; so does leaving mid-way (the 3 s hold goes home, sensors off).
  const float keep = m.off[0];
  u.longp();
  u.run(300);
  u.shortp();
  CHECK(b.settings.screen() == SettingsApp::S_LIST && !b.r.compass.enabled);
  CHECK(b.r.settings.loadMagCal(m) && m.off[0] == keep && b.keys.writes == w0 + 1);
  u.longp();
  CHECK(b.r.compass.enabled);
  u.home();
  CHECK(b.host.active() == &b.launcher && !b.r.compass.enabled && !b.settings.calibrating());
  CHECK(b.r.settings.loadMagCal(m) && m.off[0] == keep);

  // Board mounting: +1 per long press, wraps at 24; the stored offsets move along.
  b.host.start(Box::APP_SETTINGS, u.now);
  u.run(100);
  u.shortp();
  CHECK(b.settings.selected() == SettingsApp::ROW_MOUNT);
  u.longp();
  CHECK(b.r.settings.mountIndex() == 1);
  Vec3 o; o.x = keep; o.y = 0; (void)o;
  MagCal m1;
  CHECK(b.r.settings.loadMagCal(m1));
  for (int i = 0; i < 23; i++) u.longp();
  CHECK(b.r.settings.mountIndex() == 0);                             // 24 steps: back to the start
  MagCal m2;
  CHECK(b.r.settings.loadMagCal(m2));
  NEAR(m2.off[0], keep, 1e-3);                                       // a full turn through the table returns the offsets
  b.r.settings.setMountIndex(5);

  // Time zone: +1 h per long press and wraps from +14:00 to -12:00; the arrows move by 30 minutes.
  u.shortp(); u.shortp(); u.shortp();                                // Start-up app, Intro, Time zone
  CHECK(b.settings.selected() == SettingsApp::ROW_TZ);
  u.longp();
  CHECK(b.r.settings.tzMinutes() == 60);
  b.r.settings.setTzMinutes(14 * 60);
  u.longp();
  CHECK(b.r.settings.tzMinutes() == -12 * 60);
  b.r.input.setKey(KEY_RIGHT, true, u.now); u.run(50); b.r.input.setKey(KEY_RIGHT, false, u.now); u.run(50);
  CHECK(b.r.settings.tzMinutes() == -12 * 60 + 30);
  b.r.input.setKey(KEY_LEFT, true, u.now); u.run(50); b.r.input.setKey(KEY_LEFT, false, u.now); u.run(50);
  CHECK(b.r.settings.tzMinutes() == -12 * 60);

  // About: long press opens, any press goes back. Then Home.
  u.shortp();
  CHECK(b.settings.selected() == SettingsApp::ROW_ABOUT);
  u.longp();
  CHECK(b.settings.screen() == SettingsApp::S_ABOUT);
  u.run(100);
  CHECK(blackPixels(b.r.panel) > 800);
  u.shortp();
  CHECK(b.settings.screen() == SettingsApp::S_LIST);

  // Six buttons: arrows move, A opens, B leaves a sub-screen.
  b.host.start(Box::APP_SETTINGS, u.now);
  u.run(100);
  auto tap = [&](Key k) { b.r.input.setKey(k, true, u.now); u.run(50); b.r.input.setKey(k, false, u.now); u.run(50); };
  tap(KEY_DOWN); tap(KEY_DOWN); tap(KEY_DOWN); tap(KEY_DOWN); tap(KEY_DOWN);
  CHECK(b.settings.selected() == SettingsApp::ROW_ABOUT);
  tap(KEY_ACTION);
  CHECK(b.settings.screen() == SettingsApp::S_ABOUT);
  tap(KEY_B);
  CHECK(b.settings.screen() == SettingsApp::S_LIST && b.host.active() == &b.settings);
  tap(KEY_UP); tap(KEY_UP); tap(KEY_UP); tap(KEY_UP); tap(KEY_UP);
  tap(KEY_ACTION);                                                   // the Compass row
  CHECK(b.settings.calibrating());
  tap(KEY_B);                                                        // cancels
  CHECK(!b.settings.calibrating() && b.host.active() == &b.settings);
  tap(KEY_B);
  CHECK(b.host.active() == &b.settings);                             // B on the list does nothing: leaving is the sheet's job

  // The vario picks up what Settings stored: mounting and calibration when it starts.
  b.r.settings.setMountIndex(9);
  b.host.start(Box::APP_VARIO, u.now);
  u.run(300);
  CHECK(b.varioCore().mountIndex() == 9);
  CHECK(b.varioCore().compass().calibration().valid);
}

// ctl.py cal / orient / tz reach the same storage while the vario runs in the box.
static void box_laptop_commands() {
  puts("box: laptop commands (cal, orient, tz) use the box's settings");
  Box b;
  BoxRun u(b);
  b.boot(0);
  u.run(500);
  b.r.compass.hardIron[0] = 5; b.r.compass.hardIron[1] = 9; b.r.compass.hardIron[2] = -14;
  Command c; c.type = Command::CAL; c.value = 1;
  b.r.laptop.cmds.push_back(c);
  u.run(200);
  CHECK(b.varioCore().calibrating());
  for (int psi = 0; psi < 360; psi += 20)
    for (int pitch = -90; pitch <= 90; pitch += 30)
      for (int roll = -90; roll <= 90; roll += 45) {
        b.r.compass.psi = psi; b.r.compass.pitch = pitch; b.r.compass.roll = roll;
        u.run(20);
      }
  c.value = 2;
  b.r.laptop.cmds.push_back(c);
  u.run(200);
  MagCal m;
  CHECK(!b.varioCore().calibrating() && b.r.settings.loadMagCal(m));
  NEAR(m.off[2], -14, 3);
  Command o; o.type = Command::ORIENT; o.value = 11;
  b.r.laptop.cmds.push_back(o);
  Command tz; tz.type = Command::TZ; tz.value = -300;
  b.r.laptop.cmds.push_back(tz);
  u.run(200);
  CHECK(b.r.settings.mountIndex() == 11 && b.r.settings.tzMinutes() == -300 && b.varioCore().mountIndex() == 11);
  // The vario hands the tracer's time of day to the home screen.
  for (int i = 0; i < 3; i++) { b.r.tracer.lines.push_back(xline(i, 1000 + i)); u.run(1000); }
  CHECK(b.clock.localMinutes(0, u.now) == 10 * 60 + 0);
  CHECK(b.clock.localMinutes(-300, u.now) == 5 * 60);
}

// Tetris: the pause screen resumes on any press.
static void box_tetris_home() {
  puts("box: Tetris pause screen, any press / A / B resumes");
  Box b;
  BoxRun u(b);
  b.boot(0);
  b.host.start(Box::APP_TETRIS, 0);
  u.run(300);
  u.shortp();                                                        // start
  u.run(500);
  u.shortp();                                                        // pause
  CHECK(b.tetris->game().state() == tetris::ST_PAUSED);
  u.longp();
  CHECK(b.host.active() == &b.tetris && b.tetris->game().state() == tetris::ST_PLAYING);   // a long press resumes too
  u.shortp();
  CHECK(b.tetris->game().state() == tetris::ST_PAUSED);
  b.r.input.setKey(KEY_ACTION, true, u.now); u.run(50); b.r.input.setKey(KEY_ACTION, false, u.now); u.run(50);
  CHECK(b.tetris->game().state() == tetris::ST_PLAYING && b.host.active() == &b.tetris);   // A resumes
  u.shortp();
  CHECK(b.tetris->game().state() == tetris::ST_PAUSED);
  b.r.input.setKey(KEY_B, true, u.now); u.run(50); b.r.input.setKey(KEY_B, false, u.now); u.run(50);
  CHECK(b.host.active() == &b.tetris && b.tetris->game().state() == tetris::ST_PLAYING);   // B resumes
  u.longp();                                                         // playing: a long press still swaps the hold piece
  CHECK(b.host.active() == &b.tetris && b.tetris->game().state() == tetris::ST_PLAYING);
}

// The shared app arena: an app exists only while it is the active one, and the apps take turns in one block of memory.
struct LifeApp : IApp {
  static int alive, begun, ended;
  int id;
  explicit LifeApp(int i) : id(i) { alive++; }
  ~LifeApp() { alive--; }
  const char* name() const override { return "life"; }
  void begin(uint32_t) override { begun++; }
  void end() override { ended++; }
  void tick(uint32_t) override {}
  bool wantsFrame(uint32_t) override { return true; }
  bool handlesHold() const override { return id == 1; }
};
int LifeApp::alive = 0, LifeApp::begun = 0, LifeApp::ended = 0;
static LifeApp* makeLife(void* mem, void* ctx) { return new (mem) LifeApp((int)(intptr_t)ctx); }

static void lazy_app_slots() {
  puts("lazy apps: built on begin, destroyed on end, two apps take turns in one arena");
  AppArenaStore<sizeof(LifeApp)> arena;
  LazyApp<LifeApp> a(arena, "a", makeLife, (void*)1), b(arena, "b", makeLife, (void*)2);
  CHECK(strcmp(a.name(), "a") == 0 && !a.alive() && LifeApp::alive == 0);   // name works while not built
  CHECK(!a.wantsFrame(0) && !a.handlesHold() && a.comboExit() && a.frameIntervalMs() == 0);
  a.begin(0);
  CHECK(a.alive() && LifeApp::alive == 1 && LifeApp::begun == 1 && a->id == 1 && a.handlesHold() && a.wantsFrame(0));
  b.begin(0);                                                        // the arena is taken: b cannot start, nothing breaks
  CHECK(!b.alive() && !b.wantsFrame(0) && LifeApp::alive == 1);
  a.end();
  CHECK(!a.alive() && LifeApp::alive == 0 && LifeApp::ended == 1);
  a.end();                                                           // ending twice is harmless
  CHECK(LifeApp::ended == 1);
  b.begin(0);
  CHECK(b.alive() && b->id == 2 && !b.handlesHold() && LifeApp::alive == 1);
  b.end();
  a.begin(0);
  CHECK(a.alive() && a->id == 1 && LifeApp::alive == 1);
  {
    AppArenaStore<sizeof(LifeApp)> arena2;
    LazyApp<LifeApp> c(arena2, "c", makeLife, (void*)3);
    c.begin(0);
    CHECK(LifeApp::alive == 2);
  }                                                                  // destroying the slot destroys a live app
  CHECK(LifeApp::alive == 1);
  a.end();
  CHECK(LifeApp::alive == 0);
  AppArenaStore<4> tiny;                                             // too small: refuses instead of overrunning
  LazyApp<LifeApp> d(tiny, "d", makeLife, (void*)4);
  d.begin(0);
  CHECK(!d.alive() && LifeApp::alive == 0);
}

// In the box the arena holds the sim, Tetris and the compass tool in turn; the high score survives the app.
static void box_lazy_apps() {
  puts("box: sim, Tetris and the compass tool share one arena, the best score is kept");
  Box b(1);
  BoxRun u(b);
  b.keys.set(SLOT_TETRIS_BEST, 4321);
  b.boot(0);
  CHECK(!b.tetris.alive() && !b.compassTool.alive() && !b.flightSim.alive());
  b.host.start(Box::APP_TETRIS, 0);
  u.run(300);
  CHECK(b.tetris.alive() && b.tetris->game().highScore() == 4321 && !b.compassTool.alive());
  b.host.start(Box::APP_COMPASS, u.now);
  u.run(300);
  CHECK(!b.tetris.alive() && b.compassTool.alive() && b.compassTool->core().buildViewModel(u.now).page == ViewModel::PAGE_COMPASS);
  b.host.start(Box::APP_SIM, u.now);
  u.run(100);
  CHECK(!b.compassTool.alive() && b.flightSim.alive() && !b.r.compass.enabled);
  b.host.start(Box::APP_TETRIS, u.now);
  u.run(300);
  CHECK(b.tetris.alive() && !b.flightSim.alive() && b.tetris->game().highScore() == 4321);
  b.host.start(Box::APP_VARIO, u.now);
  CHECK(!b.tetris.alive());
}


// The start-up animation: plays at a power-on, any press skips it (and is used up), it never outlives its arena slot, and the
// start-up app comes next with its radios as before.
static uint32_t panelHash(Box& b) {
  uint32_t h = 2166136261u;
  const uint8_t* p = b.r.panel.raw();
  for (int i = 0; i < 400 * 240 / 8; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void box_intro() {
  puts("box: start-up animation (length, skip, arena, off, no pack, with pack, settings row)");
  CHECK(sizeof(intro::IntroApp) <= sizeof(sim::SimBox));              // the arena stays as big as the sim: no heap lost to the intro
  {
    std::unique_ptr<Box> pb(new Box());                               // no map pack: the 2D version
    Box& b = *pb;
    BoxRun u(b);
    CHECK(b.r.settings.introEnabled());                               // default on
    b.boot(0, true);
    CHECK(b.host.active() == &b.introApp && b.introApp.alive() && b.arena.busy());
    CHECK(b.introApp->mode() == intro::IntroApp::Mode::FLAT && b.introApp->site() < 0);
    CHECK(b.host.gameMode() && !b.r.tracer.paused);                    // 50 fps frame mode; the vario's scan runs in parallel
    u.run(1000);                                                      // length: 225 steps of 20 ms in real time
    CHECK(b.introApp->stepsDone() >= 48 && b.introApp->stepsDone() <= 51);
    u.run(1600);
    CHECK(b.host.active() == &b.introApp && b.introApp->stepsDone() >= 125);
    u.run(2200);
    CHECK(b.host.active() == &b.vario && b.vario.alive() && !b.introApp.alive() && !b.r.tracer.paused);
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // a press skips it and does not reach the vario
    Box& b = *pb;
    BoxRun u(b);
    b.boot(0, true);
    u.run(600);
    b.r.input.press(u.now + 10, 120);
    u.run(500);
    CHECK(b.host.active() == &b.vario && !b.introApp.alive());
    CHECK(b.varioCore().buildViewModel(u.now).page == ViewModel::PAGE_COMPASS);
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // the 3 s hold only skips: no home screen
    Box& b = *pb;
    BoxRun u(b);
    b.boot(0, true);
    u.run(300);
    b.r.input.press(u.now + 10, 3400);
    u.run(3800);
    CHECK(b.host.active() == &b.vario);
    std::unique_ptr<Box> pc(new Box());
    Box& c = *pc;
    BoxRun w(c);
    c.boot(0, true);
    w.run(300);
    c.r.input.setKey(KEY_ACTION, true, w.now);                        // the D-pad / A / B keys skip on their press
    w.run(100);
    CHECK(c.host.active() == &c.vario);
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // another start-up app: the tracer stays off
    Box& b = *pb;
    b.keys.set(SLOT_BOOT_APP, Box::APP_TETRIS);
    BoxRun u(b);
    b.boot(0, true);
    CHECK(b.r.tracer.paused);
    u.run(5000);
    CHECK(b.host.active() == &b.tetris && b.tetris.alive() && !b.introApp.alive());
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // Settings > Intro off: straight to the start-up app
    Box& b = *pb;
    b.r.settings.setIntroEnabled(false);
    b.boot(0, true);
    CHECK(b.host.active() == &b.vario && !b.introApp.alive());
    b.r.settings.setIntroEnabled(true);
    b.boot(0);                                                        // plain boot without the flag: no intro
    CHECK(b.host.active() == &b.vario);
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // intro after the clock sleep (unless Off)
    Box& b = *pb;
    BoxRun u(b);
    b.boot(0);
    u.run(300);
    u.sleep();
    CHECK(b.sleep == Box::SS_CLOCK);
    b.r.input.press(u.now + 10, 1200);
    u.run(2500);
    CHECK(b.sleep == Box::SS_AWAKE && b.host.active() == &b.introApp && b.introApp.alive());
    CHECK(!b.r.tracer.paused);                                        // scan in parallel
    u.run(5000);
    CHECK(b.host.active() == &b.vario && !b.introApp.alive());
    b.r.settings.setIntroEnabled(false);
    u.sleep();
    b.r.input.press(u.now + 10, 1200);
    u.run(2500);
    CHECK(b.sleep == Box::SS_AWAKE && b.host.active() == &b.vario && !b.introApp.alive());
  }
  {
    std::unique_ptr<Box> pb(new Box());                               // the Settings row
    Box& b = *pb;
    BoxRun u(b);
    b.host.start(Box::APP_SETTINGS, 0);
    u.run(300);
    for (int i = 0; i < SettingsApp::ROW_INTRO; i++) u.shortp();
    CHECK(b.settings.selected() == SettingsApp::ROW_INTRO);
    u.longp();
    CHECK(!b.r.settings.introEnabled());
    u.longp();
    CHECK(b.r.settings.introEnabled());
  }
  {
    // With the Sopot pack: the 3D scene, the same on every run, ending on the white name.
    std::vector<uint8_t> bytes;
    FILE* pf = fopen("fixtures/sopot.pack", "rb");
    CHECK(pf != nullptr);
    if (!pf) return;
    fseek(pf, 0, SEEK_END);
    bytes.resize((size_t)ftell(pf));
    fseek(pf, 0, SEEK_SET);
    CHECK(fread(bytes.data(), 1, bytes.size(), pf) == bytes.size());
    fclose(pf);
    static terrain::MemReader rd;
    static TerrainKit kit;
    rd = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
    CHECK(kit.open(&rd));
    uint32_t mid[2] = {0, 0}, last[2] = {0, 0};
    for (int run = 0; run < 2; run++) {
      std::unique_ptr<Box> pb(new Box());
      Box& b = *pb;
      b.setTerrain(&kit);
      BoxRun u(b);
      b.boot(0, true);
      CHECK(b.introApp.alive() && b.introApp->mode() == intro::IntroApp::Mode::TERRAIN && b.introApp->site() >= 0);
      CHECK(b.introApp->world().cache().complete());                 // the window was filled in begin()
      while (b.host.active() == &b.introApp && b.introApp->stepsDone() < 70) u.run(10);
      mid[run] = panelHash(b);
      while (b.host.active() == &b.introApp && b.introApp->stepsDone() < 224) u.run(10);
      last[run] = panelHash(b);
      u.run(300);
      CHECK(b.host.active() == &b.vario && !b.introApp.alive());
    }
    CHECK(mid[0] == mid[1] && last[0] == last[1] && mid[0] != last[0]);   // deterministic
    // the end frame is the name on white: mostly white pixels
    std::unique_ptr<Box> pb(new Box());
    Box& b = *pb;
    b.setTerrain(&kit);
    BoxRun u(b);
    b.boot(0, true);
    while (b.host.active() == &b.introApp && b.introApp->stepsDone() < 224) u.run(10);
    int white = 0;
    Bitmap1 bm = b.r.panel.bitmap();
    for (int y = 0; y < 400; y++)
      for (int x = 0; x < 240; x++) white += bm.getPixel(x, y);
    CHECK(white > 240 * 400 * 85 / 100 && white < 240 * 400 * 99 / 100);
  }
}

// The clock sleep: the pure state machine, the wall clock helper, the screen, and the whole box going to sleep and waking.
static void clock_sleep_logic() {
  puts("clock sleep: state machine, minute schedule, wake hold, low battery");
  WallClock wc;
  CHECK(wc.msToNextMinute(0) == -1);                                  // unknown
  wc.set(10 * 3600 + 38 * 60 + 50, 1000);                             // 10:38:50 at t = 1 s
  CHECK(wc.msToNextMinute(1000) == 10000 && wc.msToNextMinute(5000) == 6000 && wc.msToNextMinute(11000) == 60000);
  CHECK(wc.msToNextMinute(1000 + 12u * 3600u * 1000u + 1) > 0);       // valid for 30 days now (was 12 h)
  CHECK(wc.msToNextMinute(1000 + WallClock::kValidMs + 1) == -1);

  {  // the PC sets the time: valid for 30 days, then unknown; the wake-up is always finite and <= 60 s
    WallClock pc;
    CHECK(pc.setEpoch(1760875200LL, 5000));                           // 2025-10-19 12:00:00 UTC (a Sunday)
    CHECK(!pc.setEpoch(12345, 5000));                                 // junk refused, the old time stays
    CHECK(pc.localMinutes(0, 5000) == 12 * 60 && pc.localMinutes(180, 5000) == 15 * 60 && pc.localMinutes(-300, 5000) == 7 * 60);
    long days = 0;
    CHECK(pc.localDays(0, 5000, &days));
    char ds[16];
    WallClock::formatDate(days, ds, sizeof ds);
    CHECK(strcmp(ds, "Sun 19 Oct") == 0);
    CHECK(pc.localDays(720, 5000, &days) && (WallClock::formatDate(days, ds, sizeof ds), strcmp(ds, "Mon 20 Oct") == 0));   // +12 h: the next day
    CHECK(pc.localMinutes(0, 5000 + 29u * 86400u * 1000u) == 12 * 60);                      // 29 days later, same minute of day
    CHECK(pc.localMinutes(0, 5000 + WallClock::kValidMs + 1) == -1 && pc.msToNextMinute(5000 + WallClock::kValidMs + 1) == -1);
    CHECK(!pc.localDays(0, 5000 + WallClock::kValidMs + 1, &days));
    for (uint32_t t = 5000; t < 5000 + 3u * 3600u * 1000u; t += 7919) {                      // never infinite, never longer than a minute
      const long ms = pc.msToNextMinute(t);
      CHECK(ms >= 1 && ms <= 60000);
      SleepIn si; si.nowMs = t; si.localMinute = pc.localMinutes(0, t); si.msToMinute = ms;
      ClockSleep cs; cs.begin();
      cs.step(si);                                                                           // first step draws
      const SleepStep w = cs.step(si);
      CHECK(w.act == SLEEP_WAIT && w.wakeAtMs - t >= 1 && w.wakeAtMs - t <= ClockSleep::kMaxSleepMs);
    }
    {  // unknown time: still a finite wait
      SleepIn si; si.nowMs = 1000; si.localMinute = -1; si.msToMinute = -1;
      ClockSleep cs; cs.begin();
      cs.step(si);
      const SleepStep w = cs.step(si);
      CHECK(w.act == SLEEP_WAIT && w.wakeAtMs - 1000 <= ClockSleep::kMaxSleepMs && w.wakeAtMs > 1000);
    }
    // A GPS fix overrides the PC and keeps the date; across midnight the date follows.
    pc.setEpoch(1760918400LL - 10, 0);                                // 2025-10-19 23:59:50 UTC
    pc.set(0, 20000);                                                 // GPS says 00:00:00 twenty seconds later: it is the 20th
    CHECK(pc.localDays(0, 20000, &days) && (WallClock::formatDate(days, ds, sizeof ds), strcmp(ds, "Mon 20 Oct") == 0));
    WallClock g;
    g.set(3600, 0);
    CHECK(!g.localDays(0, 0, &days) && g.localMinutes(0, 0) == 60);   // GPS alone has no date
    CHECK(g.localMinutes(0, 12u * 3600u * 1000u + 1) == 60 + 720);    // and the 12 h rule is gone: 30 days
  }
  auto in = [](uint32_t t, bool down, int batt, int min, long toMin) {
    SleepIn i; i.nowMs = t; i.down = down; i.battPct = batt; i.localMinute = min; i.msToMinute = toMin; return i;
  };
  ClockSleep s;
  s.begin();
  SleepStep st = s.step(in(0, false, 80, 100, 7000));
  CHECK(st.act == SLEEP_DRAW);                                        // the first screen
  st = s.step(in(0, false, 80, 100, 7000));
  CHECK(st.act == SLEEP_WAIT && st.wakeAtMs == 7050);                 // sleeps to the minute change, not a poll
  CHECK(s.step(in(7050, false, 80, 101, 60000)).act == SLEEP_DRAW);   // once per minute
  CHECK(s.step(in(7050, false, 80, 101, 60000)).act == SLEEP_WAIT);
  // A pocket press (under 1 s) redraws, never wakes.
  st = s.step(in(20000, true, 80, 101, 47000));
  CHECK(st.act == SLEEP_WAIT && st.wakeAtMs == 20020);
  CHECK(s.step(in(20999, true, 80, 101, 47000)).act == SLEEP_WAIT);
  CHECK(s.step(in(21100, false, 80, 101, 47000)).act == SLEEP_DRAW);
  CHECK(s.step(in(21100, false, 80, 101, 47000)).act == SLEEP_WAIT);
  // A hold of 1 s wakes.
  s.step(in(30000, true, 80, 101, 0));
  CHECK(s.step(in(30999, true, 80, 101, 0)).act == SLEEP_WAIT);
  CHECK(s.step(in(31000, true, 80, 101, 0)).act == SLEEP_WAKE);
  // Unknown time: shown once, then only a look every minute (never a redraw of "--:--" for nothing).
  s.begin();
  CHECK(s.step(in(0, false, 80, -1, -1)).act == SLEEP_DRAW);
  st = s.step(in(0, false, 80, -1, -1));
  CHECK(st.act == SLEEP_WAIT && st.wakeAtMs == 60000);
  CHECK(s.step(in(60000, false, 80, -1, -1)).act == SLEEP_WAIT);
  CHECK(s.step(in(61000, false, 80, 5, 3000)).act == SLEEP_DRAW);     // the clock went from stale to ...: whatever it reads is drawn
  // The switch still down from the power-off hold: no wake until it was released once.
  s.begin();
  CHECK(s.step(in(0, true, 80, 100, 5000)).act == SLEEP_DRAW);
  CHECK(s.step(in(2000, true, 80, 100, 5000)).act != SLEEP_WAKE);
  CHECK(s.step(in(2100, false, 80, 100, 5000)).act == SLEEP_DRAW);
  // The 3 s hold goes on after the sleep began: a bar fills, 3 s later System OFF; never a wake.
  s.begin();
  st = s.step(in(0, true, 80, 100, 5000));
  CHECK(st.act == SLEEP_DRAW && st.bar == 0);                         // clock + empty bar at once
  CHECK(s.step(in(100, true, 80, 100, 5000)).act == SLEEP_WAIT);
  st = s.step(in(1500, true, 80, 100, 5000));
  CHECK(st.act == SLEEP_DRAW && st.bar == 6);
  CHECK(s.step(in(2900, true, 80, 100, 5000)).act != SLEEP_WAKE);     // far beyond the 1 s wake hold, still no wake
  CHECK(s.step(in(2999, true, 80, 100, 5000)).act != SLEEP_OFF);
  CHECK(s.step(in(3000, true, 80, 100, 5000)).act == SLEEP_OFF);      // 6 s in all
  // Released at 1.5 s: the clock stays, redrawn without the bar; the continued hold did not arm a wake.
  s.begin();
  s.step(in(0, true, 80, 100, 5000));
  s.step(in(1500, true, 80, 100, 5000));
  st = s.step(in(1600, false, 80, 100, 5000));
  CHECK(st.act == SLEEP_DRAW && st.bar == -1);
  CHECK(s.step(in(1600, false, 80, 100, 5000)).act == SLEEP_WAIT);
  CHECK(s.step(in(20000, false, 80, 101, 40000)).act == SLEEP_DRAW);
  // A nearly empty battery (two readings) ends in System OFF; one noisy reading does not.
  s.begin();
  s.step(in(0, false, 80, 100, 5000));
  s.step(in(5050, false, 80, 101, 60000));
  CHECK(s.step(in(70000, false, 1, 101, 1000)).act == SLEEP_WAIT);
  CHECK(s.step(in(72000, false, 40, 101, 1000)).act != SLEEP_OFF);
  CHECK(s.step(in(140000, false, 1, 102, 1000)).act != SLEEP_OFF);
  CHECK(s.step(in(142000, false, 2, 102, 1000)).act == SLEEP_OFF);
  s.begin();
  CHECK(s.step(in(0, false, 0, 100, 5000)).act == SLEEP_DRAW);        // low at the start: draw, then confirm
  CHECK(s.step(in(2000, false, 0, 100, 5000)).act == SLEEP_OFF);
}

#include "platform/sensors/bmp280.h"
// The weather station of the sleeping clock: BMP280 maths, the 24 h ring, tendency, forecast, chart scale, and the box reading
// the sensors once per minute redraw only.
static void weather_station() {
  puts("weather station: BMP280 datasheet example, ring, tendency, forecast, chart scale, sensor reads per minute");
  {
    Bmp280Cal c;                                                   // the datasheet's example (section 3.12)
    const uint8_t raw[24] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B, 0x27, 0x0B, 0x8C, 0x00,
                             0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17};
    c.parse(raw);
    CHECK(c.t1 == 27504 && c.p1 == 36477 && c.p9 == 6000 && c.p2 == -10685);
    const uint32_t pa = c.pressurePa(519888, 415148);
    CHECK(pa >= 100650 && pa <= 100656);                           // 100653 Pa
  }
  static WeatherLog w;
  w.clear();
  int cls = 9, lo = 0, hi = 0;
  CHECK(w.count() == 0 && w.latest() == 0 && !w.tendency(cls) && w.forecast() == FC_NONE && !w.chartRange(lo, hi));
  w.push(NAN); w.push(50.0f); w.push(2000.0f);
  CHECK(w.count() == 0);                                           // junk is ignored
  for (int i = 0; i < 18; i++) w.push(1010.0f);
  CHECK(w.count() == 18 && !w.tendency(cls));                      // 3 h of history needs 19 samples
  w.push(1010.0f);
  CHECK(w.tendency(cls) && cls == 0 && w.forecast() == FC_SUN_CLOUD);
  auto fcOf = [&](float d) { w.clear(); for (int i = 0; i < 18; i++) w.push(1010.0f); w.push(1010.0f + d); return w.forecast(); };
  CHECK(fcOf(+1.2f) == FC_SUN && fcOf(+0.9f) == FC_SUN_CLOUD && fcOf(-0.9f) == FC_SUN_CLOUD);
  CHECK(fcOf(-1.5f) == FC_CLOUD && fcOf(-3.6f) == FC_RAIN && fcOf(-6.5f) == FC_STORM);
  fcOf(+4.0f); CHECK(w.tendency(cls) && cls == 2);
  fcOf(+2.0f); CHECK(w.tendency(cls) && cls == 1);
  fcOf(-4.0f); CHECK(w.tendency(cls) && cls == -2);
  // The ring keeps 24 h: 144 samples, the oldest drop out; ages count back from the newest.
  w.clear();
  for (int i = 0; i < 200; i++) w.push(900.0f + i * 0.1f);
  CHECK(w.count() == WeatherLog::kN && w.get(0) == 9199);
  CHECK(w.get(143) == 9056 && w.get(144) == 0);
  CHECK(w.chartRange(lo, hi) && lo % 10 == 0 && hi % 10 == 0 && hi - lo >= 40 && lo <= w.get(143) && hi >= w.get(0));
  w.clear(); w.push(1013.2f); w.push(1013.4f);
  CHECK(w.chartRange(lo, hi) && hi - lo == 40 && lo <= 10132 && hi >= 10134);   // a flat day still gets a 4 hPa scale
  CHECK(chartY(hi, lo, hi, 62) == 0 && chartY(lo, lo, hi, 62) == 61 && chartX(0, 184) == 183 && chartX(143, 184) == 0);
  // Sampling every 10 min, a failed read retried after a minute.
  MockBaro bo;
  w.clear();
  w.sample(1000, bo); w.sample(1000 + 599000, bo);
  CHECK(w.count() == 1 && bo.reads == 1);
  w.sample(1000 + 600000, bo);
  CHECK(w.count() == 2);
  bo.present = false;
  w.sample(1000 + 1200000, bo); w.sample(1000 + 1230000, bo);
  CHECK(w.count() == 2 && bo.reads == 3);                          // one failed read, then it waits a minute
  bo.present = true;
  w.sample(1000 + 1260000, bo);
  CHECK(w.count() == 3);

  // In the box: the sensors are read at the minute redraws (single shots), not while the power-off hold counts down.
  static Box b;
  BoxRun u(b);
  b.r.settings.setIntroEnabled(false);
  b.clock.set(10 * 3600 + 38 * 60 + 50, 0);
  b.r.thermo.set(19.6f, 63);
  b.boot(0);
  u.run(500);
  const int awake = b.r.baro.reads;
  CHECK(awake == 1 && b.weather.count() == 1);                     // sampled while awake, so the history starts before the sleep
  u.sleep();                                                       // the sheet's Sleep
  b.clock.set(10 * 3600 + 38 * 60 + 50, u.now);                    // 13:38:50 again, so the minute change is 10 s away
  u.run(900);
  CHECK(b.sleep == Box::SS_CLOCK && b.r.thermo.reads >= 1 && isfinite(b.weather.tempC) && lroundf(b.weather.rhPct) == 63);
  const int th = b.r.thermo.reads;
  u.run(30000);
  CHECK(b.r.thermo.reads == th + 1);                               // the minute change: one single shot
  u.run(5 * 60 * 1000);
  CHECK(b.r.thermo.reads == th + 6);
  b.r.thermo.present = false;
  u.run(60000);
  CHECK(!isfinite(b.weather.tempC));                               // a missing sensor reads "--", never the last value
  CHECK(b.r.baro.reads <= awake + 2);                              // pressure: only the 10 min cadence
}

static int countInk(MockPanel& p) { return blackPixels(p); }

static void clock_sleep_box() {
  puts("clock sleep: the box powers off into the clock, wakes by a 1 s hold into the vario, no reset");
  static Box b;
  BoxRun u(b);
  b.r.settings.setTzMinutes(180);
  b.r.settings.setIntroEnabled(false);                                // the wake into the vario is checked bare here
  b.clock.set(10 * 3600 + 38 * 60 + 50, 0);                           // 13:38:50 local
  b.boot(0);
  u.run(500);
  CHECK(b.r.compass.enabled && !b.r.tracer.paused && b.host.active() == &b.vario);
  const int vf = b.r.panel.presents;
  u.sleep();                                                          // the sheet's Sleep
  b.clock.set(10 * 3600 + 38 * 60 + 50, u.now);                       // 13:38:50 local again: the next minute is 10 s away
  CHECK(b.r.power.requests == 1 && b.sleep == Box::SS_CLOCK);
  CHECK(!b.r.compass.enabled && b.r.tracer.paused);                   // sensors and radios off
  u.run(500);
  CHECK(b.clockDraws == 1 && countInk(b.r.panel) > 2000);             // the clock is on the panel
  const int ink = countInk(b.r.panel);
  b.r.panel.dump("out/sleep_1338.pgm");
  const int pres = b.r.panel.presents;
  // Nothing ticks, nothing redraws, until the minute changes (10 s after the 13:38:50 mark).
  u.run(4000);
  CHECK(b.r.panel.presents == pres && b.clockDraws == 1);
  u.run(6000);
  CHECK(b.clockDraws == 2 && b.r.panel.presents == pres + 1);         // 13:39
  const int d2 = b.clockDraws;
  u.run(5 * 60 * 1000);
  CHECK(b.clockDraws == d2 + 5);                                      // exactly one screen per minute
  // Taps and pocket presses do not wake.
  u.press(150);
  u.press(700);
  CHECK(b.sleep == Box::SS_CLOCK && b.host.active() == &b.vario && !b.r.compass.enabled && b.r.tracer.paused);
  // A hold of 1.2 s wakes into the start-up app, without a reset: the wall clock is still known.
  b.r.input.press(u.now + 10, 1200);
  u.run(1100);
  CHECK(b.sleep == Box::SS_RELEASE || b.sleep == Box::SS_CLOCK);
  u.run(1000);
  CHECK(b.sleep == Box::SS_AWAKE && b.host.active() == &b.vario);
  CHECK(!b.r.tracer.paused && b.clock.localMinutes(180, u.now) >= 0);
  u.run(500);
  CHECK(b.r.compass.enabled);                                         // the vario restarted its sensors
  CHECK(b.r.panel.presents > pres + 6);                               // and draws its own screens again
  CHECK(countInk(b.r.panel) != ink);
  CHECK(b.r.power.requests == 1);                                     // the wake hold did not count as another power-off
  (void)vf;

  // After 30 days without a tracer fix the clock reads "--:--" (never a stale time): the screen changes once more.
  b.boot(u.now);
  u.sleep();
  CHECK(b.sleep == Box::SS_CLOCK);
  u.run(500);
  const int dBefore = b.clockDraws;
  for (uint32_t e = u.now + 31u * 86400u * 1000u; u.now < e; u.now += 5000) b.tick(u.now);   // a sleeping box needs no 10 ms steps
  CHECK(b.clockDraws > dBefore + 40000 && b.clock.localMinutes(180, u.now) == -1);
  b.r.panel.dump("out/sleep_unknown.pgm");

  // Low battery: the sleep ends in System OFF (and stays there until a reset).
  b.sleep = Box::SS_AWAKE;
  b.boot(u.now);
  b.clock.set(8 * 3600, u.now);
  u.run(300);
  u.sleep();
  CHECK(b.sleep == Box::SS_CLOCK);
  u.run(5000);
  b.r.battery.pct = 1;
  u.run(130000);
  CHECK(b.sleep == Box::SS_OFF && countInk(b.r.panel) == 0);
  b.r.input.press(u.now + 10, 1500);
  u.run(3000);
  CHECK(b.sleep == Box::SS_OFF);                                      // a hold does not bring it back

  // The expert path: holding the switch on past the sheet (3 s) to 6 s is System OFF at once; releasing early keeps the sheet.
  Box c;
  BoxRun v(c);
  c.r.settings.setIntroEnabled(false);
  c.clock.set(9 * 3600, 0);
  c.boot(0);
  v.run(300);
  c.r.input.press(v.now + 10, 4500);                  // released 1.5 s into the "keep holding" bar
  v.run(5000);
  CHECK(c.sleep == Box::SS_AWAKE && c.host.active() == &c.vario && c.r.power.offs == 0 && c.host.sheetOpen());
  v.run(7000);                                        // 6 s without input: the sheet closes by itself
  CHECK(!c.host.sheetOpen());
  c.r.input.press(v.now + 10, 7000);
  v.run(6500);
  CHECK(c.r.power.offs == 1 && c.sleep == Box::SS_OFF);
}

// BLE (the laptop link) is off while a game runs and back when it is left; the vario's own tracer logic is untouched.
static void box_links_off_in_games() {
  puts("box: the laptop link is off while the flight sim runs and back afterwards");
  Box b;
  BoxRun u(b);
  b.boot(0);
  u.run(300);
  CHECK(b.r.links.on && b.r.links.offs == 0);                  // the vario keeps it
  b.host.start(Box::APP_LAUNCHER, u.now);
  u.run(100);
  CHECK(b.r.links.on && b.r.links.offs == 0);
  b.host.start(Box::APP_SIM, u.now);
  u.run(100);
  CHECK(b.host.gameMode() && !b.r.links.on && b.r.links.offs == 1);
  b.host.start(Box::APP_SIM, u.now);                           // restarting a game keeps it off, idempotently
  CHECK(!b.r.links.on);
  b.host.start(Box::APP_LAUNCHER, u.now);
  u.run(100);
  CHECK(b.r.links.on);
  b.host.start(Box::APP_VARIO, u.now);
  u.run(100);
  CHECK(b.r.links.on && b.r.tracer.paused == false);
}

// The sleep screen's look: nothing in the side margins, the time numerals sit in fixed cells (tabular: the time does not
// jump), unknown time and unknown sensors still draw, the countdown bar only touches the bottom strip, drawing is deterministic.
static void clock_view_look() {
  puts("clock view: margins, tabular numerals, unknown states, countdown bar");
  static MockPanel mp;
  static WeatherLog wl;
  auto render = [&](int minute, int batt, const WeatherLog* w, int bar) {
    mp.setRotation(1);
    mp.clear();
    Bitmap1 bm = mp.bitmap();
    clockview::draw(bm, minute, batt, w, bar, 12, nullptr);
    return bm;
  };
  auto ink = [&](const Bitmap1& bm, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += bm.getPixel(x, y) == 0;
    return n;
  };
  for (int i = 0; i < WeatherLog::kN; i++) wl.push(1010.0f + 3.0f * sinf(i * 0.05f));
  wl.tempC = 18.0f; wl.rhPct = 55.0f;
  Bitmap1 bm = render(11 * 60 + 11, 50, &wl, -1);
  CHECK(ink(bm, 0, 0, 11, 399) == 0 && ink(bm, 228, 0, 239, 399) == 0);          // 12 px margins stay white
  CHECK(ink(bm, 0, 0, 239, 28) > 0 && ink(bm, 0, 40, 239, 140) > 500);           // battery; the hero time
  const int c0 = (ui::W - (4 * 43 + 2 * 5 + 28)) / 2;                              // the four numeral cells of the hero time
  const int cells[4] = {c0, c0 + 48, c0 + 2 * 43 + 5 + 28, c0 + 3 * 43 + 2 * 5 + 28};
  int n1[4];
  for (int i = 0; i < 4; i++) n1[i] = ink(bm, cells[i], 40, cells[i] + 42, 140);
  CHECK(n1[0] == n1[1] && n1[2] == n1[3] && n1[0] == n1[2]);                       // identical numerals, identical cells
  Bitmap1 b8 = render(8 * 60 + 8, 50, &wl, -1);
  for (int i = 0; i < 4; i++) CHECK(ink(b8, cells[i], 40, cells[i] + 42, 140) > 0 && ink(b8, cells[i] - 4, 38, cells[i] - 1, 142) == 0);
  CHECK(ink(b8, 0, 0, 11, 399) == 0 && ink(b8, 228, 0, 239, 399) == 0);
  Bitmap1 bu = render(-1, -1, nullptr, -1);                                        // no time, no battery, no weather
  CHECK(ink(bu, 0, 40, 239, 60) == 0 && ink(bu, 0, 80, 239, 100) > 0 && ink(bu, 0, 0, 239, 28) > 0);   // the battery reads "--"
  CHECK(ink(bu, 12, 306, 228, 372) > 0);                                           // the empty chart's dotted baseline and "--"
  Bitmap1 bb = render(600, 50, &wl, 5);
  CHECK(ink(bb, 0, 386, 239, 399) > 0);
  Bitmap1 bn = render(600, 50, &wl, -1);
  CHECK(ink(bn, 0, 386, 239, 399) == 0);
  static uint8_t before[400 * 240 / 8];
  render(745, 80, &wl, -1);
  memcpy(before, mp.raw(), sizeof before);
  render(745, 80, &wl, -1);
  CHECK(memcmp(before, mp.raw(), sizeof before) == 0);
}

static void test_box() {
  clock_sleep_logic();
  weather_station();
  clock_view_look();
  clock_sleep_box();
  combo_and_debounce();
  host_frame_loop();
  box_flow();
  box_vario_matches_alone();
  box_vario_lazy();
  box_links_off_in_games();
  box_gestures();
  settings_store();
  settings_app();
  box_laptop_commands();
  box_tetris_home();
  lazy_app_slots();
  box_lazy_apps();
  box_intro();
}

}  // namespace bx
