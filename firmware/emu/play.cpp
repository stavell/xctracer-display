// PC player: runs the platform host (the same AppHost the device runs) with mock ports in an SDL window, drawing
// into the SAME native 1-bit frame buffer the device uses (400x240, stride 50, rotation 1, LSB leftmost, set =
// white). The host draws only when the active app asks for a frame.
//
//   build.sh play tetris [--scale 2] [--plain] [--seed N]       Tetris alone (no launcher)
//   build.sh play sim [--pack FILE] [--scale 2]                  the flight simulator alone (needs a map pack; default the
//                                                               Bulgaria pack of the study if it exists, else the Sopot fixture)
//   build.sh play box [--igc f.igc --start S --speed N] [--lines f.txt] [--sim] [--battery P] [--temp C]
//                                                               the whole box: launcher + vario + Tetris + compass
//   --no-intro           the box starts straight into the start-up app (default: the start-up animation first, unless Settings turned it off)
//   --pack FILE          map pack for the vario's AGL and map page (default: the Bulgaria pack of the terrain study if
//                        it exists, else fixtures/sopot.pack; "none" = no terrain, the box as it was)
//   build.sh play <app> --frames 300 --screenshot out.png --keys "main 10 left a up shot:x.png"
//   build.sh smoke                                              scripted run of the box, writes out/box_*.png
//
// The mock tracer plays an IGC file (converted by host/make_lines.py) or a line file at --speed lines per
// second (default 3, the tracer's rate), only while the vario runs: a paused tracer misses its lines, like the
// real one flying on. The compass heading turns with the mouse wheel or the , and . keys.
//
// Headless mode (--frames, --screenshot or --keys given): no window, virtual time (10 ms host ticks, 30 ms
// frames), a scripted input sequence, PNG output. Script tokens, separated by spaces:
//   left right up down a b main   tap the key (down for 3 frames, up)
//   hold                          MAIN held 0.5 s (Tetris: swap the hold piece)
//   long                          MAIN held 0.9 s (the "long press": list in the vario, start in the launcher)
//   off                           MAIN held 3.2 s (3 s hold: power off in the vario, back to the launcher elsewhere)
//   combo                         A + B held 1.6 s (back to the launcher)
//   +left / -left                 press / release and keep it
//   12                            wait 12 frames
//   turn:90                       set the compass heading
//   shot:file.png                 save a screenshot now
// Without --keys the Tetris script is "main" followed by fixed moves (a demo game).
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <string>
#include <vector>
#include <memory>
#include "box.h"

static const int LW = 240, LH = 400;
static const uint32_t TICK_MS = 10;                  // the device loop's delay(10)

struct Look {
  uint32_t paper, ink;                               // 0xRRGGBB
};
static Look gLook = {0xC9D1BE, 0x262B26};            // Sharp memory LCD: pale grey-green, dark grey ink

// Logical (portrait) pixels to RGB, read back through the panel's bitmap so the rotation mapping is the one
// the device uses.
static void toRgb(MockPanel& p, std::vector<uint32_t>& out) {
  Bitmap1 bm = p.bitmap();
  out.resize((size_t)LW * LH);
  for (int y = 0; y < LH; y++)
    for (int x = 0; x < LW; x++) out[(size_t)y * LW + x] = bm.getPixel(x, y) ? gLook.paper : gLook.ink;
}

// ---- minimal PNG writer (stored deflate blocks)
static uint32_t crcTable[256];
static void crcInit() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
static uint32_t crcUpdate(uint32_t c, const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c;
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(x >> (8 * i)));
}
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> h;
  put32(h, (uint32_t)data.size());
  fwrite(h.data(), 1, 4, f);
  fwrite(type, 1, 4, f);
  if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
  uint32_t c = crcUpdate(0xFFFFFFFFu, (const uint8_t*)type, 4);
  c = crcUpdate(c, data.data(), data.size()) ^ 0xFFFFFFFFu;
  std::vector<uint8_t> t;
  put32(t, c);
  fwrite(t.data(), 1, 4, f);
}
static bool writePng(const char* path, const std::vector<uint32_t>& px, int scale) {
  crcInit();
  int w = LW * scale, h = LH * scale;
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 3 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      uint32_t c = px[(size_t)(y / scale) * LW + x / scale];
      raw.push_back((uint8_t)(c >> 16));
      raw.push_back((uint8_t)(c >> 8));
      raw.push_back((uint8_t)c);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t pos = 0; pos < raw.size(); pos += 65535) {
    size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back((uint8_t)n);
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)~n);
    z.push_back((uint8_t)(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
  }
  put32(z, (b << 16) | a);
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w);
  put32(ihdr, (uint32_t)h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  return true;
}

