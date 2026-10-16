#pragma once
// Tests for gfx/atlas and gfx/font and the view's fast text path. Included inside namespace gt by
// gfx_tests.h (uses its Rng, Img, refSet, RefCanvas and the CHECK macro).

namespace {
struct PatCtx { const std::vector<uint8_t>* v; int w; };
bool patPx(void* c, int x, int y) { const PatCtx* p = (const PatCtx*)c; return (*p->v)[(size_t)y * p->w + x] != 0; }
}

// ---- Atlas: native-to-native draw against the per-pixel model ----
static void testAtlasBlit() {
  puts("atlas draw vs per-pixel reference");
  Rng r{31337};
  int bad = 0, badBuild = 0, same = 0, other = 0, inked = 0;
  for (int it = 0; it < 12000; it++) {
    int w = r.range(1, 70), h = r.range(1, 45), rot = r.range(0, 3);
    int density = r.range(0, 3);                              // 0: empty, 1: sparse, 2: half, 3: full
    std::vector<uint8_t> pat((size_t)w * h);
    for (auto& p : pat) p = density == 0 ? 0 : density == 3 ? 1 : (r.range(0, 99) < (density == 1 ? 15 : 50));
    std::vector<uint8_t> mem(Atlas::bytesFor(w, h, rot) + 2, 0xAA);      // guard bytes after the end
    mem.resize(mem.size());
    PatCtx pc{&pat, w};
    Atlas a;
    CHECK(a.build(mem.data(), w, h, rot, patPx, &pc));
    CHECK(mem[mem.size() - 1] == 0xAA && mem[mem.size() - 2] == 0xAA);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (a.get(x, y) != pat[(size_t)y * w + x]) badBuild++;
    int drot = r.range(0, 5) == 0 ? r.range(0, 3) : rot;
    (drot == rot ? same : other)++;
    Img d(r, r.range(1, 100), r.range(1, 70), r.range(0, 3), drot);
    Img d2(d);
    int dw = d.b.width(), dh = d.b.height();
    for (int k = 0; k < 4; k++) {
      int x = r.range(0, 12) == 0 ? r.range(-100000, 100000) : r.range(-w - 4, dw + 4);
      int y = r.range(0, 12) == 0 ? r.range(-100000, 100000) : r.range(-h - 4, dh + 4);
      DrawMode m = (DrawMode)r.range(0, 3);
      a.draw(d.b, x, y, m);
      DrawMode pm = m == DRAW_COPY ? DRAW_SET : m;
      for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
          if (pat[(size_t)j * w + i]) { refSet(d2.b, x + i, y + j, pm); inked++; }
      if (d.v != d2.v) { bad++; if (bad < 4) printf("  atlas mismatch it %d rot %d/%d x %d y %d m %d\n", it, rot, drot, x, y, (int)m); break; }
    }
  }
  CHECK(bad == 0);
  CHECK(badBuild == 0);
  CHECK(same > 8000 && other > 500 && inked > 100000);
  printf("  %d same-rotation, %d cross-rotation atlases\n", same, other);
}

// ---- Font1 against Adafruit_GFX with the same GFXfont ----
static const GFXfont* const TEST_FONTS[5] = {&FreeSans9pt7b, &FreeSansBold9pt7b, &FreeSansBold12pt7b,
                                             &FreeSansBold18pt7b, &FreeSansBold24pt7b};
static const char* const TEST_FONT_NAMES[5] = {"9", "9B", "12B", "18B", "24B"};

