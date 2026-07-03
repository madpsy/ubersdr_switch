# ubersdr_switch — MS-S5 WiFi Antenna Switch reverse engineering

Reverse-engineering and documentation of the **MS-S5 WiFi-controlled low-power
antenna switch** (ANTENI.NET Ltd. "ANTENNAS webSWITCH control 1 to 5") — an
ESP8266-based, WiFi-controlled 5-way antenna switch, **compatible with KiwiSDR** (and
other SDR/receiver setups) — recovered from a raw 4 MB flash dump
([`esp8266_cc50e345ca21_flash_backup_20260703.bin`](esp8266_cc50e345ca21_flash_backup_20260703.bin)).

The device selects one of five antennas onto a single receiver ("Radio") port, making
it suitable as a low-power front-end antenna selector for a KiwiSDR or similar
web-based SDR receiver.

## What this repo contains

- **Full documentation** of the hardware and stock firmware in [`docs/`](docs/).
- **Replica firmware** in [`firmware/`](firmware/) — a clean, interface-compatible
  re-implementation that adds a **modern dark web UI** (default at `/`), a **JSON
  REST API** for scripting antenna selection (see **[`docs/rest-api.md`](docs/rest-api.md)**),
  and optional **MQTT** support (see **[`docs/mqtt.md`](docs/mqtt.md)**).
  The classic stock page is preserved at `/old`.
- **Reproducible analysis scripts** in [`scripts/analysis/`](scripts/analysis/) that
  recover the pinout directly from the binary.
- **Flash dump / restore scripts** in [`scripts/`](scripts/).
- The original firmware image, so the device can always be restored.

## REST API (quick look)

The replica firmware exposes a flexible JSON API on port 80 for controlling the switch
from scripts or a browser. Full reference: **[`docs/rest-api.md`](docs/rest-api.md)**.

```bash
HOST=http://ESP-45CA21.local        # or the device IP

curl "$HOST/api/status"                    # -> {"position":3,"label":"ANT: 3",...}
curl -X POST "$HOST/api/antenna?position=2"  # select ANT2
curl -X POST "$HOST/api/antenna/up"          # step up
curl -X POST "$HOST/api/antenna/ground"      # select GROUND
```

## Board overview

```
ESP8266  ──GPIO12/13/14──►  74HCT138 (3-to-8 decoder)  ──►  relays RL1–RL5
  │                                                          (one antenna A1–A5
  ├─ GPIO0/1/2/3 : SET / DOWN / ERASE / UP buttons            → common "Radio" SMA)
  ├─ GPIO4/5     : I2C OLED (0.91" 128×32 SSD1306)
  └─ GPIO16      : OLED reset (Heltec WiFi Kit 8)
```

The MCU is a **Heltec WiFi Kit 8 (HTIT-W8266)** — an ESP8266 module with an on-board
0.91" 128×32 SSD1306 OLED whose reset line is wired to GPIO16.

The 74HCT138 guarantees exactly one relay — hence one antenna — is connected at a
time. 12 VDC powers the relay coils; an on-board regulator powers the ESP8266.

## Key facts (recovered)

| | |
|---|---|
| Product | MS-S5 WiFi antenna switch (ANTENI.NET "ANTENNAS webSWITCH control 1 to 5"), KiwiSDR-compatible |
| Firmware | "ANTENNA SELECTOR V2", ANTENI.NET Ltd. (Arduino / NONOS-SDK 2.2.2, 2019) |
| Multiplexer select | GPIO12=A0, GPIO13=A1, GPIO14=A2 (writes antenna index as binary) |
| Buttons | GPIO0=SET, GPIO1=DOWN, GPIO2=ERASE (hold), GPIO3=UP (all active-low) |
| Board | Heltec WiFi Kit 8 (HTIT-W8266), ESP8266 |
| Display | 0.91" 128×32 SSD1306 OLED, I²C on GPIO4/GPIO5, reset on GPIO16 |
| WiFi setup | tzapu WiFiManager — AP `AutoConnectAP` @ `http://192.168.4.1` |
| Station access | `http://ESP-45CA21.local` (mDNS), web UI + OTA update |
| Web control | `GET /5/on` (Up), `GET /4/on` (Dn) — **no direct-select in stock FW** |
| REST API | Replica firmware adds a JSON `/api/*` API (direct select, status, max) — see [`docs/rest-api.md`](docs/rest-api.md) |

Full detail: **[`docs/README.md`](docs/README.md)**.

## Documentation

Start at the documentation index: **[`docs/README.md`](docs/README.md)**.

| Document | Topic |
|----------|-------|
| [`docs/README.md`](docs/README.md) | Documentation index + quick reference |
| [`docs/schematic.md`](docs/schematic.md) | Redrawn schematic, block diagram, and front/rear panel layout |
| [`docs/hardware-pinout.md`](docs/hardware-pinout.md) | Complete GPIO map, init, signal chain, power |
| [`docs/74hct138-truth-table.md`](docs/74hct138-truth-table.md) | Decoder select-line → antenna truth table |
| [`docs/buttons.md`](docs/buttons.md) | The four physical buttons (UP / DOWN / SET / ERASE) |
| [`docs/web-interface.md`](docs/web-interface.md) | Full stock HTTP interface + exact served HTML |
| [`docs/rest-api.md`](docs/rest-api.md) | JSON REST API (`/api/*`) for scripting antenna selection |
| [`docs/mqtt.md`](docs/mqtt.md) | MQTT — topics, payloads, commands, HA example, config API |
| [`docs/wifi-ap-config.md`](docs/wifi-ap-config.md) | AP / captive portal / mDNS / OTA |
| [`docs/firmware-analysis.md`](docs/firmware-analysis.md) | **End-to-end reverse-engineering walkthrough** |
| [`PINOUT.md`](PINOUT.md) | Quick pin reference (redirects into `docs/`) |
| [`scripts/analysis/README.md`](scripts/analysis/README.md) | How the analysis scripts work |

## Dumping / restoring firmware

```bash
# Read the full 4 MB flash from the device (default port /dev/ttyACM0)
./scripts/dump_firmware.sh /dev/ttyACM0

# Restore the original backup
./scripts/restore_firmware.sh esp8266_cc50e345ca21_flash_backup_20260703.bin /dev/ttyACM0
```

Both scripts auto-locate `esptool` (including the PlatformIO-bundled copy). See
[`scripts/dump_firmware.sh`](scripts/dump_firmware.sh) and
[`scripts/restore_firmware.sh`](scripts/restore_firmware.sh).

## Reproducing the analysis

```bash
mkdir -p build
python3 scripts/analysis/mkelf.py
~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump \
    -d -m xtensa build/fw.elf > build/fw.dis
python3 scripts/analysis/pins.py           # recover GPIO pins
python3 scripts/analysis/decode_table.py   # decode the 74HCT138 table
```

See [`scripts/analysis/README.md`](scripts/analysis/README.md) and
[`docs/firmware-analysis.md`](docs/firmware-analysis.md).

## Writing replacement firmware

Everything needed for a pin-compatible rewrite is documented. A new sketch would:

- drive GPIO12/13/14 with the antenna index (0=GROUND, 1–5=ANT1–5),
- read the four buttons on GPIO0–3 (active-low),
- drive the OLED on GPIO4/5,
- add a proper web UI / REST API with **direct antenna selection** (the stock
  firmware only supports up/down stepping).

The original firmware can always be restored from the included image.
