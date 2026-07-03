#!/usr/bin/env bash
#
# restore_firmware.sh — Write a full flash image back to the ESP8266
# ANTENNA SELECTOR V2 over the serial/USB port (restores the original firmware).
#
# Usage:
#   ./scripts/restore_firmware.sh IMAGE.bin [PORT]
#
#   IMAGE.bin  the flash image to write (e.g. the original backup)
#   PORT       serial device (default: /dev/ttyACM0)
#
# Example (restore the shipped backup):
#   ./scripts/restore_firmware.sh esp8266_cc50e345ca21_flash_backup_20260703.bin /dev/ttyACM0
#
# Requires esptool (falls back to the PlatformIO-bundled copy).

set -euo pipefail

IMAGE="${1:?Usage: $0 IMAGE.bin [PORT]}"
PORT="${2:-/dev/ttyACM0}"
BAUD="${ESPTOOL_BAUD:-460800}"

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

[ -f "$IMAGE" ] || { echo "ERROR: image '$IMAGE' not found." >&2; exit 1; }
[ -e "$PORT" ]  || { echo "ERROR: port '$PORT' not found." >&2; exit 1; }

echo ">> Writing '$IMAGE' to $PORT @ ${BAUD} baud (full flash, offset 0x0)"
read -r -p "This overwrites the entire flash. Continue? [y/N] " ans
[ "$ans" = "y" ] || [ "$ans" = "Y" ] || { echo "Aborted."; exit 1; }

"${ESPTOOL[@]}" --chip esp8266 --port "$PORT" --baud "$BAUD" \
    write_flash --flash_size detect 0x0 "$IMAGE"

echo ">> Restore complete."
