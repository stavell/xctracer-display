#!/usr/bin/env python3
"""Static stack check for the device images: the loop task has 4 KB of stack (see CLAUDE.md).

    scripts/stack_check.py [elf ...]        (default: both images, built by `pio run`)
        --frame-limit 1024   fail when one function's frame is larger than this (bytes)
        --chain-limit 3072   fail when the worst call chain from loop() / setup() is deeper than this
        --stack 4096         the loop task's stack, only for the report

How: the compiler's own numbers would be `-fstack-usage` (.su files), but the arm-none-eabi-gcc 7.2 in the
PlatformIO toolchain has no call-graph output, so both halves come from the linked ELF: the frame of a function
is read from its prologue (sum of `push` / `vpush` register lists and `sub sp, #n`, which is exactly what the
compiler emitted), the call graph from `bl` / tail `b` instructions. Then: the deepest chain
loop() -> ... is the sum of frames along the worst path.

What it cannot see (a clean run is NOT a proof):
  * indirect calls. A virtual call made by a project method (the vtable slot is read from the instructions
    before the `blx`) is assumed to reach that slot of every project class derived from an interface named in
    the calling class's declaration (IApp, IPanel, ...; for a free function such as loop() any interface, since
    AppHost::tick is inlined into it): an over-estimate, since the receiver's exact type is not known. Virtual calls made by library code (Print::write, Adafruit_GFX::drawPixel,
    Bluefruit), function pointers and FreeRTOS hooks are not followed; the count of such functions is printed.
  * recursion: cycles are cut (each function once per path) and listed, the real depth could be larger.
  * `alloca` / variable-length arrays (a dynamic `sub sp, sp, rN` is reported as a warning and counted as 0).
  * library code that is called through the above, hand-written assembly, and the stack of anything that is not
    the loop task: BLE callbacks run on the callback task (3 KB), the SoftDevice and interrupt handlers use
    the main stack, the FreeRTOS timer service task has its own.
  * the data a function keeps on the stack beyond its prologue (the compiler allocates everything up front, so
    this is rare), and the few words FreeRTOS keeps at the stack's top for the task context.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS = os.path.expanduser("~/.platformio/packages/toolchain-gccarmnoneeabi/bin")
DEFAULT_ELFS = [os.path.join(ROOT, "firmware/.pio/build", e, "firmware.elf") for e in ("feather_sense", "feather_sense_box")]


def tool(name):
    p = os.path.join(TOOLS, "arm-none-eabi-" + name)
    return p if os.path.exists(p) else "arm-none-eabi-" + name


def run(args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, text=True, errors="replace").stdout


def class_info():
    """From firmware/src: {class: (bases, interfaces mentioned in its declaration)}."""
    info = {}
    decl = re.compile(r"^\s*(?:class|struct)\s+(\w+)\s*(?:final\s*)?(?::\s*([^{;]*))?\{", re.M)
    for d, _, files in os.walk(os.path.join(ROOT, "firmware/src")):
        for f in files:
            if not f.endswith(".h"):
                continue
            text = open(os.path.join(d, f), errors="replace").read()
            for m in decl.finditer(text):
                depth, k = 1, m.end()
                while k < len(text) and depth:
                    depth += {"{": 1, "}": -1}.get(text[k], 0)
                    k += 1
                body = text[m.end():k]
                bases = re.findall(r"(?:public|private|protected)?\s*(\w+)", m.group(2) or "")
                bases = [b for b in bases if b not in ("public", "private", "protected", "virtual")]
                ifs = set(re.findall(r"\bI[A-Z]\w+\b", body)) - {m.group(1)}
                info[m.group(1)] = (bases, ifs)
    return info


def ancestors(cls, info):
    out, todo = set(), [cls]
    while todo:
        c = todo.pop()
        if c in out:
            continue
        out.add(c)
        todo += info.get(c, ([], set()))[0]
    return out


HDR = re.compile(r"^([0-9a-f]+) <(.*)>:$")
TARGET = re.compile(r"<(.+?)(?:\+0x[0-9a-f]+)?>\s*$")
REGS = re.compile(r"\{([^}]*)\}")


def count_regs(lst):
    n = 0
    for part in lst.split(","):
        part = part.strip()
        if not part:
            continue
        m = re.match(r"[rsd]?(\d+)-[rsd]?(\d+)$", part)
        n += int(m.group(2)) - int(m.group(1)) + 1 if m else 1
    return n


class Func:
    def __init__(self, name):
        self.name = name
        self.frame = 0
        self.calls = set()
        self.vslots = set()        # vtable byte offsets of the virtual calls this function makes
        self.unknown = 0           # indirect calls whose target could not be tied to a vtable slot (function pointers)
        self.dynamic = False
        self.mentioned = set()     # interfaces named in the declaration of the class this method belongs to


def parse(elf):
    asm = run([tool("objdump"), "-d", "-C", "--no-show-raw-insn", elf])
    funcs = {}
    cur = None
    lastldr = {}
    for line in asm.splitlines():
        m = HDR.match(line)
        if m:
            cur = funcs.setdefault(m.group(2), Func(m.group(2)))
            cur.addr = int(m.group(1), 16)
            lastldr = {}
            continue
        if cur is None or ":\t" not in line:
            continue
        ins = line.split(":\t", 1)[1].strip()
        op, _, rest = ins.partition("\t")
        op = op.strip()
        rest = rest.strip()
        lm = re.match(r"(r\d+),\s*\[(r\d+)(?:,\s*#(\d+))?\]", rest) if op in ("ldr", "ldr.w") else None
        if lm:
            lastldr[lm.group(1)] = int(lm.group(3) or 0)       # the vtable slot load before a virtual call
        elif rest[:3] and re.match(r"(r\d+),", rest):
            lastldr.pop(rest.split(",")[0].strip(), None)      # any other write to that register
        if op in ("push", "push.w", "stmdb") and "{" in rest:
            if op == "stmdb" and not rest.startswith("sp!"):
                continue
            cur.frame += 4 * count_regs(REGS.search(rest).group(1))
        elif op in ("vpush", "vstmdb") and "{" in rest:
            cur.frame += 8 * count_regs(REGS.search(rest).group(1)) if "d" in rest else 4 * count_regs(REGS.search(rest).group(1))
        elif op in ("sub", "sub.w", "subw") and rest.startswith("sp"):
            m2 = re.match(r"sp,\s*(?:sp,\s*)?#(\d+)", rest)
            if m2:
                cur.frame += int(m2.group(1))
            else:
                cur.dynamic = True
        elif op in ("bl", "b.w", "b", "bx") and "<" in rest and op != "bx":
            t = TARGET.search(rest)
            if t and t.group(1) != cur.name:
                full = re.search(r"<(.+?)>\s*$", rest).group(1)
                if "+0x" in full and full.split("+0x")[0] == cur.name:
                    continue                                   # a jump inside the function
                cur.calls.add(t.group(1))
        elif op in ("blx", "bx") and re.match(r"r\d+$", rest):
            if rest in lastldr:
                cur.vslots.add(lastldr[rest])
            else:
                cur.unknown += 1                               # function pointer, or a switch table (bx)
    return funcs


def project_functions(elf):
    """Demangled names of the functions defined in this project's own object files (build/<env>/src/**.o)."""
    src = os.path.join(os.path.dirname(elf), "src")
    names = set()
    for d, _, files in os.walk(src):
        for f in files:
            if f.endswith(".o"):
                for line in run([tool("nm"), "-C", "--defined-only", os.path.join(d, f)]).splitlines():
                    m = re.match(r"[0-9a-f]+ [TtWw] (.+)$", line)
                    if m:
                        names.add(m.group(1))
    return names


