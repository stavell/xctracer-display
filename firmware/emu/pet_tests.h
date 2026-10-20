#pragma once
// The pet snapshot (platform/pet): strict decoder (every reject case), the mailbox (latest wins, keeps the last good), the laptop
// link's opcode dispatch with the vario's commands as one consumer, the status the box reports back. Included by tests.cpp.
#include "apps/vario/app/command_queue.h"
#include "pet_fixture.h"
#include "platform/link/opcode_dispatch.h"
#include "platform/pet/pet_status.h"

namespace pet_t {

using namespace petfx;

static std::vector<uint8_t> good(uint8_t seq = 1) { return oneOfEach(seq); }

static void decoder() {
  puts("pet: snapshot decoder accepts the good one, rejects every bad case");
  pet::Snapshot s;
  std::vector<uint8_t> g = good(7);
  CHECK(g.size() == 80);
  CHECK(pet::decode(g.data(), g.size(), s));
  CHECK(s.seq == 7 && s.n == 6 && s.bowl == 40 && s.slot[0].state == pet::S_PERMISSION && strcmp(s.slot[0].label, "WIDG") == 0);
  CHECK(s.slot[1].label[3] == ' ' && s.slot[4].subagents == 2 && s.slot[4].tool == pet::T_EDIT && s.slot[2].minutes == 20);
  CHECK(pet::decode(good().data(), 8 + 12 * 6, s));

  pet::Snapshot keep;
  keep.seq = 99;
  auto bad = [&](std::vector<uint8_t> b) { pet::Snapshot t = keep; const bool ok = pet::decode(b.data(), b.size(), t); return !ok && t.seq == 99; };   // rejected, out untouched
  { auto b = g; b[0] = 0x31; CHECK(bad(b)); }                       // wrong opcode
  { auto b = g; b[1] = 0; CHECK(bad(b)); }                          // wrong version
  { auto b = g; b[1] = 2; CHECK(bad(b)); }
  { auto b = g; b.pop_back(); CHECK(bad(b)); }                      // shorter than n says
  { auto b = g; b.push_back(0); CHECK(bad(b)); }                    // longer than n says
  { auto b = g; b[7] = 5; CHECK(bad(b)); }                          // n smaller than the bytes
  { auto b = g; b[7] = 7; CHECK(bad(b)); }                          // n above 6
  { auto b = g; b[7] = 255; CHECK(bad(b)); }
  { auto b = std::vector<uint8_t>(g.begin(), g.begin() + 7); CHECK(bad(b)); }   // no room for the header
  CHECK(bad({}));
  { auto b = g; b[8] = 6; CHECK(bad(b)); }                          // slot index above 5
  { auto b = g; b[8 + 12] = 0; CHECK(bad(b)); }                     // slot index used twice
  { auto b = g; b[9] = 6; CHECK(bad(b)); }                          // unknown state
  { auto b = g; b[9 + 12 * 3] = 200; CHECK(bad(b)); }
  { auto b = g; b[10] = 8; CHECK(bad(b)); }                         // unknown tool category
  { auto b = g; b[11] = 16; CHECK(bad(b)); }                        // subagents above 15
  { auto b = g; b[14] = 101; CHECK(bad(b)); }                       // context use 101..254
  { auto b = g; b[14] = 255; pet::Snapshot t; CHECK(pet::decode(b.data(), b.size(), t) && t.slot[0].context == 255); }   // 255 = unknown
  { auto b = g; b[4] = 101; CHECK(bad(b)); }                        // bowl above 100
  { auto b = g; b[5] = 100; CHECK(bad(b)); }                        // hidden above 99
  { auto b = g; b[6] = 100; CHECK(bad(b)); }
  { auto b = g; b[16] = 0x07; CHECK(bad(b)); }                      // a control character in a label
  { auto b = g; b[17] = 0x80; CHECK(bad(b)); }
  // fewer slots are fine
  { auto b = make(3, 0, 0, 0, 0, {}); pet::Snapshot t; CHECK(pet::decode(b.data(), b.size(), t) && t.n == 0); }
  { auto b = make(3, 3, 100, 99, 99, {{5, pet::S_DONE, pet::T_OTHER, 15, 200, 200, 100, 255, "A"}}); pet::Snapshot t;
    CHECK(pet::decode(b.data(), b.size(), t) && t.n == 1 && t.slot[0].index == 5 && t.paused() && t.bowlIsLimit() && strcmp(t.slot[0].label, "A   ") == 0); }
}

static void mailbox() {
  puts("pet: mailbox keeps the last good snapshot, latest write wins");
  pet::Mailbox m;
  pet::Snapshot s;
  uint32_t seq = 77;
  CHECK(!m.latest(s, seq) && seq == 0 && !m.everGood() && m.lastWriteMs() == 0);
  auto a = good(1);
  m.put(a.data(), a.size(), 1000);
  CHECK(m.latest(s, seq) && seq == 1 && s.seq == 1 && m.lastWriteMs() == 1000);
  uint32_t seq2 = 0;
  CHECK(m.latest(s, seq2) && seq2 == 1);                            // no new write: the same seq
  // two writes before a read: the newest wins, the older is never seen
  auto b = good(2), c = good(3);
  m.put(b.data(), b.size(), 2000);
  m.put(c.data(), c.size(), 2500);
  CHECK(m.latest(s, seq) && s.seq == 3 && seq == 2 && m.lastWriteMs() == 2500);
  // a bad write keeps the last good one, does not refresh the freshness, counts as rejected
  auto bad = good(4);
  bad[1] = 9;
  m.put(bad.data(), bad.size(), 3000);
  CHECK(m.latest(s, seq) && s.seq == 3 && seq == 2 && m.lastWriteMs() == 2500 && m.rejected() == 1);
  uint8_t big[200] = {0x30, 1};
  m.put(big, sizeof big, 3100);                                     // longer than any snapshot
  CHECK(m.latest(s, seq) && s.seq == 3 && m.rejected() == 2);
  m.put(a.data(), 0, 3200);                                         // an empty write
  CHECK(m.latest(s, seq) && s.seq == 3 && m.rejected() == 3);
  auto d = good(5);
  m.put(d.data(), d.size(), 4000);
  CHECK(m.latest(s, seq) && s.seq == 5 && seq == 3 && m.lastWriteMs() == 4000);
  // the seq byte wrapping is the daemon's business: the mailbox seq counts snapshots
  auto w = good(0);
  m.put(w.data(), w.size(), 5000);
  CHECK(m.latest(s, seq) && s.seq == 0 && seq == 4);
}

struct Count : IPacketSink {
  int n = 0;
  uint8_t last = 0;
  uint32_t at = 0;
  void onPacket(const uint8_t* d, uint16_t, uint32_t now) override { n++; last = d[0]; at = now; }
};

static void dispatch() {
  puts("pet: opcode dispatch, the vario's commands and the pet feed as consumers");
  OpcodeDispatch od;
  Count draw, other;
  CommandQueue cmds;
  pet::Feed feed;
  CHECK(od.add(0x01, 0x04, draw));
  CHECK(od.add(0x10, 0x16, cmds));
  CHECK(od.add(0x70, 0x71, cmds));
  CHECK(od.add(pet::OP_SNAPSHOT, pet::OP_SNAPSHOT, feed));
  CHECK(!od.add(0x14, 0x20, other));                                // overlaps 0x10..0x16
  CHECK(!od.add(0x30, 0x30, other));                                // taken
  CHECK(!od.add(0x50, 0x40, other));                                // empty range
  CHECK(od.add(0x40, 0x4f, other));
  const uint8_t clear[] = {0x01}, flush[] = {0x04}, page[] = {0x14, 2}, tz[] = {0x11, 0x2c, 0x01}, sim[] = {0x12, 1}, unknown[] = {0x55, 1};
  CHECK(od.dispatch(clear, 1, 10) && draw.n == 1 && draw.at == 10);
  CHECK(od.dispatch(flush, 1, 11) && draw.n == 2 && draw.last == 0x04);
  Command c;
  CHECK(od.dispatch(page, 2, 12) && cmds.pop(c) && c.type == Command::PAGE && c.value == 2);
  CHECK(od.dispatch(tz, 3, 12) && cmds.pop(c) && c.type == Command::TZ && c.value == 300);
  CHECK(od.dispatch(sim, 2, 12) && cmds.pop(c) && c.type == Command::SIM && c.value == 1);
  // the vario decoder still drops what it dropped (a bad page, a short TZ) and the queue stays empty
  const uint8_t badPage[] = {0x14, 99}, shortTz[] = {0x11, 1};
  CHECK(od.dispatch(badPage, 2, 13) && !cmds.pop(c));
  CHECK(od.dispatch(shortTz, 2, 13) && !cmds.pop(c));
  CHECK(!od.dispatch(unknown, 2, 14) && !od.dispatch(unknown, 0, 14) && draw.n == 2 && other.n == 0);
  // a text line, truncated to the Command, exactly as before the dispatch existed
  std::vector<uint8_t> xc = {0x13};
  for (char ch : std::string("$XCTRC,test*00")) xc.push_back((uint8_t)ch);
  CHECK(od.dispatch(xc.data(), xc.size(), 15) && cmds.pop(c) && c.type == Command::XCTRC && strcmp(c.text, "$XCTRC,test*00") == 0);
  // the pet snapshot goes to the feed and to nobody else
  auto snap = good(9);
  CHECK(od.dispatch(snap.data(), snap.size(), 5000));
  pet::Snapshot s;
  uint32_t seq;
  CHECK(feed.latest(s, seq) && s.seq == 9 && feed.lastWriteMs() == 5000 && !cmds.pop(c) && draw.n == 2);
  // snapshots flood while the vario's queue is full of nothing: the vario ring cannot fill up with them
  for (int i = 0; i < 20; i++) { auto b = good((uint8_t)i); od.dispatch(b.data(), b.size(), 6000 + i); }
  CHECK(!cmds.pop(c) && feed.latest(s, seq) && s.seq == 19);
}

static void status() {
  puts("pet: the status the box reports back");
  pet::Status st;
  st.shownValid = true; st.shownSeq = 42; st.petActive = true; st.battPct = 87; st.battMv = 3987; st.appId = 7;
  uint8_t b[pet::STATUS_BYTES];
  pet::encode(st, b);
  CHECK(b[0] == 1 && b[1] == 42 && b[2] == 3 && b[3] == 87 && b[4] == (3987 & 255) && b[5] == (3987 >> 8) && b[6] == 7 && b[7] == 0);
  pet::Status u;
  pet::encode(u, b);
  CHECK(b[1] == 0 && b[2] == 0 && b[3] == 255 && b[4] == 0 && b[5] == 0 && b[6] == 255);
  st.battPct = 250;
  pet::encode(st, b);
  CHECK(b[3] == 100);

  pet::Reporter r;
  CHECK(!r.due(0, false, st));                                      // nothing without a connection
  CHECK(r.due(100, true, st));                                      // a new connection sends at once
  CHECK(!r.due(200, true, st) && !r.due(29000, true, st));
  CHECK(r.due(30100, true, st));                                    // every 30 s
  CHECK(!r.due(30200, true, st));
  pet::Status mv = st; mv.battMv += 20;
  CHECK(!r.due(31000, true, mv));                                   // millivolt jitter alone is not a change
  pet::Status sh = st; sh.shownSeq = 43;
  CHECK(r.due(32000, true, sh));                                    // a new snapshot shown
  pet::Status ap = sh; ap.appId = 0; ap.petActive = false;
  CHECK(r.due(33000, true, ap));                                    // the active app changed
  CHECK(!r.due(34000, false, ap));                                  // disconnect ...
  CHECK(r.due(35000, true, ap));                                    // ... and a reconnect sends again
}

static void run() {
  decoder();
  mailbox();
  dispatch();
  status();
}

}  // namespace pet_t
