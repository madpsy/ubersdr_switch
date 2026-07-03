# MQTT — UberSDR Antenna Switch

The replica firmware includes an optional, plaintext MQTT client
([knolleary/PubSubClient](https://github.com/knolleary/pubsubclient)) that publishes
device state and — optionally — accepts inbound commands to change the antenna.

- **TLS is not supported** (heap/complexity cost on the ESP8266 is too high).
- **Configuration persists in EEPROM** (not LittleFS), so it survives a filesystem
  reflash (`pio run -t uploadfs`).
- **Reconnect is automatic** with exponential backoff (5 s → 10 s → … → 60 s cap).
- **LWT** (`availability = offline`, retained) is set so the broker marks the device
  offline if it drops unexpectedly.

---

## Configuration

MQTT is configured via the **⚙ Settings** button in the web UI, or via the REST API:

```bash
HOST=http://ESP-45CA21.local

# Read current config (password is write-only — returned as "***" if set)
curl "$HOST/api/mqtt"

# Enable MQTT with a broker at 192.168.1.10
curl -X POST -H "Content-Type: application/json" \
  -d '{"enabled":true,"host":"192.168.1.10","port":1883,"retain":true}' \
  "$HOST/api/mqtt"

# Disable MQTT
curl -X POST -H "Content-Type: application/json" \
  -d '{"enabled":false}' "$HOST/api/mqtt"
```

### Config fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable/disable the MQTT client |
| `host` | string | `""` | Broker hostname or IP (required when enabled) |
| `port` | number | `1883` | Broker TCP port |
| `user` | string | `""` | Username (leave blank for anonymous) |
| `pass` | string | `""` | Password (write-only; returned as `"***"` if set) |
| `prefix` | string | `""` | Topic prefix. Empty → `ubersdr/<hostname>` |
| `retain` | bool | `true` | Publish state/availability messages as retained |
| `commands` | bool | `false` | Subscribe to command topics (see below) |

Config changes take effect **immediately** — the client reconnects with the new settings
without a reboot.

---

## Topics

All topics share a common prefix. The default prefix is `ubersdr/<hostname>` (e.g.
`ubersdr/ESP-45CA21`). A custom prefix can be set in the config.

### Published topics

| Topic | Retained | Payload | When |
|-------|----------|---------|------|
| `<prefix>/availability` | yes | `online` / `offline` | On connect (LWT publishes `offline`) |
| `<prefix>/status` | cfg | Full status JSON (see below) | On every antenna change |
| `<prefix>/antenna` | cfg | Bare position number (`0`…`7`) | On every antenna change |
| `<prefix>/info` | cfg | Device/network info JSON | On connect + every 60 s |
| `<prefix>/event` | no | Status JSON + `"source"` field | On every antenna change |

`cfg` = follows the `retain` config flag.

### Status JSON payload (`<prefix>/status`)

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
  "mqtt_enabled": true,
  "mqtt_connected": true
}
```

### Event JSON payload (`<prefix>/event`)

Same as the status object, with additional `"source"` and `"ts"` (UTC ISO 8601 timestamp,
empty string if NTP not yet synced) fields:

```json
{
  "source": "button",
  "ts": "2024-07-03T15:04:05Z",
  "position": 2,
  "label": "ANT: 2",
  "ground": false,
  "max": 5,
  "count": 5,
  "name": "",
  "names": {},
  "url": "http://192.168.9.109",
  "hostname": "ESP-45CA21",
  "device_name": "UberSDR",
  "mqtt_enabled": true,
  "mqtt_connected": true
}
```

| Source | Meaning |
|--------|---------|
| `button` | Physical UP/DOWN button press |
| `api` | REST API call (`/api/antenna/…`) |
| `web` | Legacy stock web route (`/5/on`, `/4/on`) |
| `mqtt` | Inbound MQTT command topic |

### Info JSON payload (`<prefix>/info`)

Same field set as `GET /api/info` — full device and network details:

```json
{
  "hostname": "ESP-45CA21",
  "sta_mac": "cc:50:e3:45:ca:21",
  "sta_ip": "192.168.9.109",
  "gateway": "192.168.9.1",
  "netmask": "255.255.255.0",
  "dns": "192.168.9.1",
  "ssid": "MyWiFi",
  "bssid": "aa:bb:cc:dd:ee:ff",
  "ap_mac": "ce:50:e3:45:ca:21",
  "rssi": -58,
  "ap_mode": false,
  "sdk": "2.2.2-dev(...)",
  "core": "3.1.2",
  "chip_id": "45ca21",
  "flash_id": "1440ef",
  "flash_size": 4194304,
  "boot": 31,
  "reset": "External System",
  "heap": 34512,
  "sketch": 457755,
  "free_sketch": 2621440,
  "uptime": 428,
  "device_name": "UberSDR",
  "time": "2024-07-03T15:04:05Z"
}
```

Published on connect and every 60 seconds.

---

## Command topics (optional)

Enable with `"commands": true`. The device subscribes to these topics on connect:

| Topic | Payload | Effect |
|-------|---------|--------|
| `<prefix>/set` | `0`…`7` \| `up` \| `down` \| `ground` | Select antenna / step |
| `<prefix>/max/set` | `0`…`7` | Set max antennas |
| `<prefix>/name/set` | `{"position":N,"name":"…"}` | Set/clear a port name |
| `<prefix>/restart` | any | Reboot the device |

### Examples

```bash
BROKER=192.168.1.10
PREFIX=ubersdr/ESP-45CA21

# Select ANT3
mosquitto_pub -h $BROKER -t "$PREFIX/set" -m "3"

# Step up
mosquitto_pub -h $BROKER -t "$PREFIX/set" -m "up"

# Go to GROUND
mosquitto_pub -h $BROKER -t "$PREFIX/set" -m "ground"

# Set max to 5
mosquitto_pub -h $BROKER -t "$PREFIX/max/set" -m "5"

# Name ANT2
mosquitto_pub -h $BROKER -t "$PREFIX/name/set" -m '{"position":2,"name":"40m Dipole"}'

# Reboot
mosquitto_pub -h $BROKER -t "$PREFIX/restart" -m "1"
```

---

## Home Assistant example

```yaml
# configuration.yaml

mqtt:
  sensor:
    - name: "Antenna Switch Position"
      state_topic: "ubersdr/ESP-45CA21/antenna"
      availability_topic: "ubersdr/ESP-45CA21/availability"
      payload_available: "online"
      payload_not_available: "offline"

  select:
    - name: "Antenna Switch"
      state_topic: "ubersdr/ESP-45CA21/antenna"
      command_topic: "ubersdr/ESP-45CA21/set"
      availability_topic: "ubersdr/ESP-45CA21/availability"
      payload_available: "online"
      payload_not_available: "offline"
      options:
        - "0"
        - "1"
        - "2"
        - "3"
        - "4"
        - "5"
```

> **Note:** Enable `commands: true` in the MQTT config for the `command_topic` to work.

---

## Reconnect behaviour

The client uses exponential backoff on failed connect attempts:

| Attempt | Wait before retry |
|---------|-------------------|
| 1st failure | 5 s |
| 2nd failure | 10 s |
| 3rd failure | 20 s |
| 4th failure | 40 s |
| 5th+ failure | 60 s (cap) |

On a successful reconnect the backoff resets to 5 s. The client does not attempt to
connect while WiFi is down (it waits for `WL_CONNECTED` first).

---

## REST API for MQTT config

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/mqtt` | Read current config + connection status |
| `POST` | `/api/mqtt` | Update config (partial updates OK; takes effect immediately) |

The `GET` response includes a `"connected"` field showing live broker connection status.
The password field is write-only: `GET` returns `"***"` if a password is set, `""` if not.

See [`rest-api.md`](rest-api.md) for the full REST API reference.
