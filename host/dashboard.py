"""Live system dashboard pushed to the display over BLE. Ctrl-C to stop."""
import asyncio
import collections
import socket
import time

import psutil
from bleak.exc import BleakError
from PIL import Image, ImageChops, ImageDraw, ImageFont

from display_link import DisplayLink, W, H

INTERVAL = 0.5   # seconds between frames
BAND = 40        # rows per diff band
HIST = 120       # sparkline samples

F_BIG = ImageFont.load_default(size=56)
F_MED = ImageFont.load_default(size=20)
F_SM = ImageFont.load_default(size=14)


def fmt_rate(bps: float) -> str:
    for unit in ("B", "K", "M", "G"):
        if bps < 1000 or unit == "G":
            return f"{bps:.0f}{unit}" if unit == "B" else f"{bps:.1f}{unit}"
        bps /= 1000


def bar(d, x, y, w, h, frac):
    frac = max(0.0, min(1.0, frac))
    d.rectangle((x, y, x + w, y + h), outline=0)
    d.rectangle((x + 2, y + 2, x + 2 + int((w - 4) * frac), y + h - 2), fill=0)


class Stats:
    def __init__(self):
        self.cpu_hist = collections.deque(maxlen=HIST)
        self.net = psutil.net_io_counters()
        self.t = time.time()
        psutil.cpu_percent(percpu=True)  # prime

    def sample(self):
        cores = psutil.cpu_percent(percpu=True)
        total = sum(cores) / len(cores)
        self.cpu_hist.append(total)
        now, net = time.time(), psutil.net_io_counters()
        dt = max(now - self.t, 1e-3)
        up = (net.bytes_sent - self.net.bytes_sent) / dt
        down = (net.bytes_recv - self.net.bytes_recv) / dt
        self.net, self.t = net, now
        return dict(
            cores=cores, total=total, up=up, down=down,
            mem=psutil.virtual_memory().percent,
            disk=psutil.disk_usage("/").percent,
            batt=psutil.sensors_battery(),
        )


def render(s, host) -> Image.Image:
    img = Image.new("1", (W, H), 1)
    d = ImageDraw.Draw(img)
    # header
    d.text((8, 4), host[:10], font=F_MED, fill=0)
    d.text((W // 2, 4), time.strftime("%a %d %b"), font=F_MED, fill=0, anchor="ma")
    d.text((W - 8, 4), time.strftime("%H:%M:%S"), font=F_MED, fill=0, anchor="ra")
    d.line((0, 30, W, 30), fill=0)
    # CPU
    d.text((8, 36), f"{s['total']:.0f}%", font=F_BIG, fill=0)
    d.text((8, 98), "CPU", font=F_SM, fill=0)
    # per-core mini bars (right of the big number)
    cores = s["cores"]
    cx, cw, ch = 150, 242, 56
    bw = max(2, cw // len(cores) - 2)
    for i, c in enumerate(cores):
        x = cx + i * (bw + 2)
        d.rectangle((x, 36, x + bw, 36 + ch), outline=0)
        top = 36 + ch - int(ch * c / 100)
        d.rectangle((x, top, x + bw, 36 + ch), fill=0)
    # sparkline
    sx, sy, sw, sh = 8, 116, W - 16, 50
    d.rectangle((sx, sy, sx + sw, sy + sh), outline=0)
    pts = list(s["hist"])
    if len(pts) > 1:
        step = sw / (HIST - 1)
        xy = [(sx + sw - (len(pts) - 1 - i) * step, sy + sh - 1 - (sh - 2) * v / 100)
              for i, v in enumerate(pts)]
        d.line(xy, fill=0, width=1)
    # bars: mem, disk, battery
    y = 176
    rows = [("MEM", s["mem"] / 100, f"{s['mem']:.0f}%"),
            ("DSK", s["disk"] / 100, f"{s['disk']:.0f}%")]
    if s["batt"]:
        b = s["batt"]
        rows.append(("BAT", b.percent / 100, f"{b.percent:.0f}%" + ("+" if b.power_plugged else "")))
    for label, frac, text in rows:
        d.text((8, y), label, font=F_SM, fill=0)
        bar(d, 48, y + 1, 250, 12, frac)
        d.text((308, y), text, font=F_SM, fill=0)
        y += 18
    # network
    d.text((8, H - 18), f"UP {fmt_rate(s['up'])}/s", font=F_SM, fill=0)
    d.text((W - 8, H - 18), f"DN {fmt_rate(s['down'])}/s", font=F_SM, fill=0, anchor="ra")
    return img


async def push(link, img, prev):
    """Send only the BAND-row strips that differ from prev (all if prev is None)."""
    sent = 0
    for y in range(0, H, BAND):
        box = (0, y, W, min(y + BAND, H))
        strip = img.crop(box)
        if prev is None or ImageChops.difference(strip.convert("L"), prev.crop(box).convert("L")).getbbox():
            await link.blit(strip, 0, y)
            sent += 1
    if sent:
        await link.flush()
    return sent


async def run():
    stats = Stats()
    host = socket.gethostname().split(".")[0]
    while True:
        try:
            async with DisplayLink() as link:
                print("connected")
                prev = None
                while link.connected:
                    t0 = time.time()
                    s = stats.sample()
                    s["hist"] = stats.cpu_hist
                    img = render(s, host)
                    n = await push(link, img, prev)
                    prev = img
                    print(f"cpu {s['total']:.0f}%  bands sent {n}/{H // BAND}")
                    await asyncio.sleep(max(0, INTERVAL - (time.time() - t0)))
        except (BleakError, RuntimeError, OSError) as e:
            print(f"link lost: {e}; retrying in 3s")
            await asyncio.sleep(3)


if __name__ == "__main__":
    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
