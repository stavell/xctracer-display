#pragma once
// Placeholder art of the Agents app: pets built from 1-bit and gray shapes, no sprites (docs/claude_pet.md section 10, step 2). Pure
// drawing onto a white canvas; every function gets the cell's local origin and the animation clock, nothing is stored.
// 8 species with distinct silhouettes (blob, box, triangle, bean, diamond, frog, cat, owl), 4 variants (none, hat, scarf, glasses).
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/pet/pet_snapshot.h"

namespace pet_art {

const int CELL = 120;

struct Look {
  uint8_t species = 0, variant = 0, state = pet::S_IDLE, tool = pet::T_THINK;
  uint8_t context = pet::CONTEXT_UNKNOWN;
  uint8_t fat = 0;            // 0..2: rounder as the bowl fills
  uint8_t escalation = 0;     // 0 < 2 min in the state, 1 from 2 min (higher jump), 2 from 10 min (holds a sign)
  uint8_t followers = 0;      // subagents
  bool asleep = false;        // the herd with no link: drawn as IDLE whatever the state
};

// One cell's pet at (ox, oy) = the cell's top-left, at animation time t (ms).
void drawPet(const Bitmap1& bm, int ox, int oy, const Look& l, uint32_t t);
// The egg, tMs 0..1999 since the slot appeared; the pet pops out at the end.
void drawHatch(const Bitmap1& bm, int ox, int oy, const Look& l, uint32_t tMs);
// The pet waving goodbye, tMs 0..1999 since the slot disappeared.
void drawWave(const Bitmap1& bm, int ox, int oy, const Look& l, uint32_t tMs);
// Up to 4 followers along the cell's bottom corners, a "+" instead of the 4th when more.
void drawFollowers(const Bitmap1& bm, int ox, int oy, int n, uint32_t t, bool walking);

// Top-bar pieces.
void drawBowl(const Bitmap1& bm, int x, int y, int pct, bool isLimit);   // 36 x 28 at (x, y)
void drawUnplugged(const Bitmap1& bm, int x, int y);                       // 30 x 28 at (x, y)

}  // namespace pet_art
