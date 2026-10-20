#pragma once
// Renders of the Agents app: out/pet_*.pgm (and .png from build.sh). One screen per state, tool animations, the full herd, followers,
// hidden sessions, the unplugged herd, the egg and the goodbye. Included by emu_main.cpp.
#include <string>
#include "pet_rig.h"

namespace petshots {

using petfx::SlotSpec;
using namespace pet;

static void dump(PetRig& t, const char* dir, const char* name) {
  t.r.panel.dump((std::string(dir) + "/pet_" + name + ".pgm").c_str());
}

// Six slots in one state, species 0..5 with the four variants spread over them.
static std::vector<SlotSpec> allIn(uint8_t state, uint8_t tool = T_THINK, uint8_t minutes = 0) {
  static const char* labels[6] = {"WIDG", "API", "DOCS", "WEB", "CORE", "ML"};
  std::vector<SlotSpec> v;
  for (int i = 0; i < 6; i++) v.push_back({(uint8_t)i, state, tool, (uint8_t)(i == 1 ? 2 : 0), (uint8_t)i, (uint8_t)(i % 4), (uint8_t)(i * 20), minutes, labels[i]});
  return v;
}

// Shows the app at time `at` after the snapshot arrived (the last frame drawn before that).
static void shot(const char* dir, const char* name, const std::vector<uint8_t>& snap, uint32_t at, uint32_t fresh = 0) {
  static PetRig* t = nullptr;
  delete t;
  t = new PetRig();
  t->run(100);
  t->send(snap);
  t->run(at);
  (void)fresh;
  dump(*t, dir, name);
}

static void shots(const char* dir) {
  using petfx::make;
  // one screen per state; the waiting ones twice (cell inverted / not, a jump at its peak)
  shot(dir, "permission_a", make(1, 0, 35, 0, 0, allIn(S_PERMISSION)), 300 + 3 * 167);          // 0.8 s: peak jump, inverted at 0..0.5 s of each second
  shot(dir, "permission_b", make(1, 0, 35, 0, 0, allIn(S_PERMISSION)), 5000 + 2 * 167);
  shot(dir, "question", make(1, 0, 35, 0, 0, allIn(S_QUESTION)), 1670 + 2 * 167);
  shot(dir, "failed", make(1, 0, 35, 0, 0, allIn(S_FAILED)), 1000);
  shot(dir, "done", make(1, 0, 35, 0, 0, allIn(S_DONE)), 3000);
  shot(dir, "idle", make(1, 0, 35, 0, 0, allIn(S_IDLE)), 6000);
  // the working tools, two screens (species 6 and 7 appear on the second)
  {
    auto a = allIn(S_WORKING), b = allIn(S_WORKING);
    for (int i = 0; i < 6; i++) { a[i].tool = (uint8_t)i; b[i].tool = (uint8_t)(i == 0 ? T_MCP : i == 1 ? T_OTHER : i); b[i].species = (uint8_t)(i + 2 > 7 ? 7 : i + 2); }
    b[0].species = 6; b[1].species = 7; b[2].tool = T_READ; b[3].tool = T_EDIT; b[4].tool = T_SHELL; b[5].tool = T_WEB;
    shot(dir, "working_a", make(1, 0, 35, 0, 0, a), 1000 + 1500);
    shot(dir, "working_b", make(1, 0, 35, 0, 0, b), 1000 + 1000);
  }
  // the herd: mixed states, a long wait with a sign, followers, a full bowl
  {
    std::vector<SlotSpec> h = {{0, S_PERMISSION, 0, 0, 0, 1, 30, 12, "WIDG"}, {1, S_WORKING, T_EDIT, 3, 1, 0, 60, 5, "API"}, {2, S_QUESTION, 0, 0, 6, 2, 90, 3, "DOCS"},
                               {3, S_DONE, 0, 0, 3, 3, 70, 2, "WEB"}, {4, S_WORKING, T_SHELL, 1, 7, 0, 45, 5, "CORE"}, {5, S_FAILED, 0, 0, 5, 1, 255, 20, "ML"}};
    shot(dir, "herd", make(1, 0, 80, 0, 0, h), 1000 + 4 * 167);
  }
  // followers: 1 to 6 subagents, 5 and 6 show 4 and a "+"
  {
    auto v = allIn(S_WORKING, T_AGENT);
    for (int i = 0; i < 6; i++) v[i].sub = (uint8_t)(i + 1);
    shot(dir, "followers", make(1, 0, 20, 0, 0, v), 1000);
  }
  // hidden sessions: "+3", two of them waiting (inverted in the top bar); the bowl is a limit
  shot(dir, "hidden", make(1, 1, 55, 3, 2, allIn(S_WORKING, T_READ)), 1000);
  // the daemon stopped: asleep herd and the unplugged sign (30 s without a snapshot)
  shot(dir, "unplugged", make(1, 0, 35, 0, 0, allIn(S_PERMISSION)), 31000);
  // the egg (0.9 s: cracked) and the goodbye
  {
    static PetRig* t = nullptr;
    delete t;
    t = new PetRig();
    t->run(100);
    t->send(make(1, 0, 35, 0, 0, {{0, S_DONE, 0, 0, 0, 0, 50, 1, "WIDG"}}));
    t->run(100);
    t->send(make(2, 0, 35, 0, 0, {{0, S_DONE, 0, 0, 0, 0, 50, 1, "WIDG"}, {1, S_DONE, 0, 0, 1, 1, 50, 1, "API"}, {2, S_DONE, 0, 0, 2, 2, 50, 1, "DOCS"}}));
    t->run(700);
    dump(*t, dir, "hatch_a");
    t->run(400);
    dump(*t, dir, "hatch_b");
    t->run(2000);
    t->send(make(3, 0, 35, 0, 0, {{0, S_DONE, 0, 0, 0, 0, 50, 1, "WIDG"}}));
    t->run(500);
    dump(*t, dir, "wave");
  }
}

}  // namespace petshots
