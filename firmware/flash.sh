#!/usr/bin/env bash
#
# flash.sh — flash the ANTENNA SELECTOR V2 replica firmware to a connected device.
#
# Flashes BOTH the firmware and the LittleFS web-asset image (data/) so the device
# is fully functional after one command. If no serial port is supplied it scans for
# connected hardware (via `pio device list`), shows each port with its description /
# USB hardware ID, flags the one that looks like an ESP8266 USB-UART bridge, lets you
# pick, and asks you to confirm before writing anything.
#
# Usage:
#   ./flash.sh                 # prompt for the serial port, then flash all
#   ./flash.sh /dev/ttyACM0    # flash to the given port
#   ./flash.sh -p /dev/ttyUSB0 # explicit -p PORT
#   ./flash.sh --fw   [PORT]   # flash firmware only
#   ./flash.sh --fs   [PORT]   # flash the LittleFS web assets only
#   ./flash.sh --all  [PORT]   # flash firmware + filesystem (default)
#   ./flash.sh --no-build ...  # skip the explicit pre-build step
#
# Debugging:
#   ./flash.sh --debug [PORT]  # flash the serial-logging build (esp12e-debug) AND
#                              #   open the serial monitor to watch boot/WiFi logs
#   ./flash.sh --monitor [PORT]# flash normally, then open the serial monitor
#   ./flash.sh --boot [PORT]   # DON'T flash; open the boot-ROM monitor at 74880 baud
#                              #   (reads the ESP8266 reset reason when crash-looping)
#   ./flash.sh --logs [SECS] [PORT]  # DON'T flash; reset + print serial for SECS
#                              #   seconds then exit (works without a TTY). Combine
#                              #   with --debug to flash the logging build first, e.g.
#                              #   ./flash.sh --debug --logs 20 /dev/ttyACM0
#   (NOTE: serial shares GPIO1/GPIO3 with the UP/DOWN buttons — buttons are disabled
#    while the monitor is attached. For conflict-free logs, browse to /debug instead.)
#
# WiFi pre-provisioning (optional — bakes credentials into data/wifi.json so the
# device joins your network on boot without the captive portal):
#   ./flash.sh                       # interactively offers to enter WiFi details
#   ./flash.sh --ssid NAME --pass PW # set credentials non-interactively
#   ./flash.sh --wifi                # force the SSID/password prompts
#   ./flash.sh --no-wifi             # never touch wifi.json (use the AP portal)
#
# By default this runs a build first (via build.sh) so you get a clear compile
# result before flashing. The PlatformIO upload targets also compile on their own,
# so --no-build is safe if you just built. Use --no-build to skip the extra step.
#
# Environment:
#   PORT   default serial port if none given on the command line
#   BAUD   upload baud rate (default 460800)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BAUD="${BAUD:-460800}"
MODE="all"
MODE_SET=0               # did the user explicitly choose a flash mode?
PORT="${PORT:-}"
DO_BUILD=1
WIFI_MODE="ask"          # ask | set | skip
WIFI_SSID="${WIFI_SSID:-}"
WIFI_PASS="${WIFI_PASS:-}"
ENV="esp12e"             # PlatformIO environment (esp12e | esp12e-debug)
OPEN_MONITOR=0           # after flashing, open the serial monitor?
OPEN_BOOT=0              # after flashing, open the 74880-baud boot-ROM monitor?
CAPTURE_LOGS=0           # --logs: non-interactive reset+capture then exit
LOG_SECS=15              # seconds to capture for --logs
LOG_BAUD=115200          # baud for --logs capture