static void testFontGlyphs() {
  puts("font1 glyphs vs Adafruit_GFX drawChar: every font, char, rotation and mode");
  static RefCanvas ref;
  static std::vector<uint8_t> base(sizeof ref.buf), got(sizeof ref.buf);
  static Font1 ft;
  Rng r{2718};
  int cases = 0, bad = 0, inkSeen = 0;
  for (int fi = 0; fi < 5; fi++)
    for (int rot = 0; rot < 4; rot++) {
      CHECK(ft.build(TEST_FONTS[fi], rot));
      if (rot == 1) printf("  font %-3s rotation 1: %u bytes\n", TEST_FONT_NAMES[fi], (unsigned)ft.ramBytes());
      ref.setRotation((uint8_t)rot); ref.setFont(TEST_FONTS[fi]); ref.setTextSize(1);
      Bitmap1 bm{got.data(), 400, 240, 50, (uint8_t)rot};
      int lw = bm.width(), lh = bm.height();
      for (int c = 32; c <= 126; c++)
        for (int m = 0; m < 3; m++)
          for (int k = 0; k < 4; k++) {
            int x = r.range(-70, lw + 70), y = r.range(-70, lh + 70);
            if (k == 1) x = r.range(-50, 6);                     // left edge
            if (k == 2) x = r.range(lw - 50, lw + 8);            // right edge
            if (k == 3) y = r.range(0, 1) ? r.range(-60, 6) : r.range(lh - 12, lh + 60);   // top / bottom
            for (auto& b : base) b = (uint8_t)r.next();
            got = base;
            if (m == DRAW_INVERT) {                              // GFX has no XOR: take its ink, flip with it
              memset(ref.buf, 0, sizeof ref.buf);
              ref.drawChar((int16_t)x, (int16_t)y, (unsigned char)c, 1, 0, 1);
              for (size_t i = 0; i < sizeof ref.buf; i++) ref.buf[i] ^= base[i];
            } else {
              memcpy(ref.buf, base.data(), sizeof ref.buf);
              ref.drawChar((int16_t)x, (int16_t)y, (unsigned char)c, m == DRAW_SET ? 1 : 0, 0, 1);
            }
            char s[2] = {(char)c, 0};
            int end = drawText(bm, x, y, s, ft, (DrawMode)m);
            cases++;
            if (memcmp(ref.buf, got.data(), sizeof ref.buf) != 0 || end != x + ft.glyph((unsigned char)c)->xa) {
              bad++;
              if (bad < 6) printf("  glyph mismatch font %s rot %d char %d mode %d at %d,%d\n", TEST_FONT_NAMES[fi], rot, c, m, x, y);
            }
            if (m == DRAW_SET && base != got) inkSeen++;
          }
    }
  CHECK(bad == 0);
  CHECK(cases == 5 * 4 * 95 * 3 * 4);
  CHECK(inkSeen > 2000);
  printf("  %d SET cases changed the buffer\n", inkSeen);
  printf("  %d glyph cases\n", cases);
}

