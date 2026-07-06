# Prebuilt firmware binaries

These binaries are built from the source in [`../src/`](../src/) and [`../data/`](../data/)
and committed here so users can flash the firmware **without needing PlatformIO or a compiler**.

| File | Description |
|------|-------------|
| `firmware.bin` | Compiled ESP8266 firmware (flashed to the code partition) |
| `littlefs.bin` | LittleFS web-asset image (flashed to the data partition — contains the web UI, icons, manifest, etc.) |
| `manifest.json` | ESP Web Tools manifest — used by the browser-based web flasher |

## Web flasher (Chrome / Edge — no software required)

The easiest way to flash is the browser-based web flasher in [`../../docs/flash/`](../../docs/flash/).
Open `docs/flash/index.html` via a local HTTPS server or GitHub Pages, plug in the device, and click
**Install** — no drivers, Python, or PlatformIO needed.

> **Note:** Web Serial requires HTTPS. The page cannot be opened directly from disk (`file://`).
> Serve it locally with e.g. `python3 -m http.server` behind a reverse proxy, or deploy to GitHub Pages.

## Command-line flashing

```bash
# From the repo root — auto-detects port, prompts for WiFi credentials:
./firmware/flash.sh

# Or specify the port:
./firmware/flash.sh /dev/ttyACM0
```

`flash.sh` uses these prebuilt binaries automatically if PlatformIO is not installed.
If PlatformIO **is** installed, it builds from source first.

## Updating the prebuilt binaries

After making source changes, rebuild and copy:

```bash
cd firmware
./build.sh build   # -> .pio/build/esp12e/firmware.bin + littlefs.bin
                   #    also copies both to prebuilt/ AND docs/flash/
```

Then commit all changed files (`prebuilt/firmware.bin`, `prebuilt/littlefs.bin`,
`docs/flash/firmware.bin`, `docs/flash/littlefs.bin`).
