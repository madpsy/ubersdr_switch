# UberSDR Antenna Switch

WiFi-controlled antenna switch firmware for the **MS-S5 / ANTENI.NET "ANTENNAS webSWITCH"** — an ESP8266-based 5-way antenna switch compatible with KiwiSDR and other SDR setups.

---

## Quick install (flash prebuilt firmware)

> **Requirements:** a USB cable, Python 3, and `esptool` (or PlatformIO).
> No compiler needed — prebuilt binaries are included in [`firmware/prebuilt/`](firmware/prebuilt/).

```bash
# Clone the repo
git clone https://github.com/your-org/ubersdr_switch.git
cd ubersdr_switch

# Flash to the connected device (auto-detects port, prompts for WiFi credentials)
./firmware/flash.sh
```

`flash.sh` will:
1. Scan for connected USB-serial devices and let you pick one
2. Optionally bake in your WiFi SSID + password so the device joins your network on first boot
3. Flash the firmware **and** the web assets (LittleFS) in one step

After flashing, the device reboots and either:
- Joins your WiFi network (if credentials were provided) and shows its IP on the OLED
- Opens a captive portal at **`http://192.168.4.1`** (connect to `AutoConnectAP` WiFi) to configure WiFi

Once connected, open **`http://ESP-XXXXXX.local`** (or the IP shown on the OLED) in a browser.

### First-time WiFi setup (no credentials baked in)

1. Power on the device — the OLED shows `ssid: AutoConnectAP`
2. Connect your phone/laptop to the **`AutoConnectAP`** WiFi network
3. Browse to **`http://192.168.4.1`** → Configure WiFi → enter your SSID + password → Save
4. The device reboots and joins your network; the OLED shows the assigned IP for 2 seconds

### Flash options

```bash
./firmware/flash.sh                    # auto-detect port, flash all (default)
./firmware/flash.sh /dev/ttyACM0       # specify port explicitly
./firmware/flash.sh --fw  /dev/ttyACM0 # firmware only
./firmware/flash.sh --fs  /dev/ttyACM0 # web assets (LittleFS) only
./firmware/flash.sh --no-wifi          # skip WiFi credential prompt
./firmware/flash.sh --ssid MyNet --pass secret  # non-interactive
```

### Physical buttons (quick reference)

| Button | Short press | Hold 3 s |
|--------|-------------|----------|
| **▲ UP** *(green)* | Next antenna | Toggle lock / unlock |
| **▼ DOWN** *(black)* | Previous antenna | Show IP address on OLED (3 s) |
| **SET** | Bump max antennas (0→7→0) | — |
| **ERASE** | — | Wipe all settings + WiFi, reboot |

---

## Web UI

The modern dark web UI is served at **`/`** (default). Features:

- **Antenna grid** — tap any port to select it; gold ⭐ star sets the startup default
- **Schedule** — time-based antenna switching (requires NTP sync)
- **Settings** — MQTT, NTP/timezone, display mode (port or clock prominent), startup port behaviour, factory reset
- **Help** — button gesture reference + full REST API reference

The classic stock page is preserved at `/old`.

### Add to home screen (mobile)

The web UI is a Progressive Web App (PWA). To install it:
- **Android Chrome**: tap the browser menu → "Add to Home Screen"
- **iOS Safari**: tap Share → "Add to Home Screen"

Once installed it launches in standalone mode (no browser chrome) with the correct icon and title.

---

## REST API (quick look)

```bash
HOST=http://ESP-45CA21.local        # or the device IP

curl "$HOST/api/status"                      # current state (JSON)
curl -X POST "$HOST/api/antenna?position=2"  # select ANT2
curl -X POST "$HOST/api/antenna/up"          # step up
curl -X POST "$HOST/api/antenna/ground"      # select GROUND
```

Full reference: **[`docs/rest-api.md`](docs/rest-api.md)**

---

## Building from source

