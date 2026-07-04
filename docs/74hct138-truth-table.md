# 74HCT138 Decoder Truth Table

The three ESP8266 outputs **GPIO14 (A0) / GPIO12 (A1) / GPIO13 (A2)** feed the
74HCT138 3-to-8 decoder select inputs. The firmware writes the antenna position
index (0–7) as a plain binary number across these three lines.

> **Note:** the disassembly write order is GPIO14 → GPIO12 → GPIO13, which was
> initially interpreted as A2/A0/A1. Physical relay testing showed the actual PCB
> traces connect them as GPIO14→A0, GPIO12→A1, GPIO13→A2. The raw GPIO values in
> the table below are unchanged (they come directly from the disassembly); only the
> column labels and the bit-weight interpretation are corrected here.

This table was recovered by grouping every consecutive
`digitalWrite(14)`, `digitalWrite(12)`, `digitalWrite(13)` triple in the
disassembly and reading the values — see
[`scripts/analysis/decode_table.py`](../scripts/analysis/decode_table.py). Eight unique codes were
found (a full monotonic 0–7 sequence).

| Position | GPIO14 (A0) | GPIO12 (A1) | GPIO13 (A2) | Binary | 74HCT138 output | Meaning |
|:--------:|:-----------:|:-----------:|:-----------:|:------:|:---------------:|---------|
| 0 | 0 | 0 | 0 | 000 | Y0 | **GROUND** |
| 1 | 1 | 0 | 0 | 001 | Y1 | ANT 1 |
| 2 | 0 | 1 | 0 | 010 | Y2 | ANT 2 |
| 3 | 1 | 1 | 0 | 011 | Y3 | ANT 3 |
| 4 | 0 | 0 | 1 | 100 | Y4 | ANT 4 |
| 5 | 1 | 0 | 1 | 101 | Y5 | ANT 5 |
| 6 | 0 | 1 | 1 | 110 | Y6 | ANT 6 (unpopulated on 1-to-5 board) |
| 7 | 1 | 1 | 1 | 111 | Y7 | ANT 7 (unpopulated on 1-to-5 board) |

## Notes

- The firmware supports **8 positions** (GROUND + ANT1–7) in software, but this
  particular board populates only **five relays (RL1–RL5)**, i.e. ANT1–ANT5.
- The **SET** button (GPIO0) limits the top of the selectable range (the stored
  "max antennas" value, default 7, held in DRAM at `0x3ffe84e0`).
- The position counter wraps: incrementing past the max returns to GROUND
  (disassembly shows `bgei a2, 8` bounding the count).

## Raw recovered blocks

```
addr         G14 G12 G13    -> position (A0 A1 A2)
0x40204004    1   0   0     4   (100)
0x40204090    0   1   0     2   (010)
0x4020411c    1   1   0     6   (110)
0x402041a8    0   0   1     1   (001)
0x40204234    1   0   1     5   (101)
0x402042c0    0   1   1     3   (011)
0x4020434c    1   1   1     7   (111)
0x402043d8    0   0   0     0   (000)
0x4020450b    1   1   1     7   (setup default, before restore)
```

## Selecting an antenna in new firmware

```c
// pos: 0 = GROUND, 1..5 = ANT1..ANT5
// Actual PCB wiring: GPIO14=A0(LSB), GPIO12=A1, GPIO13=A2(MSB)
void selectAntenna(uint8_t pos) {
    digitalWrite(14, (pos >> 0) & 1);   // A0 (LSB)
    digitalWrite(12, (pos >> 1) & 1);   // A1
    digitalWrite(13, (pos >> 2) & 1);   // A2 (MSB)
}
```
