// The box's USB serial loader over an in-memory flash, on stdin/stdout: the other end of host/load_pack.py for the
// end-to-end test (`build.sh loadtest`). Prints noise lines like the real status output, answers with the loader's own
// replies, and on EXIT checks what the "flash" holds.
#include <chrono>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include "flash_mock.h"
#include "platform/storage/pack_loader.h"
#include "platform/terrain/crc32.h"

static uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
  MemFlash flash;
  storage::PackLoader loader(flash);
  const bool damage = argc > 1 && std::string(argv[1]) == "--drop";   // lose every 50th chunk reply's frame: exercises the retries
  uint32_t lastNoise = 0;
  int frames = 0;
  setvbuf(stdout, nullptr, _IONBF, 0);
  for (;;) {
    pollfd p = {0, POLLIN, 0};
    int r = poll(&p, 1, 1);
    uint32_t now = nowMs();
    if (r > 0) {
      uint8_t buf[256];
      ssize_t n = read(0, buf, sizeof buf);
      if (n <= 0) break;
      for (ssize_t i = 0; i < n; i++) {
        if (damage && loader.active() && i == 20 && ++frames % 50 == 0) buf[i] ^= 0x55;   // a bit error on the wire
        loader.feedByte(buf[i], now);
        if (const char* rep = loader.reply()) { printf("%s\n", rep); loader.replyDone(); }
      }
    }
    loader.service(now);
    if (const char* rep = loader.reply()) { printf("%s\n", rep); loader.replyDone(); }
    if (now - lastNoise > 200) { lastNoise = now; printf("st ok=1 bad=0 fresh=1 fix=1 alt=1000\n"); }
    if (loader.rebootRequested()) break;
    if (!loader.active() && loader.chunksWritten() > 0) break;   // EXIT without reboot
  }
  storage::FlashHeader h;
  storage::HeaderStatus hs = storage::readHeader(flash, &h);
  uint32_t crc = hs == storage::HDR_OK ? terrain::crc32(0, flash.mem.data() + storage::kPackBase, h.packLen) : 0;
  fprintf(stderr, "SIM header=%s len=%u crc_ok=%d chunks=%u\n", storage::headerStatusName(hs), hs == storage::HDR_OK ? h.packLen : 0,
          hs == storage::HDR_OK && crc == h.packCrc, loader.chunksWritten());
  return 0;
}
