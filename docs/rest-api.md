# REST API — UberSDR Antenna Switch

The replica firmware adds a clean, flexible **JSON REST API** on top of the antenna
model, served on port 80 alongside the stock web UI and the legacy Up/Dn routes. It is
designed for scripting an SDR/receiver setup (e.g. selecting the antenna feeding a
KiwiSDR) from a browser, `curl`, or any HTTP client.

- **Base URL:** `http://<device>/` — where `<device>` is the mDNS name
  (`http://uberant.local` by default, or `http://<device-name>.local` if customised), the DHCP IP, or `192.168.4.1` in AP/config mode.
- **Web UI:** the modern dark UI at `/` (the default page) drives this API and includes
  a **`?` button** with an inline API reference. The classic stock page is at `/old`.
- **Content type:** every response is `application/json`.
- **CORS:** all `/api/*` responses send `Access-Control-Allow-Origin: *` (plus
  `Allow-Methods`/`Allow-Headers`), so the API is callable from browser `fetch()`/XHR.
- **Methods:** mutating endpoints accept **any** HTTP method for convenience
  (so `curl http://.../api/antenna/up` works), but the **canonical** method is `POST`.
- **Parameters** may be supplied either as a **query string** (`?position=3`) or as a
  **JSON body** (`{"position":3}`).

## Data model

| Field | Meaning |
|-------|---------|
| `position` | Current selection index: `0` = GROUND, `1`…`7` = ANT1…ANT7 |
| `label` | Human label as shown on the OLED: `GROUND`, the port's **custom name** if set, else `ANT: n` |
| `name` | Custom name of the *current* antenna (empty string if unset / GROUND) |
| `names` | Object of all custom names, e.g. `{"1":"80m Vertical","3":"Beverage"}` |
| `ground` | `true` when `position == 0` |
| `max` | Highest selectable antenna (the SET/`MAX` value, `0`…`7`) |
| `count` | Same as `max` — number of selectable antennas above GROUND |
| `url` | The device's own web address (`http://<ip>`), or empty until an IP is assigned |
| `hostname` | mDNS hostname (derived from device name, e.g. `uberant`; falls back to `ESP-XXXXXX` if unset) |
| `device_name` | Configurable switch name (default `"UberANT"`) — shown on OLED splash and web UI header; also determines the `.local` hostname |
| `mqtt_enabled` | `true` if MQTT is enabled in config |
| `mqtt_connected` | `true` if currently connected to the MQTT broker |
| `locked` | `true` if the antenna switch is locked (changes blocked) |
| `time` | Current UTC time as ISO 8601 (`"2024-07-03T15:04:05Z"`), or `""` if NTP not synced |
| `local_time` | Current local time with UTC offset suffix (e.g. `"2024-07-03T16:04:05+01:00"`), or `""` if NTP not synced |
| `tz_offset` | Timezone offset in minutes east of UTC (e.g. `60` = UTC+1, `-300` = UTC−5). Persisted to EEPROM. |
| `scheduler_active` | `true` if at least one schedule entry is enabled |
| `scheduler_next` | Next scheduled firing as `"HH:MM ANT n"` or `"HH:MM GROUND"` (local time), or `""` if none / NTP not synced |

### Status object (returned by most endpoints)

```json
{
  "position": 3,
  "label": "80m Vertical",
  "ground": false,
  "max": 5,
  "count": 5,
  "name": "80m Vertical",
  "names": { "1": "40m Dipole", "3": "80m Vertical" },
  "url": "http://192.168.9.109",
  "hostname": "ESP-45CA21",
  "device_name": "UberSDR",
  "mqtt_enabled": true,
  "mqtt_connected": true,
  "locked": false,
  "time": "2024-07-03T15:04:05Z",
  "local_time": "2024-07-03T16:04:05+01:00",
  "tz_offset": 60
}
```

### Info object (`GET /api/info`)