# --- parse args ---
while [ $# -gt 0 ]; do
    case "$1" in
        --fw)  MODE="fw";  MODE_SET=1; shift ;;
        --fs)  MODE="fs";  MODE_SET=1; shift ;;
        --all) MODE="all"; MODE_SET=1; shift ;;
        --no-build) DO_BUILD=0; shift ;;
        --no-wifi)  WIFI_MODE="skip"; shift ;;
        --wifi)     WIFI_MODE="set";  shift ;;
        --ssid) WIFI_SSID="${2:-}"; WIFI_MODE="set"; shift 2 ;;
        --pass) WIFI_PASS="${2:-}"; WIFI_MODE="set"; shift 2 ;;
        # --debug: build/flash the serial-logging firmware and open the monitor.
        --debug)   ENV="esp12e-debug"; OPEN_MONITOR=1; shift ;;
        --monitor) OPEN_MONITOR=1; shift ;;
        --boot)    OPEN_BOOT=1; shift ;;
        # --logs [SECS]: reset the board and print the serial log for SECS seconds
        # (default 15), then exit. Works in non-interactive shells (unlike the pio
        # monitor). Combine with --debug to also flash the serial-logging build first.
        --logs)
            CAPTURE_LOGS=1
            if [ "${2:-}" ] && printf '%s' "$2" | grep -qE '^[0-9]+$'; then
                LOG_SECS="$2"; shift 2
            else
                shift
            fi ;;
        -p|--port) PORT="${2:-}"; shift 2 ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)
            echo "unknown option: $1" >&2; exit 2 ;;
        *)
            PORT="$1"; shift ;;
    esac
done

# Decide whether we're flashing at all. If the user asked ONLY to open a monitor
# (--monitor or --boot) without --debug or an explicit flash mode, just monitor.
DO_FLASH=1
if [ "$MODE_SET" -eq 0 ] && [ "$ENV" = "esp12e" ] \
   && { [ "$OPEN_MONITOR" -eq 1 ] || [ "$OPEN_BOOT" -eq 1 ] || [ "$CAPTURE_LOGS" -eq 1 ]; }; then
    DO_FLASH=0
fi

