# ANTENNA SELECTOR V2 — Replica Firmware

A pin- and interface-compatible re-implementation of the ANTENI.NET Ltd.
"ANTENNAS webSWITCH control 1 to 5" (MS-S5) stock firmware, for the ESP8266-based
5-way antenna switch reverse-engineered in [`../docs/`](../docs/).

The target board is a **Heltec WiFi Kit 8 (HTIT-W8266)** — an ESP8266 module with an
on-board 0.91" 128×32 SSD1306 OLED whose reset line is wired to GPIO16.

It **mimics the existing interface and functionality exactly** while being written
from scratch as clean, maintainable Arduino/PlatformIO source, with the web assets
kept as **separate HTML/CSS files** (served from the on-flash LittleFS filesystem).

The original firmware can always be restored from the included flash image with
[`../scripts/restore_firmware.sh`](../scripts/restore_firmware.sh).

---

## What it replicates (verified against the docs)

| Stock behaviour | Replica | Reference |
|-----------------|---------|-----------|
| Antenna select via 74HCT138 on GPIO12/13/14 (index 0=GROUND, 1–7=ANTn, binary) | ✅ [`antenna.h`](src/antenna.h) | [`../docs/74hct138-truth-table.md`](../docs/74hct138-truth-table.md) |
| `setup()` init: 14/12/13 preset HIGH, buttons 0/1/2/3 HIGH (pull-ups) | ✅ | [`../docs/hardware-pinout.md`](../docs/hardware-pinout.md) |
| UP (GPIO3) next / DOWN (GPIO1) prev, wrapping GROUND↔max | ✅ [`buttons.h`](src/buttons.h) | [`../docs/buttons.md`](../docs/buttons.md) |
| SET (GPIO0): bump "max antennas", shows `MAX n`, wraps at 8 (default 7) | ✅ | [`../docs/buttons.md`](../docs/buttons.md) |
| ERASE (GPIO2): hold to wipe WiFi config + reboot | ✅ | [`../docs/buttons.md`](../docs/buttons.md) |
| OLED (0.91" 128×32 SSD1306, GPIO4/5, reset GPIO16) shows `GROUND`/`ANT: n` + web URL, `MAX n`, erase prompts | ✅ [`display.h`](src/display.h) | [`../docs/manual-setup.md`](../docs/manual-setup.md) |
| App server: `GET /5/on` (Up), `GET /4/on` (Dn), literal substring match | ✅ [`webserver.h`](src/webserver.h) | [`../docs/web-interface.md`](../docs/web-interface.md) |
| Exact served page: `Web ANTENNAS switch`, Up/Dn buttons, `ANTENNA:` label, `Anteni.net Ltd.` | ✅ [`data/index.html`](data/index.html) + [`data/style.css`](data/style.css) | [`../docs/web-interface.md`](../docs/web-interface.md) |
| Verbatim headers `HTTP/1.1 200 OK` / `Content-type:text/html` / `Connection: close` | ✅ | [`../docs/web-interface.md`](../docs/web-interface.md) §1.2 |
| WiFiManager captive portal: SoftAP `AutoConnectAP` @ `http://192.168.4.1` | ✅ | [`../docs/wifi-ap-config.md`](../docs/wifi-ap-config.md) |
| Station mDNS `uberant.local` (derived from device name; falls back to `ESP-XXXXXX` if unset) | ✅ | [`../docs/wifi-ap-config.md`](../docs/wifi-ap-config.md) |
| `/info`, `/erase`, `/restart`, OTA `/update` → POST `/u` | ✅ | [`../docs/web-interface.md`](../docs/web-interface.md) §2 |
| Boot gestures: hold ERASE to wipe WiFi, hold UP to force config portal | ✅ | [`../docs/manual-setup.md`](../docs/manual-setup.md) |

> **Stock parity:** the stock web UI (`/`, `/5/on`, `/4/on`) is reproduced exactly,
> including its relative Up/Dn-only stepping and byte-for-byte HTML.

### Enhancements beyond the stock firmware

| Enhancement | Where | Reference |
|-------------|-------|-----------|
| **JSON REST API** (`/api/*`): direct position select, status, info, max control, CORS | ✅ [`main.cpp`](src/main.cpp) | [`../docs/rest-api.md`](../docs/rest-api.md) |
| **Custom port names** (ANT1–7, ≤16 chars) persisted to **EEPROM** (survive a filesystem reflash); shown on OLED + web UI | ✅ [`antenna.h`](src/antenna.h) | [`../docs/rest-api.md`](../docs/rest-api.md) |
| **Modern web UI** is now the default at `/` (dark, responsive; direct-select grid + `?` API help modal). Classic stock page preserved at `/old`. | ✅ [`data/app.html`](data/app.html) | [`../docs/rest-api.md`](../docs/rest-api.md) |
| `UberSDR` boot splash + larger, auto-sized status screen on the 128×32 OLED | ✅ [`display.h`](src/display.h) | — |
| Typo fix: OLED shows `press ERASE…` (stock displayed `pres`) | ✅ [`config.h`](src/config.h) | [`../docs/manual-setup.md`](../docs/manual-setup.md) |

---

## Project layout

```
firmware/
├── platformio.ini        # build config (ESP8266, 4M/1M, libs; esp12e + esp12e-debug)
├── build.sh              # build helper (build / fs / upload / uploadfs / all / monitor)
├── flash.sh              # flash helper (port auto-detect, WiFi provisioning, --debug)
├── src/
│   ├── main.cpp          # boot flow, WiFiManager, mDNS, OTA, button state machine
│   ├── config.h          # pins, constants, verbatim strings (from ../docs/)
│   ├── debuglog.h        # in-memory log ring buffer (+ optional serial mirror)
│   ├── antenna.h         # position model + 74HCT138 decoder drive + persisted max
│   ├── buttons.h         # 4 active-low buttons, debounce + edge detection
│   ├── display.h         # 0.91" 128×32 SSD1306 OLED screens (Heltec, reset on GPIO16)
│   └── webserver.h       # raw Up/Dn app server (serves data/ over LittleFS) + /debug
└── data/                 # web assets, flashed to LittleFS (editable, not embedded)
    ├── app.html          # modern dark REST-driven UI — the DEFAULT page at "/"
    ├── old.html          # classic stock "Web ANTENNAS switch" page (%LABEL%), at /old
    ├── style.css         # exact stock CSS (.button etc., used by old.html)
    ├── portal.html       # station-mode portal menu
    ├── info.html         # /info template (%TOKENS% substituted at runtime)
    ├── wifi.json.example # template for baked-in WiFi credentials
    └── wifi.json         # (optional, git-ignored) baked-in SSID/password
```

The HTML/CSS live in [`data/`](data/) as real files and are uploaded to the
device's LittleFS partition separately from the code, so the UI can be edited
without recompiling the firmware.

---

## GPIO map (must match the board)

| GPIO | Function |
|------|----------|
| GPIO14 | 74HCT138 A0 (LSB) |
| GPIO12 | 74HCT138 A1 |
| GPIO13 | 74HCT138 A2 (MSB) |
| GPIO0  | SET button (max antenna count) |
| GPIO1  | DOWN button (also UART0 TX) |
| GPIO2  | ERASE button (hold) |
| GPIO3  | UP button (also UART0 RX) |
| GPIO4 / GPIO5 | I2C OLED SDA / SCL (0.91" 128×32 SSD1306) |
| GPIO16 | OLED RESET (Heltec WiFi Kit 8) |

> GPIO1/GPIO3 double as UART0 TX/RX, so the serial console conflicts with the
> DOWN/UP buttons — exactly as on the stock unit. Serial logging is therefore
> **off by default**; enable it (breaking those buttons) via `-D REPLICA_DEBUG`.

---

## Build & flash

The easiest way is the included helper scripts (they auto-locate PlatformIO):

```bash
cd firmware

# Build just the firmware
./build.sh                       # or: ./build.sh build

# Build the LittleFS web-asset image (data/)
./build.sh fs

# Flash EVERYTHING (firmware + web assets). Prompts for the serial port, or pass it:
./flash.sh                       # auto-detect / prompt for the port
./flash.sh /dev/ttyACM0          # flash both to this port
./flash.sh --fw  /dev/ttyACM0    # firmware only
./flash.sh --fs  /dev/ttyACM0    # web assets (LittleFS) only
```

`flash.sh` lists detected serial ports and asks you to pick one when no path is
given; you can also pass the device path as a positional argument or via `-p PORT`.

### Raw PlatformIO equivalents

```bash
pio run                                       # build firmware
pio run -t upload   --upload-port /dev/ttyACM0   # flash firmware
pio run -t uploadfs --upload-port /dev/ttyACM0   # flash web assets (data/ -> LittleFS)
pio device monitor                            # serial (conflicts with UP/DOWN buttons)
```

Both a firmware image **and** the filesystem image must be flashed. If the
filesystem is missing, the web pages show a "Filesystem image not uploaded" notice.

> The WiFi setup stack is the same **tzapu WiFiManager** the stock unit uses
> (pinned to v2.0.17 for the non-blocking portal API). Credentials are stored by the
> ESP8266 SDK in the flash config area and reused automatically on the next boot; the
> "max antennas" value is persisted to the EEPROM area so it survives power-cycles.

### OTA (over WiFi, no cable)

Once the device is on your network, the firmware can be updated over WiFi at
`http://uberant.local/update` (POST to `/u`), matching the stock OTA flow. Build
`pio run` produces `.pio/build/esp12e/firmware.bin` to upload there.

---

## WiFi setup

There are **two ways** to get the device on your network.

### A) Pre-provision credentials (easiest — no portal)

`flash.sh` can bake your SSID/password into `data/wifi.json`, which the firmware
reads on boot and uses to connect directly:

```bash
# Interactive: flash.sh offers to enter WiFi details before flashing
./flash.sh /dev/ttyACM0

# Non-interactive: pass them on the command line
./flash.sh --ssid "MyNetwork" --pass "MyPassword" /dev/ttyACM0

# Force the SSID/password prompts
./flash.sh --wifi /dev/ttyACM0

# Never touch wifi.json (use the captive portal instead)
./flash.sh --no-wifi /dev/ttyACM0
```

The credentials file [`data/wifi.json`](data/wifi.json.example) looks like:

```json
{ "ssid": "YourWiFiName", "pass": "YourWiFiPassword" }
```

`data/wifi.json` is **git-ignored** (never commit real secrets); an example lives at
[`data/wifi.json.example`](data/wifi.json.example). Leave `pass` empty for an open
network. To change the network later, re-run `flash.sh` (it detects the existing file
and offers to update it), or use the portal/`/erase`.

### B) Captive portal (WiFiManager)

1. Power on. If there are no `wifi.json` credentials and none saved (or you hold
   **UP** at boot), the device starts the SoftAP **`AutoConnectAP`** (open) at
   **`http://192.168.4.1`** — the OLED shows `ssid: AutoConnectAP`.
2. Join `AutoConnectAP`, open `http://192.168.4.1`, choose **Configure WiFi**, enter
   your SSID/password, and **Save**. The device reboots and connects as a station.
3. After connecting it advertises **`http://uberant.local`** (and its DHCP IP),
   shown on the OLED, serving the antenna control page.

To wipe WiFi credentials: visit `/erase`, or **hold the ERASE button** during the
boot countdown.

> The WiFi setup stack is the same **tzapu WiFiManager** the stock unit uses
> (pinned to v2.0.17). Portal-entered credentials are stored by the ESP8266 SDK in
> the flash config area and reused automatically on the next boot. The "max antennas"
> value is persisted to the EEPROM area so it survives power-cycles.

---

## Debugging

Because GPIO1/GPIO3 (UART0 TX/RX) are used as the DOWN/UP buttons, ordinary serial
debugging conflicts with them. There are **two** debug channels:

### 1) `/debug` web page (conflict-free — recommended)

The firmware always keeps the last ~40 log lines in RAM and serves them, plus live
runtime state, at:

```
http://uberant.local/debug      (or http://<device-IP>/debug)
```

This shows WiFi status, SSID, IP, antenna position/max, free heap, uptime, reset
reason, and a log of boot / WiFi / button / web events. Viewing it does **not**
disturb the buttons.

### 2) Serial log via `flash.sh --debug`

Flashes the serial-logging build (`esp12e-debug`, which defines `REPLICA_DEBUG`) and
opens the serial monitor automatically:

```bash
./flash.sh --debug /dev/ttyACM0     # flash debug firmware + open serial monitor
./flash.sh --monitor /dev/ttyACM0   # flash the normal build, then open the monitor
```

Serial runs at **115200 baud**. **Note:** while the serial monitor is attached the
UP/DOWN buttons are disabled (they share the UART pins) — use it only to read output,
or prefer the `/debug` page above.

Our own log lines print cleanly at 115200. If instead you see **repeating garbage**,
the device is almost certainly **reset-looping** (the ESP8266 boot ROM prints its
banner at 74880 baud on every reset, which is unreadable at 115200). To read the raw
reset reason at the boot-ROM baud:

```bash
./flash.sh --boot /dev/ttyACM0      # opens the monitor at 74880 baud (no flashing)
```

The `esp12e-debug` monitor also enables the `esp8266_exception_decoder` filter, so a
crash stack trace is decoded to source file/line automatically.

To build/flash the debug firmware manually:

```bash
pio run -e esp12e-debug -t upload
pio device monitor -e esp12e-debug        # 115200, with exception decoder
```

---

## flash.sh / build.sh option reference

`./build.sh <cmd> [PORT]`

| Command | Action |
|---------|--------|
| `build` (default) | Build the firmware |
| `fs` | Build the LittleFS image from `data/` |
| `upload` | Build + flash firmware |
| `uploadfs` | Build + flash web assets |
| `all` | Build + flash firmware and web assets |
| `monitor` | Open the serial monitor |
| `clean` | Remove build artifacts |

`./flash.sh [options] [PORT]` (default: build + flash firmware **and** filesystem)

| Option | Action |
|--------|--------|
| `PORT` / `-p PORT` | Serial port (auto-detects & confirms if omitted) |
| `--fw` / `--fs` / `--all` | Flash firmware only / filesystem only / both (default) |
| `--ssid NAME` / `--pass PW` | Bake WiFi credentials into `data/wifi.json` |
| `--wifi` / `--no-wifi` | Force the WiFi prompts / skip WiFi provisioning |
| `--debug` | Flash the serial-logging build **and** open the serial monitor (115200) |
| `--monitor` | Flash normally, then open the serial monitor |
| `--boot` | Don't flash; open the boot-ROM monitor at 74880 baud (reset reason) |
| `--no-build` | Skip the explicit pre-build step |

---

## Restoring the original firmware

```bash
../scripts/restore_firmware.sh ../esp8266_cc50e345ca21_flash_backup_20260703.bin /dev/ttyACM0
```
