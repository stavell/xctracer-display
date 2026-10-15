#include "app/command_decode.h"
#include <string.h>
#include "protocol.h"

bool decodeCommand(const uint8_t* d, uint16_t len, Command& c) {
  if (len == 0) return false;
  switch (d[0]) {
    case CMD_SET_TZ:
      if (len < 3) return false;
      c.type = Command::TZ;
      c.value = (int16_t)(d[1] | (d[2] << 8));
      return true;
    case CMD_SIM:
      if (len < 2) return false;
      c.type = Command::SIM;
      c.value = d[1] != 0;
      return true;
    case CMD_CAL:
      if (len < 2) return false;
      c.type = Command::CAL;
      c.value = d[1];
      return true;
    case CMD_PAGE:
      if (len < 2) return false;
      c.type = Command::PAGE;
      c.value = d[1] != 0;
      return true;
    case CMD_XCTRC:
    case CMD_SET_FILTER: {
      size_t n = (size_t)(len - 1) < sizeof(c.text) - 1 ? (size_t)(len - 1) : sizeof(c.text) - 1;
      c.type = d[0] == CMD_XCTRC ? Command::XCTRC : Command::FILTER;
      memcpy(c.text, d + 1, n);
      c.text[n] = 0;
      return true;
    }
  }
  return false;
}
