#pragma once
// Decodes the non-drawing laptop packets (protocol.h) into a Command. Pure: no hardware, no queues.
#include <stdint.h>
#include "app/ports.h"

// True when d is a complete TZ, SIM, CAL, PAGE, XCTRC or FILTER packet. False for the draw commands
// (CLEAR, BLIT, DATA, FLUSH), unknown bytes and packets that are too short. Text is truncated to fit
// Command::text and always NUL-terminated.
bool decodeCommand(const uint8_t* d, uint16_t len, Command& out);
