# How the Firmware Was Reverse-Engineered — Full End-to-End Walkthrough

This is a detailed, reproducible account of how the pinout, 74HCT138 decoder truth
table, the four buttons, and the web/AP interface were recovered from a single raw
flash dump with **no source code and no symbols**:
[`esp8266_cc50e345ca21_flash_backup_20260703.bin`](../esp8266_cc50e345ca21_flash_backup_20260703.bin)
(4,194,304 bytes = 4 MB).

Every helper script referenced lives in [`scripts/analysis/`](../scripts/analysis/). Where useful, real
excerpts from the disassembly are shown.

---

## Table of contents

1. [Objective & constraints](#1-objective--constraints)
2. [Toolchain](#2-toolchain)
3. [Step 1 — String triage: identify the firmware](#3-step-1--string-triage-identify-the-firmware)
4. [Step 2 — Understand the ESP8266 flash image format](#4-step-2--understand-the-esp8266-flash-image-format)
5. [Step 3 — Parse the segment table](#5-step-3--parse-the-segment-table)
6. [Step 4 — The ESP8266 / Xtensa memory map](#6-step-4--the-esp8266--xtensa-memory-map)
7. [Step 5 — Build an ELF wrapper for objdump](#7-step-5--build-an-elf-wrapper-for-objdump)
8. [Step 6 — First disassembly attempt & why it failed](#8-step-6--first-disassembly-attempt--why-it-failed)
9. [Step 7 — Decoding `l32r` by hand (calibration)](#9-step-7--decoding-l32r-by-hand-calibration)
10. [Step 8 — Locating the GPIO hardware registers](#10-step-8--locating-the-gpio-hardware-registers)
11. [Step 9 — A dead-end: UART mistaken for GPIO](#11-step-9--a-dead-end-uart-mistaken-for-gpio)
12. [Step 10 — Finding the Arduino GPIO primitives](#12-step-10--finding-the-arduino-gpio-primitives)
13. [Step 11 — Recovering constant pin arguments](#13-step-11--recovering-constant-pin-arguments)
14. [Step 12 — Reading `setup()`](#14-step-12--reading-setup)
15. [Step 13 — Decoding the 74HCT138 truth table](#15-step-13--decoding-the-74hct138-truth-table)
16. [Step 14 — Identifying each button's role](#16-step-14--identifying-each-buttons-role)
17. [Step 15 — Reconstructing the web & AP interface](#17-step-15--reconstructing-the-web--ap-interface)
18. [Full reproduction commands](#18-full-reproduction-commands)
19. [Dumping / restoring from hardware](#19-dumping--restoring-from-hardware)

---

## 1. Objective & constraints

- **Input:** one 4 MB flash read-back. No `.elf`, no `.map`, no debug symbols.
- **Goal:** recover enough to (a) fully document the stock firmware and (b) be able
  to write a pin-compatible replacement.
- **Unknowns to solve:** which GPIOs drive the multiplexer, which GPIOs read the
  buttons, the decoder's bit-to-antenna mapping, and the HTTP/AP behaviour.

The hard part is that GPIO assignments are **not** text — they are integer literals
buried in compiled Xtensa machine code. So the work splits into: identify the
firmware from strings (easy), then disassemble and trace the code to the pin numbers
(the real effort).

## 2. Toolchain

Everything needed was already present via PlatformIO / pip:

| Tool | Use |
|------|-----|
| `strings`, `grep`, `xxd`, `dd` | triage, locate byte offsets, dump regions |
| `python3` | parse the image, build an ELF, decode instructions, script the trace |
| `xtensa-esp32-elf-objdump` | disassemble Xtensa code |

Key insight about the disassembler: the ESP8266 CPU is an **Xtensa LX106**. There is
no ESP8266-specific objdump on the machine, but the **esp32 toolchain's**
`xtensa-esp32-elf-objdump` (for the LX6) shares the same base Xtensa ISA and
disassembles ESP8266 code correctly. Path:

```
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump
```

## 3. Step 1 — String triage: identify the firmware

```bash
strings -n 6 esp8266_...bin | grep -iE 'antenn|switch|wifi|http|version|sdk'
```

Highlights that immediately fingerprinted the firmware:

```
ANTENNA SELECTOR V2
ANTENI.NET Ltd.
2.2.2-dev(38a443e)
SDK ver: %s compiled @ Jul  3 2019 15:53:05
AutoConnectAP
http://192.168.4.1
https://github.com/tzapu/WiFiManager
<body><h1>Web ANTENNAS switch</h1>
GET /5/on   GET /4/on   GET /5/off   GET /4/off
ANT: 1 … ANT: 7    GROUND    MAX
pres ERASE for erase    stored information    hold the button
core_esp8266_main.cpp   Schedule.cpp   (Arduino core)
```

Conclusion: an **Arduino-core ESP8266** sketch, NONOS-SDK 2.2.2 (mid-2019), using the
**tzapu WiFiManager** library, with an up/down antenna web UI, an OLED, and four
buttons. What strings *cannot* tell us: the GPIO numbers. That requires disassembly.

## 4. Step 2 — Understand the ESP8266 flash image format

An ESP8266 firmware image begins with an **8-byte header**:

| Offset | Field |
|--------|-------|
| 0 | magic `0xE9` |
| 1 | segment count |
| 2 | flash mode |
| 3 | flash size / frequency |
| 4–7 | entry point (little-endian) |

then a series of segments, each `(load_addr: u32, length: u32, data[length])`.

Parsing the very start of the file (offset 0x0) showed a **1-segment** image — that's
the **bootloader** stub. The real application image begins at flash offset **0x1000**
(the standard ESP8266 layout), which is where we parsed the header that mattered.

## 5. Step 3 — Parse the segment table

Reading the header at 0x1000 yielded `magic 0xE9, segs 5, entry 0x401000b8` and five
segments:

```
seg0: load=0x40201010 len=0x50580 fileoff=0x1010   ; flash-cached code + rodata
seg1: load=0x40100000 len=0x00104 fileoff=0x51598   ; IRAM vectors
seg2: load=0x40100104 len=0x0691c fileoff=0x516a4   ; IRAM code (.text)
seg3: load=0x3ffe8000 len=0x00508 fileoff=0x57fc8   ; DRAM .data
seg4: load=0x3ffe8510 len=0x00d54 fileoff=0x584d8   ; DRAM .rodata
```

This mapping (**virtual address ⇄ file offset**) is the Rosetta Stone for the rest of
the analysis: it lets us read the *value* stored at any virtual address, and later
tell objdump where each byte lives.

## 6. Step 4 — The ESP8266 / Xtensa memory map

Interpreting the segments requires knowing the address windows:

| Window | Region | Notes |
|--------|--------|-------|
| `0x40200000`–`0x402FFFFF` | **flash cache (irom0)** | `.irom0.text` — code + most rodata; vaddr = flash offset + `0x40200000` |
| `0x40100000`–`0x40107FFF` | **IRAM** | fast RAM code (`.text`), incl. the Arduino GPIO primitives |
| `0x3FFE8000`–`0x3FFFFFFF` | **DRAM** | `.data` / `.rodata` — short strings, globals |
| `0x60000000`–`0x60000FFF` | **peripheral registers** | UART, GPIO, etc. (memory-mapped I/O) |

Two facts from this table drive later steps:
- Our main strings (e.g. `ANT: 1` at file `0x58614`) map to vaddr `0x40258614`
  (= `0x40200000 + 0x58614`), i.e. they live in **irom0** rodata.
- GPIO hardware lives at **`0x600003xx`**; UART0 at **`0x60000000`**. Distinguishing
  these mattered (see Step 9).

## 7. Step 5 — Build an ELF wrapper for objdump

`objdump -d` will only disassemble bytes that live in real **sections** with the
`CODE` flag and correct virtual addresses. A raw `.bin` has neither. So
[`scripts/analysis/mkelf.py`](../scripts/analysis/mkelf.py) synthesises a minimal **ELF32
(EM_XTENSA=94, little-endian, ET_EXEC)** and places each image segment into its own
`SHT_PROGBITS` section at its true load address:

```
.irom0text   0x40201010  (ALLOC|EXEC)   seg0
.iram0vec    0x40100000  (ALLOC|EXEC)   seg1
.iram0text   0x40100104  (ALLOC|EXEC)   seg2
.dram0a      0x3ffe8000  (ALLOC|WRITE)  seg3
.dram0b      0x3ffe8510  (ALLOC|WRITE)  seg4
```

The script writes the ELF header, a section-header string table, and the section
headers. Verifying with `objdump -h` confirmed the sections were recognised as
`CODE`/`DATA` at the right VMAs — the prerequisite for meaningful disassembly.

> First version of the script emitted **program headers** (PT_LOAD) instead of
> section headers. `objdump -d` ignored them (it disassembles sections), producing an
> empty listing. Switching to `SHT_PROGBITS` **sections** fixed it.

## 8. Step 6 — First disassembly attempt & why it failed

```bash
xtensa-esp32-elf-objdump -d -m xtensa build/fw.elf > build/fw.dis
```

This produced ~136,000 lines and ~10,700 `l32r` instructions. Good. But when
extracting pointer constants, the naive approach failed for a subtle reason.

Xtensa loads 32-bit constants via **`l32r aN, <literal_addr>`** — a PC-relative load
from a *literal pool*. objdump prints the literal's address, e.g.:

```
40201015:  c1d0c9   l32r  a12, 0x401f3758
```

To know *what constant* is loaded (e.g. a string pointer or a register address), you
must read the 4 bytes stored **at that literal address**. Two problems appeared:

1. Some literal addresses (like `0x401f3758`) fall **outside** our mapped segments,
   so reading returned nothing.
2. A byte-scan for the `l32r` opcode produced misaligned false decodes (Xtensa is a
   variable-length ISA: 2- and 3-byte instructions interleave, so you can't just step
   one byte at a time).

The fix for (2) is to trust objdump's instruction boundaries and parse *its* output.
The fix for (1) is to correctly compute the literal address — which required
calibrating the `l32r` formula (next step).

## 9. Step 7 — Decoding `l32r` by hand (calibration)

To read literal values reliably, the exact `l32r` target formula was calibrated
against objdump ground truth. The instruction at `0x40201015` disassembles to
`l32r a12, 0x401f3758`; its raw bytes in memory are `c1 d0 c9`:

- opcode `op0 = byte0 & 0x0F = 0x1` ✔ (l32r)
- `imm16 = byte1 | (byte2 << 8) = 0xd0 | (0xc9<<8) = 0xC9D0`

Solving for the published target `0x401f3758`:

```
target = ((PC + 3) & ~3) + (sign_extend16(imm16) << 2)
       = ((0x40201015 + 3) & ~3) + ((0xC9D0 - 0x10000) << 2)
       = 0x40201018 + (-0x3630 << 2)
       = 0x401f3758                                   ✔ exact match
```

So: **`target = ((PC+3) & ~3) + ((imm16 - 0x10000) << 2)`**. With this, any `l32r`'s
literal address is computable, and the literal *value* is read via the
vaddr→file-offset map from Step 5. (Literals outside our sections are simply pointers
into regions we don't need, e.g. into other flash areas.)

## 10. Step 8 — Locating the GPIO hardware registers

Rather than guess pins, the strategy was to find the code that touches GPIO
**hardware registers**, then work outward. The ESP8266 GPIO block:

| Register | Address |
|----------|---------|
| `GPIO_OUT`        | `0x60000000`* |
| `GPIO_OUT_W1TS`   | `0x60000304` |
| `GPIO_OUT_W1TC`   | `0x60000304`/`0x60000308` (set/clear) |
| `GPIO_ENABLE`     | `0x60000308` |
| `GPIO_ENABLE_W1TC`| `0x60000310` |
| `GPIO_IN`         | `0x60000318` |

Scanning the disassembly for `l32r` whose **value** matched one of these (reading the
literal from the ELF) found the interesting spots — notably in IRAM around
`0x4010031f` (`GPIO_OUT_W1TC`), `0x40100325` (`GPIO_ENABLE`), `0x40100368`
(`GPIO_IN`).

## 11. Step 9 — A dead-end: UART mistaken for GPIO

An early register map wrongly labelled **`0x60000000` as `GPIO_OUT`**. Disassembling
functions around it (e.g. `0x40211d3e`, `0x4021275f`) showed tight loops polling a
status field and writing single bytes — which turned out to be **`uart_write` /
`Serial.write`** (`0x60000000` is the UART0 FIFO; `0x6000001c` is UART1;
`0x60000f00`/`0x60000f1c` are UART status). Recognising this ruled out those
functions and refocused the search on the true GPIO block at **`0x600003xx`**. This
correction was essential — otherwise Serial output would have been misread as pin
toggling.

## 12. Step 10 — Finding the Arduino GPIO primitives

Reading the IRAM code around the real GPIO registers revealed the two Arduino core
functions (the compiler kept them in fast IRAM):

**`__digitalWrite(pin=a2, val=a3)` @ `0x401002fc`:**

```
40100311:  movi.n  a2, 15
40100313:  bltu    a2, a12, ...        ; pin > 15 ? (handle GPIO16 separately)
40100316:  movi    a2, 1
40100319:  ssl     a12                 ; shift amount = pin
4010031c:  sll     a12, a2             ; a12 = 1 << pin
4010031f:  l32r    a3, =0x60000304     ; GPIO_OUT_W1TS
40100322:  bnez    a13, ...            ; if val != 0 use W1TS else W1TC(0x308)
40100325:  l32r    a3, =0x60000308
40100328:  memw
4010032b:  s32i.n  a12, a3, 0          ; write (1<<pin) to set/clear register
```

**`__digitalRead(pin=a2)` @ `0x40100360`:**

```
40100368:  l32r    a2, =0x60000318     ; GPIO_IN
40100370:  l32i.n  a2, a2, 0
40100372:  ssl     a3                  ; shift = pin
40100375:  sll     a3, a4              ; a3 = 1 << pin
40100378:  and     a3, a3, a2
...        returns (GPIO_IN >> pin) & 1  ; special-cases GPIO16 at 0x6000078c
```

These two addresses are the anchors for recovering the pins.

## 13. Step 11 — Recovering constant pin arguments

The sketch does **not** `call0 0x401002fc` directly; it calls indirectly:

```
l32r a0, =0x401002fc      ; load address of __digitalWrite
...
callx0 a0                 ; call it
```

So [`scripts/analysis/pins.py`](../scripts/analysis/pins.py):

1. scans for every `l32r aN, <lit>` where the literal **value** equals
   `0x401002fc` (digitalWrite) or `0x40100360` (digitalRead);
2. finds the next `callx0 aN` using that same register;
3. walks **backwards** from the call to the most recent `movi`/`movi.n` into the
   argument registers **a2** (pin) and **a3** (value), recovering the constants.

Results (abridged):

```
=== digitalWrite: 33 indirect call sites ===
  pin=14 val=1 | pin=12 val=0 | pin=13 val=0     (repeating 14/12/13 triples)
  ...
  pin=1  val=1 | pin=3 val=1 | pin=2 val=1 | pin=0 val=1   (setup block)

=== digitalRead: 8 indirect call sites ===
  pin=2 | pin=3 | pin=0 | pin=1 | pin=3 | pin=1 | pin=3
```

Interpretation:
- **Outputs = GPIO12, 13, 14** → the three 74HCT138 select lines.
- **Inputs = GPIO0, 1, 2, 3** → the four buttons.

## 14. Step 12 — Reading `setup()`

The one-off block at `0x40204504` is the initialisation:

```
digitalWrite(14, 1)   ; A2  \
digitalWrite(12, 1)   ; A0   } decoder lines preset to 111
digitalWrite(13, 1)   ; A1  /
digitalWrite(1,  1)   ; DOWN  \
digitalWrite(3,  1)   ; UP     } button pins driven HIGH = pull-ups enabled,
digitalWrite(2,  1)   ; ERASE  } buttons are active-LOW
digitalWrite(0,  1)   ; SET   /
```

This confirms directions: 12/13/14 outputs, 0/1/2/3 inputs with pull-ups (active-low
buttons).

## 15. Step 13 — Decoding the 74HCT138 truth table

[`scripts/analysis/decode_table.py`](../scripts/analysis/decode_table.py) groups every consecutive
`digitalWrite(14)`, `digitalWrite(12)`, `digitalWrite(13)` triple and prints the
3-bit code. Eight **distinct** codes were found, forming a clean 0–7 sequence:

```
G14 G12 G13   pos (A0 A1 A2)
 0   0   0     0  (000)  GROUND
 1   0   0     1  (001)  ANT 1
 0   1   0     2  (010)  ANT 2
 1   1   0     3  (011)  ANT 3
 0   0   1     4  (100)  ANT 4
 1   0   1     5  (101)  ANT 5
 0   1   1     6  (110)  ANT 6
 1   1   1     7  (111)  ANT 7
```

So **A0=GPIO14, A1=GPIO12, A2=GPIO13**, and the firmware simply writes the antenna
index as binary across those lines. The disassembly write order (GPIO14 → GPIO12 →
GPIO13) was initially interpreted as A2/A0/A1, but physical relay testing confirmed
the actual PCB traces connect them as A0/A1/A2 respectively.
Full table in [`74hct138-truth-table.md`](74hct138-truth-table.md).

## 16. Step 14 — Identifying each button's role

Using [`scripts/analysis/dumpfunc.py`](../scripts/analysis/dumpfunc.py) — a windowed disassembler that
annotates `l32r` literal values *and* resolves DRAM string pointers to text — each
`digitalRead` handler was read:

**UP / DOWN** (`0x40204c44`):

```
digitalRead(1) -> if pressed (LOW): counter = counter - 1     ; DOWN = GPIO1
digitalRead(3) -> if pressed (LOW): counter = counter + 1     ; UP   = GPIO3
if (counter == -1) counter = <stored max>                     ; wrap
```

**SET / max count** (`0x402048d5`): `digitalRead(0)` increments a value bounded by
`bgei a2, 8`, and its handler calls the display routine at `0x40204750`, which draws
the string at `0x3ffe8735`. Resolving that pointer → **`"MAX"`**. Hence GPIO0 = SET
(sets the maximum number of antennas).

**ERASE** (`0x402045f9`): `digitalRead(2)` enters a branch that draws the strings at
`0x3ffe86b0`/`0x3ffe86d9`/`0x3ffe86df` — resolved to
**`"pres ERASE for erase stored information"`**, **`"ERASE"`**, **`"hold the button"`**
— with `0x9c4`/`0xbb8` (2500/3000 ms) hold delays. Hence GPIO2 = ERASE (hold to wipe
WiFi config).

String pointers were resolved by mapping the DRAM vaddr back into the ELF and reading
until the null terminator — e.g. `0x3ffe8735 → "MAX"`, `0x3ffe86df → "hold the button"`.
Details in [`buttons.md`](buttons.md).

## 17. Step 15 — Reconstructing the web & AP interface

With the firmware identified, targeted `grep`/`dd` over the flash reassembled the
served content in flash order:

- **App server:** parses the request line for `GET /5/on` (Up) and `GET /4/on` (Dn);
  there is **no direct-select route**. The full served HTML page, CSS, HTTP headers
  (`HTTP/1.1 200 OK` / `Content-type:text/html` / `Connection: close`) and the dynamic
  `ANT: n`/`GROUND` label were recovered — see [`web-interface.md`](web-interface.md).
- **WiFiManager captive portal:** SoftAP **`AutoConnectAP`** at **`192.168.4.1`**,
  pages `/wifi /0wifi /wifisave /param /paramsave /info /close /exit /erase /restart
  /update /u`, and station-mode mDNS hostname **`ESP-45CA21.local`** (from the MAC,
  stored at flash `0x3fd0b4`). Full detail in [`wifi-ap-config.md`](wifi-ap-config.md).

## 18. Full reproduction commands

All analysis scripts live in [`../scripts/analysis/`](../scripts/analysis/) and are
run from the **repository root**. Regenerable artifacts are written to `build/`.

```bash
mkdir -p build

# --- 0. parse the image segment table (sanity) ---
python3 - <<'PY'
import struct
d=open('esp8266_cc50e345ca21_flash_backup_20260703.bin','rb').read()
b=0x1000; off=b+8
for i in range(d[b+1]):
    la,ln=struct.unpack('<II', d[off:off+8]); off+=8
    print(f"seg{i}: load=0x{la:08x} len=0x{ln:x} fileoff=0x{off:x}"); off+=ln
PY

# --- 1. build the ELF wrapper and disassemble ---
python3 scripts/analysis/mkelf.py                         # -> build/fw.elf
OBJDUMP=~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump
"$OBJDUMP" -h build/fw.elf                                # verify CODE/DATA sections
"$OBJDUMP" -d -m xtensa build/fw.elf > build/fw.dis

# --- 2. recover pins (traces digitalWrite/digitalRead call sites) ---
python3 scripts/analysis/pins.py

# --- 3. decode the 74HCT138 truth table ---
python3 scripts/analysis/decode_table.py

# --- 4. inspect any address window with literal + string annotations ---
python3 scripts/analysis/dumpfunc.py 0x40204750-0x402047d0   # the "MAX" display routine
python3 scripts/analysis/dumpfunc.py 0x40204504-0x40204548   # setup() GPIO init
python3 scripts/analysis/dumpfunc.py 0x40204c44-0x40204c70   # UP/DOWN button logic
```

Script reference:

| Script | Purpose |
|--------|---------|
| [`scripts/analysis/mkelf.py`](../scripts/analysis/mkelf.py) | Wrap the raw image segments in an Xtensa ELF with correct section VMAs |
| [`scripts/analysis/pins.py`](../scripts/analysis/pins.py) | Find indirect calls to the GPIO primitives and recover constant pin/value args |
| [`scripts/analysis/decode_table.py`](../scripts/analysis/decode_table.py) | Group select-line writes into the decoder truth table |
| [`scripts/analysis/dumpfunc.py`](../scripts/analysis/dumpfunc.py) | Windowed disassembler with `l32r` literal + DRAM-string annotation |
| [`scripts/analysis/analyze.py`](../scripts/analysis/analyze.py) | Earlier GPIO-register scan / caller finder (exploratory) |

## 19. Dumping / restoring from hardware

To pull a fresh image from the device, or to restore the original backup, use the
helper scripts (they auto-locate esptool, including the PlatformIO-bundled copy):

```bash
# Dump full 4 MB flash (default port /dev/ttyACM0):
./scripts/dump_firmware.sh /dev/ttyACM0
#   -> esp8266_dump_<MAC>_<date>.bin  (+ sha256)

# Restore an image (e.g. the original backup):
./scripts/restore_firmware.sh esp8266_cc50e345ca21_flash_backup_20260703.bin /dev/ttyACM0
```

Equivalent raw esptool commands:

```bash
# Dump
esptool.py --chip esp8266 --port /dev/ttyACM0 --baud 460800 \
    read_flash 0x0 0x400000 backup.bin

# Restore
esptool.py --chip esp8266 --port /dev/ttyACM0 --baud 460800 \
    write_flash --flash_size detect 0x0 backup.bin
```

See [`scripts/dump_firmware.sh`](../scripts/dump_firmware.sh) and
[`scripts/restore_firmware.sh`](../scripts/restore_firmware.sh).

## Appendix — key addresses discovered

| Address | Meaning |
|---------|---------|
| `0x401002fc` | `__digitalWrite(pin, val)` (Arduino core, IRAM) |
| `0x40100360` | `__digitalRead(pin)` (Arduino core, IRAM) |
| `0x40204504` | `setup()` GPIO initialisation block |
| `0x40204750` | OLED routine that draws `"MAX"` (SET button) |
| `0x40204c44` | UP/DOWN button handling |
| `0x402045f9` | ERASE button handling |
| `0x402048d5` | SET button handling |
| `0x3ffe8735` | string `"MAX"` |
| `0x3ffe86b0` | string `"pres ERASE for erase "` |
| `0x3ffe86df` | string `"hold the button"` |
| `0x3fd0b4`   | stored hostname `"ESP-45CA21"` (SDK config area) |
| `0x60000304`/`0x60000308` | GPIO_OUT set/clear registers |
| `0x60000318` | GPIO_IN register |
| `0x60000000` | UART0 FIFO (the "GPIO_OUT" false-positive in Step 9) |
