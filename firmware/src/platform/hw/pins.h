#pragma once
// Arduino pin numbers on Feather nRF52840 Sense. Change wiring here only.
#define PIN_DISP_CS      1    // "RX" pad, P0.24 (display CS, active high)
// EXTCOMIN is not wired (the MI pad is free): VCOM goes over SPI (SharpPanel::keepVcom)
// SCK = PIN_SPI_SCK (26), MOSI = PIN_SPI_MOSI (25): hardware SPI
// PIN_BUTTON1 (7) is the Feather user switch, active low
// Front buttons of the console case (D-pad and A / B): active low, internal pull-up. The GPIOs are not
// decided yet, so the keys are disabled: define a PIN_KEY_* (an Arduino pin number) to switch that key on.
// The table that maps pins to Key values is platform/hw/keymap.h.
// #define PIN_KEY_UP     <pin>
// #define PIN_KEY_DOWN   <pin>
// #define PIN_KEY_LEFT   <pin>
// #define PIN_KEY_RIGHT  <pin>
// #define PIN_KEY_A      <pin>
// #define PIN_KEY_B      <pin>
#define DISPLAY_ROTATION 1   // 1 = portrait, 3 = portrait rotated 180; 0/2 = landscape
