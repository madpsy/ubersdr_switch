# Documentation Index — ANTENNA SELECTOR V2

Reverse-engineering documentation for the ANTENI.NET Ltd. "ANTENNAS webSWITCH control
1 to 5" board, recovered from the ESP8266 flash dump
[`../esp8266_cc50e345ca21_flash_backup_20260703.bin`](../esp8266_cc50e345ca21_flash_backup_20260703.bin).

## Contents

| Document | What's inside |
|----------|---------------|
| [`schematic.md`](schematic.md) | Redrawn schematic / block diagram of the 5-port switch |
| [`hardware-pinout.md`](hardware-pinout.md) | Complete GPIO map, `setup()` init, signal chain, power |
| [`74hct138-truth-table.md`](74hct138-truth-table.md) | Decoder select-line → antenna truth table + selection code |
| [`buttons.md`](buttons.md) | The four buttons (UP / DOWN / SET / ERASE), pins and behaviour |
| [`web-interface.md`](web-interface.md) | The stock HTTP server, routes, and the exact served HTML |
| [`wifi-ap-config.md`](wifi-ap-config.md) | WiFiManager captive portal, SoftAP, mDNS, OTA, erase flow |
| [`firmware-analysis.md`](firmware-analysis.md) | **Full end-to-end reverse-engineering walkthrough** |

## Quick reference

### GPIO map

| GPIO | Function |
|------|----------|
| GPIO12 | 74HCT138 **A0** (LSB) |
| GPIO13 | 74HCT138 **A1** |
| GPIO14 | 74HCT138 **A2** (MSB) |
| GPIO0  | **SET** button (max antenna count) |
| GPIO1  | **DOWN** button |
| GPIO2  | **ERASE** button (hold) |
| GPIO3  | **UP** button |
| GPIO4 / GPIO5 | I2C OLED (SDA / SCL) |

### Antenna selection code

Write the position index (0 = GROUND, 1–5 = ANT1–5) as binary across the three
select lines:

```c
digitalWrite(12, (pos >> 0) & 1);  // A0
digitalWrite(13, (pos >> 1) & 1);  // A1
digitalWrite(14, (pos >> 2) & 1);  // A2
```

### Device identity

- Firmware: **"ANTENNA SELECTOR V2"**, ANTENI.NET Ltd.
- Chip: ESP8266, NONOS-SDK 2.2.2, Arduino core, built 3 Jul 2019
- MAC: `cc:50:e3:45:ca:21` → hostname/mDNS **`ESP-45CA21.local`**
- Config AP: **`AutoConnectAP`** @ **`http://192.168.4.1`** (WiFiManager)

## Tooling

- [`../scripts/analysis/`](../scripts/analysis/) — the reverse-engineering scripts
  (see its [`README.md`](../scripts/analysis/README.md))
- [`../scripts/dump_firmware.sh`](../scripts/dump_firmware.sh) — read flash from the device
- [`../scripts/restore_firmware.sh`](../scripts/restore_firmware.sh) — write an image back
