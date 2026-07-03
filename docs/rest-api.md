# REST API — UberSDR Antenna Switch

The replica firmware adds a clean, flexible **JSON REST API** on top of the antenna
model, served on port 80 alongside the stock web UI and the legacy Up/Dn routes. It is
designed for scripting an SDR/receiver setup (e.g. selecting the antenna feeding a
KiwiSDR) from a browser, `curl`, or any HTTP client.

- **Base URL:** `http://<device>/` — where `<device>` is the mDNS name
  (`http://ESP-XXXXXX.local`), the DHCP IP, or `192.168.4.1` in AP/config mode.
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
| `hostname` | mDNS hostname (`ESP-XXXXXX`) |
| `device_name` | Configurable switch name (default `"UberSDR"`) — shown on OLED splash and web UI header |
| `mqtt_enabled` | `true` if MQTT is enabled in config |
| `mqtt_connected` | `true` if currently connected to the MQTT broker |

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
  "mqtt_connected": true
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
  "device_name": "UberSDR", "time": "2024-07-03T15:04:05Z"
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
| GET | `/api/ntp` | — | Read NTP config + sync status + current UTC time | ntp object |
| POST | `/api/ntp` | `{"host":"…","port":N}` | Update NTP config (takes effect immediately) | `{"saved":true}` |
| GET | `/api/events` | — | Last 25 antenna-change events (newest first) | JSON array |
| POST | `/api/restart` | — | Reboot the device (responds, then restarts) | `{"restarting":true}` |

## Errors

Errors are returned as JSON with an appropriate HTTP status:

```json
{ "error": "position out of range (0..max)" }
```

| Status | When |
|--------|------|
| `400 Bad Request` | Required parameter (`position` / `value`) missing |
| `422 Unprocessable Entity` | Parameter out of range (`position` > `max`, or `value` outside `0..7`) |

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
| `<prefix>/restart` | any | Reboot the device |

```bash
# Publish a command (mosquitto_pub example)
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/set" -m "up"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/set" -m "3"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/max/set" -m "5"
mosquitto_pub -h 192.168.1.10 -t "ubersdr/ESP-45CA21/name/set" \
  -m '{"position":3,"name":"80m Vertical"}'
```

## NTP config endpoint

`GET /api/ntp` returns the current NTP configuration and sync status:

```json
{
  "host": "pool.ntp.org",
  "port": 123,
  "synced": true,
  "time": "2024-07-03T15:04:05Z"
}
```

`POST /api/ntp` accepts `host` and/or `port`. The SNTP stack is reconfigured immediately.
If the server is unreachable the device continues operating without a timestamp; it retries
every 30 seconds until sync succeeds, then re-syncs every hour.

```bash
# Use a local NTP server
curl -X POST -H "Content-Type: application/json" \
  -d '{"host":"192.168.1.1","port":123}' "$HOST/api/ntp"

# Check sync status
curl "$HOST/api/ntp"
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

## Relationship to the stock routes

The legacy stock routes remain available and unchanged for byte-compatibility:

- `GET /5/on` — Up (antenna +1), returns the stock HTML page.
- `GET /4/on` — Dn (antenna −1), returns the stock HTML page.

The REST API is additive: it does not alter the stock web UI or its HTML. See
[`web-interface.md`](web-interface.md) for the stock interface, and
[`../firmware/README.md`](../firmware/README.md) for the firmware overview.