```json
{
  "hostname": "ESP-45CA21", "ssid": "MyWiFi", "rssi": -58,
  "sta_ip": "192.168.9.109", "gateway": "192.168.9.1", "netmask": "255.255.255.0",
  "dns": "192.168.9.1", "sta_mac": "cc:50:e3:45:ca:21", "ap_mac": "ce:50:e3:45:ca:21",
  "bssid": "aa:bb:cc:dd:ee:ff", "ap_mode": false,
  "chip_id": "45ca21", "flash_id": "1440ef", "flash_size": 4194304,
  "sdk": "2.2.2-dev(...)", "core": "3.1.2", "boot": 31, "reset": "External System",
  "heap": 34512, "sketch": 435063, "free_sketch": 2621440, "uptime": 428,
  "device_name": "UberSDR",
  "time": "2024-07-03T15:04:05Z",
  "local_time": "2024-07-03T16:04:05+01:00",
  "tz_offset": 60
}
```

Sizes (`flash_size`, `heap`, `sketch`, `free_sketch`) are in **bytes**; `uptime` is in
**seconds**; `rssi` is in **dBm**. This is the same field set as the HTML `/info` page,
and is what the web UI's **ℹ️ info** button renders.

## Endpoints

| Method | Path | Body / query | Description | Returns |
|--------|------|--------------|-------------|---------|
| GET | `/api/status` | — | Current state (incl. `mqtt_enabled`, `mqtt_connected`) | status object |
| GET | `/api/info` | — | Device / network / firmware info | info object (see below) |
| GET | `/api/antenna` | — | Current state | status object |
| POST | `/api/antenna/up` | — | Step +1 (wraps GROUND↔max) | status object |
| POST | `/api/antenna/down` | — | Step −1 (wraps) | status object |
| POST | `/api/antenna/ground` | — | Select GROUND (position 0) | status object |
| POST | `/api/antenna` | `position=N` or `{"position":N}` | Select position `N` (0…max) | status object |
| GET | `/api/max` | — | Current max value | `{"max":N}` |
| POST | `/api/max` | `value=N` or `{"value":N}` | Set max antennas (0…7) | `{"max":N}` |
| POST | `/api/max/bump` | — | Bump max (wraps 0…7), like the SET button | `{"max":N}` |
| GET | `/api/names` | — | All custom names | names object `{"1":"…"}` |
| POST | `/api/name` | `position=N&name=…` or `{"position":N,"name":"…"}` | Name ANT `N` (1…7). Empty name clears it. | `{"position":N,"name":"…"}` |
| DELETE | `/api/name` | `position=N` | Clear the name for ANT `N` | `{"position":N,"name":""}` |
| GET | `/api/mqtt` | — | Read MQTT config + connection status | mqtt config object |
| POST | `/api/mqtt` | JSON body (partial OK) | Update MQTT config (takes effect immediately) | `{"saved":true}` |
| GET | `/api/ntp` | — | Read NTP config + sync status + current UTC time + timezone | ntp object |
| POST | `/api/ntp` | `{"host":"…","port":N,"tz_offset":N}` | Update NTP config and/or timezone offset (takes effect immediately) | `{"saved":true}` |
| GET | `/api/schedule` | — | Read all 10 schedule slots + global flags | schedule object |
| POST | `/api/schedule` | `{"id":N,"enabled":bool,"days":mask,"hour":H,"minute":M,"position":P}` | Set/update slot N (0–9). Also accepts `{"scheduler_enabled":bool}` (master switch) or `{"respect_lock":bool}` for global flags. | schedule object |
| DELETE | `/api/schedule?id=N` | — | Clear/disable slot N (0–9) | schedule object |
| GET | `/api/events` | — | Last 25 antenna-change events (newest first) | JSON array |
| POST | `/api/display` | `{"text":"…","duration":N,"align":"…"}` | Show a custom message on the OLED | `{"text":"…","duration":N,"align":"…"}` |
| GET | `/api/display/blank` | — | Read display blank (screen-saver) config | `{"enabled":bool,"timeout":N}` |
| POST | `/api/display/blank` | `{"enabled":true,"timeout":N}` | Set display blank config (persisted to EEPROM) | `{"enabled":bool,"timeout":N}` |
| GET | `/api/display/mode` | — | Read OLED display mode | `{"mode":"port"\|"clock"}` |
| POST | `/api/display/mode` | `{"mode":"port"}` or `{"mode":"clock"}` | Set OLED display mode (persisted to EEPROM). `"port"` = antenna label prominent (default); `"clock"` = time prominent. Port changes always show the antenna label for 2 s regardless. | `{"mode":"port"\|"clock"}` |
| GET | `/api/default-port` | — | Read the startup default port | `{"default_port":N}` |
| POST | `/api/default-port` | `{"default_port":N}` | Set the startup default port (0=GROUND, 1–7=ANT1–ANT7). Persisted to EEPROM. Clamped to GROUND if max antennas is later reduced below this value. | `{"default_port":N}` |
| GET | `/api/startup-port` | — | Read startup port behaviour | `{"mode":"default"\|"last"}` |
| POST | `/api/startup-port` | `{"mode":"default"}` or `{"mode":"last"}` | Set startup port behaviour. `"default"` = use the starred default port; `"last"` = restore the last used port (written to LittleFS with a 2-second debounce; reset to GROUND if the filesystem is reflashed). Persisted to EEPROM. | `{"mode":"default"\|"last"}` |
| GET | `/api/lock` | — | Read lock state | `{"locked":bool}` |
| POST | `/api/lock` | `{"locked":true}` | Set lock state | `{"locked":bool}` |
| POST | `/api/lock/toggle` | — | Toggle lock state | `{"locked":bool}` |
| POST | `/api/restart` | — | Reboot the device (responds, then restarts) | `{"restarting":true}` |

