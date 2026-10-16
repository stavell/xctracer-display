// Thermal map motion: interpolation between tracer samples, snapping, replay speed, cadence, zoom glide.
#pragma once

namespace mt {

// A straight flight north at `mps` (ground) with the data time running `speed` times faster than the wall
// clock: one sample every 1000/(3*speed) wall ms. Feeds MapMotion the way App::feedFlight does.
struct Feeder {
  MapMotion mm;
  FlightModel fm;
  double speed = 1, mps = 10;
  double lat0 = 43.0, lon0 = 26.0;
  double t = 0;                 // wall ms of the next sample
  int k = 0;                    // sample number
  double ownY = 0;              // northing of the newest sample, m from the first
  void feedUntil(uint32_t nowMs, double jitterMs = 0) {
    while (t <= nowMs) {
      double dataSec = k / 3.0;
      Sample s;
      s.fix = true; s.lat = lat0 + mps * dataSec / 111320.0; s.lon = lon0; s.course = 0; s.speedKmh = (float)(mps * 3.6);
      s.vario = 0; s.alt = 1000;
      int sec = (int)dataSec;
      s.hh = 10; s.mm = (uint8_t)((sec / 60) % 60); s.ss = (uint8_t)(sec % 60);
      uint32_t arr = (uint32_t)(t + jitterMs * sin(k * 2.1));
      bool ns = fm.feed(s);
      mm.feed(arr, s, ns, fm.clockMs());
      ownY = (s.lat - 43.0) * 111320.0;
      k++;
      t = k * 1000.0 / (3.0 * speed);
    }
  }
};

static void run() {
  puts("thermal map motion");
  // 1. interpolated centre at 30 fps: continuous, no jump, never past the newest sample
  for (double speed : {1.0, 3.0}) {
    Feeder f; f.speed = speed;
    float prev = NAN, maxStep = 0, sum = 0; int cnt = 0;
    for (uint32_t now = 0; now < 20000; now += 33) {
      f.feedUntil(now);
      MapView v = f.mm.view(now, 0, 0, 10);
      if (!v.valid) continue;
      CHECK(v.ay <= f.ownY + 0.01);                      // never ahead of the newest measured position
      if (now > 4000) {
        if (isfinite(prev)) { float st = v.ay - prev; maxStep = fmaxf(maxStep, st); sum += st; cnt++; }
        prev = v.ay;
      }
    }
    float avg = sum / cnt, expect = (float)(f.mps * speed * 0.033);   // metres of data motion per 33 ms frame
    printf("  speed %.0fx, 10 m/s: %.3f m per 33 ms frame (a sample step is %.2f m), largest %.3f m (%.2fx), delay %u ms, rate %.2f\n",
           speed, avg, f.mps / 3.0, maxStep, maxStep / avg, (unsigned)f.mm.delayMs(), f.mm.dataRate());
    NEAR(avg, expect, expect * 0.05);
    CHECK(maxStep < 1.5f * avg);
    NEAR(f.mm.dataRate(), speed, 0.15 * speed);
  }
  // 1b. with arrival jitter (+-25 ms) and a wind the frame-fixed centre still moves smoothly
  { Feeder f; f.mps = 10;
    float prevx = NAN, prevy = NAN, maxStep = 0; double sum = 0; int cnt = 0;
    for (uint32_t now = 0; now < 20000; now += 33) {
      f.feedUntil(now, 25);
      MapView v = f.mm.view(now, 3.0f, 4.0f, 10);
      if (now > 4000 && isfinite(prevx)) { float st = hypotf(v.ax - prevx, v.ay - prevy); maxStep = fmaxf(maxStep, st); sum += st; cnt++; }
      prevx = v.ax; prevy = v.ay;
    }
    printf("  jitter 25 ms + wind: mean step %.3f m, largest %.3f m\n", sum / cnt, maxStep);
    CHECK(maxStep < 2.0f * sum / cnt); }
  // 2. holds when the samples stop, never runs past the last one; comes back without a jump
  { Feeder f;
    uint32_t now = 0;
    for (; now < 6000; now += 33) { f.feedUntil(now); f.mm.view(now, 0, 0, 10); }
    double lastY = f.ownY;
    float prev = -1e9f; bool mono = true;
    for (uint32_t e = now + 5000; now < e; now += 33) { MapView v = f.mm.view(now, 0, 0, 10); CHECK(v.ay <= lastY + 0.01); if (v.ay < prev - 1e-3f) mono = false; prev = v.ay; }
    CHECK(mono);
    NEAR(prev, lastY, 0.01);                       // arrived at the newest sample and holds there
    MapView a = f.mm.view(now, 0, 0, 10), b = f.mm.view(now + 1000, 0, 0, 10);
    CHECK(a.ay == b.ay && a.ax == b.ax);
    // data comes back after a 1.5 s pause: a glide, not a jump
    // (a fresh Feeder, 1.5 s without samples after a steady run, then the samples return)
    Feeder g; now = 0;
    for (; now < 6000; now += 33) { g.feedUntil(now); g.mm.view(now, 0, 0, 10); }
    g.k += 4; g.t = g.k * 1000.0 / 3.0;           // the samples of the next 1.3 s never arrived; the aircraft kept flying
    now += 1500;
    g.feedUntil(now);
    float last = g.mm.view(now, 0, 0, 10).ay, maxJ = 0;
    for (uint32_t n2 = now + 33; n2 < now + 2500; n2 += 33) { g.feedUntil(n2); float y = g.mm.view(n2, 0, 0, 10).ay; maxJ = fmaxf(maxJ, y - last); last = y; }
    printf("  after a 1.5 s pause: largest frame step %.2f m (normal 0.33 m)\n", maxJ);
    CHECK(maxJ < 1.5f); }
  // 3. snaps on a gap above 2 s and on a teleport
  { Feeder f;
    uint32_t now = 0;
    for (; now < 6000; now += 33) { f.feedUntil(now); f.mm.view(now, 0, 0, 10); }
    f.k += 7; f.t = f.k * 1000.0 / 3.0;           // 2.3 s of silence
    now += 2600;
    f.feedUntil(now);
    MapView v = f.mm.view(now, 0, 0, 10);
    NEAR(v.ay, f.ownY, 0.01);                     // at the newest sample at once
    now += 333;
    f.lat0 += 0.02;                               // 2 km sideways within one sample
    f.feedUntil(now);
    MapView w = f.mm.view(now, 0, 0, 10);
    CHECK(isfinite(w.ay));
    MapView w2 = f.mm.view(now + 33, 0, 0, 10);
    CHECK(fabsf(w2.ay - w.ay) < 1.0f);            // no glide across the jump: it sits at the newest sample
  }
  // 4. no position: invalid view
  { Feeder f; uint32_t now = 0;
    for (; now < 3000; now += 33) { f.feedUntil(now); f.mm.view(now, 0, 0, 10); }
    Sample s; s.fix = false;
    f.mm.feed(now, s, true, 5000);
    CHECK(!f.mm.view(now, 0, 0, 10).valid); }
  // 5. heading follows the path, on the shortest arc through north
  { MapMotion mm; mm.reset();
    float prevH = NAN, maxStep = 0;
    uint32_t arr = 0; double lat = 43, lon = 26;
    for (int k = 0; k < 90; k++) {                 // a turn from 350 deg through north to 10 deg
      double crs = 350;
      if (k >= 30 && k <= 70) crs = 350 + (k - 30) * 0.5;
      if (k > 70) crs = 370;
      lat += 10.0 * cos(crs * D2R) / 3.0 / 111320.0; lon += 10.0 * sin(crs * D2R) / 3.0 / (111320.0 * cos(43 * D2R));
      Sample s; s.fix = true; s.lat = lat; s.lon = lon; s.course = (float)fmod(crs, 360); s.hh = 10; s.mm = 0; s.ss = (uint8_t)(k / 3);
      mm.feed(arr, s, k % 3 == 0, (k / 3) * 1000u);
      for (int fr = 0; fr < 10; fr++) {
        MapView v = mm.view(arr + fr * 33, 0, 0, 5);
        if (k > 12 && isfinite(v.hdgRad)) {
          float h = v.hdgRad * 57.29578f;
          if (isfinite(prevH)) { float d = fabsf(fmodf(h - prevH + 540, 360) - 180); maxStep = fmaxf(maxStep, d); }
          prevH = h;
        }
      }
      arr += 333;
    }
    printf("  heading through north: largest step %.2f deg per frame\n", maxStep);
    CHECK(maxStep < 3.0f); }
}

// The app: the thermal page at 30 fps while flying, slow when the data stops, same at replay speed 3,
// the line ending at the arrow, the zoom gliding.
struct Flight {
  Rig r;
  Driven app;
  uint32_t now = 0;
  double speed, mps;
  double t = 0; int k = 0;
  double x = 0, y = 0, hdg = 0;   // metres from the start, heading in degrees
  double turn = 0;                // degrees per second
  Flight(double sp, double ms, int page = 1)
      : app(r.panel, r.input, r.battery, r.tracer, r.laptop, r.settings, r.compass, r.thermo, r.power), speed(sp), mps(ms) {
    app.begin(0);
    Command pc; pc.type = Command::PAGE; pc.value = page; r.laptop.cmds.push_back(pc);
  }
  void run(uint32_t ms, bool feed = true, double accel = 0) {
    for (uint32_t e = now + ms; now < e; now += 10) {
      while (feed && t <= now) {
        double ds = k / 3.0;
        double v = mps + accel * ds;
        x += v / 3.0 * sin(hdg * D2R); y += v / 3.0 * cos(hdg * D2R); hdg += turn / 3.0;
        int sec = (int)ds;
        char b[200];
        snprintf(b, sizeof b, "XCTRC,2026,8,16,10,%d,%d,0,%.7f,%.7f,1387.00,%.2f,%.1f,0.1,,,,967.62,85",
                 (10 + sec / 60) % 60, sec % 60, 43.0 + y / 111320.0, 26.9279670 + x / (111320.0 * cos(43 * D2R)), v * 3.6, fmod(hdg + 720, 360));
        r.laptop.inject(line(b));
        k++;
        t = k * 1000.0 / (3.0 * speed);
      }
      app.tick(now);
    }
  }
  int presentsIn(uint32_t ms, bool feed = true) { int b = r.panel.presents; run(ms, feed); return r.panel.presents - b; }
};

static void runApp() {
  puts("thermal map in the app");
  for (double speed : {1.0, 3.0}) {
    Flight f(speed, 10);
    f.run(40000);                                   // a full 30 s of track on the map: the zoom has settled
    int fast = f.presentsIn(5000);
    printf("  speed %.0fx, straight at 10 m/s: %.1f draws per s (a draw per half pixel of map motion)\n", speed, fast / 5.0);
    CHECK(fast >= 5 * 5 && fast <= 5 * 31);
    const ViewModel& vm = f.app.buildViewModel(f.now);
    CHECK(vm.mapLive && vm.page == ViewModel::PAGE_THERMAL);
    CHECK(isfinite(vm.mapCx) && isfinite(vm.mapCy) && isfinite(vm.mapHdg) && isfinite(vm.mapScale));
    // the line ends at or behind the arrow: the centre is ahead of the last kept trail point along the track
    if (vm.trailCount >= 1) {
      const AirPoint& lp = vm.trail[vm.trailCount - 1];
      float ahead = (vm.mapCx - lp.x) * sinf(vm.mapHdg) + (vm.mapCy - lp.y) * cosf(vm.mapHdg);
      CHECK(ahead >= -0.05f);
      CHECK(ahead < 12.0f);
    }
    // data frozen: once the view has caught up, the slow cadence
    f.run(1500, false);
    int slow = f.presentsIn(10000, false);
    printf("  speed %.0fx, data frozen: %.1f draws per s\n", speed, slow / 10.0);
    CHECK(slow >= 20 && slow <= 40);
    CHECK(fast > 1.5 * slow / 2);
  }
  // circling (18 deg/s): the map turns fast, so it draws at the animation cadence
  for (double speed : {1.0, 3.0}) {
    Flight f(speed, 10); f.turn = 18;
    f.run(8000);
    int n = f.presentsIn(5000);
    printf("  speed %.0fx, circling 18 deg/s: %.1f draws per s (10 ms ticks, 33 ms asks for a tick every 40 ms)\n", speed, n / 5.0);
    CHECK(n >= 5 * 22 && n <= 5 * 31);
  }
  // other pages are unchanged: the compass page with the same data keeps the slow cadence
  { Flight f(1, 10, 0);
    f.run(8000);
    int n = f.presentsIn(5000);
    printf("  compass page, same data: %.1f draws per s\n", n / 5.0);
    CHECK(n >= 12 && n <= 20); }
  // zoom glide: speeding up forces the fixed zoom steps down; the shown scale glides
  { Flight f(1, 5);
    f.run(3000);
    float first = f.app.buildViewModel(f.now).mapScale;
    int steps = 0; float prev = first, maxRel = 0;
    for (int i = 0; i < 1500; i++) {                // 50 s at 33 ms
      f.run(30, true, 0.7);
      float s = f.app.buildViewModel(f.now).mapScale;
      if (s != prev) { steps++; maxRel = fmaxf(maxRel, fabsf(s - prev) / prev); }
      prev = s;
    }
    printf("  zoom %.2f -> %.2f px/m while accelerating: %d changing frames, largest frame change %.1f %%\n", first, prev, steps, maxRel * 100);
    CHECK(prev < first * 0.7f);
    CHECK(maxRel < 0.12f);
    CHECK(steps > 10); }
  // page entry: the zoom starts at its step, no glide
  { Flight f(1, 10, 0);
    f.run(8000);
    { Command pc; pc.type = Command::PAGE; pc.value = 1; f.r.laptop.cmds.push_back(pc); }
    f.run(100);
    const ViewModel& vm = f.app.buildViewModel(f.now);
    int mw, mh; thermalMapSize(240, 400, mw, mh);
    CHECK(vm.mapScale == thermalMapScale(vm, mw, mh)); }
}

}  // namespace mt
