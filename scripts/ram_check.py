#!/usr/bin/env python3
"""RAM budget check for the device images: fail when the .heap section of any built image is below 48 KB.

    scripts/ram_check.py [elf ...]      (default: every firmware/.pio/build/*/firmware.elf)
        --min-heap 49152                the smallest acceptable .heap, bytes

Why: static RAM (.bss + .data) is linked first and .heap is whatever is left. FreeRTOS takes the loop task's stack, the
USB and BLE tasks, their queues and the Bluefruit buffers from that heap. The box image once linked with 2376 bytes of
heap: no task could start, the board showed a blank screen and no USB. Nothing in the build noticed. See NOTES.md,
"RAM budget".
"""
import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SIZE = os.path.expanduser("~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-size")


def sections(elf):
    out = subprocess.run([SIZE if os.path.exists(SIZE) else "arm-none-eabi-size", "-A", elf], check=True,
                         stdout=subprocess.PIPE, text=True).stdout
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"^(\.\w+)\s+(\d+)\s+\d+", out, re.M)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elfs", nargs="*")
    ap.add_argument("--min-heap", type=int, default=48 * 1024)
    a = ap.parse_args()
    elfs = a.elfs or sorted(glob.glob(os.path.join(ROOT, "firmware/.pio/build/*/firmware.elf")))
    if not elfs:
        print("ram_check: no built images found (run `pio run` first)")
        return 1
    bad = 0
    print("%-22s %10s %10s %10s" % ("image", ".bss", ".data", ".heap"))
    for elf in elfs:
        s = sections(elf)
        name = os.path.basename(os.path.dirname(elf))
        heap = s.get(".heap", 0)
        ok = heap >= a.min_heap
        print("%-22s %10d %10d %10d   %s" % (name, s.get(".bss", 0), s.get(".data", 0), heap,
                                            "ok" if ok else "FAIL: heap below %d bytes, the image will not boot" % a.min_heap))
        bad += not ok
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
