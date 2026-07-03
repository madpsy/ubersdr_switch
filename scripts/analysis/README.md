# Firmware Analysis Scripts

These Python scripts reverse-engineer the ESP8266 "ANTENNA SELECTOR V2" flash dump
([`../../esp8266_cc50e345ca21_flash_backup_20260703.bin`](../../esp8266_cc50e345ca21_flash_backup_20260703.bin))
to recover the GPIO pinout, the 74HCT138 decoder truth table, and the button mapping.

The full narrative of how they were used is in
[`../../docs/firmware-analysis.md`](../../docs/firmware-analysis.md).

## Requirements

- Python 3
- `xtensa-esp32-elf-objdump` (bundled with PlatformIO's esp32 toolchain; the ESP8266
  Xtensa LX106 shares the base ISA so this disassembles the image correctly):
  `~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump`

## Scripts

| Script | Purpose |
|--------|---------|
| [`mkelf.py`](mkelf.py) | Wrap the raw flash image's segments in a synthetic Xtensa ELF (`build/fw.elf`) with correct section virtual addresses, so objdump can disassemble it. |
| [`pins.py`](pins.py) | Find indirect (`l32r`+`callx0`) calls to the Arduino core `__digitalWrite`/`__digitalRead` primitives and recover the constant pin/value arguments → reveals which GPIOs are the multiplexer lines vs the buttons. |
| [`decode_table.py`](decode_table.py) | Group consecutive select-line writes (GPIO14/13/12) into the 74HCT138 truth table. |
| [`dumpfunc.py`](dumpfunc.py) | Windowed disassembler that annotates `l32r` literal values and resolves DRAM string pointers to text — used to read individual handlers. |
| [`analyze.py`](analyze.py) | Exploratory GPIO-register scan / caller finder (used during discovery). |

## Usage (run from the repository root)

```bash
mkdir -p build

# 1. Build the ELF wrapper and disassemble
python3 scripts/analysis/mkelf.py                        # -> build/fw.elf
OBJDUMP=~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump
"$OBJDUMP" -d -m xtensa build/fw.elf > build/fw.dis      # -> build/fw.dis

# 2. Recover GPIO pins
python3 scripts/analysis/pins.py

# 3. Decode the 74HCT138 truth table
python3 scripts/analysis/decode_table.py

# 4. Inspect any address window (hex range or center address)
python3 scripts/analysis/dumpfunc.py 0x40204750-0x402047d0
python3 scripts/analysis/dumpfunc.py 0x40204504
```

`build/fw.elf` and `build/fw.dis` are regenerable intermediate artifacts and are
git-ignored.
