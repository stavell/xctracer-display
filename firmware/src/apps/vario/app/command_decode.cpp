#include "apps/vario/app/command_decode.h"
#include <string.h>
#include "platform/sensors/mount.h"
#include "apps/vario/hw/protocol.h"
#include "apps/vario/view/viewmodel.h"

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
    case CMD_SET_ORIENT:
      if (len < 2 || !mountValid(d[1])) return false;
      c.type = Command::ORIENT;
      c.value = d[1];
      return true;
    case CMD_PAGE:
      if (len < 2) return false;
      c.type = Command::PAGE;
      if (d[1] >= ViewModel::PAGE_COUNT) return false;     // unknown page: ignore the packet
      c.value = d[1];
      return true;
#ifdef DIAGNOSTICS
    case CMD_DIAG_HANG:
      c.type = Command::DIAG_HANG;
      return true;
    case CMD_DIAG_OFF:
      c.type = Command::DIAG_OFF;
      return true;
#endif
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
