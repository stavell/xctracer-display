#pragma once
// Tests for platform/storage: flash header, frame protocol (with golden vectors from Python's zlib), the pack reader,
// and the loader state machine over an in-memory flash, including interrupted and damaged loads.
// Included by tests.cpp, after CHECK is defined.
#include <string>
#include <vector>
#include "flash_mock.h"
#include "platform/storage/flash_reader.h"
#include "platform/storage/pack_header.h"
#include "platform/storage/pack_loader.h"
#include "platform/storage/pack_proto.h"
#include "platform/terrain/crc32.h"
#include "platform/terrain/terrain.h"

namespace storage_tests {
using namespace storage;

static std::string hexOf(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}

// The host's side: frames in, one reply line out. Time advances 1 ms per service call.
struct Host {
  PackLoader& L;
  uint32_t now = 1000;
  explicit Host(PackLoader& l) : L(l) {}

  std::string sendRaw(const uint8_t* f, size_t n, int maxSteps = 200000) {
    std::string line;
    for (size_t i = 0; i < n; i++) {
      L.feedByte(f[i], now);
      if (L.reply()) { line = L.reply(); L.replyDone(); return line; }
    }
    for (int k = 0; k < maxSteps; k++) {
      now++;
      L.service(now);
      if (L.reply()) { line = L.reply(); L.replyDone(); return line; }
    }
    return "(no reply)";
  }
  std::string rpc(uint8_t cmd, const std::vector<uint8_t>& payload) {
    uint8_t f[kMaxPayload + 16];
    size_t n = encodeFrame(f, sizeof f, cmd, payload.data(), payload.size());
    return sendRaw(f, n);
  }
  std::string hello() { return rpc(CMD_HELLO, {'L', 'O', 'A', 'D', 'P', 'A', 'C', 'K'}); }
  static void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
  std::string erase(uint32_t len) { std::vector<uint8_t> p; put32(p, len); return rpc(CMD_ERASE, p); }
  std::string write(uint32_t off, const uint8_t* d, size_t n) {
    std::vector<uint8_t> p; put32(p, off);
    p.insert(p.end(), d, d + n);
    return rpc(CMD_WRITE, p);
  }
  std::string finish(uint32_t len, uint32_t crc) { std::vector<uint8_t> p; put32(p, len); put32(p, crc); return rpc(CMD_FINISH, p); }
  std::string status() { return rpc(CMD_STATUS, {}); }
  std::string exitMode(int reboot) { return rpc(CMD_EXIT, {(uint8_t)reboot}); }
  // A whole load as load_pack.py does it. Returns the reply of FINISH.
  std::string load(const std::vector<uint8_t>& pack, uint32_t chunk = 512, bool doFinish = true) {
    std::string r = hello();
    if (r.rfind("pk ok", 0) != 0) return r;
    r = erase((uint32_t)pack.size());
    if (r.rfind("pk ok", 0) != 0) return r;
    for (size_t off = 0; off < pack.size(); off += chunk) {
      size_t n = pack.size() - off < chunk ? pack.size() - off : chunk;
      r = write((uint32_t)off, pack.data() + off, n);
      if (r.rfind("pk ok", 0) != 0) return r;
    }
    if (!doFinish) return "(not finished)";
    return finish((uint32_t)pack.size(), terrain::crc32(0, pack.data(), pack.size()));
  }
};

static bool readFile(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize((size_t)n);
  bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

static void testHeader() {
  puts("storage: flash header");
  uint8_t b[kFlashHeaderBytes];
  FlashHeader h, o;
  h.packLen = 1720000; h.packCrc = 0xDEADBEEF;
  encodeHeader(b, h);
  CHECK(parseHeader(b, 2u << 20, &o) == HDR_OK);
  CHECK(o.packLen == 1720000 && o.packCrc == 0xDEADBEEFu);
  uint8_t blank[kFlashHeaderBytes];
  memset(blank, 0xFF, sizeof blank);
  CHECK(parseHeader(blank, 2u << 20, &o) == HDR_BLANK);
  uint8_t z[kFlashHeaderBytes] = {0};
  CHECK(parseHeader(z, 2u << 20, &o) == HDR_BAD);
  for (int i = 0; i < (int)kFlashHeaderBytes; i++) {      // every single flipped bit is caught
    uint8_t c[kFlashHeaderBytes];
    memcpy(c, b, sizeof c);
    c[i] ^= 0x10;
    CHECK(parseHeader(c, 2u << 20, &o) == HDR_BAD);
  }
  CHECK(parseHeader(b, 1u << 20, &o) == HDR_BAD);          // longer than the chip
  h.packLen = 100;                                         // shorter than a pack header
  encodeHeader(b, h);
  CHECK(parseHeader(b, 2u << 20, &o) == HDR_BAD);
  h.packLen = (2u << 20) - kPackBase;                      // exactly the whole chip behind the header sector
  encodeHeader(b, h);
  CHECK(parseHeader(b, 2u << 20, &o) == HDR_OK);
  h.packLen = (2u << 20) - kPackBase + 1;
  encodeHeader(b, h);
  CHECK(parseHeader(b, 2u << 20, &o) == HDR_BAD);
}

static void testProto() {
  puts("storage: frames");
  uint8_t f[64];
  const uint8_t pl[] = {4, 3, 2, 1, 'a', 'b'};
  size_t n = encodeFrame(f, sizeof f, CMD_WRITE, pl, sizeof pl);
  CHECK(hexOf(f, n) == "a55a03060004030201616204e14bb1");        // the same bytes Python's zlib gives (host/load_pack.py)
  n = encodeFrame(f, sizeof f, CMD_HELLO, (const uint8_t*)"LOADPACK", 8);
  CHECK(hexOf(f, n) == "a55a0108004c4f41445041434b21b7d79c");
  n = encodeFrame(f, sizeof f, CMD_STATUS, nullptr, 0);
  CHECK(hexOf(f, n) == "a55a050000f91b8af9");
  CHECK(encodeFrame(f, 8, CMD_STATUS, nullptr, 0) == 0);

  FrameParser p;
  uint32_t t = 0;
  auto feedAll = [&](const uint8_t* d, size_t k) {
    FrameParser::Result r = FrameParser::NONE;
    for (size_t i = 0; i < k; i++) { FrameParser::Result x = p.feed(d[i], t++); if (x != FrameParser::NONE) r = x; }
    return r;
  };
  n = encodeFrame(f, sizeof f, CMD_WRITE, pl, sizeof pl);
  CHECK(feedAll(f, n) == FrameParser::FRAME);
  CHECK(p.cmd() == CMD_WRITE && p.length() == sizeof pl && memcmp(p.payload(), pl, sizeof pl) == 0);
  uint8_t bad[64];
  memcpy(bad, f, n);
  bad[7] ^= 1;
  CHECK(feedAll(bad, n) == FrameParser::BAD);
  // text and line noise in front of a frame, even a lone sync byte, do not hide it
  const uint8_t noise[] = {'s', 't', ' ', 'o', 'k', '\n', 0xA5, 0xA5, 0x01, 0x00};
  CHECK(feedAll(noise, sizeof noise) == FrameParser::NONE);
  CHECK(feedAll(f, n) == FrameParser::FRAME);
  // a frame that stops half way is dropped after the gap and the next one is read
  for (size_t i = 0; i < 5; i++) p.feed(f[i], t++);
  t += kFrameGapMs + 10;
  FrameParser::Result r = FrameParser::NONE;
  for (size_t i = 0; i < n; i++) { FrameParser::Result x = p.feed(f[i], t++); if (x != FrameParser::NONE) r = x; }
  CHECK(r == FrameParser::FRAME);
  // an impossible length is refused at once
  const uint8_t huge[] = {0xA5, 0x5A, 3, 0xFF, 0xFF};
  CHECK(feedAll(huge, sizeof huge) == FrameParser::BAD);
}

static void testLoader() {
  puts("storage: loader");
  std::vector<uint8_t> pack;
  CHECK(readFile("fixtures/sopot.pack", pack));
  if (pack.empty()) return;
  const uint32_t crc = terrain::crc32(0, pack.data(), pack.size());

  // --- a clean load, then boot from the flash like the device does
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    CHECK(!L.active());
    std::string r = h.hello();
    CHECK(starts(r, "pk ok hello jedec=c84015 cap=2097152 base=4096 chunk=512"));
    CHECK(L.active());
    r = h.load(pack);
    CHECK(starts(r, "pk ok finish"));
    CHECK(!fl.programmedOverNonErased);
    CHECK(fl.blockErases + fl.sectorErases > 0);
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_OK && fh.packLen == pack.size() && fh.packCrc == crc);
    r = h.status();
    CHECK(starts(r, "pk ok status hdr=ok") && r.find("match=1") != std::string::npos);
    r = h.exitMode(1);
    CHECK(starts(r, "pk ok exit reboot=1"));
    CHECK(!L.active() && L.rebootRequested());

    FlashBlockReader rd(fl);
    rd.attach(fh.packLen);
    terrain::MemReader mem(pack.data(), (uint32_t)pack.size());
    terrain::FullTerrain ta, tb;
    CHECK(ta.open(&mem) == terrain::Terrain::OK);
    CHECK(tb.open(&rd) == terrain::Terrain::OK);
    CHECK(tb.verifyAll());
    const terrain::PackInfo& pi = ta.info();
    int agree = 0, tried = 0;
    for (int i = 0; i < 40; i++) {
      double lat = (pi.latS_e7 + (pi.latN_e7 - pi.latS_e7) * (0.3 + 0.01 * i)) / 1e7, lon = (pi.lonW_e7 + (pi.lonE_e7 - pi.lonW_e7) * (0.4 + 0.01 * i)) / 1e7;
      float a = 0, b = 0;
      bool oa = ta.elevation(lat, lon, &a), ob = tb.elevation(lat, lon, &b);
      tried++;
      if (oa == ob && (!oa || a == b)) agree++;
    }
    CHECK(agree == tried);
    // the reader exposes only the pack, not the header sector or what follows
    uint8_t x[8];
    CHECK(rd.read(0, x, 4) && memcmp(x, "BGMP", 4) == 0);
    CHECK(!rd.read((uint32_t)pack.size() - 2, x, 4));
    CHECK(rd.read((uint32_t)pack.size() - 4, x, 4));
    CHECK(!rd.read(0xFFFFFFF0u, x, 32));
    rd.setLocked(true);
    CHECK(!rd.read(0, x, 4));
    rd.setLocked(false);
    CHECK(rd.read(0, x, 4));
    // loading again over the old pack works (erase first) and the old one is gone at once
    CHECK(starts(h.hello(), "pk ok hello"));
    CHECK(starts(h.erase((uint32_t)pack.size()), "pk ok erase"));
    CHECK(readHeader(fl, &fh) == HDR_BLANK);
  }

