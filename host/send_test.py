import asyncio
import time
from PIL import Image, ImageDraw, ImageFont
from display_link import DisplayLink, W, H


def render() -> Image.Image:
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    f = ImageFont.load_default(size=28)
    d.text((16, 12), time.strftime("%H:%M:%S"), font=f, fill=0)
    d.rectangle((16, 70, W - 16, 110), outline=0, width=2)
    d.rectangle((20, 74, 20 + int((W - 44) * 0.62), 106), fill=0)
    d.text((16, 130), "BLE test page", font=f, fill=0)
    d.line((0, H - 1, W - 1, 0), fill=0, width=2)
    return img


async def main():
    async with DisplayLink() as link:
        t = time.time()
        await link.show(render())
        print(f"sent full frame in {time.time() - t:.2f}s (chunk {link.chunk} bytes)")
        await asyncio.sleep(1)  # let writes drain before disconnect

asyncio.run(main())
