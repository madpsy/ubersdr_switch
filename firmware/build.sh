#!/usr/bin/env bash
#
# build.sh — build (and optionally flash) the ANTENNA SELECTOR V2 replica firmware.
#
# Wraps PlatformIO so the project can be built, uploaded and have its web assets
# (data/ -> LittleFS) flashed with a single command. It auto-locates the PlatformIO
# executable (including the PlatformIO-bundled copy under ~/.platformio).
#
# Usage:
#   ./build.sh                 # build firmware + filesystem, copy both to prebuilt/ (default)
#   ./build.sh build           # same as default
#   ./build.sh fs              # build the LittleFS filesystem image (data/) only
#   ./build.sh upload  [PORT]  # build + flash firmware over USB
#   ./build.sh uploadfs [PORT] # build + flash the web assets to LittleFS
#   ./build.sh all     [PORT]  # build + flash BOTH firmware and filesystem
#   ./build.sh prebuilt        # build both + copy to prebuilt/ (explicit alias for default)
#   ./build.sh monitor [PORT]  # open the serial monitor (conflicts with UP/DOWN btns)
#   ./build.sh clean           # remove build artifacts
#
# PORT defaults to /dev/ttyACM0 (override as arg 2, or set the PORT env var).

set -euo pipefail

# --- locate the project directory (this script's own folder) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# --- locate the PlatformIO executable ---
find_pio() {
    if command -v pio >/dev/null 2>&1; then
        echo "pio"; return 0
    fi
    if command -v platformio >/dev/null 2>&1; then
        echo "platformio"; return 0
    fi
    for c in \
        "$HOME/.platformio/penv/bin/pio" \
        "$HOME/.platformio/penv/bin/platformio"; do
        [ -x "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}

PIO="$(find_pio || true)"
if [ -z "${PIO:-}" ]; then
    echo "error: PlatformIO (pio) not found." >&2
    echo "Install it from https://platformio.org/install or run: pip install platformio" >&2
    exit 1
fi

ACTION="${1:-build}"
PORT="${2:-${PORT:-/dev/ttyACM0}}"

echo "==> Using PlatformIO: $PIO"
echo "==> Project:          $SCRIPT_DIR"

# Compress text assets in data/ with gzip before packing into LittleFS.
# Both the original and .gz files are included in the image; the firmware
# serves the .gz copy (with Content-Encoding: gzip) when present, falling
# back to the uncompressed file for clients that don't accept gzip.
compress_assets() {
    echo "==> Compressing web assets..."
    for f in data/app.html data/sw.js data/style.css; do
        if [ -f "$f" ]; then
            gzip -9 -k -f "$f"
            echo "      $f -> $f.gz ($(wc -c < "$f.gz" | tr -d ' ') bytes)"
        fi
    done
}

copy_prebuilt() {
    mkdir -p prebuilt
    cp .pio/build/esp12e/firmware.bin  prebuilt/firmware.bin
    cp .pio/build/esp12e/littlefs.bin  prebuilt/littlefs.bin
    echo "==> Prebuilt binaries updated:"
    echo "      prebuilt/firmware.bin  ($(wc -c < prebuilt/firmware.bin | tr -d ' ') bytes)"
    echo "      prebuilt/littlefs.bin  ($(wc -c < prebuilt/littlefs.bin | tr -d ' ') bytes)"
}

case "$ACTION" in
    build|prebuilt)
        echo "==> Building firmware + filesystem..."
        compress_assets
        "$PIO" run
        "$PIO" run -t buildfs
        copy_prebuilt
        ;;
    fs)
        echo "==> Building LittleFS filesystem image from data/ ..."
        compress_assets
        "$PIO" run -t buildfs
        echo "==> Filesystem image: .pio/build/esp12e/littlefs.bin"
        ;;
    upload)
        echo "==> Building + flashing firmware to $PORT ..."
        "$PIO" run -t upload --upload-port "$PORT"
        ;;
    uploadfs)
        echo "==> Building + flashing web assets (LittleFS) to $PORT ..."
        compress_assets
        "$PIO" run -t uploadfs --upload-port "$PORT"
        ;;
    all)
        echo "==> Building + flashing firmware AND filesystem to $PORT ..."
        compress_assets
        "$PIO" run -t upload   --upload-port "$PORT"
        "$PIO" run -t uploadfs --upload-port "$PORT"
        copy_prebuilt
        echo "==> Done. Both firmware and web assets flashed."
        ;;
    monitor)
        echo "==> Opening serial monitor on $PORT (Ctrl-C to exit) ..."
        echo "    NOTE: GPIO1/GPIO3 are the DOWN/UP buttons; serial conflicts with them."
        "$PIO" device monitor --port "$PORT"
        ;;
    clean)
        echo "==> Cleaning build artifacts..."
        "$PIO" run -t clean
        ;;
    *)
        echo "usage: $0 {build|fs|upload|uploadfs|all|prebuilt|monitor|clean} [PORT]" >&2
        exit 2
        ;;
esac
