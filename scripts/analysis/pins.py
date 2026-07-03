#!/usr/bin/env python3
"""Recover GPIO pin usage from ESP8266 ANTENNA SELECTOR V2 firmware.

Core Arduino GPIO primitives (from disassembly):
  __digitalWrite(pin, val) @ 0x401002fc
  __digitalRead(pin)       @ 0x40100360

They are called indirectly:  l32r aN, =<func> ; ... ; callx0 aN
So we find every l32r that loads one of these addresses, then the next callx0
using that register, then walk back for constant a2 (pin) / a3 (val).
"""
import re, struct

DIS = 'build/fw.dis'
ELF = 'build/fw.elf'
elf = open(ELF, 'rb').read()
SECS = [(0x40201010, 0x50580, 0x34), (0x40100000, 0x104, 0x505b4),
        (0x40100104, 0x691c, 0x506b8), (0x3ffe8000, 0x508, 0x56fd4),
        (0x3ffe8510, 0xd54, 0x574dc)]


def rd32(v):
    for a, l, fo in SECS:
        if a <= v < a + l:
            return struct.unpack('<I', elf[fo + (v - a):fo + (v - a) + 4])[0]
    return None


LINE = re.compile(r'^\s*([0-9a-f]{6,8}):\s+[0-9a-f]+\s+(\S+)\s*(.*)$')
insns = []
for line in open(DIS):
    m = LINE.search(line)
    if not m:
        continue
    insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))

TARGETS = {
    0x401002fc: ('digitalWrite', True),
    0x40100360: ('digitalRead', False),
    0x40100394: ('func_0x40100394', False),  # candidate pinMode (next fn)
    0x401002d6: ('func_0x401002d6', False),
}


def back_const(i, reg, depth=20):
    for j in range(i - 1, max(0, i - depth), -1):
        pc, mn, ops = insns[j]
        dst = ops.split(',')[0].strip()
        if dst == reg:
            if mn in ('movi', 'movi.n'):
                m = re.search(r',\s*(-?\d+|0x[0-9a-f]+)\s*$', ops)
                if m:
                    return int(m.group(1), 0)
            else:
                return None  # reg clobbered by non-const
    return None


results = {name: [] for name, _ in TARGETS.values()}

for i, (pc, mn, ops) in enumerate(insns):
    if mn != 'l32r':
        continue
    m = re.search(r'(a\d+),\s*0x([0-9a-f]+)', ops)
    if not m:
        continue
    reg = m.group(1)
    val = rd32(int(m.group(2), 16))
    if val not in TARGETS:
        continue
    name, has_val = TARGETS[val]
    # find next callx0 <reg> within a window
    for j in range(i + 1, min(len(insns), i + 12)):
        pc2, mn2, ops2 = insns[j]
        if mn2 == 'callx0' and ops2.strip() == reg:
            pin = back_const(j, 'a2')
            v = back_const(j, 'a3') if has_val else None
            results[name].append((pc2, pin, v))
            break
        # reg reloaded
        if mn2 == 'l32r' and ops2.split(',')[0].strip() == reg:
            break

for name, lst in results.items():
    print(f"\n=== {name}: {len(lst)} indirect call sites ===")
    for pc, pin, v in lst:
        extra = f" val={v}" if v is not None else ""
        print(f"  callx@0x{pc:08x}  pin={pin}{extra}")