## Lock

The antenna switch can be **locked** to prevent accidental or unauthorised antenna changes.
When locked:

- Physical UP/DOWN button presses are ignored.
- REST API antenna-change calls return **HTTP 423 Locked** with `{"error":"locked"}`.
- MQTT antenna commands are silently ignored.
- The OLED status screen shows a small `[L]` indicator in the top-right corner.
- The web UI shows a red padlock button and a full-screen overlay when a change is attempted.

The lock state is **not** persisted to EEPROM — it resets to unlocked on reboot.

| Method | Path | Body | Description |
|--------|------|------|-------------|
| GET | `/api/lock` | — | Read lock state: `{"locked":bool}` |
| POST | `/api/lock` | `{"locked":true}` or `{"locked":false}` | Set lock state explicitly |
| POST | `/api/lock/toggle` | — | Toggle lock state |

```bash
# Lock
curl -X POST "$HOST/api/lock/toggle"

# Unlock
curl -X POST -H "Content-Type: application/json" \
  -d '{"locked":false}' "$HOST/api/lock"

# Read state
curl "$HOST/api/lock"
```

**Physical gesture:** hold the UP button for 3 seconds to toggle lock/unlock.

**MQTT:** publish `"lock"`, `"unlock"`, or `"toggle"` to `<prefix>/lock`.

## Errors

Errors are returned as JSON with an appropriate HTTP status:

```json
{ "error": "position out of range (0..max)" }
```

| Status | When |
|--------|------|
| `400 Bad Request` | Required parameter (`position` / `value`) missing |
| `422 Unprocessable Entity` | Parameter out of range (`position` > `max`, or `value` outside `0..7`) |
| `423 Locked` | Antenna change attempted while the switch is locked |

## Port names

Each antenna **ANT1–ANT7** can be given a custom name (GROUND cannot be named). Names:

- are capped at **16 characters** and sanitised (control chars, quotes and backslashes
  are stripped);
- **persist across reboots, power-cycles AND firmware/filesystem reflashes** — they are
  stored in the **EEPROM sector** (alongside the `max` value), which is *outside* the
  LittleFS partition, so `pio run -t uploadfs` does not erase them;
- when set, are shown **instead of `ANT: n`** on the OLED (which auto-shrinks the text
  to fit) and in the web UI, and appear in the `label`/`name`/`names` status fields.

```bash
# name ANT3, then read it back
curl -X POST "$H/api/name?position=3&name=80m%20Vertical"
curl "$H/api/names"          # -> {"3":"80m Vertical"}

# clear a name (either form)
curl -X POST   "$H/api/name?position=3&name="
curl -X DELETE "$H/api/name?position=3"
```

## Examples

