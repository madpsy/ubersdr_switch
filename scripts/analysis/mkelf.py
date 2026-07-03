#!/usr/bin/env python3
"""Build a minimal ELF32 (Xtensa, little-endian) with SECTION headers wrapping
the ESP8266 app segments so xtensa objdump -d disassembles at correct vaddrs."""
import struct

BIN = 'esp8266_cc50e345ca21_flash_backup_20260703.bin'
OUT = 'build/fw.elf'

data = open(BIN, 'rb').read()
base = 0x1000
nseg = data[base + 1]
entry = struct.unpack('<I', data[base + 4:base + 8])[0]
off = base + 8
segs = []
for i in range(nseg):
    load_addr, length = struct.unpack('<II', data[off:off + 8])
    off += 8
    segs.append((load_addr, data[off:off + length]))
    off += length

names = ['.irom0text', '.iram0vec', '.iram0text', '.dram0a', '.dram0b']
flag = [0x6, 0x6, 0x6, 0x3, 0x3]  # ALLOC|EXEC or ALLOC|WRITE

# Build section header string table
shstr = b'\x00'
name_off = {}
for n in names + ['.shstrtab']:
    name_off[n] = len(shstr)
    shstr += n.encode() + b'\x00'

# Layout: ehdr(52) then section data, then shstrtab, then shdrs
ehsize = 52
cur = ehsize
sec_data_off = []
for _, blob in segs:
    # align to 4
    if cur % 4:
        cur += 4 - (cur % 4)
    sec_data_off.append(cur)
    cur += len(blob)
if cur % 4:
    cur += 4 - (cur % 4)
shstr_off = cur
cur += len(shstr)
if cur % 4:
    cur += 4 - (cur % 4)
shoff = cur

# Assemble body
body = bytearray(shoff)
for (load_addr, blob), o in zip(segs, sec_data_off):
    body[o:o + len(blob)] = blob
body[shstr_off:shstr_off + len(shstr)] = shstr

# Section headers: null + one per seg + shstrtab
def shdr(nameoff, stype, flags, addr, offset, size, link=0, info=0, align=4, entsize=0):
    return struct.pack('<IIIIIIIIII', nameoff, stype, flags, addr, offset, size, link, info, align, entsize)

shdrs = shdr(0, 0, 0, 0, 0, 0)  # null
for (load_addr, blob), o, n, f in zip(segs, sec_data_off, names, flag):
    shdrs += shdr(name_off[n], 1, f, load_addr, o, len(blob))  # SHT_PROGBITS
shstr_idx = 1 + len(segs)
shdrs += shdr(name_off['.shstrtab'], 3, 0, 0, shstr_off, len(shstr))  # SHT_STRTAB

body += shdrs

# ELF header
EI = bytes([0x7f, ord('E'), ord('L'), ord('F'), 1, 1, 1, 0]) + b'\x00' * 8
ehdr = EI + struct.pack('<HHIIIIIHHHHHH',
    2, 94, 1, entry, 0, shoff, 0x300,
    ehsize, 0, 0, 40, 1 + len(segs) + 1, shstr_idx)
body[0:52] = ehdr

open(OUT, 'wb').write(body)
print("wrote", OUT, len(body), "bytes")
for (load_addr, blob), n in zip(segs, names):
    print(f"  {n:12s} 0x{load_addr:08x} len 0x{len(blob):x}")
