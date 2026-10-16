#pragma once
#include <bluefruit.h>
#include "apps/vario/hw/laptop_ble.h"
#include "apps/vario/hw/tracer_ble.h"
#include "platform/hw/power_nrf.h"

// The vario's radios for PowerNrf::shutdown(): the laptop link (peripheral) and the tracer link (central).
class VarioRadios : public IRadios {
 public:
  VarioRadios(LaptopBle& laptop, TracerBle& tracer) : laptop_(laptop), tracer_(tracer) {}
  void disconnect() override {
    laptop_.disconnect();
    tracer_.disconnect();
  }
  void stop() override {
    laptop_.stopAdvertising();
    Bluefruit.Scanner.stop();
  }

 private:
  LaptopBle& laptop_;
  TracerBle& tracer_;
};
