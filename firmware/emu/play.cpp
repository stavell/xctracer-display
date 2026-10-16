// PC player: runs an IApp in real time in an SDL window, drawing into the SAME native 1-bit frame buffer
// the device uses (400x240, stride 50, rotation 1, LSB leftmost, set = white), 30 frames per second at
// most and only when the app reports a new frame.
//
//   build.sh play tetris [--scale 2] [--plain] [--seed N]
//   build.sh play tetris --frames 300 --screenshot out.png [--keys "main 10 left left a up shot:x.png"]
//
// Headless mode (--frames or --screenshot given): no window, virtual time at 30 fps, a scripted input
// sequence, PNG output. Script tokens, separated by spaces:
//   left right up down a b main   tap the key (down, one frame, up)
//   hold                          MAIN held for 0.5 s (the long press: hold piece)
//   +left / -left                 press / release and keep it
//   12                            wait 12 frames
//   shot:file.png                 save a screenshot now
// Without --keys the script is "main" followed by random-looking but fixed moves (a demo game).
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <string>
#include <vector>
#include "apps/games/tetris/tetris_app.h"
#include "platform/app_host.h"

static const int NATIVE_W = 400, NATIVE_H = 240, STRIDE = 50;
static const int LW = 240, LH = 400;
static const uint32_t FRAME_MS = 33;                 // 30 fps cap, the device cadence

static uint8_t gFb[STRIDE * NATIVE_H];
static Bitmap1 frame() { return Bitmap1{gFb, NATIVE_W, NATIVE_H, STRIDE, 1}; }

struct Look {
  uint32_t paper, ink;                               // 0xRRGGBB
};
static Look gLook = {0xC9D1BE, 0x262B26};            // Sharp memory LCD: pale grey-green, dark grey ink

static void renderFrame(IApp& app, uint32_t now) {
  memset(gFb, 0xFF, sizeof gFb);                     // white canvas
  Bitmap1 bm = frame();
  app.draw(bm, now);
}

// Logical (portrait) pixels to RGB, read back through the bitmap so the rotation mapping is the one the
// device uses.
static void toRgb(std::vector<uint32_t>& out) {
  Bitmap1 bm = frame();
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

// ---- apps
struct Args {
  std::string app = "tetris";
  int scale = 2;
  bool plain = false, headless = false;
  int frames = -1;
  std::string shot, keys;
  uint32_t seed = 1;
  bool seedGiven = false;
};

static IApp* makeApp(const std::string& name, const Args& a) {
  if (name == "tetris") {
    // Random seed per run unless --seed is given (the game also mixes in the start time).
    uint32_t s = a.seedGiven ? a.seed : (uint32_t)SDL_GetTicks() * 2654435761u + (uint32_t)time(nullptr);
    return new TetrisApp(s);
  }
  return nullptr;
}

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
  IApp& app;
  Args& args;
  uint32_t now = 1000;
  std::vector<uint32_t> rgb;
  int shots = 0;

  void step(int n = 1) {                              // n frames of 33 ms, ticking every 11 ms
    for (int f = 0; f < n; f++)
      for (int i = 0; i < 3; i++) {
        now += FRAME_MS / 3;
        app.tick(now);
      }
    if (app.wantsFrame(now)) renderFrame(app, now);
  }
  void shot(const std::string& path) {
    if (app.wantsFrame(now)) renderFrame(app, now);
    toRgb(rgb);
    if (writePng(path.c_str(), rgb, args.scale)) printf("wrote %s (t=%u ms)\n", path.c_str(), now - 1000);
    else fprintf(stderr, "cannot write %s\n", path.c_str());
    shots++;
  }
  void key(Key k, bool down) {
    app.onKey(k, down, now);
  }
  void tap(Key k) {
    key(k, true);
    step(1);
    key(k, false);
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
      else if (tok == "hold") { key(KEY_MAIN, true); step(15); key(KEY_MAIN, false); step(1); }
      else if (tok[0] == '+' && keyByName(tok.substr(1), k)) key(k, true);
      else if (tok[0] == '-' && keyByName(tok.substr(1), k)) key(k, false);
      else if (keyByName(tok, k)) { tap(k); step(1); }
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
    used += 2;
    if (!strcmp(m, "up")) { s += " 6"; used += 6; }
  }
  return s;
}

static int runHeadless(IApp& app, Args& a) {
  Runner r{app, a};
  app.begin(r.now);
  r.step(2);
  std::string script = a.keys.empty() ? demoScript(a.frames > 0 ? a.frames : 300) : a.keys;
  r.run(script);
  if (a.frames > 0) r.step(1);
  if (!a.shot.empty()) r.shot(a.shot);
  return 0;
}

