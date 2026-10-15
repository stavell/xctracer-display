"""BLE client for the SharpDisplay terminal. Protocol: firmware/src/protocol.h"""
import asyncio
import struct
from bleak import BleakClient, BleakScanner
from PIL import Image

SERVICE_UUID = "d15a0001-5b7a-4c3e-9f10-2a6e8c4b7d01"
CMD_UUID = "d15a0002-5b7a-4c3e-9f10-2a6e8c4b7d01"
W, H = 400, 240
CLEAR, BLIT, DATA, FLUSH = 1, 2, 3, 4


def pack_1bpp(img: Image.Image) -> bytes:
    """1 bpp, MSB-first, rows padded to bytes, 1 = black."""
    img = img.convert("1")
    stride = (img.width + 7) // 8
    px = img.load()
    out = bytearray(stride * img.height)
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y] == 0:  # PIL mode "1": 0 = black
                out[y * stride + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


class DisplayLink:
    async def __aenter__(self):
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: SERVICE_UUID in [u.lower() for u in ad.service_uuids],
            timeout=40,
        )
        if dev is None:
            raise RuntimeError("SharpDisplay not found")
        self.client = BleakClient(dev)
        await self.client.connect()
        char = self.client.services.get_characteristic(CMD_UUID)
        self.chunk = min(char.max_write_without_response_size, 240) - 1
        return self

    @property
    def connected(self) -> bool:
        return self.client.is_connected

    async def __aexit__(self, *exc):
        await self.client.disconnect()

    async def _w(self, data: bytes):
        await self.client.write_gatt_char(CMD_UUID, data, response=False)

    async def clear(self):
        await self._w(bytes([CLEAR]))

    async def flush(self):
        await self._w(bytes([FLUSH]))

    async def blit(self, img: Image.Image, x=0, y=0):
        await self._w(bytes([BLIT]) + struct.pack("<4H", x, y, img.width, img.height))
        data = pack_1bpp(img)
        for i in range(0, len(data), self.chunk):
            await self._w(bytes([DATA]) + data[i:i + self.chunk])

    async def show(self, img: Image.Image):
        await self.blit(img)
        await self.flush()
