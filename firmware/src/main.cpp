#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include "pins.h"

static const uint16_t W = 400, H = 240;
static const uint16_t BLACK = 0, WHITE = 1;
Adafruit_SharpMem display(&SPI, PIN_DISP_CS, W, H, 2000000);

SoftwareTimer vcomTimer;
static void vcomToggle(TimerHandle_t) {
  digitalToggle(PIN_DISP_EXTCOMIN);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_DISP_EXTCOMIN, OUTPUT);
  vcomTimer.begin(500, vcomToggle);  // 500 ms toggle = 1 Hz VCOM
  vcomTimer.start();

  display.begin();
  display.clearDisplay();
  display.setRotation(0);
  display.setTextColor(BLACK);
  display.setTextSize(3);
  display.setCursor(20, 20);
  display.print("Hello, display");
  display.drawRect(10, 80, 380, 150, BLACK);
  display.fillRect(30, 100, 120, 60, BLACK);
  display.setTextSize(2);
  display.setCursor(170, 120);
  display.print("400x240");
  display.refresh();
  Serial.println("ready");
}

void loop() {
  static bool last = true;
  bool now = digitalRead(PIN_BUTTON1);
  if (now != last) {
    delay(20);
    now = digitalRead(PIN_BUTTON1);
    if (now != last) {
      last = now;
      Serial.println(now ? "user switch: released" : "user switch: pressed");
    }
  }
  delay(5);
}