```bash
HOST=http://ESP-45CA21.local      # or the device IP, or http://192.168.4.1 (AP mode)

# Read current state
curl "$HOST/api/status"

# Step up / down
curl -X POST "$HOST/api/antenna/up"
curl -X POST "$HOST/api/antenna/down"

# Go to GROUND
curl -X POST "$HOST/api/antenna/ground"

# Select ANT3 (query string)
curl -X POST "$HOST/api/antenna?position=3"

# Select ANT3 (JSON body)
curl -X POST -H "Content-Type: application/json" \
     -d '{"position":3}' "$HOST/api/antenna"

# Read / set the max selectable antenna
curl "$HOST/api/max"
curl -X POST "$HOST/api/max?value=5"
curl -X POST "$HOST/api/max/bump"
```

### From JavaScript (browser)

```js
// Select ANT2
await fetch("http://ESP-45CA21.local/api/antenna", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ position: 2 }),
}).then(r => r.json()).then(console.log);
```

### From Python

```python
import requests
HOST = "http://ESP-45CA21.local"

print(requests.get(f"{HOST}/api/status").json())
requests.post(f"{HOST}/api/antenna", json={"position": 4})
requests.post(f"{HOST}/api/antenna/up")
```

## MQTT config endpoint

`GET /api/mqtt` returns the current MQTT configuration and live connection status:

```json
{
  "enabled": true,
  "host": "192.168.1.10",
  "port": 1883,
  "user": "myuser",
  "pass": "***",
  "prefix": "",
  "retain": true,
  "commands": false,
  "connected": true,
  "device_name": "UberSDR"
}
```

`POST /api/mqtt` accepts a JSON body with any subset of the fields above (including
`device_name` to rename the switch). The password field is write-only: `GET` returns
`"***"` if a password is set, `""` if not. Sending `"pass": "***"` in a POST leaves
the stored password unchanged.

```bash
# Enable MQTT with a broker at 192.168.1.10
curl -X POST -H "Content-Type: application/json" \
  -d '{"enabled":true,"host":"192.168.1.10","port":1883,"retain":true}' \
  "$HOST/api/mqtt"

# Add authentication
curl -X POST -H "Content-Type: application/json" \
  -d '{"user":"myuser","pass":"mypassword"}' "$HOST/api/mqtt"

# Enable command topics
curl -X POST -H "Content-Type: application/json" \
  -d '{"commands":true}' "$HOST/api/mqtt"

# Disable MQTT
curl -X POST -H "Content-Type: application/json" \
  -d '{"enabled":false}' "$HOST/api/mqtt"

# Rename the switch (shown on OLED splash and web UI header)
curl -X POST -H "Content-Type: application/json" \
  -d '{"device_name":"ShackSDR"}' "$HOST/api/mqtt"
```

Config changes take effect **immediately** — no reboot required. See
[`mqtt.md`](mqtt.md) for the full MQTT topic reference, payload formats, and Home
Assistant integration examples.

### MQTT command topics (when `commands: true`)

When command topics are enabled, the device subscribes to:

| Topic | Payload | Effect |
|-------|---------|--------|
| `<prefix>/set` | `"up"` / `"down"` / `"ground"` / `"0"`…`"7"` | Select antenna |
| `<prefix>/max/set` | `"0"`…`"7"` | Set max antennas |
| `<prefix>/name/set` | `{"position":N,"name":"…"}` | Set/clear a port name |
| `<prefix>/display` | `{"text":"…","duration":N,"align":"…"}` | Show a custom OLED message |
| `<prefix>/lock` | `"lock"` / `"unlock"` / `"toggle"` | Lock or unlock antenna selection |
| `<prefix>/restart` | any | Reboot the device |

```bash
# Publish a command (mosquitto_pub example)
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/set" -m "up"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/set" -m "3"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/max/set" -m "5"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/name/set" \
  -m '{"position":3,"name":"80m Vertical"}'
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/display" \
  -m '{"text":"TX ON","duration":3,"align":"center"}'
```

## NTP config endpoint

`GET /api/ntp` returns the current NTP configuration, sync status, and timezone:

```json
{
  "host": "pool.ntp.org",
  "port": 123,
  "synced": true,
  "time": "2024-07-03T15:04:05Z",
  "local_time": "2024-07-03T16:04:05+01:00",
  "tz_offset": 60,
  "scheduler_active": true,
  "scheduler_next": "08:00 ANT 3"
}
```

