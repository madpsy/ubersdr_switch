#!/usr/bin/env bash
#
# build.sh — build (and optionally flash) the ANTENNA SELECTOR V2 replica firmware.
#
# Wraps PlatformIO so the project can be built, uploaded and have its web assets
# (data/ -> LittleFS) flashed with a single command. It auto-locates the PlatformIO
# executable (including the PlatformIO-bundled copy under ~/.platformio).
#
# Usage:
#   ./build.sh                 # build firmware only (default)
#   ./build.sh build           # build firmware only
#   ./build.sh fs              # build the LittleFS filesystem image (data/)
#   ./build.sh upload  [PORT]  # build + flash firmware over USB
#   ./build.sh uploadfs [PORT] # build + flash the web assets to LittleFS
#   ./build.sh all     [PORT]  # build + flash BOTH firmware and filesystem
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

case "$ACTION" in
    build)
        echo "==> Building firmware..."
        "$PIO" run
        echo "==> Firmware image: .pio/build/esp12e/firmware.bin"
        ;;
    fs)
        echo "==> Building LittleFS filesystem image from data/ ..."
        "$PIO" run -t buildfs
        echo "==> Filesystem image: .pio/build/esp12e/littlefs.bin"
        ;;
    upload)
        echo "==> Building + flashing firmware to $PORT ..."
        "$PIO" run -t upload --upload-port "$PORT"
        ;;
    uploadfs)
        echo "==> Building + flashing web assets (LittleFS) to $PORT ..."
        "$PIO" run -t uploadfs --upload-port "$PORT"
        ;;
    all)
        echo "==> Building + flashing firmware AND filesystem to $PORT ..."
        "$PIO" run -t upload   --upload-port "$PORT"
        "$PIO" run -t uploadfs --upload-port "$PORT"
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
        echo "usage: $0 {build|fs|upload|uploadfs|all|monitor|clean} [PORT]" >&2
        exit 2
        ;;
esac