# --- locate PlatformIO ---
find_pio() {
    if command -v pio >/dev/null 2>&1; then echo "pio"; return 0; fi
    if command -v platformio >/dev/null 2>&1; then echo "platformio"; return 0; fi
    for c in "$HOME/.platformio/penv/bin/pio" "$HOME/.platformio/penv/bin/platformio"; do
        [ -x "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}
PIO="$(find_pio || true)"

# --- locate esptool (used as fallback when PlatformIO is not installed) ---
find_esptool() {
    for c in esptool esptool.py \
              "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" \
              "$HOME/.platformio/penv/bin/esptool.py"; do
        command -v "$c" >/dev/null 2>&1 && { echo "$c"; return 0; }
        [ -x "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}
ESPTOOL="$(find_esptool || true)"

PREBUILT_FW="$SCRIPT_DIR/prebuilt/firmware.bin"
PREBUILT_FS="$SCRIPT_DIR/prebuilt/littlefs.bin"
# LittleFS offset for 4M2M layout (eagle.flash.4m2m.ld): 0x200000
LITTLEFS_OFFSET="0x200000"

USE_PREBUILT=0
if [ -z "${PIO:-}" ]; then
    if [ -z "${ESPTOOL:-}" ]; then
        echo "error: neither PlatformIO nor esptool found." >&2
        echo "  Install PlatformIO: https://platformio.org/install" >&2
        echo "  Or install esptool:  pip install esptool" >&2
        exit 1
    fi
    if [ ! -f "$PREBUILT_FW" ] || [ ! -f "$PREBUILT_FS" ]; then
        echo "error: PlatformIO not found and prebuilt binaries are missing." >&2
        echo "  Expected: $PREBUILT_FW" >&2
        echo "            $PREBUILT_FS" >&2
        echo "  Install PlatformIO to build from source, or add the prebuilt binaries." >&2
        exit 1
    fi
    echo "==> PlatformIO not found — using prebuilt binaries with esptool."
    USE_PREBUILT=1
    DO_BUILD=0   # nothing to build
fi

# --- hardware detection -----------------------------------------------------
# Parse `pio device list` into parallel arrays:
#   PORTS[]  = device path (/dev/ttyUSB0 ...)
#   DESCS[]  = human description + hardware id (for identifying the ESP8266)
PORTS=()
DESCS=()

detect_devices() {
    PORTS=(); DESCS=()
    local cur_port="" cur_desc="" cur_hwid="" line
    # `pio device list` prints blocks like:
    #   /dev/ttyUSB0
    #   ----------
    #   Hardware ID: USB VID:PID=1A86:7523 ...
    #   Description: USB2.0-Serial
    # Only real USB-serial adapters are relevant (ttyUSB*/ttyACM*). The dozens of
    # built-in motherboard ports (ttyS*) show up as "n/a" and are ignored.
    flush() {
        [ -n "$cur_port" ] || return 0
        case "$cur_port" in
            /dev/ttyUSB*|/dev/ttyACM*)
                # Skip entries with no real USB identity.
                if [ -n "$cur_hwid" ] && [ "$cur_hwid" != "n/a" ] || \
                   { [ -n "$cur_desc" ] && [ "$cur_desc" != "n/a" ]; } || \
                   [ -e "$cur_port" ]; then
                    PORTS+=("$cur_port")
                    local d="${cur_desc:-serial device}"
                    [ "$d" = "n/a" ] && d="serial device"
                    DESCS+=("$d${cur_hwid:+  [$cur_hwid]}")
                fi ;;
        esac
    }
    while IFS= read -r line; do
        case "$line" in
            /dev/*)
                flush
                cur_port="$line"; cur_desc=""; cur_hwid="" ;;
            "Description:"*) cur_desc="$(echo "${line#Description:}" | sed 's/^ *//')" ;;
            "Hardware ID:"*) cur_hwid="$(echo "${line#Hardware ID:}"  | sed 's/^ *//')" ;;
        esac
    done < <("$PIO" device list 2>/dev/null)
    flush

    # Fallback: raw /dev globbing if pio returned nothing.
    if [ "${#PORTS[@]}" -eq 0 ]; then
        local p
        for p in /dev/ttyACM* /dev/ttyUSB*; do
            [ -e "$p" ] || continue
            PORTS+=("$p"); DESCS+=("serial device")
        done
    fi
}

# Heuristic: does a description look like a common ESP8266 USB-UART bridge?
looks_like_esp() {
    echo "$1" | grep -qiE 'CP210|CH340|CH910|1A86|10C4|Silicon Labs|USB2.0-Serial|FT232|ESP'
}

if [ -z "${PORT:-}" ]; then
    echo "No serial port specified — scanning for connected hardware..."
    detect_devices

    if [ "${#PORTS[@]}" -eq 0 ]; then
        echo "No serial devices detected."
        echo "Plug in the device (or check permissions) and try again, or enter a path."
        printf "Enter the serial port device path (e.g. /dev/ttyACM0): "
        read -r PORT
        if [ -z "${PORT:-}" ]; then
            echo "No port entered. Aborted." >&2
            exit 1
        fi
    else
        echo "Detected device(s):"
        best=-1
        for i in "${!PORTS[@]}"; do
            tag=""
            if looks_like_esp "${DESCS[$i]}"; then tag="  <- likely ESP"; [ "$best" -lt 0 ] && best="$i"; fi
            printf "  %d) %-14s %s%s\n" "$((i+1))" "${PORTS[$i]}" "${DESCS[$i]}" "$tag"
        done
        [ "$best" -lt 0 ] && best=0   # default to the first if none matched

        default_no=$((best+1))
        printf "Select device to flash [1-%d] (default %d = %s), or type a path: " \
               "${#PORTS[@]}" "$default_no" "${PORTS[$best]}"
        read -r sel
        if [ -z "$sel" ]; then
            PORT="${PORTS[$best]}"
        elif [[ "$sel" =~ ^[0-9]+$ ]] && [ "$sel" -ge 1 ] && [ "$sel" -le "${#PORTS[@]}" ]; then
            PORT="${PORTS[$((sel-1))]}"
        else
            PORT="$sel"
        fi
    fi

    # Reject an empty selection rather than "confirming" a blank port.
    if [ -z "${PORT:-}" ]; then
        echo "No port selected. Aborted." >&2
        exit 1
    fi

    # Confirm before writing to the device.
    printf "About to flash '%s' (mode: %s). Continue? [Y/n]: " "$PORT" "$MODE"
    read -r ok
    case "${ok:-Y}" in [Nn]*) echo "Aborted."; exit 1 ;; esac
fi

if [ -z "${PORT:-}" ]; then
    echo "error: no serial port provided." >&2
    exit 1
fi

# If we're only opening a monitor (--monitor / --boot with no flash), skip straight
# to the monitor section.
if [ "$DO_FLASH" -eq 1 ]; then

# --- optional WiFi pre-provisioning (writes data/wifi.json) ------------------
# If credentials are provided/entered, the firmware will connect to that network
# directly on boot (no captive portal needed). Only relevant when the filesystem is
# being flashed (modes fs/all). Skipped for --fw or --no-wifi.
WIFI_JSON="data/wifi.json"

write_wifi_json() {
    local ssid="$1" pass="$2"
    # Minimal JSON escaping for backslash and double-quote.
    ssid=$(printf '%s' "$ssid" | sed 's/\\/\\\\/g; s/"/\\"/g')
    pass=$(printf '%s' "$pass" | sed 's/\\/\\\\/g; s/"/\\"/g')
    printf '{\n  "ssid": "%s",\n  "pass": "%s"\n}\n' "$ssid" "$pass" > "$WIFI_JSON"
    echo "==> Wrote $WIFI_JSON (will be flashed to the device's filesystem)."
}

if [ "$MODE" != "fw" ] && [ "$WIFI_MODE" != "skip" ]; then
    if [ "$WIFI_MODE" = "set" ]; then
        # Non-interactive: use provided --ssid/--pass (prompt only for missing ssid).
        if [ -z "$WIFI_SSID" ]; then
            printf "WiFi SSID: "; read -r WIFI_SSID
        fi
        if [ -n "$WIFI_SSID" ]; then
            write_wifi_json "$WIFI_SSID" "$WIFI_PASS"
        fi
    else
        # Interactive (default): offer to bake in credentials.
        if [ -f "$WIFI_JSON" ]; then
            existing_ssid=$(grep -o '"ssid"[^,}]*' "$WIFI_JSON" | sed 's/.*: *"\(.*\)".*/\1/')
            printf "WiFi already configured (SSID '%s'). Change it? [y/N]: " "$existing_ssid"
            read -r ch
            case "${ch:-N}" in [Yy]*) WANT_WIFI="y" ;; *) WANT_WIFI="n" ;; esac
        else
            printf "Bake in WiFi credentials so the device joins your network automatically? [y/N]: "
            read -r ch
            case "${ch:-N}" in [Yy]*) WANT_WIFI="y" ;; *) WANT_WIFI="n" ;; esac
        fi
        if [ "${WANT_WIFI:-n}" = "y" ]; then
            printf "WiFi SSID: "; read -r in_ssid
            printf "WiFi password (leave blank for open network): "
            read -rs in_pass; echo
            if [ -n "$in_ssid" ]; then
                write_wifi_json "$in_ssid" "$in_pass"
            else
                echo "==> No SSID entered; leaving WiFi config unchanged."
            fi
        else
            echo "==> Skipping WiFi pre-provisioning (use the AutoConnectAP portal instead)."
        fi
    fi