def virtual_targets(elf, funcs, info, project):
    """{class: [(interfaces that sub-vtable implements, {vtable byte offset: function names})]} for project classes."""
    by_addr = {f.addr: f for f in funcs.values()}
    nm = run([tool("nm"), "-C", "-S", "--defined-only", elf])
    out = {}
    for line in nm.splitlines():
        m = re.match(r"([0-9a-f]+) ([0-9a-f]+) \w vtable for (.+)$", line)
        if not m:
            continue
        cls = m.group(3).split("<")[0].split("::")[-1]
        if cls not in info:
            continue
        a, size = int(m.group(1), 16), int(m.group(2), 16)
        dump = run([tool("objdump"), "-s", "--start-address=0x%x" % a, "--stop-address=0x%x" % (a + size), elf])
        words = []
        for dl in dump.splitlines():
            dm = re.match(r"\s+[0-9a-f]+ ((?:[0-9a-f]{8} ?){1,4})", dl)
            if dm:
                words += [int.from_bytes(bytes.fromhex(w), "little") for w in dm.group(1).split()]
        if len(words) < 3:
            continue
        tinfo = words[1]
        starts = [2] + [k + 1 for k in range(3, len(words)) if words[k] == tinfo]   # a secondary vtable repeats the typeinfo
        bases = info[cls][0]
        for n, st in enumerate(starts):
            # sub-vtable n belongs to the n-th base (the primary one first); the first also holds the class's own
            kinds = ancestors(bases[n], info) if n < len(bases) else set()
            if n == 0:
                kinds = kinds | {cls}
            tab = {}
            for k in range(st, len(words)):
                if k != st and words[k] == tinfo:
                    break
                v = words[k] & ~1
                if v in by_addr and by_addr[v].name in project:
                    tab.setdefault((k - st) * 4, set()).add(by_addr[v].name)
            out.setdefault(cls, []).append((kinds, tab))
    return out


