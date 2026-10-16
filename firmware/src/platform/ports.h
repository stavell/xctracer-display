#pragma once
// The seams between an app and the generic hardware. The device implements these in platform/hw/,
// the PC emulator implements them with mocks in emu/. App-specific ports live with the app.
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>
#include "platform/gfx/bitmap1.h"
#include "platform/util/types.h"

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_HOLD };

// Physical inputs. Today only KEY_MAIN exists on the device (the Feather user switch); the others are the
// console's D-pad and its A / B buttons (KEY_ACTION is A), wired later through the table in
// platform/hw/input_button.h.
enum Key : uint8_t { KEY_MAIN, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ACTION, KEY_B, KEY_COUNT };

struct KeyEdge {                    // a debounced press (down) or release of one key, with the time it happened
  Key key;
  bool down;
  uint32_t ms;
};

class IPanel {
 public:
  virtual ~IPanel() {}
  virtual Adafruit_GFX& gfx() = 0;
  virtual Bitmap1 bitmap() = 0;     // the native buffer (400x240, stride 50) with the current rotation: for dithered grey
  virtual void setRotation(uint8_t r) = 0;
  virtual void clear() = 0;         // fast white fill of the whole canvas (never fillRect: it is per-pixel and slow)
  virtual void present() = 0;       // refresh the panel only if the picture changed
  virtual void invalidate() = 0;    // the next present() always refreshes
  virtual void refresh() = 0;       // unconditional refresh
  // Copy a full native-layout frame (400x240, 1 bpp, LSB = leftmost pixel, 1 = white) and refresh.
  virtual void showRaw(const uint8_t* frame) = 0;
};

class IInput {                      // the user switch (and, later, the front buttons)
 public:
  virtual ~IInput() {}
  virtual ButtonEvent poll(uint32_t nowMs) = 0;      // short / long / hold of the user switch (KEY_MAIN)
  virtual uint32_t heldMs(uint32_t nowMs) = 0;       // how long KEY_MAIN has been down, 0 when up
  // Next key edge in order, false when there is none. Edges are queued by the sampling timer, so a tap that
  // starts and ends inside one slow redraw is still seen. Inputs without edge support deliver none.
  virtual bool popEdge(KeyEdge& out) { (void)out; return false; }
};

// A few persistent numbers for the box itself, kept in the nRF's flash: the start-up app, game high scores and
// the settings every app shares (time zone, board mounting, magnetometer calibration; see
// platform/settings/box_settings.h, which gives them their meaning). Slots are small integers; an unset slot
// reads as 0.
enum StoreSlot : uint8_t {
  SLOT_BOOT_APP, SLOT_TETRIS_BEST,
  SLOT_MOUNT,                                  // board -> case rotation 0..23
  SLOT_TZ,                                     // minutes east of UTC, as int32
  SLOT_MAG_VALID, SLOT_MAG_X, SLOT_MAG_Y, SLOT_MAG_Z, SLOT_MAG_R,   // hard-iron offsets and radius (float bits)
  SLOT_MIGRATED,                               // 1 once the values older firmware kept elsewhere were imported
  SLOT_COUNT
};
class IKeyStore {
 public:
  virtual ~IKeyStore() {}
  virtual uint32_t get(StoreSlot slot) const = 0;
  virtual void set(StoreSlot slot, uint32_t value) = 0;   // persists at once; a no-op when unchanged
  // Several sets that belong together become one write: set() between these only remembers, the outermost
  // endBatch() persists. Batches nest.
  virtual void beginBatch() {}
  virtual void endBatch() {}
};

class IBattery {
 public:
  virtual ~IBattery() {}
  virtual void update(uint32_t nowMs) = 0;
  virtual int percent() = 0;
};

// Accelerometer + magnetometer + gyro in the BOARD frame (the chips' own axes after the fixed per-chip
// mapping in hw/); the app applies the case mounting (platform/sensors/mount.h). Powered down unless enabled.
class ICompass {
 public:
  virtual ~ICompass() {}
  virtual void enable(bool on) = 0;
  // true when a new reading is available. accel m/s^2, mag uT, gyro rad/s; haveGyro is false when the gyro is
  // absent or not yet running (gyro is then meaningless and the app uses the magnetic compass alone).
  virtual bool read(Vec3& accel, Vec3& mag, Vec3& gyro, bool& haveGyro) = 0;
};

// Air temperature sensor, always on (the profile builds during the whole flight).
class IThermo {
 public:
  virtual ~IThermo() {}
  virtual bool read(float& tempC, float& rhPct) = 0;   // true when a new reading is available; both values finite
};

class IPower {
 public:
  virtual ~IPower() {}
  virtual void shutdown() = 0;                         // does not return on the device
};