fi

echo "==> PlatformIO: $PIO"
echo "==> Env:        $ENV"
echo "==> Port:       $PORT   (baud $BAUD)"
echo "==> Mode:       $MODE"
[ "$OPEN_MONITOR" -eq 1 ] && echo "==> Serial monitor will open after flashing (Ctrl-C to exit)."

# Optional explicit build first so compile errors surface clearly before flashing.
# The PlatformIO upload targets also build on their own; skip with --no-build.
if [ "$DO_BUILD" -eq 1 ] && [ "$USE_PREBUILT" -eq 0 ]; then
    case "$MODE" in
        fw)  "$PIO" run -e "$ENV" ;;
        fs)  "$PIO" run -e "$ENV" -t buildfs ;;
        all) "$PIO" run -e "$ENV"; "$PIO" run -e "$ENV" -t buildfs ;;
    esac
fi

flash_fw() {
    if [ "$USE_PREBUILT" -eq 1 ]; then
        echo "==> Flashing prebuilt firmware ($PREBUILT_FW)..."
        "$ESPTOOL" --port "$PORT" --baud "$BAUD" write_flash \
            --flash_mode dio --flash_freq 40m --flash_size 4MB \
            0x0 "$PREBUILT_FW"
    else
        echo "==> Flashing firmware ($ENV)..."
        "$PIO" run -e "$ENV" -t upload --upload-port "$PORT"
    fi
}
flash_fs() {
    # NOTE: uploadfs REPLACES the entire LittleFS partition with a fresh image built
    # from data/. This is safe for user settings: custom antenna names, the max value
    # and WiFi credentials are all stored OUTSIDE LittleFS (names + max in the EEPROM
    # sector, WiFi in the SDK RF-config region), so none of them are affected here.
    if [ "$USE_PREBUILT" -eq 1 ]; then
        echo "==> Flashing prebuilt web assets ($PREBUILT_FS) at $LITTLEFS_OFFSET..."
        "$ESPTOOL" --port "$PORT" --baud "$BAUD" write_flash \
            --flash_mode dio --flash_freq 40m --flash_size 4MB \
            "$LITTLEFS_OFFSET" "$PREBUILT_FS"
    else
        echo "==> Flashing web assets (LittleFS)..."
        "$PIO" run -e "$ENV" -t uploadfs --upload-port "$PORT"
    fi
}

