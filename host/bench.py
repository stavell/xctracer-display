"""Update-rate benchmark. Watch the display; compare its counter with the terminal."""
import asyncio
import time
from PIL import Image, ImageDraw, ImageFont
from display_link import DisplayLink, W, H

F = ImageFont.load_default(size=26)


def small(n, label):
    img = Image.new("1", (W, 40), 1)
    d = ImageDraw.Draw(img)
    d.text((8, 6), f"{label}  #{n:05d}", font=F, fill=0)
    d.rectangle((W - 120, 8, W - 8, 32), outline=0)
    d.rectangle((W - 118, 10, W - 118 + (n % 10) * 11, 30), fill=0)
    return img


def full(n, label):
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=0)
    for k in range(0, W, 20):                      # pattern: any garbling shows
        d.line((k, 0, W - 1 - k, H - 1), fill=0)
    d.rectangle((60, 80, W - 60, 160), fill=1, outline=0, width=2)
    d.text((72, 100), f"{label}  #{n:05d}", font=F, fill=0)
    return img


async def phase(link, name, make, y, hz, secs=8):
    n, worst = 0, 0.0
    period = 1 / hz
    end = time.time() + secs
    nxt = time.time()
    while time.time() < end:
        n += 1
        t = time.time()
        await link.blit(make(n, f"{name} {hz}Hz"), 0, y)
        await link.flush()
        worst = max(worst, time.time() - t)
        nxt += period
        await asyncio.sleep(max(0, nxt - time.time()))
    await asyncio.sleep(0.5)
    await link.blit(make(n, f"{name} FINAL"), 0, y)
    await link.flush()
    print(f"{name} {hz:>2} Hz: sent {n} frames, worst send call {worst*1000:.0f} ms"
          f"  -> display should read #{n:05d}", flush=True)
    await asyncio.sleep(5)


async def main():
    async with DisplayLink() as link:
        await link.clear()
        await link.flush()
        print("STAGE A: small strip (400x40)", flush=True)
        for hz in (1, 2, 5, 10, 20):
            await phase(link, "strip", small, 100, hz)
        print("STAGE B: full frames (400x240)", flush=True)
        for hz in (1, 2, 3, 5):
            await phase(link, "full", full, 0, hz)
        print("done")

asyncio.run(main())