// ---- machines: what runs behind the host
struct Args {
  std::string app = "tetris";
  int scale = 2;
  bool plain = false, headless = false;
  int frames = -1;
  std::string shot, keys;
  uint32_t seed = 1;
  bool seedGiven = false;
  std::string igc, lines;
  std::string pack = "default";
  bool noIntro = false;                              // box: skip the start-up animation (the smoke run needs the app at once)
  bool sim = false;                                  // the vario's built-in test flight instead of tracer lines
  int start = 0, seconds = 3600;
  double speed = 3;                                  // tracer lines per second
  int battery = 76;
  float temp = 21.4f;
};

struct Machine {
  virtual ~Machine() {}
  virtual MockPanel& panel() = 0;
  virtual MockInput& input() = 0;
  virtual AppHost& host() = 0;
  virtual void tick(uint32_t now) { host().tick(now); }   // the box adds its clock sleep
  virtual void boot(uint32_t now) = 0;
  virtual void restart(uint32_t now) = 0;
  virtual Rig* rig() { return nullptr; }              // the mock hardware, box only
  virtual sim::SimApp* flightSim() { return nullptr; }   // the flight simulator, if this machine has one
  virtual MockTilt* tilt() { return nullptr; }
};

struct TetrisMachine : Machine {
  MockPanel p;
  MockInput in;
  TetrisApp app;
  AppHost h;
  explicit TetrisMachine(uint32_t seed) : app(seed), h(p, in) { h.add(app); }
  MockPanel& panel() override { return p; }
  MockInput& input() override { return in; }
  AppHost& host() override { return h; }
  void boot(uint32_t now) override { h.start(0, now); }
  void restart(uint32_t now) override {              // R: a new game
    h.start(0, now);
    in.press(now + 20, 60);                           // a tap of the switch starts the game
  }
};

static const char* kStudyPack =
    "/private/tmp/claude-501/-Users-stavel-dev-display/da365ad1-40f2-499d-917a-3a6befca5ade/scratchpad/terrain/bulgaria.pack";
static const char* kFixturePack = "fixtures/sopot.pack";

// The pack is read into memory (1.7 MB; the device reads the QSPI flash instead, through the same IBlockReader).
struct PackHolder {
  std::vector<uint8_t> bytes;
  terrain::MemReader reader;
  TerrainKit* kit = new TerrainKit();               // ~65 kB: not on the stack
  std::string loaded;
  bool load(const std::string& spec) {
    std::string path = spec;
    if (spec == "none") return false;
    if (spec == "default") path = access(kStudyPack, R_OK) == 0 ? kStudyPack : kFixturePack;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "pack %s: cannot open (running without terrain)\n", path.c_str()); return false; }
    fseek(f, 0, SEEK_END);
    bytes.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    bool ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    reader = terrain::MemReader(bytes.data(), (uint32_t)bytes.size());
    if (!ok || !kit->open(&reader)) { fprintf(stderr, "pack %s: rejected (running without terrain)\n", path.c_str()); return false; }
    loaded = path;
    return true;
  }
};

// The flight simulator alone: no launcher (the pause menu's Home does nothing), same host and same fixed-step loop.
struct SimMachine : Machine {
  MockPanel p;
  MockInput in;
  PackHolder pack;
  MockTilt tl;
  std::unique_ptr<sim::SimApp> app{new sim::SimApp};
  AppHost h;
  explicit SimMachine(const std::string& packSpec) : h(p, in) {
    if (pack.load(packSpec)) {
      app->setTerrain(&pack.kit->terrain);
      printf("terrain: %s (%zu bytes)\n", pack.loaded.c_str(), pack.bytes.size());
    }
    app->setTilt(&tl);
    h.add(*app);
  }
  MockPanel& panel() override { return p; }
  MockInput& input() override { return in; }
  AppHost& host() override { return h; }
  void boot(uint32_t now) override { h.start(0, now); }
  void restart(uint32_t now) override { h.start(0, now); }
  sim::SimApp* flightSim() override { return app.get(); }
  MockTilt* tilt() override { return &tl; }
};

