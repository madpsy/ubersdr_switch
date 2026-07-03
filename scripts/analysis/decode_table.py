#!/usr/bin/env python3
"""Decode the 74HCT138 select-line truth table and button reads.

Each antenna-select routine writes GPIO14/12/13 (in that call order) to a 3-bit
code. We group consecutive digitalWrite(14),(12),(13) triples and print the
implied binary value with several bit-order interpretations.
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

DW = 0x401002fc


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
                return None
    return None


# gather (pc, pin, val) for digitalWrite calls in program order
calls = []
for i, (pc, mn, ops) in enumerate(insns):
    if mn != 'l32r':
        continue
    m = re.search(r'(a\d+),\s*0x([0-9a-f]+)', ops)
    if not m or rd32(int(m.group(2), 16)) != DW:
        continue
    reg = m.group(1)
    for j in range(i + 1, min(len(insns), i + 12)):
        pc2, mn2, ops2 = insns[j]
        if mn2 == 'callx0' and ops2.strip() == reg:
            pin = back_const(j, 'a2')
            val = back_const(j, 'a3')
            if pin in (12, 13, 14):
                calls.append((pc2, pin, val))
            break
        if mn2 == 'l32r' and ops2.split(',')[0].strip() == reg:
            break

# group into triples of consecutive 14/12/13 writes
print("Select-line writes (GPIO14=A?, GPIO12=A?, GPIO13=A?):")
i = 0
blocks = []
while i + 2 < len(calls):
    trip = calls[i:i + 3]
    pins = tuple(t[1] for t in trip)
    if set(pins) == {12, 13, 14}:
        d = {t[1]: t[2] for t in trip}
        blocks.append((trip[0][0], d[14], d[12], d[13]))
        i += 3
    else:
        i += 1

print(f"\n{'addr':<12}{'G14':>4}{'G12':>4}{'G13':>4}   interpretations")
for addr, g14, g12, g13 in blocks:
    # try a few bit weightings
    v_a = (g14 << 2) | (g13 << 1) | g12   # A2=14 A1=13 A0=12
    v_b = (g12 << 2) | (g13 << 1) | g14   # A2=12 A1=13 A0=14
    v_c = (g13 << 2) | (g12 << 1) | g14
    print(f"0x{addr:08x}{g14:>4}{g12:>4}{g13:>4}   "
          f"[14=A2,13=A1,12=A0]={v_a}  [12=A2,13=A1,14=A0]={v_b}")
