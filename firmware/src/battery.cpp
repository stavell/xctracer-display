#include "battery.h"
#include <Arduino.h>

static float volts = 0;
static int pct = 0;

static int toPercent(float v) {
  static const float tv[] = {3.30f, 3.60f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
  static const int tp[] = {0, 5, 20, 35, 50, 65, 80, 92, 100};
  if (v <= tv[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v <= tv[i]) return tp[i - 1] + (int)((v - tv[i - 1]) / (tv[i] - tv[i - 1]) * (tp[i] - tp[i - 1]));
  return 100;
}

void batteryUpdate() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (last && now - last < 5000) return;
  last = now;
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) raw += analogRead(PIN_VBAT);
  volts = (raw / 8.0f) * 0.73242188f * 2.0f / 1000.0f;   // 3.0 V / 4096, 2:1 divider
  pct = toPercent(volts);
}

int batteryPct() { return pct; }
float batteryVolts() { return volts; }