  // --- an interrupted load leaves no pack
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    std::string r = h.load(pack, 512, false);
    CHECK(r == "(not finished)");
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_BLANK);
    // only half of the chunks
    MemFlash f2;
    PackLoader L2(f2);
    Host h2(L2);
    h2.hello();
    h2.erase((uint32_t)pack.size());
    for (size_t off = 0; off < pack.size() / 2; off += 512) h2.write((uint32_t)off, pack.data() + off, 512);
    CHECK(readHeader(f2, &fh) == HDR_BLANK);
    // the host disappears: load mode ends by itself with a reboot request
    CHECK(L2.active() && !L2.rebootRequested());
    h2.now += kLoaderIdleMs + 100;
    L2.service(h2.now);
    CHECK(!L2.active() && L2.rebootRequested());
  }

  // --- wrong CRC announced: the header is not written
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    h.hello();
    h.erase((uint32_t)pack.size());
    for (size_t off = 0; off < pack.size(); off += 512) h.write((uint32_t)off, pack.data() + off, std::min<size_t>(512, pack.size() - off));
    std::string r = h.finish((uint32_t)pack.size(), crc ^ 1);
    CHECK(starts(r, "pk err finish crc="));
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_BLANK);
    r = h.finish((uint32_t)pack.size(), crc);               // the right one afterwards still works
    CHECK(starts(r, "pk ok finish"));
    CHECK(readHeader(fl, &fh) == HDR_OK);
    r = h.finish((uint32_t)pack.size(), crc);               // a repeated FINISH (lost ack) is harmless
    CHECK(starts(r, "pk ok finish"));
  }

  // --- a pack with a missing chunk fails the stored-bytes check
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    h.hello();
    h.erase((uint32_t)pack.size());
    for (size_t off = 0; off < pack.size(); off += 512) {
      if (off == 4096) continue;
      h.write((uint32_t)off, pack.data() + off, std::min<size_t>(512, pack.size() - off));
    }
    CHECK(starts(h.finish((uint32_t)pack.size(), crc), "pk err finish crc="));
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_BLANK);
  }

  // --- something that is not a pack (right CRC, wrong content)
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    std::vector<uint8_t> junk(5000, 0x42);
    CHECK(starts(h.load(junk), "pk err finish pack header"));
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_BLANK);
  }

  // --- a damaged frame, a retried chunk, a flipped bit in the chip
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    h.hello();
    h.erase((uint32_t)pack.size());
    uint8_t f[kMaxPayload + 16];
    std::vector<uint8_t> p;
    Host::put32(p, 0);
    p.insert(p.end(), pack.begin(), pack.begin() + 512);
    size_t n = encodeFrame(f, sizeof f, CMD_WRITE, p.data(), p.size());
    f[100] ^= 0x20;
    CHECK(starts(h.sendRaw(f, n, 10), "pk err frame"));
    CHECK(fl.mem[kPackBase] == 0xFF);                       // nothing was written from the damaged frame
    CHECK(starts(h.write(0, pack.data(), 512), "pk ok w off=0 n=512"));
    CHECK(starts(h.write(0, pack.data(), 512), "pk ok w off=0 n=512"));   // the same chunk again (ack lost)
    CHECK(!fl.programmedOverNonErased);
    fl.flipAt = kPackBase + 1024 + 7;                       // a stuck bit: the read-back catches it
    CHECK(starts(h.write(1024, pack.data() + 1024, 512), "pk err write verify off=1024"));
    fl.flipAt = -1;
    // writes outside the announced length are refused
    CHECK(starts(h.write((uint32_t)pack.size() - 100, pack.data(), 200), "pk err write range"));
    CHECK(starts(h.write(0, pack.data(), 0), "pk err write size"));
  }

  // --- bad requests and a loader nobody asked for
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    std::string r = h.write(0, pack.data(), 16);             // before HELLO
    CHECK(starts(r, "pk err idle"));
    CHECK(!L.active());
    uint8_t f[64];
    size_t n = encodeFrame(f, sizeof f, CMD_HELLO, (const uint8_t*)"LOADPAC!", 8);   // a wrong magic never enters load mode
    h.sendRaw(f, n, 5);
    CHECK(!L.active());
    // text on the serial line (status lines, a terminal) is harmless
    const char* txt = "st ok=12 bad=0\nraw $XCTRC,1,2,3\n";
    for (const char* c = txt; *c; c++) L.feedByte((uint8_t)*c, h.now);
    CHECK(!L.active() && !L.reply());
    CHECK(starts(h.hello(), "pk ok hello"));
    CHECK(starts(h.write(0, pack.data(), 16), "pk err write not erased"));
    CHECK(starts(h.erase(10), "pk err erase length"));
    CHECK(starts(h.erase(2u << 20), "pk err erase length"));
    CHECK(starts(h.finish(1000, 0), "pk err finish not erased"));
    CHECK(starts(h.rpc(99, {}), "pk err cmd 99"));
    CHECK(starts(h.status(), "pk ok status hdr=blank"));
    CHECK(starts(h.exitMode(0), "pk ok exit reboot=0"));
    CHECK(!L.rebootRequested());
  }

  // --- erase: header sector first, 64 kB blocks where they fit, sectors elsewhere; chip failure is reported
  {
    MemFlash fl;
    for (auto& b : fl.mem) b = 0x00;                         // garbage everywhere
    PackLoader L(fl);
    Host h(L);
    h.hello();
    CHECK(starts(h.erase(300000), "pk ok erase end="));
    const uint32_t end = kPackBase + ((300000 + 4095) / 4096) * 4096;
    CHECK(fl.mem[0] == 0xFF && fl.mem[end - 1] == 0xFF && fl.mem[end] == 0x00);
    CHECK(end == 307200 && fl.blockErases == 4 && fl.sectorErases == 11);   // four blocks from 0 (header sector included), a sector tail
  }
  {
    MemFlash fl;
    fl.erasePolls = 1000000;                                 // an erase that never ends
    PackLoader L(fl);
    Host h(L);
    h.hello();
    std::vector<uint8_t> p;
    Host::put32(p, 5000);
    uint8_t f[32];
    size_t n = encodeFrame(f, sizeof f, CMD_ERASE, p.data(), p.size());
    std::string r = h.sendRaw(f, n, 40000);
    CHECK(starts(r, "pk err erase timeout"));
  }

  // --- the pack really fits: a pack as large as the chip allows
  {
    MemFlash fl;
    PackLoader L(fl);
    Host h(L);
    std::vector<uint8_t> big(fl.capacity() - kPackBase, 0);
    memcpy(big.data(), pack.data(), 192);
    uint32_t sz = (uint32_t)big.size();
    for (int i = 0; i < 4; i++) big[8 + i] = (uint8_t)(sz >> (8 * i));
    for (size_t i = 192; i < big.size(); i++) big[i] = (uint8_t)(i * 31 + (i >> 8));
    CHECK(starts(h.load(big, 512), "pk ok finish"));
    FlashHeader fh;
    CHECK(readHeader(fl, &fh) == HDR_OK && fh.packLen == sz);
  }

  // --- CrcScan matches a direct CRC and survives a read failure
  {
    MemFlash fl;
    for (size_t i = 0; i < 10000; i++) fl.mem[kPackBase + i] = (uint8_t)(i * 7);
    CrcScan s;
    s.begin(&fl, kPackBase, 10000);
    int steps = 0;
    while (!s.step(1000)) steps++;
    CHECK(steps == 9 && s.crc() == terrain::crc32(0, fl.mem.data() + kPackBase, 10000));
    fl.failRead = true;
    s.begin(&fl, kPackBase, 10000);
    CHECK(s.step(100) && s.failed());
  }
}

inline void run() {
  testHeader();
  testProto();
  testLoader();
}

}  // namespace storage_tests