static void testFontStrings() {
  puts("font1 strings vs Adafruit_GFX print / getTextBounds (wrapping, newlines, cursor)");
  static RefCanvas ref;
  static std::vector<uint8_t> base(sizeof ref.buf), got(sizeof ref.buf);
  static Font1 ft;
  Rng r{161803};
  int cases = 0, bad = 0, badBounds = 0, wrapped = 0;
  for (int fi = 0; fi < 5; fi++)
    for (int rot = 0; rot < 4; rot++) {
      CHECK(ft.build(TEST_FONTS[fi], rot));
      ref.setRotation((uint8_t)rot); ref.setFont(TEST_FONTS[fi]); ref.setTextSize(1);
      Bitmap1 bm{got.data(), 400, 240, 50, (uint8_t)rot};
      int lw = bm.width(), lh = bm.height();
      for (int it = 0; it < 150; it++) {
        char s[16];
        int n = r.range(0, 12);
        for (int i = 0; i < n; i++) s[i] = r.range(0, 24) == 0 ? '\n' : r.range(0, 30) == 0 ? '\r' : (char)r.range(32, 126);
        s[n] = 0;
        int x = r.range(0, 2) == 0 ? r.range(-30, lw) : r.range(lw / 2, lw - 5), y = r.range(-30, lh + 30);
        int m = r.range(0, 1);                                   // SET or CLEAR
        for (auto& b : base) b = (uint8_t)r.next();
        got = base;
        memcpy(ref.buf, base.data(), sizeof ref.buf);
        ref.setTextColor(m == DRAW_SET ? 1 : 0);
        ref.setCursor((int16_t)x, (int16_t)y);
        ref.print(s);
        int end = drawText(bm, x, y, s, ft, (DrawMode)m, lw);
        cases++;
        if (memcmp(ref.buf, got.data(), sizeof ref.buf) != 0 || end != ref.getCursorX()) {
          bad++;
          if (bad < 6) printf("  string mismatch font %s rot %d \"%s\" at %d,%d end %d vs %d\n", TEST_FONT_NAMES[fi], rot, s, x, y, end, ref.getCursorX());
        }
        if (n > 0 && ref.getCursorY() != y) wrapped++;
        int16_t gx, gy; uint16_t gw, gh; int bx, by, bw, bh;
        ref.getTextBounds(s, 0, 0, &gx, &gy, &gw, &gh);
        textBounds(s, ft, bx, by, bw, bh, lw);
        if (gx != bx || gy != by || gw != bw || gh != bh || textWidth(s, ft, lw) != (int)gw) {
          badBounds++;
          if (badBounds < 6) printf("  bounds mismatch font %s \"%s\": gfx %d,%d %ux%u ours %d,%d %dx%d\n", TEST_FONT_NAMES[fi], s, gx, gy, gw, gh, bx, by, bw, bh);
        }
      }
    }
  CHECK(bad == 0);
  CHECK(badBounds == 0);
  CHECK(cases == 5 * 4 * 150);
  CHECK(wrapped > 100);
  printf("  %d string cases (%d wrapped or had a newline)\n", cases, wrapped);
}

