#pragma once
// The Agents app (apps/pet) in a bare AppHost with the system sheet over mock ports; snapshots go in through the real mailbox.
// Shared by pet_tests.h and pet_shots.h.
#include "apps/pet/pet_app.h"
#include "mocks.h"
#include "pet_fixture.h"
#include "platform/ui/system_sheet.h"

struct PetRig {
  Rig r;
  pet_app::PetApp app{r.pets};
  SystemSheet sheet;
  AppHost host{r.panel, r.input};
  uint32_t now = 1000;
  PetRig() {
    host.add(app);
    host.setSystemSheet(&r.power, &sheet);
    host.start(0, now);
  }
  void run(uint32_t ms) {                       // the main loop: one host iteration every 10 ms
    for (uint32_t e = now + ms; now < e; now += 10) host.tick(now);
  }
  void send(const std::vector<uint8_t>& b) { r.pets.onPacket(b.data(), (uint16_t)b.size(), now); }
};