Requires [PlatformIO](https://platformio.org/install).

```bash
cd firmware

./build.sh                  # compile firmware only
./build.sh fs               # build LittleFS web-asset image
./build.sh all /dev/ttyACM0 # compile + flash both
```

To update the prebuilt binaries after making changes:

```bash
cd firmware
./build.sh build            # produces .pio/build/esp12e/firmware.bin
./build.sh fs               # produces .pio/build/esp12e/littlefs.bin
cp .pio/build/esp12e/firmware.bin  prebuilt/firmware.bin
cp .pio/build/esp12e/littlefs.bin  prebuilt/littlefs.bin
```

---

## Hardware overview

```
ESP8266  ──GPIO12/13/14──►  74HCT138 (3-to-8 decoder)  ──►  relays RL1–RL5
  │                                                          (one antenna A1–A5
  ├─ GPIO0/1/2/3 : SET / DOWN / ERASE / UP buttons            → common "Radio" SMA)
  ├─ GPIO4/5     : I2C OLED (0.91" 128×32 SSD1306)
  └─ GPIO16      : OLED reset (Heltec WiFi Kit 8)
```

The MCU is a **Heltec WiFi Kit 8 (HTIT-W8266)** — an ESP8266 module with an on-board
0.91" 128×32 SSD1306 OLED whose reset line is wired to GPIO16.

The 74HCT138 guarantees exactly one relay — hence one antenna — is connected at a time.
12 VDC powers the relay coils; an on-board regulator powers the ESP8266.

---

## Key facts

| | |
|---|---|
| Product | MS-S5 WiFi antenna switch (ANTENI.NET "ANTENNAS webSWITCH control 1 to 5"), KiwiSDR-compatible |
| Firmware | "ANTENNA SELECTOR V2", ANTENI.NET Ltd. (Arduino / NONOS-SDK 2.2.2, 2019) |
| Multiplexer select | GPIO14=A0, GPIO12=A1, GPIO13=A2 (writes antenna index as binary) |
| Buttons | GPIO0=SET, GPIO1=DOWN, GPIO2=ERASE (hold), GPIO3=UP (all active-low) |
| Board | Heltec WiFi Kit 8 (HTIT-W8266), ESP8266 |
| Display | 0.91" 128×32 SSD1306 OLED, I²C on GPIO4/GPIO5, reset on GPIO16 |
| WiFi setup | tzapu WiFiManager — AP `AutoConnectAP` @ `http://192.168.4.1` |
| Station access | `http://ESP-XXXXXX.local` (mDNS), web UI + OTA update |
| REST API | JSON `/api/*` API — see [`docs/rest-api.md`](docs/rest-api.md) |

---

## Documentation

| Document | Topic |
|----------|-------|
| [`docs/README.md`](docs/README.md) | Documentation index + quick reference |
| [`docs/rest-api.md`](docs/rest-api.md) | JSON REST API (`/api/*`) for scripting antenna selection |
| [`docs/mqtt.md`](docs/mqtt.md) | MQTT — topics, payloads, commands, HA example, config API |
| [`docs/buttons.md`](docs/buttons.md) | Physical buttons — stock + replica firmware gestures |
| [`docs/manual-setup.md`](docs/manual-setup.md) | WiFi setup, manual control, button gestures |
| [`docs/schematic.md`](docs/schematic.md) | Redrawn schematic, block diagram, front/rear panel layout |
| [`docs/hardware-pinout.md`](docs/hardware-pinout.md) | Complete GPIO map, init, signal chain, power |
| [`docs/74hct138-truth-table.md`](docs/74hct138-truth-table.md) | Decoder select-line → antenna truth table |
| [`docs/web-interface.md`](docs/web-interface.md) | Full stock HTTP interface + exact served HTML |
| [`docs/wifi-ap-config.md`](docs/wifi-ap-config.md) | AP / captive portal / mDNS / OTA |
| [`docs/firmware-analysis.md`](docs/firmware-analysis.md) | End-to-end reverse-engineering walkthrough |
| [`scripts/analysis/README.md`](scripts/analysis/README.md) | How the analysis scripts work |

---

## Dumping / restoring the original firmware

```bash
# Read the full 4 MB flash from the device
./scripts/dump_firmware.sh /dev/ttyACM0

# Restore the original backup
./scripts/restore_firmware.sh esp8266_cc50e345ca21_flash_backup_20260703.bin /dev/ttyACM0
```

The original firmware image is included in the repo so the device can always be restored.