// Every screen of the view, fast text path against the same screen through Adafruit_GFX text.
static void testScreensFastText() {
  puts("screens: fast text path identical to GFX text path");
  static ViewModel vm;
  static ListModel lm;
  static FastSharp fast(nullptr, 0, 400, 240, 0);
  static std::vector<uint8_t> a(sizeof(RefCanvas::buf));
  const char* names[] = {"compass", "thermal", "standalone", "list", "list_empty", "profile", "profile_short", "profile_empty",
                         "home", "home_unreachable", "message", "message2", "status", "banner"};
  int frames = 0, bad = 0;
  for (int rot = 0; rot < 4; rot++) {
    fast.setRotation((uint8_t)rot);
    Bitmap1 bm = fast.bm();
    for (const char* nm : names) {
      std::string s = nm;
      vm = ViewModel();
      vm.fresh = true; vm.link = ViewModel::LINK_CONNECTED; vm.hasSaved = true;
      strcpy(vm.device, "XC-Tracer");
      vm.s.fix = true; vm.s.alt = 1413; vm.s.vario = 2.4f; vm.s.speedKmh = 41; vm.s.course = 120; vm.s.battPct = 85;
      vm.avgVario = 1.8f; vm.glide = 9.4f;
      vm.wind.valid = true; vm.wind.speedKmh = 14; vm.wind.toDeg = 262; vm.wind.ageS = 12;
      vm.localMinutes = 13 * 60 + 52; vm.localBattPct = 76;
      vm.heading = 120; vm.headingSrc = HDG_GPS;
      vm.trailCount = 80;
      for (int i = 0; i < vm.trailCount; i++) { vm.trail[i].x = 40 * sinf(i * 0.3f); vm.trail[i].y = 40 * cosf(i * 0.3f); vm.trail[i].vario = 1.5f; }
      vm.markCount = 1; vm.marks[0].x = 30; vm.marks[0].y = 20; vm.marks[0].avgClimb = 2.1f;
      uint32_t held = 0;
      if (s == "thermal") vm.page = ViewModel::PAGE_THERMAL;
      if (s == "standalone" || s == "status") {
        vm.fresh = false; vm.link = ViewModel::LINK_SEARCHING;
        vm.heading = 123; vm.headingSrc = HDG_MAG; vm.ambientC = 21.4f; vm.humidityPct = 48;
        if (s == "status") vm.link = ViewModel::LINK_CONNECTED;
      }
      if (s == "banner") held = 2400;
      if (s.rfind("profile", 0) == 0) {
        vm.page = ViewModel::PAGE_PROFILE;
        int cnt = s == "profile" ? 12 : s == "profile_short" ? 4 : 0;
        for (int i = 0; i < cnt; i++) {
          vm.profile[i].altM = 950 + 100.0f * i; vm.profile[i].speedKmh = 8 + 2.5f * i; vm.profile[i].toDeg = 250 + 5.0f * i;
          vm.profile[i].ageS = 30 + 100 * i; vm.profile[i].samples = 20;
          // Landscape with temperatures divides by zero in drawWindProfile (areaH / 16 rows): not a
          // layout the device uses, so those frames carry no temperatures.
          vm.profile[i].tempC = (rot & 1) ? 18.0f - 0.65f * i : NAN; vm.profile[i].tempSamples = (rot & 1) ? 12 : 0;
        }
        vm.profileCount = cnt;
        vm.s.alt = s == "profile_short" ? 1500 : 1530; vm.profileAlt = vm.s.alt;
      }
      if (s.rfind("home", 0) == 0) {
        vm.page = ViewModel::PAGE_HOME;
        vm.homeValid = true; vm.homeDist = 3400; vm.homeBearing = 200; vm.homeRelBearing = 80; vm.homeGlideReq = 6.2f;
        vm.homeArrival = 350; vm.homeReachable = true;
        if (s == "home_unreachable") { vm.homeDist = 12800; vm.homeGlideReq = 18.4f; vm.homeArrival = NAN; vm.homeReachable = false; }
      }
      lm = ListModel();
      if (s == "list") {
        lm.count = 4; lm.selected = 3;
        strcpy(lm.items[0].name, "XC-Tracer"); lm.items[0].rssi = -52; strcpy(lm.items[0].id, "D6:75");
        strcpy(lm.items[1].name, "XC-Tracer Mini III long"); lm.items[1].rssi = -83; strcpy(lm.items[1].id, "A1:0C");
        strcpy(lm.items[2].name, "Q-Series Soundbar"); lm.items[2].rssi = -78; strcpy(lm.items[2].id, "00:00");
        strcpy(lm.items[3].name, "Wjgpqy"); lm.items[3].rssi = -91; strcpy(lm.items[3].id, "FF:EE");
      }
      for (int pass = 0; pass < 2; pass++) {
        viewFastText(pass == 1);
        fast.clearDisplay();
        if (s == "list" || s == "list_empty") drawList(fast, lm, vm, held, &bm);
        else if (s == "message") drawMessage(fast, "Powering off", nullptr, &bm);
        else if (s == "message2") drawMessage(fast, "Searching", "XC-Tracer 0123", &bm);
        else drawLive(fast, vm, held, &bm);
        if (pass == 0) memcpy(a.data(), fast.getBuffer(), a.size());
      }
      viewFastText(true);
      frames++;
      int white = 0;
      for (size_t i = 0; i < a.size(); i++) white += __builtin_popcount(a[i]);
      CHECK(white > 1000 && white < 400 * 240 - 300);
      if (memcmp(a.data(), fast.getBuffer(), a.size()) != 0) { bad++; printf("  screen differs: %s rot %d\n", nm, rot); }
    }
  }
  CHECK(bad == 0);
  CHECK(viewFontRam() > 10000);
  printf("  %d frames compared, sheets hold %u bytes\n", frames, (unsigned)viewFontRam());
}
