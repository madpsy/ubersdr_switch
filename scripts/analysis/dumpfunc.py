#!/usr/bin/env python3
"""Dump a window of disassembly with l32r literal-value annotations."""
import struct, re, sys

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


def dump(lo, hi):
    pat = re.compile(r'^\s*([0-9a-f]{6,8}):')
    for line in open(DIS):
        m = pat.search(line)
        if not m:
            continue
        pc = int(m.group(1), 16)
        if lo <= pc <= hi:
            lm = re.search(r'l32r\s+a\d+,\s*0x([0-9a-f]+)', line)
            ann = ''
            if lm:
                val = rd32(int(lm.group(1), 16))
                if val is not None:
                    ann = f'   ; =0x{val:08x}'
            print(line.rstrip() + ann)


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        if '-' in arg:
            lo, hi = arg.split('-')
            lo, hi = int(lo, 16), int(hi, 16)
        else:
            c = int(arg, 16)
            lo, hi = c - 0x40, c + 0x40
        print(f'\n===== 0x{lo:08x} .. 0x{hi:08x} =====')
        dump(lo, hi)
