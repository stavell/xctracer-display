"""Control the box over BLE.
  python ctl.py filter 'XC*'     name filter for the vario scan list ('*' = everything)
  python ctl.py tz 180           local time offset from UTC, minutes
  python ctl.py sim on|off       built-in flight simulator (bench testing)
  python ctl.py page compass|thermal   switch the page
  python ctl.py cal start|save|cancel  magnetometer calibration
  python ctl.py diag hang|off    diagnostics build only: hang the loop (watchdog test) / power off
"""
import asyncio
import struct
import sys
from display_link import DisplayLink, CMD_UUID

CMD_SET_FILTER, CMD_SET_TZ, CMD_SIM, CMD_PAGE, CMD_CAL = 0x10, 0x11, 0x12, 0x14, 0x15
CMD_DIAG_HANG, CMD_DIAG_OFF = 0x70, 0x71


async def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd, arg = sys.argv[1], sys.argv[2]
    if cmd == "filter":
        data = bytes([CMD_SET_FILTER]) + arg.encode()[:23]
    elif cmd == "tz":
        data = bytes([CMD_SET_TZ]) + struct.pack("<h", int(arg))
    elif cmd == "sim":
        data = bytes([CMD_SIM, 1 if arg == "on" else 0])
    elif cmd == "cal":
        data = bytes([CMD_CAL, {"start": 1, "save": 2, "cancel": 0}[arg]])
    elif cmd == "page":
        data = bytes([CMD_PAGE, 1 if arg == "thermal" else 0])
    elif cmd == "diag" and arg in ("hang", "off"):
        data = bytes([CMD_DIAG_HANG if arg == "hang" else CMD_DIAG_OFF])
    else:
        print(__doc__)
        return
    async with DisplayLink() as link:
        await link.client.write_gatt_char(CMD_UUID, data, response=True)
        print("sent", cmd, arg)

asyncio.run(main())
