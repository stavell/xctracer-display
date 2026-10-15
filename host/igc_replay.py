"""Replay an IGC flight to the box as if the XC Tracer were streaming it (real time, 1 line/s).
  python igc_replay.py flight.igc --find            list the strongest climbs with start offsets
  python igc_replay.py flight.igc --start 4300      replay from 4300 s after the first fix
Switch the real tracer off first, otherwise its live stream mixes with the replay."""
import argparse
import asyncio
import time
from display_link import DisplayLink, CMD_UUID
from igc import read_igc, xctrc_line

CMD_XCTRC = 0x13


def clock(t):
    return f"{t // 3600:02d}:{(t // 60) % 60:02d}:{t % 60:02d} UTC"


def find_climbs(fx, win=150, top=6):
    scored = []
    for i in range(0, len(fx) - win, 15):
        seg = fx[i:i + win]
        gain = seg[-1].palt - seg[0].palt
        turn = sum(abs(((seg[j].course - seg[j - 1].course + 180) % 360) - 180) for j in range(1, len(seg)))
        if gain > 80 and turn > 720:   # real gain while turning more than two full circles
            scored.append((gain, i))
    picked = []
    for gain, i in sorted(scored, reverse=True):
        if all(abs(i - j) > 300 for _, j in picked):
            picked.append((gain, i))
        if len(picked) == top:
            break
    return sorted(picked, key=lambda x: x[1])


async def replay(path, start):
    date, fx = read_igc(path)
    t0 = fx[0].t
    i0 = next((i for i, f in enumerate(fx) if f.t - t0 >= start), 0)
    print(f"replaying from {clock(fx[i0].t)} ({len(fx) - i0} s). Ctrl-C to stop.", flush=True)
    async with DisplayLink() as link:
        t_start = time.monotonic()
        for k, f in enumerate(fx[i0:]):
            line = xctrc_line(date, f)
            await link.client.write_gatt_char(CMD_UUID, bytes([CMD_XCTRC]) + line.encode(), response=False)
            if k % 30 == 0:
                print(f"{clock(f.t)}  alt {f.palt:.0f} m  vario {f.vario:+.1f}  {f.speed:.0f} km/h", flush=True)
            delay = t_start + (k + 1) - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("igc")
    ap.add_argument("--find", action="store_true")
    ap.add_argument("--start", type=int, default=0, help="seconds after the first fix")
    a = ap.parse_args()
    if a.find:
        date, fx = read_igc(a.igc)
        t0 = fx[0].t
        for gain, i in find_climbs(fx):
            print(f"--start {fx[i].t - t0 - 120:5d}   {clock(fx[i].t)}   +{gain:.0f} m in 150 s")
        return
    try:
        asyncio.run(replay(a.igc, a.start))
    except KeyboardInterrupt:
        pass

main()