`POST /api/ntp` accepts `host`, `port`, and/or `tz_offset` (signed integer, minutes east
of UTC). The SNTP stack is reconfigured immediately. The timezone offset is persisted to
EEPROM and applied to all timestamps in the web UI and MQTT info pushes.
If the server is unreachable the device continues operating without a timestamp; it retries
every 30 seconds until sync succeeds, then re-syncs every hour.

```bash
# Use a local NTP server
curl -X POST -H "Content-Type: application/json" \
  -d '{"host":"192.168.1.1","port":123}' "$HOST/api/ntp"

# Set timezone to UTC+1 (e.g. UK BST / Central Europe)
curl -X POST -H "Content-Type: application/json" \
  -d '{"tz_offset":60}' "$HOST/api/ntp"

# Set timezone to US Eastern (UTC−5)
curl -X POST -H "Content-Type: application/json" \
  -d '{"tz_offset":-300}' "$HOST/api/ntp"

# Check sync status and current local time
curl "$HOST/api/ntp"
```

## OLED display mode endpoint

`GET /api/display/mode` returns the current OLED display mode:

```json
{ "mode": "port" }
```

`POST /api/display/mode` sets the mode (persisted to EEPROM, takes effect immediately):

| Mode | OLED layout |
|------|-------------|
| `"port"` (default) | Antenna label large (top); device name + time small (bottom) |
| `"clock"` | Time (`HH:MM:SS`) large (top); antenna label + device name small (bottom). Falls back to port mode if NTP is not synced. |

In both modes, when the antenna position changes the display temporarily switches to port-prominent layout for **2 seconds** so the user sees clear confirmation of the switch.

```bash
# Switch to clock-prominent mode
curl -X POST -H "Content-Type: application/json" \
  -d '{"mode":"clock"}' "$HOST/api/display/mode"

# Switch back to port-prominent mode (default)
curl -X POST -H "Content-Type: application/json" \
  -d '{"mode":"port"}' "$HOST/api/display/mode"

# Read current mode
curl "$HOST/api/display/mode"
```

## OLED display endpoint

`POST /api/display` shows a custom message on the OLED for a defined period, then
automatically resumes the normal status display.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `text` | string | **required** | Message to display (any printable ASCII) |
| `duration` | integer (seconds) | `1` | How long to show the message (min 1 s, max 60 s) |
| `align` | string | `"center"` | Horizontal alignment: `"left"`, `"right"`, or `"center"` |

The text is **auto-sized** to the largest font where all lines fit the 128×32 OLED panel
both in width and height:

| Font size | Pixel height | Max lines | Max chars/line |
|-----------|-------------|-----------|----------------|
| 3 (large) | 24 px | 1 | 6 |
| 2 (medium) | 16 px | 2 | 10 |
| 1 (small) | 8 px | 4 | 21 |

**Multi-line:** embed `\n` in the `text` value to split across lines. The firmware also
accepts the two-character sequence `\\n` so it works in JSON strings without escaping.
Lines that are still too wide at size 1 are truncated with `>`. The whole block is
vertically centred on the panel.

```bash
# Show a large centred "M" for 1 second (default)
curl -X POST -H "Content-Type: application/json" \
  -d '{"text":"M"}' "$HOST/api/display"

# Show "TX ON" right-aligned for 5 seconds
curl -X POST -H "Content-Type: application/json" \
  -d '{"text":"TX ON","duration":5,"align":"right"}' "$HOST/api/display"

# Two-line message (auto-sizes to fit both lines)
curl -X POST -H "Content-Type: application/json" \
  -d '{"text":"ANT 3\n80m Vert","duration":4}' "$HOST/api/display"

# Show a long string (auto-shrinks, truncates if needed)
curl -X POST -H "Content-Type: application/json" \
  -d '{"text":"dsofdskfjndsnkfndsjnfkndsfnjksdnfsnd","duration":3}' "$HOST/api/display"
```

Response:
```json
{ "text": "TX ON", "duration": 5, "align": "right" }
```

## Event log endpoint

`GET /api/events` returns the last 25 antenna-change events as a JSON array, newest first.
Each event includes the UTC timestamp (empty string if NTP not yet synced), the source of
the change, the antenna position, and the human label.