// ---- interactive
static const char* kHelp =
    "Keys:  arrows = D-pad (left/right move, down soft drop, up hard drop)\n"
    "       X = A (rotate clockwise)    Z = B (rotate counter-clockwise)    Space = hard drop\n"
    "       Enter / Esc = MAIN (start, pause, resume)    C = hold piece (long press of MAIN)\n"
    "       R = restart    Q or close the window = quit\n";

static int runWindow(IApp& app, Args& a) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Window* win = SDL_CreateWindow(app.name(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, LW * a.scale,
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
  app.begin(now);
  uint32_t lastFrame = 0, fpsAt = now;
  int frames = 0;
  uint32_t releaseMainAt = 0;                         // pending end of the C "long press"
  bool running = true;
  bool first = true;
  while (running) {
    now = SDL_GetTicks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP) continue;
      bool down = e.type == SDL_KEYDOWN;
      if (down && e.key.repeat) continue;
      now = SDL_GetTicks();
      switch (e.key.keysym.sym) {
        case SDLK_LEFT: app.onKey(KEY_LEFT, down, now); break;
        case SDLK_RIGHT: app.onKey(KEY_RIGHT, down, now); break;
        case SDLK_DOWN: app.onKey(KEY_DOWN, down, now); break;
        case SDLK_UP:
        case SDLK_SPACE: app.onKey(KEY_UP, down, now); break;
        case SDLK_x: app.onKey(KEY_ACTION, down, now); break;
        case SDLK_z: app.onKey(KEY_B, down, now); break;
        case SDLK_RETURN:
        case SDLK_ESCAPE: app.onKey(KEY_MAIN, down, now); break;
        case SDLK_c:
          if (down && !releaseMainAt) {
            app.onKey(KEY_MAIN, true, now);
            releaseMainAt = now + TetrisApp::LONG_PRESS_MS + 60;
          }
          break;
        case SDLK_r:
          if (down) {
            app.end();
            app.begin(now);
            app.onKey(KEY_MAIN, true, now);
            app.onKey(KEY_MAIN, false, now);
          }
          break;
        case SDLK_q: running = false; break;
        default: break;
      }
    }
    now = SDL_GetTicks();
    if (releaseMainAt && (int32_t)(now - releaseMainAt) >= 0) {
      app.tick(now);
      app.onKey(KEY_MAIN, false, now);
      releaseMainAt = 0;
    }
    app.tick(now);
    if ((first || app.wantsFrame(now)) && now - lastFrame >= FRAME_MS) {
      first = false;
      renderFrame(app, now);
      toRgb(rgb);
      SDL_UpdateTexture(tex, nullptr, rgb.data(), LW * 4);
      SDL_RenderClear(ren);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
      lastFrame = now;
      frames++;
    }
    if (now - fpsAt >= 1000) {
      char t[64];
      snprintf(t, sizeof t, "%s  %d fps", app.name(), frames);
      SDL_SetWindowTitle(win, t);
      frames = 0;
      fpsAt = now;
    }
    SDL_Delay(2);
  }
  app.end();
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
    if (s == "--scale" && i + 1 < argc) a.scale = atoi(argv[++i]);
    else if (s == "--plain") a.plain = true;
    else if (s == "--frames" && i + 1 < argc) { a.frames = atoi(argv[++i]); a.headless = true; }
    else if (s == "--screenshot" && i + 1 < argc) { a.shot = argv[++i]; a.headless = true; }
    else if (s == "--keys" && i + 1 < argc) { a.keys = argv[++i]; a.headless = true; }
    else if (s == "--seed" && i + 1 < argc) { a.seed = (uint32_t)strtoul(argv[++i], nullptr, 10); a.seedGiven = true; }
    else if (s[0] != '-' && pos++ == 0) a.app = s;
    else { fprintf(stderr, "unknown argument %s\n", s.c_str()); return 2; }
  }
  if (a.scale < 1) a.scale = 1;
  if (a.plain) gLook = {0xFFFFFF, 0x000000};
  IApp* app = makeApp(a.app, a);
  if (!app) {
    fprintf(stderr, "unknown app '%s' (known: tetris)\n", a.app.c_str());
    return 2;
  }
  int rc = a.headless ? runHeadless(*app, a) : runWindow(*app, a);
  delete app;
  return rc;
}
