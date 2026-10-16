#pragma once
// Which GPIO is which front button. A key exists only when its PIN_KEY_* is defined in pins.h, so with the
// current wiring (none) the table holds just the end marker and the user switch is the only input.
#include <stdint.h>
#include "platform/hw/pins.h"
#include "platform/ports.h"

struct KeyPin {
  Key key;
  int16_t pin;
};

static const KeyPin KEY_PINS[] = {
#ifdef PIN_KEY_UP
    {KEY_UP, PIN_KEY_UP},
#endif
#ifdef PIN_KEY_DOWN
    {KEY_DOWN, PIN_KEY_DOWN},
#endif
#ifdef PIN_KEY_LEFT
    {KEY_LEFT, PIN_KEY_LEFT},
#endif
#ifdef PIN_KEY_RIGHT
    {KEY_RIGHT, PIN_KEY_RIGHT},
#endif
#ifdef PIN_KEY_A
    {KEY_ACTION, PIN_KEY_A},
#endif
#ifdef PIN_KEY_B
    {KEY_B, PIN_KEY_B},
#endif
    {KEY_COUNT, -1},   // end marker
};
static const int KEY_PIN_COUNT = (int)(sizeof KEY_PINS / sizeof KEY_PINS[0]) - 1;