def analyse(funcs, virtuals, root):
    memo = {}
    cycles = set()

    def successors(f):
        out = [c for c in f.calls if c in funcs]
        for off in sorted(f.vslots):
            for cls in sorted(virtuals):
                for kinds, tab in virtuals[cls]:
                    if kinds & f.mentioned:
                        out += [v for v in sorted(tab.get(off, ())) if v != f.name]
        return sorted(set(out))

    def chain(name, stack):
        """Longest chain from `name`: (bytes, path, cut) where cut says a cycle edge was dropped below it."""
        if name in memo:
            return memo[name]
        f = funcs.get(name)
        if f is None:
            return (0, [], False)
        best, cut = (0, []), False
        for c in successors(f):
            if c in stack or c == name:
                cycles.add((name, c))
                cut = True
                continue
            sub = chain(c, stack | {name})
            cut = cut or sub[2]
            if sub[0] > best[0]:
                best = (sub[0], sub[1])
        res = (f.frame + best[0], [name] + best[1], cut)
        if not cut:
            memo[name] = res                    # exact: nothing below it was cut
        return res

    total, path, _ = chain(root, frozenset())
    reach = set()
    todo = [root]
    while todo:
        n = todo.pop()
        if n in reach or n not in funcs:
            continue
        reach.add(n)
        todo += successors(funcs[n])
    return total, path, reach, cycles


def short(n, w=96):
    return n if len(n) <= w else n[: w - 3] + "..."


def check(elf, a):
    print("=== %s" % os.path.relpath(elf, ROOT))
    if not os.path.exists(elf):
        print("  missing: build it first (cd firmware && ../.venv/bin/pio run)")
        return False
    funcs = parse(elf)
    project = project_functions(elf)
    info = class_info()
    virtuals = virtual_targets(elf, funcs, info, project)
    # A virtual call in method C::f can reach the classes that derive from an interface (I...) named in C's
    # declaration: the receiver is a member or a parameter of that type. Anything else is not followed.
    for f in funcs.values():
        if not f.vslots:
            continue
        cls = f.name.split("(")[0].split("::")
        cls = cls[-2] if len(cls) >= 2 else ""
        mentioned = info.get(cls, ([], set()))[1]
        if f.name in project and not cls:                       # a free function (loop, draw code): AppHost::tick and the
            mentioned = set().union(*(v[1] for v in info.values()))   # like are inlined into it, so any interface may be called
        if f.name in project and mentioned:
            f.mentioned = mentioned
        if not f.mentioned:
            f.unknown += len(f.vslots)
            f.vslots = set()
    ok = True
    roots = [("loop", "loop"), ("setup", "setup")]
    print("  functions in the image: %d, project classes with virtual methods: %d" % (len(funcs), len(virtuals)))
    for demangled, label in roots:
        demangled = next((n for n in (label, label + "()") if n in funcs), demangled)
        if demangled not in funcs:
            print("  %s: not found in the image" % label)
            ok = False
            continue
        total, path, reach, cycles = analyse(funcs, virtuals, demangled)
        print("  %s: worst chain %d B of %d B (%d%%), %d functions reachable" % (label, total, a.stack, 100 * total // a.stack, len(reach)))
        for n in path:
            print("      %4d  %s" % (funcs[n].frame, short(n)))
        if total > a.chain_limit:
            print("  FAIL: chain from %s is %d B, limit %d B" % (label, total, a.chain_limit))
            ok = False
        big = sorted(((funcs[n].frame, n) for n in reach), reverse=True)[:6]
        print("    largest single frames reachable from %s:" % label)
        for fr, n in big:
            print("      %4d  %s%s" % (fr, short(n), "   <-- over the limit" if fr > a.frame_limit else ""))
            if fr > a.frame_limit:
                ok = False
        dyn = [n for n in reach if funcs[n].dynamic]
        ind = [n for n in reach if funcs[n].unknown]
        if dyn:
            print("    WARNING: dynamic stack allocation (not counted): %s" % ", ".join(short(n, 60) for n in sorted(dyn)[:5]))
        if cycles:
            print("    note: %d call cycle(s) cut (recursion or over-approximated virtual calls), e.g. %s -> %s" %
                  (len(cycles), short(sorted(cycles)[0][0], 50), short(sorted(cycles)[0][1], 50)))
        print("    %d reachable functions make indirect calls that are not virtual calls (function pointers, switch tables): not followed" % len(ind))
    print("  %s" % ("OK" if ok else "FAILED"))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf", nargs="*", default=DEFAULT_ELFS)
    ap.add_argument("--frame-limit", type=int, default=1024)
    ap.add_argument("--chain-limit", type=int, default=3072)
    ap.add_argument("--stack", type=int, default=4096)
    a = ap.parse_args()
    ok = True
    for elf in a.elf:
        ok = check(elf, a) and ok
    print("STACK CHECK %s (static analysis only: indirect calls, recursion and other tasks' stacks are not covered)" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
