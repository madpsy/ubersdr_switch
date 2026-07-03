#!/usr/bin/env bash
#
# dump_firmware.sh — Read the full 4 MB flash image from the ESP8266
# ANTENNA SELECTOR V2 over the serial/USB port using esptool.
#
# Usage:
#   ./scripts/dump_firmware.sh [PORT] [SIZE] [OUTFILE]
#
#   PORT     serial device (default: /dev/ttyACM0)
#   SIZE     flash size in bytes (default: 0x400000 = 4 MB)
#   OUTFILE  output file (default: esp8266_dump_<MAC>_<date>.bin)
#
# Examples:
#   ./scripts/dump_firmware.sh
#   ./scripts/dump_firmware.sh /dev/ttyUSB0
#   ./scripts/dump_firmware.sh /dev/ttyACM0 0x400000 my_backup.bin
#
# Requires esptool. If not on PATH, this script falls back to the copy bundled
# with PlatformIO (~/.platformio/packages/tool-esptoolpy/esptool.py).

set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
SIZE="${2:-0x400000}"        # 4 MB
BAUD="${ESPTOOL_BAUD:-460800}"

# --- locate esptool ---------------------------------------------------------
if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
elif [ -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ]; then
    ESPTOOL=(python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py")
else
    echo "ERROR: esptool not found. Install with:  pip install esptool" >&2
    exit 1
fi

# --- sanity checks ----------------------------------------------------------
if [ ! -e "$PORT" ]; then
    echo "ERROR: serial port '$PORT' does not exist." >&2
    echo "Available candidates:" >&2
    ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "  (none found)" >&2
    exit 1
fi

echo ">> esptool:  ${ESPTOOL[*]}"
echo ">> port:     $PORT @ ${BAUD} baud"
echo ">> size:     $SIZE bytes"

# --- read chip identity (also gives us the MAC for the filename) ------------
echo ">> Reading chip info..."
CHIP_INFO="$("${ESPTOOL[@]}" --chip esp8266 --port "$PORT" --baud "$BAUD" flash_id 2>&1 || true)"
echo "$CHIP_INFO"

MAC="$(printf '%s\n' "$CHIP_INFO" | grep -ioE 'MAC: ([0-9a-f]{2}:){5}[0-9a-f]{2}' | head -n1 | awk '{print $2}' | tr -d ':')"
[ -z "$MAC" ] && MAC="unknown"
DATE="$(date +%Y%m%d)"

OUTFILE="${3:-esp8266_dump_${MAC}_${DATE}.bin}"

# --- dump -------------------------------------------------------------------
echo ">> Dumping flash to: $OUTFILE"
"${ESPTOOL[@]}" --chip esp8266 --port "$PORT" --baud "$BAUD" \
    read_flash 0x0 "$SIZE" "$OUTFILE"

echo ">> Done."
ls -l "$OUTFILE"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$OUTFILE"
fi
