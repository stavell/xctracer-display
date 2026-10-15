#pragma once
// Arduino pin numbers on Feather nRF52840 Sense. Change wiring here only.
#define PIN_DISP_CS      1    // "RX" pad, P0.24 (display CS, active high)
#define PIN_DISP_EXTCOMIN 24  // "MI" pad, P0.15 (assumed; unverified)
// SCK = PIN_SPI_SCK (26), MOSI = PIN_SPI_MOSI (25): hardware SPI
// PIN_BUTTON1 (7) is the Feather user switch, active low