```json
[
  {"ts":"2024-07-03T15:04:05Z","source":"api","position":3,"label":"80m Vertical"},
  {"ts":"2024-07-03T14:58:12Z","source":"button","position":0,"label":"GROUND"},
  {"ts":"","source":"web","position":1,"label":"ANT: 1"}
]
```

Sources: `"button"` (physical UP/DOWN), `"api"` (REST API), `"web"` (legacy `/5/on` `/4/on`
routes), `"mqtt"` (MQTT command topic).

## Schedule endpoint

The scheduler fires antenna changes at configured times on selected days of the week,
using **local time** (the stored `tz_offset`). NTP sync is required — the scheduler
does not run if the clock is not set. Each fire appears in the event log with
`"source":"schedule"` and triggers an MQTT state publish.

Up to **10 slots** (indices 0–9). Each slot has:

| Field | Type | Description |
|-------|------|-------------|
| `id` | 0–9 | Slot index |
| `enabled` | bool | Whether this slot is active |
| `days` | 0–127 | Bitmask: bit0=Sun, bit1=Mon, bit2=Tue, bit3=Wed, bit4=Thu, bit5=Fri, bit6=Sat. `127` = every day |
| `hour` | 0–23 | Local hour to fire |
| `minute` | 0–59 | Local minute to fire |
| `position` | 0–7 | Antenna position (0=GROUND, 1–7=ANT1–ANT7) |

`GET /api/schedule` returns:

```json
{
  "enabled": true,
  "respect_lock": false,
  "entries": [
    {"id":0,"enabled":true,"days":62,"hour":8,"minute":0,"position":3},
    {"id":1,"enabled":true,"days":127,"hour":22,"minute":0,"position":0},
    {"id":2,"enabled":false,"days":127,"hour":0,"minute":0,"position":0}
  ]
}
```

`enabled`: master on/off switch — when `false`, no scheduled changes fire (entries are preserved).
`respect_lock`: when `true`, scheduled changes are skipped while the switch is locked.

`POST /api/schedule` sets one entry **or** one/both global flags:

```bash
# Add/update slot 0: ANT3 at 08:00 Mon–Fri (Mon=2,Tue=4,Wed=8,Thu=16,Fri=32 → 62)
curl -X POST -H "Content-Type: application/json" \
  -d '{"id":0,"enabled":true,"days":62,"hour":8,"minute":0,"position":3}' \
  "$HOST/api/schedule"

# Every day at 22:00 switch to GROUND
curl -X POST -H "Content-Type: application/json" \
  -d '{"id":1,"enabled":true,"days":127,"hour":22,"minute":0,"position":0}' \
  "$HOST/api/schedule"

# Disable slot 0 without deleting it
curl -X POST -H "Content-Type: application/json" \
  -d '{"id":0,"enabled":false}' "$HOST/api/schedule"

# Delete (clear) slot 0
curl -X DELETE "$HOST/api/schedule?id=0"

# Pause the scheduler (master off) without deleting entries
curl -X POST -H "Content-Type: application/json" \
  -d '{"scheduler_enabled":false}' "$HOST/api/schedule"

# Re-enable the scheduler
curl -X POST -H "Content-Type: application/json" \
  -d '{"scheduler_enabled":true}' "$HOST/api/schedule"

# Set respect-lock flag
curl -X POST -H "Content-Type: application/json" \
  -d '{"respect_lock":true}' "$HOST/api/schedule"
```

### Days bitmask reference

| Value | Days |
|-------|------|
| `127` | Every day (Sun–Sat) |
| `62` | Mon–Fri (weekdays) |
| `65` | Sat + Sun (weekend) |
| `2` | Monday only |
| `64` | Saturday only |

---

## Relationship to the stock routes

The legacy stock routes remain available and unchanged for byte-compatibility:

- `GET /5/on` — Up (antenna +1), returns the stock HTML page.
- `GET /4/on` — Dn (antenna −1), returns the stock HTML page.

The REST API is additive: it does not alter the stock web UI or its HTML. See
[`web-interface.md`](web-interface.md) for the stock interface, and
[`../firmware/README.md`](../firmware/README.md) for the firmware overview.
