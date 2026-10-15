#pragma once
// The seams between the app logic and the hardware. The device implements these in hw/,
// the PC emulator implements them with mocks in emu/.
#include <Adafruit_GFX.h>
#include <stddef.h>
#include <stdint.h>
#include "core/types.h"
#include "gfx/bitmap1.h"
#include "view/viewmodel.h"

enum ButtonEvent { BTN_NONE, BTN_SHORT, BTN_LONG, BTN_HOLD };

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

class IInput {                      // the single user switch
 public:
  virtual ~IInput() {}
  virtual ButtonEvent poll(uint32_t nowMs) = 0;
  virtual uint32_t heldMs(uint32_t nowMs) = 0;
};

class IBattery {
 public:
  virtual ~IBattery() {}
  virtual void update(uint32_t nowMs) = 0;
  virtual int percent() = 0;
};

// BLE central: finds the vario by name filter, remembers it, delivers its raw text lines.
class ITracer {
 public:
  virtual ~ITracer() {}
  virtual void update(uint32_t nowMs) = 0;
  virtual ViewModel::Link link() const = 0;
  virtual bool hasSaved() const = 0;
  virtual const char* deviceName() const = 0;
  virtual const char* filter() const = 0;
  virtual void setFilter(const char* glob) = 0;
  virtual void listOpen() = 0;                         // start an active scan
  virtual void listClose() = 0;
  virtual int listCount() = 0;
  virtual bool listItem(int i, ListItem& out) = 0;     // strongest signal first
  virtual bool listConnect(int i) = 0;                 // connect and remember
  virtual bool popLine(char* buf, size_t n) = 0;       // next raw line received, without CR/LF
  virtual void describe(char* buf, size_t n) const = 0;  // which device is saved / connected (diagnostics)
};

// Commands from the laptop (BLE peripheral). Heavy parsing stays out of the BLE callbacks:
// they only copy the text into a queue, the app thread does the rest.
struct Command {
  enum Type { NONE, SIM, TZ, FILTER, XCTRC, PAGE, CAL, DIAG_HANG, DIAG_OFF } type = NONE;   // CAL: 1 start, 2 save, 0 cancel
  int value = 0;
  char text[200] = "";
};

class ILaptop {
 public:
  virtual ~ILaptop() {}
  virtual bool pop(Command& out) = 0;
  virtual bool hostDrawing(uint32_t nowMs) = 0;        // the laptop drew recently: it owns the screen
  virtual void service(uint32_t nowMs) = 0;            // e.g. perform a pending host refresh
};

class ISettings {
 public:
  virtual ~ISettings() {}
  virtual int tzMinutes() const = 0;
  virtual void setTzMinutes(int m) = 0;
  virtual bool loadMagCal(MagCal& out) = 0;          // false if none is stored
  virtual void saveMagCal(const MagCal& cal) = 0;
};

// Accelerometer + magnetometer, already mapped to the device frame (x right of the screen, y top of the
// screen, z out of the screen). Powered down unless enabled.
class ICompass {
 public:
  virtual ~ICompass() {}
  virtual void enable(bool on) = 0;
  virtual bool read(Vec3& accel, Vec3& mag) = 0;     // true when a new reading pair is available
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
