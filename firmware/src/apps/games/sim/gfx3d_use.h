// The sim is built on platform/gfx3d (renderer, terrain, air, glider, world); inside namespace sim its names are used unqualified,
// and sim::World etc. still resolve (the emulator tests, SimBox).
#pragma once

#include "platform/gfx3d/sim_math.h"

namespace sim {
using namespace gfx3d;
}  // namespace sim
