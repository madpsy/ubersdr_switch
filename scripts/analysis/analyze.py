#!/usr/bin/env python3
"""Analyze the ESP8266 ANTENNA SELECTOR V2 flash dump to recover GPIO pin usage.

Strategy:
  1. Locate the Arduino core functions __digitalWrite/__pinMode/__digitalRead by
     finding IRAM code that touches GPIO registers (0x60000300..0x6000031c) and
     the per-pin GPIO_PINn table (0x60000328 + pin*4) / IO MUX / func-select.
  2. Find all call0/callx callers of those functions and, by tracking the most
     recent 'movi aN, imm' into the pin argument register (a2), recover the
     constant pin numbers passed.
"""
import struct, re, os, sys, collections

ELF = 'build/fw.elf'
DIS = 'build/fw.dis'

elf = open(ELF, 'rb').read()
SECS = [(0x40201010, 0x50580, 0x34), (0x40100000, 0x104, 0x505b4),
        (0x40100104, 0x691c, 0x506b8), (0x3ffe8000, 0x508, 0x56fd4),
        (0x3ffe8510, 0xd54, 0x574dc)]


def rd32(v):
    for a, l, fo in SECS:
        if a <= v < a + l:
            return struct.unpack('<I', elf[fo + (v - a):fo + (v - a) + 4])[0]
    return None


# ---- parse full disassembly into a list of (pc, mnemonic, operands, raw) ----
LINE = re.compile(r'^\s*([0-9a-f]{6,8}):\s+[0-9a-f]+\s+(\S+)\s*(.*)$')
insns = []
for line in open(DIS):
    m = LINE.search(line)
    if not m:
        continue
    pc = int(m.group(1), 16)
    insns.append((pc, m.group(2), m.group(3).strip(), line.rstrip()))

by_pc = {pc: i for i, (pc, *_ ) in enumerate(insns)}


def l32r_val(operands):
    m = re.search(r'0x([0-9a-f]+)', operands)
    if not m:
        return None
    return rd32(int(m.group(1), 16))


# ---- 1. find call targets ----
# Collect functions that reference GPIO registers.
GPIO_REGS = {0x60000300, 0x60000304, 0x60000308, 0x6000030c, 0x60000310,
             0x60000314, 0x60000318, 0x6000031c}
gpio_ref_pcs = []
for pc, mn, ops, raw in insns:
    if mn == 'l32r':
        v = l32r_val(ops)
        if v in GPIO_REGS:
            gpio_ref_pcs.append((pc, v))

print("GPIO-register-referencing instructions:")
for pc, v in gpio_ref_pcs:
    print(f"  0x{pc:08x}  ->0x{v:08x}")


def func_start(idx):
    """Walk backwards to a likely function prologue (addi a1,a1,-N)."""
    i = idx
    while i > 0:
        pc, mn, ops, raw = insns[i]
        if mn == 'addi' and ops.startswith('a1, a1, -'):
            return pc
        # also stop at previous ret.n boundary +1
        i -= 1
    return insns[0][0]


# ---- collect candidate function starts that use GPIO ----
gpio_funcs = sorted({func_start(by_pc[pc]) for pc, v in gpio_ref_pcs})
print("\nCandidate GPIO helper function starts:", [hex(x) for x in gpio_funcs])


# ---- 2. find callers of each gpio func and recover constant pin args ----
def find_callers(target):
    res = []
    for pc, mn, ops, raw in insns:
        if mn in ('call0', 'callx0', 'call4', 'call8', 'call12'):
            m = re.search(r'0x([0-9a-f]+)', ops)
            if m and int(m.group(1), 16) == target:
                res.append(pc)
    return res


def recover_arg(call_idx, argreg='a2', back=12):
    """Look back for movi/movi.n into argreg."""
    for j in range(call_idx - 1, max(0, call_idx - back), -1):
        pc, mn, ops, raw = insns[j]
        if mn in ('movi', 'movi.n') and ops.startswith(argreg + ','):
            m = re.search(r',\s*(-?\d+|0x[0-9a-f]+)', ops)
            if m:
                return int(m.group(1), 0)
        if mn in ('ret', 'ret.n'):
            break
    return None


print("\n==== callers of GPIO helper functions (recovered pin/value args) ====")
for f in gpio_funcs:
    callers = find_callers(f)
    if not callers:
        continue
    print(f"\nfunc 0x{f:08x}: {len(callers)} callers")
    for c in callers:
        ci = by_pc[c]
        pin = recover_arg(ci, 'a2')
        val = recover_arg(ci, 'a3')
        print(f"  call@0x{c:08x}  a2(pin)={pin}  a3(val)={val}")
