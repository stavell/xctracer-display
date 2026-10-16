#pragma once
// The seams between the vario app and its hardware: the generic ones are in platform/ports.h, these
// belong to the flight display (tracer link, laptop link, saved settings).
#include "apps/vario/core/types.h"
#include "apps/vario/view/viewmodel.h"
#include "platform/ports.h"

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
  // The vario app is left for another app (box image): stop scanning, drop the link, discard queued lines.
  // resume() starts looking for the saved tracer again; it does nothing unless pause() ran.
  virtual void pause() = 0;
  virtual void resume() = 0;
};

// Commands from the laptop (BLE peripheral). Heavy parsing stays out of the BLE callbacks:
// they only copy the text into a queue, the app thread does the rest.
struct Command {
  enum Type { NONE, SIM, TZ, FILTER, XCTRC, PAGE, CAL, ORIENT, DIAG_HANG, DIAG_OFF } type = NONE;   // CAL: 1 start, 2 save, 0 cancel; ORIENT: mounting index 0..23
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
  virtual int mountIndex() const = 0;                // board -> case rotation, 0..23 (platform/sensors/mount.h); 0 if none stored
  virtual void setMountIndex(int index) = 0;
};