struct BoxMachine : Machine {
  Box b;
  PackHolder pack;
  bool intro = true;
  BoxMachine(uint32_t seed, const std::string& packSpec) : b(seed) {
    if (pack.load(packSpec)) {
      b.setTerrain(pack.kit);
      printf("terrain: %s (%zu bytes)\n", pack.loaded.c_str(), pack.bytes.size());
    }
  }
  MockPanel& panel() override { return b.r.panel; }
  MockInput& input() override { return b.r.input; }
  AppHost& host() override { return b.host; }
  void tick(uint32_t now) override { b.tick(now); }
  void boot(uint32_t now) override { b.boot(now, intro); }          // a power-on: the start-up animation (Settings > Intro)
  void restart(uint32_t now) override { b.sleep = Box::SS_AWAKE; b.boot(now, intro); }   // R: power-cycle into the start-up app
  Rig* rig() override { return &b.r; }
  sim::SimApp* flightSim() override { return b.flightSim.get(); }
  MockTilt* tilt() override { return &b.tilt; }
};

static std::vector<std::string> loadLines(const Args& a) {
  std::vector<std::string> v;
  FILE* f = nullptr;
  if (!a.igc.empty()) {
    char cmd[1024];
    const char* py = access("../../.venv/bin/python", X_OK) == 0 ? "../../.venv/bin/python" : "python3";
    snprintf(cmd, sizeof cmd, "%s ../../host/make_lines.py igc '%s' --start %d --seconds %d", py, a.igc.c_str(), a.start, a.seconds);
    f = popen(cmd, "r");
  } else if (!a.lines.empty()) {
    f = fopen(a.lines.c_str(), "r");
  }
  if (!f) return v;
  char buf[512];
  while (fgets(buf, sizeof buf, f)) {
    std::string l = buf;
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    if (!l.empty()) v.push_back(l);
  }
  if (!a.igc.empty()) pclose(f); else fclose(f);
  return v;
}

// Feeds the mock tracer, compass, thermometer and battery. Line time starts at the first call.
struct World {
  std::vector<std::string> lines;
  double speed = 3;
  size_t next = 0;
  uint32_t t0 = 0, thermoAt = 0;
  bool started = false;
  double heading = 0;
  float temp = 21.4f;
  int battery = 76;
  bool sim = false, simSent = false;

  void tick(Rig* r, uint32_t now) {
    if (!r) return;
    if (!started) { started = true; t0 = now; thermoAt = now; r->battery.pct = battery; }
    if (sim && !simSent) { simSent = true; r->laptop.sim(true); }
    r->compass.psi = heading;
    r->compass.pitch = 0;
    r->compass.roll = 0;
    if (!lines.empty()) {
      size_t due = (size_t)((now - t0) * speed / 1000.0);
      if (due > lines.size()) due = lines.size();
      for (; next < due; next++)
        if (!r->tracer.paused) r->tracer.lines.push_back(lines[next]);   // a paused tracer loses its lines
    }
    if (now - thermoAt >= 2000) {
      thermoAt += 2000;
      r->thermo.set(temp + 0.3f * sinf((now - t0) / 20000.0f), 48);
    }
  }
};

static bool keyByName(const std::string& n, Key& k) {
  static const struct { const char* n; Key k; } t[] = {
      {"left", KEY_LEFT}, {"right", KEY_RIGHT}, {"up", KEY_UP}, {"down", KEY_DOWN},
      {"a", KEY_ACTION},  {"b", KEY_B},         {"main", KEY_MAIN},
  };
  for (auto& e : t)
    if (n == e.n) { k = e.k; return true; }
  return false;
}

// ---- headless
struct Runner {
  Machine& m;
  World& world;
  Args& args;
  uint32_t now = 1000;
  std::vector<uint32_t> rgb;
  int shots = 0;

