#pragma once
// Deterministic input generators for the fuzz tests and the soak: a fixed-seed PRNG (no rand(), so a
// run is identical on every platform) and $XCTRC line builders.
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
  uint64_t next() {                      // xorshift64*
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
  }
  uint32_t below(uint32_t n) { return n ? (uint32_t)((next() >> 32) % n) : 0; }
  int range(int lo, int hi) { return lo + (int)below((uint32_t)(hi - lo + 1)); }
  bool chance(int percent) { return (int)below(100) < percent; }
  double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// "$body*cs" with the right checksum.
inline std::string xctrcWithChecksum(const std::string& body) {
  uint8_t cs = 0;
  for (char c : body) cs ^= (uint8_t)c;
  char tail[8];
  snprintf(tail, sizeof tail, "*%02x", cs);
  return "$" + body + tail;
}

// The body (no '$', no checksum) of a tracer line, from explicit field text.
inline std::string xctrcBody(const std::string& hh, const std::string& mm, const std::string& ss,
                             const std::string& lat, const std::string& lon, const std::string& alt,
                             const std::string& spd, const std::string& crs, const std::string& vario,
                             const std::string& pres, const std::string& batt) {
  return "XCTRC,2026,8,16," + hh + "," + mm + "," + ss + ",0," + lat + "," + lon + "," + alt + "," + spd + "," +
         crs + "," + vario + ",,,," + pres + "," + batt;
}

inline std::string fnumStr(const char* fmt, double v) { char b[48]; snprintf(b, sizeof b, fmt, v); return b; }

// A plausible line for time-of-day `t` (seconds), position (lat, lon).
inline std::string xctrcValid(int t, double lat, double lon, double alt, double spd, double crs, double vario, bool fix = true) {
  t = ((t % 86400) + 86400) % 86400;
  return xctrcWithChecksum(xctrcBody(std::to_string(t / 3600), std::to_string(t / 60 % 60), std::to_string(t % 60),
      fix ? fnumStr("%.6f", lat) : "0.000000", fix ? fnumStr("%.6f", lon) : "0.000000", fnumStr("%.2f", alt),
      fnumStr("%.2f", fix ? spd : 0.0), fnumStr("%.1f", fix ? crs : 0.0), fnumStr("%.2f", vario), "967.62", "85"));
}

static const char* const kNasty[] = {"1e38", "-1e38", "1e-40", "-1e-40", "nan", "NaN", "-nan", "inf", "-inf", "Infinity", "0x1F",
                                     "0x7fffffff", "0xFFFFFFFFFFFFFFFF", "+", "-", "-.", ".", "e", "e5", "1e", "1e400", "-1e400",
                                     "99999999999999999999", "2147483648", "-2147483649", "4294967296", "-0", "+-1", "1,5",
                                     " ", "  7", "7  ", "1.2.3", "0.0000001", "340282346638528859811704183484516925440"};
static const int kNastyN = sizeof kNasty / sizeof *kNasty;

// A hostile line of the given kind (0..9), or a random kind when kind < 0.
inline std::string fuzzLine(Rng& r, int kind = -1) {
  if (kind < 0) kind = r.range(0, 9);
  std::string base = xctrcBody(std::to_string(r.range(0, 23)), std::to_string(r.range(0, 59)), std::to_string(r.range(0, 59)),
      fnumStr("%.6f", 43 + r.unit()), fnumStr("%.6f", 26 + r.unit()), fnumStr("%.2f", 1000 + r.unit() * 1000),
      fnumStr("%.2f", r.unit() * 60), fnumStr("%.1f", r.unit() * 360), fnumStr("%.2f", r.unit() * 6 - 3), "967.62", "85");
  std::string out;
  switch (kind) {
    case 0: {                                   // random bytes (no NUL: it is a C string)
      int n = r.range(0, 300);
      for (int i = 0; i < n; i++) out += (char)r.range(1, 255);
      break;
    }
    case 1: out = ""; break;
    case 2: {                                   // very long: valid prefix then padding, or just padding
      out = r.chance(50) ? "$" + base : "$XCTRC,";
      int n = r.range(150, 2000);
      while ((int)out.size() < n) out += r.chance(10) ? "," : "9";
      if (r.chance(50)) out += "*" + std::string(r.chance(50) ? "00" : "7c");
      break;
    }
    case 3: {                                   // missing / extra commas
      std::string b = base;
      int k = r.range(1, 4);
      while (k--) {
        std::vector<size_t> commas;
        for (size_t i = 0; i < b.size(); i++) if (b[i] == ',') commas.push_back(i);
        if (r.chance(50) && !commas.empty()) b.erase(commas[r.below((uint32_t)commas.size())], 1);
        else b.insert(r.below((uint32_t)b.size()), ",");
      }
      out = r.chance(80) ? xctrcWithChecksum(b) : "$" + b + "*00";
      break;
    }
    case 4: {                                   // one field replaced by a nasty value, checksum recomputed
      std::vector<std::string> f;
      size_t p = 0;
      for (;;) {
        size_t c = base.find(',', p);
        if (c == std::string::npos) { f.push_back(base.substr(p)); break; }
        f.push_back(base.substr(p, c - p));
        p = c + 1;
      }
      f[r.range(1, (int)f.size() - 1)] = kNasty[r.below(kNastyN)];
      std::string b;
      for (size_t i = 0; i < f.size(); i++) b += (i ? "," : "") + f[i];
      out = xctrcWithChecksum(b);
      break;
    }
    case 5: {                                   // valid line, one byte changed; checksum kept or recomputed
      std::string b = base;
      b[r.below((uint32_t)b.size())] = (char)r.range(1, 255);
      std::string good = xctrcWithChecksum(base);
      out = r.chance(50) ? xctrcWithChecksum(b) : "$" + b + good.substr(good.size() - 3);
      break;
    }
    case 6: {                                   // time fields out of range
      static const char* t[] = {"24", "25", "99", "-1", "60", "61", "100", "4294967296", "", "x", "1e3", "0x10"};
      out = xctrcWithChecksum(xctrcBody(t[r.below(12)], t[r.below(12)], t[r.below(12)], "43.2", "26.9", "1400", "30", "90", "1", "967", "85"));
      break;
    }
    case 7: {                                   // coordinates at the poles / date line / past them
      static const char* c[] = {"90.000000", "-90.000000", "90.000001", "180.000000", "-180.000000", "180.000001", "0.000000",
                                "360", "-360", "1e30", "nan", "inf", "89.999999", "0.000001"};
      out = xctrcWithChecksum(xctrcBody("12", "0", "0", c[r.below(14)], c[r.below(14)], "1400", "30", "90", "1", "967", "85"));
      break;
    }
    case 8: {                                   // truncated valid line
      std::string v = xctrcWithChecksum(base);
      out = v.substr(0, r.below((uint32_t)v.size() + 1));
      break;
    }
    default: {                                  // extreme numbers in every numeric field
      auto n = [&]() { return std::string(kNasty[r.below(kNastyN)]); };
      out = xctrcWithChecksum(xctrcBody("12", "30", "15", n(), n(), n(), n(), n(), n(), n(), n()));
      break;
    }
  }
  return out;
}
