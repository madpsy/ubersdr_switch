# WiFi Setup, Access Point & Config Portal

WiFi provisioning is handled by the **tzapu WiFiManager** library
(`https://github.com/tzapu/WiFiManager`), confirmed by the embedded portal HTML and
log strings.

## Boot / connection flow

1. On power-up the device reads its saved WiFi credentials from the **SDK parameter
   area** at the end of flash and attempts to connect as a **station (STA)**.
   - Serial: `Connecting as wifi client...`, `Connect Wifi, ATTEMPT #`
   - `esp_wifi_set_country` is applied (`[OK] esp_wifi_set_country:`).
2. If it connects → `AutoConnect: SUCCESS`, then it sets the hostname and starts the
   application web server (`HTTP server started`).
3. If there are no saved credentials or the connection fails → `AutoConnect: FAILED`
   and it launches the **SoftAP + captive portal**.

## Access Point (configuration mode)

| Property | Value |
|----------|-------|
| **AP SSID** | `AutoConnectAP` (WiFiManager default) |
| **AP password** | none / open (no valid password set — log: `AccessPoint set password is INVALID or <8 chars`) |
| **Portal URL / IP** | `http://192.168.4.1` (WiFiManager default SoftAP gateway) |
| **Mode** | Captive portal — all requests are redirected (`<- Request redirected to captive portal`) |

Log lines during this phase:

```
Starting Config Portal
setupConfigPortal
Config Portal Running, blocking, waiting for clients...
ssid: AutoConnectAP
http://192.168.4.1
```

### To configure WiFi

1. Join the WiFi network **`AutoConnectAP`** from a phone/laptop.
2. A captive-portal page opens (or browse to `http://192.168.4.1`).
3. Open **Configure WiFi**, select your SSID, enter the password, **Save**.
4. The device reboots and connects as a station.

## Station mode / mDNS

After connecting, the firmware sets a hostname and advertises mDNS:

- Log: `setupHostname:`, `Setting WiFi hostname`, `STA: …`
- **Hostname / mDNS name:** `ESP-45CA21` (derived from MAC `cc:50:e3:45:ca:21`,
  stored in the SDK config area at flash `0x3fd0b4`)
- Reachable at **`http://ESP-45CA21.local`** (plus its DHCP IP)
- Log: `Connected to: <SSID>`; the SSID/IP also appears on the OLED.

> **Replica connection resilience:** the replica firmware does **not** drop into AP mode
> on the first failed connect. If saved credentials exist it retries them for a bounded
> total budget (~45 s, several ~15 s attempts) before opening the captive portal, so a
> router that is briefly slow/unavailable at boot (e.g. after a power cut) is tolerated.
> The OLED animates during the wait and holding **UP** still forces the portal. Tunable
> via `WIFI_CONNECT_BUDGET_MS` / `WIFI_CONNECT_ATTEMPT_S` / `WIFI_CONNECT_RETRIES` in
> [`../firmware/src/config.h`](../firmware/src/config.h).

## Captive-portal / config pages

| Path | Method | Function |
|------|--------|----------|
| `/` | GET | Menu page |
| `/wifi` | GET | WiFi scan + configuration form |
| `/0wifi` | GET | WiFi configuration form, no scan |
| `/wifisave` | GET/POST | Save WiFi credentials (fields `s`=SSID, `p`=password) |
| `/param` | GET | Parameter page |
| `/paramsave` | POST | Save parameters |
| `/info` | GET | Info page (SDK/core/boot versions, MAC, hostname, autoconnect, uptime, free heap, sketch size) |
| `/close` | GET | Close captive-portal popup (portal stays running) |
| `/exit` | GET | Exit config portal (portal closes) |
| `/erase` | GET | Erase WiFi config and reboot |
| `/restart` | GET | Reboot device |
| `/u`, `/update` | GET/POST | OTA firmware upload (`multipart/form-data`, field `update`) |

WiFi form fields:

- `s` — SSID, `maxlength=32`
- `p` — password, `maxlength=64`, `type=password`

## Erasing the WiFi configuration

Two equivalent methods:

- **Web:** visit `/erase` (Erase WiFi Config → reboots, re-enters AP mode).
- **Hardware:** hold the **ERASE** button (GPIO2) — the OLED shows
  `pres ERASE for erase stored information` / `hold the button`, then it erases and
  reboots. See [`buttons.md`](buttons.md).

## OTA firmware update

The portal includes an OTA page:

```
/update  (GET)  -> upload form
/u       (POST, multipart/form-data, field "update") -> receives .bin
```

This means replacement firmware can be flashed **over WiFi** without opening the
enclosure. On success: `[OTA] update ok` → device reboots. On failure:
`[OTA] update failed` / `OTA Error:`.