  void tickOnce() {
    now += TICK_MS;
    world.tick(m.rig(), now);
    m.tick(now);
  }
  void step(int frames = 1) {                         // frames of 30 ms
    for (int f = 0; f < frames * 3; f++) tickOnce();
  }
  void shot(const std::string& path) {
    toRgb(m.panel(), rgb);
    if (writePng(path.c_str(), rgb, args.scale)) printf("wrote %s (t=%u ms, %s)\n", path.c_str(), now - 1000, m.host().active() ? m.host().active()->name() : "-");
    else fprintf(stderr, "cannot write %s\n", path.c_str());
    shots++;
  }
  void key(Key k, bool down) { m.input().setKey(k, down, now); }
  void tap(Key k) {
    key(k, true);
    step(3);
    key(k, false);
    step(2);
  }
  void held(Key k, int frames) {
    key(k, true);
    step(frames);
    key(k, false);
    step(2);
  }
  void run(const std::string& script) {
    size_t i = 0;
    while (i < script.size()) {
      while (i < script.size() && script[i] == ' ') i++;
      size_t j = script.find(' ', i);
      if (j == std::string::npos) j = script.size();
      std::string tok = script.substr(i, j - i);
      i = j;
      if (tok.empty()) continue;
      Key k;
      if (isdigit((unsigned char)tok[0])) step(atoi(tok.c_str()));
      else if (tok.compare(0, 5, "shot:") == 0) shot(tok.substr(5));
      else if (tok.compare(0, 5, "turn:") == 0) world.heading = atof(tok.c_str() + 5);
      else if (tok == "hold") held(KEY_MAIN, 17);
      else if (tok == "long") held(KEY_MAIN, 30);
      else if (tok == "off") held(KEY_MAIN, 107);
      else if (tok == "combo") { key(KEY_ACTION, true); key(KEY_B, true); step(54); key(KEY_ACTION, false); key(KEY_B, false); step(2); }
      else if (tok[0] == '+' && keyByName(tok.substr(1), k)) key(k, true);
      else if (tok[0] == '-' && keyByName(tok.substr(1), k)) key(k, false);
      else if (keyByName(tok, k)) tap(k);
      else fprintf(stderr, "unknown token '%s'\n", tok.c_str());
    }
  }
};

static std::string demoScript(int frames) {
  std::string s = "main 5";
  uint32_t r = 12345;
  const char* mv[] = {"left", "right", "a", "b", "left", "right", "a", "down", "up"};
  int used = 6;
  while (used < frames) {
    r = r * 1664525u + 1013904223u;
    const char* m = mv[(r >> 24) % 9];
    s += std::string(" ") + m;
    used += 5;
    if (!strcmp(m, "up")) { s += " 6"; used += 6; }
  }
  return s;
}

static int runHeadless(Machine& m, World& w, Args& a) {
  Runner r{m, w, a};
  m.boot(r.now);
  r.step(2);
  std::string script = a.keys.empty() ? (a.app == "tetris" ? demoScript(a.frames > 0 ? a.frames : 300) : std::string()) : a.keys;
  r.run(script);
  if (a.frames > 0 && a.keys.empty()) r.step(1);
  if (!a.shot.empty()) r.shot(a.shot);
  return 0;
}

// ---- interactive
static const char* kHelp =
    "Keys:  arrows = D-pad      X = A      Z = B      Space = up (Tetris hard drop)\n"
    "       Enter / Esc = the user switch (hold it: 0.7-1.5 s long press, 3 s hold; the title shows the time)\n"
    "       A + B held 1.5 s = back to the launcher      , and . or the mouse wheel = turn the compass\n"
    "       C = Tetris hold piece (a 0.46 s press of the switch)    R = restart    Q or close the window = quit\n"
    "Flight sim (game mode, 50 fps): LEFT/RIGHT bank, UP speed bar, DOWN brake, X (A) = map / 3D view, F = time x1/x3/x5, Z (B) or Esc = pause menu\n"
    "       (resume, view, assist, site, controls, zero tilt, new flight, home); after landing X = new flight\n"
    "       T = tilt steering with the mouse (x = roll, y = pitch, top of the window = top away = speed bar); T again = buttons\n";

