"""Set the device-name filter used for the vario scan list. Usage: python set_filter.py 'XC*'"""
import asyncio
import sys
from display_link import DisplayLink, CMD_UUID

CMD_SET_FILTER = 0x10


async def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else "XC*"
    async with DisplayLink() as link:
        await link.client.write_gatt_char(CMD_UUID, bytes([CMD_SET_FILTER]) + pattern.encode()[:23], response=True)
        print(f"filter set to {pattern!r}")

asyncio.run(main())
