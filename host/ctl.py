"""Control the box over BLE.
  python ctl.py filter 'XC*'     name filter for the vario scan list ('*' = everything)
  python ctl.py tz 180           local time offset from UTC, minutes
  python ctl.py sim on|off       built-in flight simulator (bench testing)
"""
import asyncio
import struct
import sys
from display_link import DisplayLink, CMD_UUID

CMD_SET_FILTER, CMD_SET_TZ, CMD_SIM = 0x10, 0x11, 0x12


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
    else:
        print(__doc__)
        return
    async with DisplayLink() as link:
        await link.client.write_gatt_char(CMD_UUID, data, response=True)
        print("sent", cmd, arg)

asyncio.run(main())