case "$MODE" in
    fw)  flash_fw ;;
    fs)  flash_fs ;;
    all) flash_fw; flash_fs ;;
esac

echo "==> Done."

fi   # end: if [ "$DO_FLASH" -eq 1 ]

# Optionally open the serial monitor (used by --debug / --monitor). NOTE: serial
# shares GPIO1/GPIO3 with the DOWN/UP buttons, so the buttons won't work while the
# monitor is attached — this is only for reading debug output.
if [ "$OPEN_MONITOR" -eq 1 ] && [ "$CAPTURE_LOGS" -eq 0 ]; then
    echo "==> Opening serial monitor on $PORT (Ctrl-C to exit)."
    echo "    (Serial shares the UP/DOWN button pins; buttons are disabled while attached.)"
    echo "    Our logs print at 115200. If you see only garbage that repeats, the device"
    echo "    is likely reset-looping — run './flash.sh --boot $PORT' to read the ESP8266"
    echo "    boot-ROM reset reason at 74880 baud."
    "$PIO" device monitor -e "$ENV" --port "$PORT"
fi

# --boot: open the monitor at the ESP8266 boot-ROM baud (74880) to read the raw
# reset reason / boot banner. Useful when the app is crash-looping.
if [ "$OPEN_BOOT" -eq 1 ]; then
    echo "==> Opening boot-ROM monitor on $PORT at 74880 baud (Ctrl-C to exit)."
    "$PIO" device monitor --port "$PORT" --baud 74880 --filter esp8266_exception_decoder
fi

# --logs: non-interactive capture. Resets the board (via DTR/RTS) and prints the
# serial output for LOG_SECS seconds, then exits. Uses PlatformIO's bundled pyserial
# so it works even where the interactive monitor can't (e.g. no controlling TTY).
if [ "$CAPTURE_LOGS" -eq 1 ]; then
    echo "==> Capturing serial from $PORT for ${LOG_SECS}s at ${LOG_BAUD} baud..."
    PYBIN="$(dirname "$PIO")/python"
    [ -x "$PYBIN" ] || PYBIN="python3"
    FLASH_PORT="$PORT" FLASH_SECS="$LOG_SECS" FLASH_BAUD="$LOG_BAUD" "$PYBIN" - <<'PYEOF'
import os, sys, time
try:
    import serial
except Exception:
    sys.stderr.write("pyserial not available; cannot capture. Try: pip install pyserial\n")
    sys.exit(1)
port = os.environ["FLASH_PORT"]
secs = float(os.environ["FLASH_SECS"])
baud = int(os.environ["FLASH_BAUD"])
s = serial.Serial(port, baud, timeout=0.2)
# Pulse reset (DTR/RTS) so we capture from power-on.
try:
    s.setDTR(False); s.setRTS(True); time.sleep(0.1)
    s.setRTS(False); time.sleep(0.1)
except Exception:
    pass
s.reset_input_buffer()
end = time.time() + secs
while time.time() < end:
    c = s.read(512)
    if c:
        sys.stdout.write(c.decode("utf-8", "replace")); sys.stdout.flush()
s.close()
sys.stdout.write("\n==> capture complete\n")
PYEOF
fi
