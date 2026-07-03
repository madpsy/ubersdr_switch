#!/usr/bin/env bash
#
# ota.sh — build and flash the ANTENNA SELECTOR V2 replica firmware over WiFi (OTA).
#
# The firmware exposes the stock OTA endpoint at http://<device>/update (POST /u),
# provided by ESP8266HTTPUpdateServer (see firmware/src/main.cpp). This wraps the
# build + curl upload so you can reflash a running unit without a USB cable.
#
# Usage:
#   ./ota.sh <IP-or-host> [firmware.bin]
#
# Examples:
#   ./ota.sh 192.168.9.92                 # build, then OTA-flash to that IP
#   ./ota.sh ESP-45CA21.local             # use the mDNS hostname
#   ./ota.sh 192.168.9.92 custom.bin      # flash a prebuilt image (skip building)
#
# After a successful upload the device reboots automatically; this script then polls
# the /debug endpoint and prints the fresh boot log so you can confirm it came back.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

HOST="${1:-}"
if [ -z "$HOST" ]; then
    echo "usage: $0 <IP-or-host> [firmware.bin]" >&2
    exit 2
fi

BIN="${2:-}"

if [ -z "$BIN" ]; then
    echo "==> Building firmware..."
    ./build.sh build
    BIN=".pio/build/esp12e/firmware.bin"
fi

if [ ! -f "$BIN" ]; then
    echo "error: firmware image not found: $BIN" >&2
    exit 1
fi

URL="http://${HOST}/update"
echo "==> OTA-flashing $BIN -> $URL"
RESP="$(curl -fsS --max-time 120 -F "update=@${BIN}" "$URL")"
echo "    device: $RESP"

case "$RESP" in
    *Success*|*success*)
        echo "==> Upload accepted; waiting for the device to reboot..."
        ;;
    *)
        echo "error: OTA update did not report success" >&2
        exit 1
        ;;
esac

# Poll /debug until the unit is back (fresh, low-uptime boot log).
for i in $(seq 1 20); do
    sleep 2
    if OUT="$(curl -fsS --max-time 5 "http://${HOST}/debug" 2>/dev/null)"; then
        echo "==> Device back online. Boot log:"
        echo "----------------------------------------"
        echo "$OUT"
        echo "----------------------------------------"
        exit 0
    fi
done

echo "warning: device did not respond on /debug within timeout (it may still be booting)." >&2
exit 0
