# Prebuilt firmware binaries

These binaries are built from the source in [`../src/`](../src/) and [`../data/`](../data/)
and committed here so users can flash the firmware **without needing PlatformIO or a compiler**.

| File | Description |
|------|-------------|
| `firmware.bin` | Compiled ESP8266 firmware (flashed to the code partition) |
| `littlefs.bin` | LittleFS web-asset image (flashed to the data partition — contains the web UI, icons, manifest, etc.) |

## Flashing

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
./build.sh build   # -> .pio/build/esp12e/firmware.bin
./build.sh fs      # -> .pio/build/esp12e/littlefs.bin
cp .pio/build/esp12e/firmware.bin  prebuilt/firmware.bin
cp .pio/build/esp12e/littlefs.bin  prebuilt/littlefs.bin
```

Then commit both files.