static int runWindow(Machine& m, World& w, Args& a) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Window* win = SDL_CreateWindow(a.app.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, LW * a.scale,
                                     LH * a.scale, SDL_WINDOW_ALLOW_HIGHDPI);
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  SDL_RenderSetLogicalSize(ren, LW, LH);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, LW, LH);
  fputs(kHelp, stdout);

  std::vector<uint32_t> rgb;
  uint32_t now = SDL_GetTicks();
  m.boot(now);
  uint32_t lastTick = now, fpsAt = now, titleAt = 0;
  int shownPresents = -1, fpsBase = m.panel().presents, fps = 0;
  uint32_t releaseMainAt = 0;                         // pending end of the C "long press"
  double workMax = 0, workShown = 0;                  // slowest host tick (ms) in the last second
  bool tiltOn = false;                                // T: the mouse is the box tilt
  bool running = true;
  while (running) {
    now = SDL_GetTicks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_MOUSEMOTION && tiltOn && m.tilt()) {
        int ww = 1, wh = 1;
        SDL_GetWindowSize(win, &ww, &wh);
        m.tilt()->roll = ((float)e.motion.x / ww - 0.5f) * 90.0f;     // window edge = 45 degrees
        m.tilt()->pitch = (0.5f - (float)e.motion.y / wh) * 60.0f;    // top of the window = 30 degrees top away
      }
      if (e.type == SDL_MOUSEWHEEL) w.heading = fmod(w.heading + (e.wheel.y > 0 ? 5 : -5) + 360, 360);
      if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) continue;
      bool down = e.type == SDL_KEYDOWN;
      now = SDL_GetTicks();
      SDL_Keycode sym = e.key.keysym.sym;
      if (down && e.key.repeat) {                     // the compass keys repeat; the buttons do not
        if (sym == SDLK_COMMA) w.heading = fmod(w.heading - 3 + 360, 360);
        if (sym == SDLK_PERIOD) w.heading = fmod(w.heading + 3, 360);
        continue;
      }
      MockInput& in = m.input();
      switch (sym) {
        case SDLK_LEFT: in.setKey(KEY_LEFT, down, now); break;
        case SDLK_RIGHT: in.setKey(KEY_RIGHT, down, now); break;
        case SDLK_DOWN: in.setKey(KEY_DOWN, down, now); break;
        case SDLK_UP:
        case SDLK_SPACE: in.setKey(KEY_UP, down, now); break;
        case SDLK_x: in.setKey(KEY_ACTION, down, now); break;
        case SDLK_z: in.setKey(KEY_B, down, now); break;
        case SDLK_RETURN:
        case SDLK_ESCAPE: in.setKey(KEY_MAIN, down, now); break;
        case SDLK_COMMA: if (down) w.heading = fmod(w.heading - 3 + 360, 360); break;
        case SDLK_PERIOD: if (down) w.heading = fmod(w.heading + 3, 360); break;
        case SDLK_c:
          if (down && !releaseMainAt) {
            in.setKey(KEY_MAIN, true, now);
            releaseMainAt = now + TetrisApp::LONG_PRESS_MS + 60;
          }
          break;
        case SDLK_t:
          if (down && m.tilt() && m.flightSim()) {
            tiltOn = !tiltOn;
            m.tilt()->on = tiltOn;
            if (tiltOn) {
              int mx, my, ww, wh;
              SDL_GetMouseState(&mx, &my);
              SDL_GetWindowSize(win, &ww, &wh);
              m.tilt()->roll = ((float)mx / ww - 0.5f) * 90.0f;
              m.tilt()->pitch = (0.5f - (float)my / wh) * 60.0f;
            }
            m.flightSim()->setControl(tiltOn ? sim::SimApp::Control::TILT : sim::SimApp::Control::BUTTONS);
          }
          break;
        case SDLK_f: if (down && m.flightSim()) m.flightSim()->cycleTimeScale(); break;
        case SDLK_r: if (down) m.restart(now); break;
        case SDLK_q: running = false; break;
        default: break;
      }
    }
    now = SDL_GetTicks();
    if (releaseMainAt && (int32_t)(now - releaseMainAt) >= 0) {
      m.input().setKey(KEY_MAIN, false, now);
      releaseMainAt = 0;
    }
    if (now - lastTick >= (m.host().gameMode() ? 1u : TICK_MS)) {   // the device loop: tick, then wait 10 ms (1 ms in game mode)
      lastTick = now;
      w.tick(m.rig(), now);
      const uint64_t c0 = SDL_GetPerformanceCounter();
      m.tick(now);
      const double ms = (SDL_GetPerformanceCounter() - c0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
      if (ms > workMax) workMax = ms;
    }
    int p = m.panel().presents;
    if (p != shownPresents) {                         // the host presented a frame
      shownPresents = p;
      toRgb(m.panel(), rgb);
      SDL_UpdateTexture(tex, nullptr, rgb.data(), LW * 4);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
    }
    if (now - fpsAt >= 1000) {
      workShown = workMax;
      workMax = 0;
      fps = m.panel().presents - fpsBase;
      fpsBase = m.panel().presents;
      fpsAt = now;
    }
    if (now - titleAt >= 100) {                       // title: app, frames per second, compass, the switch's press time
      titleAt = now;
      char t[160];
      uint32_t held = m.input().heldMs(now);
      const char* nm = m.host().active() ? m.host().active()->name() : "-";
      int n = snprintf(t, sizeof t, "%s: %s  %d fps", a.app.c_str(), nm, fps);
      if (m.host().gameMode()) {
        const FrameStats& st = m.host().stats();
        n += snprintf(t + n, sizeof t - n, "  gap max %u ms  work max %.1f ms  late %u", st.worstGapMs, workShown, st.dropped);
      }
      if (m.rig()) n += snprintf(t + n, sizeof t - n, "  hdg %03.0f", w.heading);
      if (held > 0) snprintf(t + n, sizeof t - n, "  MAIN %.1f s%s", held / 1000.0, held >= 3000 ? " HOLD" : held >= 1500 ? " (cancel zone)" : held >= 700 ? " LONG" : "");
      SDL_SetWindowTitle(win, t);
    }
    SDL_Delay(m.host().gameMode() ? 1 : 2);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

int main(int argc, char** argv) {
  Args a;
  int pos = 0;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    bool more = i + 1 < argc;
    if (s == "--scale" && more) a.scale = atoi(argv[++i]);
    else if (s == "--plain") a.plain = true;
    else if (s == "--frames" && more) { a.frames = atoi(argv[++i]); a.headless = true; }
    else if (s == "--screenshot" && more) { a.shot = argv[++i]; a.headless = true; }
    else if (s == "--keys" && more) { a.keys = argv[++i]; a.headless = true; }
    else if (s == "--seed" && more) { a.seed = (uint32_t)strtoul(argv[++i], nullptr, 10); a.seedGiven = true; }
    else if (s == "--sim") a.sim = true;
    else if (s == "--no-intro") a.noIntro = true;
    else if (s == "--pack" && more) a.pack = argv[++i];
    else if (s == "--igc" && more) a.igc = argv[++i];
    else if (s == "--lines" && more) a.lines = argv[++i];
    else if (s == "--start" && more) a.start = atoi(argv[++i]);
    else if (s == "--seconds" && more) a.seconds = atoi(argv[++i]);
    else if (s == "--speed" && more) a.speed = atof(argv[++i]);
    else if (s == "--battery" && more) a.battery = atoi(argv[++i]);
    else if (s == "--temp" && more) a.temp = (float)atof(argv[++i]);
    else if (s[0] != '-' && pos++ == 0) a.app = s;
    else { fprintf(stderr, "unknown argument %s\n", s.c_str()); return 2; }
  }
  if (a.scale < 1) a.scale = 1;
  if (a.plain) gLook = {0xFFFFFF, 0x000000};
  // Random seed per run unless --seed is given (the game also mixes in the start time).
  uint32_t seed = a.seedGiven ? a.seed : (uint32_t)time(nullptr) * 2654435761u + 17u;
  if (a.headless && !a.seedGiven) seed = 1;           // scripted runs are reproducible
  Machine* m = nullptr;
  if (a.app == "tetris") m = new TetrisMachine(seed);
  else if (a.app == "box") { BoxMachine* bm = new BoxMachine(seed, a.pack); bm->intro = !a.noIntro; m = bm; }
  else if (a.app == "sim") m = new SimMachine(a.pack);
  else {
    fprintf(stderr, "unknown app '%s' (known: tetris, box, sim)\n", a.app.c_str());
    return 2;
  }
  World w;
  w.speed = a.speed;
  w.temp = a.temp;
  w.sim = a.sim;
  w.battery = a.battery;
  w.lines = loadLines(a);
  if ((!a.igc.empty() || !a.lines.empty()) && w.lines.empty()) {
    fprintf(stderr, "no tracer lines could be read (igc/lines file missing, or python failed)\n");
    return 2;
  }
  if (!w.lines.empty()) printf("tracer: %zu lines at %.1f per second\n", w.lines.size(), w.speed);
  int rc = a.headless ? runHeadless(*m, w, a) : runWindow(*m, w, a);
  delete m;
  return rc;
}
