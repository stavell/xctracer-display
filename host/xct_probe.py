"""Find the XC Tracer over BLE, list its GATT table, log raw notifications.
Usage: python xct_probe.py [seconds] [name]"""
import asyncio
import sys
import time
from bleak import BleakClient, BleakScanner

SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 30
NAME = sys.argv[2] if len(sys.argv) > 2 else "XC-Tracer"


async def main():
    print(f"scanning for '{NAME}' ...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: NAME.lower() in ((ad.local_name or d.name or "").lower()), timeout=25)
    if dev is None:
        print("NOT FOUND: switch the tracer on and bring it close")
        return
    log = open("xct_capture.log", "a")
    async with BleakClient(dev) as c:
        print("connected:", dev.name, flush=True)
        notif = []
        for s in c.services:
            print(f"service {s.uuid}  {s.description}")
            for ch in s.characteristics:
                print(f"   char {ch.uuid}  {ch.properties}")
                if "notify" in ch.properties or "indicate" in ch.properties:
                    notif.append(ch)

        def cb(ch):
            def h(_, data: bytearray):
                line = f"{time.time():.3f} {ch.uuid} {bytes(data)!r}"
                print(line, flush=True)
                log.write(line + "\n")
            return h

        for ch in notif:
            await c.start_notify(ch, cb(ch))
        print(f"subscribed to {len(notif)} characteristic(s); listening {SECS:.0f}s", flush=True)
        await asyncio.sleep(SECS)

asyncio.run(main())
