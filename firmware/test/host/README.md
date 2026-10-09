# Host-side tests

Tests that run the real firmware on a PC, without a board. They exist so a change to any
part of the firmware — or to the web UI, docs or prebuilt images that must agree with
it — fails here before it reaches a device.

```bash
./run.sh            # every suite (g++ and node)
./run.sh --build    # …and compile the firmware with PlatformIO (esp12e + esp12e-debug)
./run.sh app mqtt   # only some C++ suites
```

Needs `g++` (C++17) and, for the web checks, `node` (no packages). `--build` needs PlatformIO.

## Suites

| Suite | What it covers |
|-------|----------------|
| `test_antenna.cpp` | Position model, the 74HCT138 select lines (GPIO14/12/13), UP/DOWN wrap, SET/max, labels, custom names and their EEPROM image (incl. the legacy 0xA5 layout) |
| `test_buttons.cpp` | Pin setup, 40 ms debounce, press/release edges, raw reads |
| `test_display.cpp` | What the OLED shows: banner, splash, status, padlock, clock/cycle modes, scrolling, custom messages, screen saver, the other screens |
| `test_eventlog.cpp` | The 25-entry event ring and its JSON |
| `test_ntp.cpp` | Sync detection, retry/re-sync, ISO 8601 UTC and local formatting |
| `test_schedule.cpp` | When entries fire (day after day), lock/master switch, next-firing preview, clamping, EEPROM |
| `test_settings.cpp` | Every persisted setting: defaults, clamps, round-trips, images from older firmware, the shared EEPROM sector layout |
| `test_mqtt.cpp` | MQTT: the native `uberant/<host>/…` topics and Tasmota discovery (checked against the Tasmota source) |
| `test_app.cpp` | **`src/main.cpp` itself**: boot and WiFi paths, boot gestures, hostname, startup port, every REST route, the stock web routes and pages, buttons and holds, the lock, screen saver, scheduler, last-port restore, MQTT end to end, persistence across reboots |
| `test_web.mjs` | Web assets parse, `.gz` copies are current, UI ↔ firmware routes and element ids, template placeholders, docs list every route/topic, web flasher ↔ prebuilt images |

## How it works

- `stub/` holds small stand-ins for the ESP8266 Arduino core and libraries (String,
  WiFi, ESP8266WebServer, LittleFS, EEPROM, WiFiManager, PubSubClient, the SSD1306…).
  The firmware's own `src/` files are compiled unchanged against them.
- The clock is fake: `delay()` advances `millis()`, so a 3 s button hold or a day of
  schedule runs instantly. `setEpoch()` "syncs NTP".
- The OLED stand-in records every frame (text, position, font, pixels), so tests read
  the screen.
- `test_app.cpp` runs each `boot()` as a fresh power-on in a forked process. What the
  device persists (EEPROM, LittleFS, saved WiFi) is carried to the next `boot()`, so
  "survives a reboot" is tested for real.
- `harness.h`: `check()`, a strict JSON parser, `boot()`, and `known()` — for a bug that
  has been found but not fixed yet: it documents today's behaviour without failing the
  run, and fails as soon as the behaviour changes, so it gets turned into a `check()`.

## Adding a test

Add checks to the suite that owns the code, or a new `test_<name>.cpp` (picked up
automatically). If firmware code needs something the stand-ins do not provide yet, add
it to `stub/` — keep stand-ins faithful to the real library's behaviour.
