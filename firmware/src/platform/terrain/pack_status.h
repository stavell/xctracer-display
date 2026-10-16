#pragma once
// What the box's own screens (home status line, Settings > About) may ask about the installed map pack, without
// knowing the vario's terrain kit. TerrainKit implements it; an image or a box without a pack passes nullptr.
#include <stddef.h>
#include <stdint.h>

namespace terrain {

class IPackStatus {
 public:
  virtual ~IPackStatus() {}
  virtual bool packOk() const = 0;                                      // a pack is installed and passed its checks
  virtual uint32_t packBytes() const = 0;
  virtual void packArea(char* dst, size_t cap) const = 0;               // e.g. "41.2-44.2 N, 22.3-28.6 E"
  // Attribution string i of the pack (0 Copernicus, 1 OpenStreetMap with the (c) sign in UTF-8, 2 the same in ASCII).
  virtual bool packAttribution(int i, char* dst, size_t cap) const = 0;
};

}  // namespace terrain
