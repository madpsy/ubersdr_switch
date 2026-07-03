# Existing Web / HTTP Interface (Full Reference)

Everything currently known about the stock "ANTENNA SELECTOR V2" web interface,
extracted from the flash image. The firmware exposes **two distinct HTTP surfaces**:

1. **Application server** (station mode) — a hand-written raw
   `WiFiServer`/`WiFiClient` server that renders the antenna Up/Dn page.
2. **WiFiManager captive portal / web portal** — the tzapu library's server used for
   setup, info, OTA and configuration (also see [`wifi-ap-config.md`](wifi-ap-config.md)).

Both can be active depending on connection state. The application page is what a user
normally interacts with once the device is on their WiFi.

> **Replica note:** this section documents the *stock* firmware. In the replica
> firmware the **default page at `/` is the modern UI** ([`../firmware/data/app.html`](../firmware/data/app.html));
> the stock page described below is preserved at **`/old`**, and the legacy `/5/on`,
> `/4/on` routes are unchanged. The replica also adds a JSON REST API
> ([`rest-api.md`](rest-api.md)) with a **direct antenna-select** endpoint.

---

## 1. Application server (antenna control)

### 1.1 Request parsing

The server reads the raw HTTP request and matches **literal substrings** of the
request line (this is why only these exact paths work — there is no real router or
query-string parsing). The only recognised tokens in the entire image are:

| Request line | Action |
|--------------|--------|
| `GET /5/on`  | **Up** — antenna index **+1** (wraps at the configured max) |
| `GET /5/off` | Up button "released" state |
| `GET /4/on`  | **Dn** — antenna index **−1** (wraps to max below GROUND) |
| `GET /4/off` | Dn button "released" state |

Confirmed exhaustively: a scan of the whole 4 MB image finds **no other** `/N/on`,
`/N/off`, `/select`, `/set`, `/api`, or query-string routes in the application code.
The `/5/` and `/4/` naming mirrors the internal Up/Dn button model (the same code
paths as physical GPIO3/GPIO1).

> **Key limitation:** there is **no direct antenna-select endpoint**. Control is
> relative Up/Dn stepping only; a client wanting antenna N must step there one
> position at a time and read back the state.

### 1.2 HTTP response

The application server emits a fixed response (strings at file `0x58701`+):

```
HTTP/1.1 200 OK
Content-type:text/html
Connection: close
<CRLF>
<HTML body>
```

- No `Content-Length` (relies on `Connection: close`).
- `Content-type:text/html` (note: no space after the colon — verbatim from firmware).
- Every request returns the same page reflecting the (possibly just-changed) state;
  clicking Up/Dn navigates to `/5/on` or `/4/on` and the page re-renders.

### 1.3 Served HTML (exact)

Captured verbatim from a running device (`curl http://<ip>/`). The stock page is
**not** well-formed HTML: after `<p>ANTENNA:</p>` it re-opens `<body>`/`<h1>`, then
emits the selection, then the Dn button and a stray `<h2>` before the footer.

Crucially the **two states are formatted differently**:

- **ANTn** (position 1…7): a **bare digit** with no `<p>` wrapper and no trailing
  newline, so the Dn `<p>` follows on the same line — e.g. for ANT4:

  ```html
  ...
  <p>ANTENNA:</p>
  <body><h1>
  4<p><a href="/4/on"><button class="button">Dn</button></a></p>
  <h2>
  <p>Anteni.net Ltd.</p>
  </body></html>
  ```

- **GROUND** (position 0): the word `GROUND` **wrapped in `<p>…</p>`** on its own line:

  ```html
  ...
  <p>ANTENNA:</p>
  <body><h1>
  <p>GROUND</p>
  <p><a href="/4/on"><button class="button">Dn</button></a></p>
  <h2>
  <p>Anteni.net Ltd.</p>
  </body></html>
  ```

Full page (GROUND state) for reference:

```html
<!DOCTYPE html><html>
<head><meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}
.button { background-color: #195B6A; border: none; color: yellow; padding: 16px 40px;
text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}
.button2 {background-color: #77878A;}</style></head>
<body><h1>Web ANTENNAS switch</h1>
<p><a href="/5/on"><button class="button">Up</button></a></p>
<p>ANTENNA:</p>
<body><h1>
<p>GROUND</p>
<p><a href="/4/on"><button class="button">Dn</button></a></p>
<h2>
<p>Anteni.net Ltd.</p>
</body></html>
```

> The **web page** shows the word `GROUND` (in `<p>`) or the **bare antenna number**
> (no `<p>`) — which differs from the OLED, which shows `GROUND` / `ANT: n`. The replica
> reproduces both web layouts exactly via `antenna.webLabel()` (substituted for
> `%LABEL%` in `data/index.html`), while the OLED keeps the `ANT: n` form
> (`antenna.label()`).

### 1.4 UI details

- **Title (`<h1>`):** `Web ANTENNAS switch`
- **Footer:** `Anteni.net Ltd.`
- **Buttons:** `Up` (links to `/5/on`) and `Dn` (links to `/4/on`), styled by
  `.button` — background `#195B6A`, **yellow** text, 30 px font, 16×40 px padding.
- **Unused CSS:** a `.button2` class (`#77878A`) is defined but never used in the
  markup (leftover from the ESP8266 WebServer example this was derived from).
- **Selection value:** on the **web page** it is the bare position number `1`…`7`
  (or `GROUND` for position 0) — NOT the `ANT: n` form. The `ANT: 1`…`ANT: 7` / `GROUND`
  strings are what the **OLED** shows.
- Mobile-friendly `viewport` meta; `<link rel="icon" href="data:,">` suppresses the
  favicon request.
- The page auto-reflects state but does **not** auto-refresh (no meta-refresh / JS).

> **Prefer the JSON REST API for automation.** The replica firmware also exposes a
> clean `/api/*` JSON interface (direct position select, status, max control) that is
> far easier to script than the stock Up/Dn routes. See [`rest-api.md`](rest-api.md).

### 1.5 Driving it from a script

```bash
HOST=ESP-45CA21.local      # or the device IP, or 192.168.4.1 in AP mode

# Step up / down one antenna
curl "http://$HOST/5/on"     # Up
curl "http://$HOST/4/on"     # Dn

# Read the current selection (the <p>…</p> right after "ANTENNA:")
curl -s "http://$HOST/" | grep -A1 'ANTENNA:' | tail -1

# Move to a known position: step down to GROUND, then up N times
for i in $(seq 1 8); do curl -s "http://$HOST/4/on" >/dev/null; done   # force GROUND
for i in $(seq 1 3); do curl -s "http://$HOST/5/on" >/dev/null; done   # -> ANT 3
```

---

## 2. WiFiManager portal (setup / info / OTA)

This is the tzapu **WiFiManager** library server. It runs the captive portal in AP
mode and a web portal in station mode. Full setup flow is in
[`wifi-ap-config.md`](wifi-ap-config.md); the complete route and template reference
follows here.

### 2.1 Endpoints

| Path | Method | Purpose |
|------|--------|---------|
| `/` | GET | Menu page (root) |
| `/wifi` | GET | WiFi scan + configuration form |
| `/0wifi` | GET | WiFi configuration form, **no** scan |
| `/wifisave` | GET/POST | Save WiFi credentials (`s`=SSID, `p`=password) |
| `/param` | GET | Parameter page |
| `/paramsave` | POST | Save parameters |
| `/info` | GET | Device information page (fields listed below) |
| `/status` | GET | Status endpoint (used by the portal UI) |
| `/close` | GET | Close captive-portal popup (portal keeps running) |
| `/exit` | GET | Exit config portal (portal closes) |
| `/erase` | GET | Erase WiFi config and reboot |
| `/restart` | GET | Reboot device |
| `/update` | GET | OTA upload form |
| `/u` | POST | OTA firmware upload receiver (`multipart/form-data`) |

### 2.2 HTTP headers used by the portal

Unlike the application server, WiFiManager sets full headers (strings present in the
image):

- `Content-Type` (proper MIME per resource), `Content-Length`
- `Cache-Control`
- `Access-Control-Allow-Origin` (CORS), `Authorization` (auth support)
- MIME table includes: `text/html`, `text/plain`, `application/javascript`,
  `application/json`, `.png` → `image/png`, `.gif`, `.json`, `.txt`
- Redirects (`302`) are used for the captive-portal flow.

### 2.3 `/info` page fields

The information page renders these labelled rows (`<dt>label</dt><dd>{1}</dd>`):

- Autoconnect
- Connected
- Station MAC
- Hostname
- DNS Server
- Station Subnet
- Station Gateway
- Station IP
- Station SSID
- BSSID
- Access Point SSID
- Access Point MAC
- Access Point IP
- SDK Version
- Chip ID
- Last reset reason
- Boot Version
- Core Version
- Flash Chip ID

(Plus, from other strings: Uptime `{1} Mins {2} Secs`, Memory – Sketch Size
`{1} / {2}`, free heap.)

### 2.4 WiFi configuration form

```html
<label for='s'>SSID</label>
<input id='s' name='s' maxlength='32' autocorrect='off' autocapitalize='none' placeholder='{v}'>
<label for='p'>Password</label>
<input id='p' name='p' maxlength='64' type='password' placeholder='{p}'>
```

- Field `s` — SSID, max 32 chars
- Field `p` — password, max 64 chars, `type=password`
- Submitted to `/wifisave`.

### 2.5 Menu page buttons

The root menu offers (each a small `<form action='…'>` with a button):

`Configure WiFi` (`/wifi`), `Configure WiFi (No Scan)` (`/0wifi`), `Info` (`/info`),
`Setup` (`/param`), `Update` (`/update`), `Close` (`/close`), `Restart` (`/restart`),
`Exit` (`/exit`), `Erase` (`/erase`).

### 2.6 OTA update

```html
<form method='POST' action='u' enctype='multipart/form-data'>
  <input type='file' name='update' accept='.bin,application/octet-stream'>
  <button type='submit'>Update</button>
</form>
```

- Upload a `.bin` to **`/u`** (field name `update`, `multipart/form-data`, with a
  `filename` part and `Content-Length`).
- Result strings: `[OTA] update ok` → device reboots; `[OTA] update failed`,
  `OTA Error:`, MD5 check (`MD5 Failed: expected:%s, calculated:%s`).
- The upload page warns it "May not function inside captive portal — open in browser
  `http://192.168.4.1/update`".

This means replacement firmware can be flashed **over WiFi** without a serial cable.

### 2.7 Template placeholders

WiFiManager substitutes these tokens in its HTML templates:
`{1} {2}` (values), `{v}` (SSID), `{p}` (password placeholder), `{c}` (body class /
theme), `{i}` `{I}` (icons), `{h}` (custom head), `{n}` (name), `{r}` `{R}` (result),
`{t}` (title), `{q} {qi} {qp}` (signal-quality markers), `{l}` `{e}`.

---

## 3. Summary — what the current interface can and cannot do

| Capability | Stock firmware |
|------------|----------------|
| Step antenna Up / Dn | ✅ `GET /5/on` / `GET /4/on` |
| Read current antenna | ✅ (scrape the HTML page) |
| **Direct-select antenna N** | ❌ not supported |
| JSON/REST API for control | ❌ (JSON MIME exists only inside WiFiManager) |
| Auto-refresh / live state push | ❌ |
| WiFi setup (captive portal) | ✅ AP `AutoConnectAP` @ `192.168.4.1` |
| Device info page | ✅ `/info` |
| OTA firmware update | ✅ `/update` → POST `/u` |
| Erase WiFi config | ✅ `/erase` (or hold the ERASE button) |
| Reboot | ✅ `/restart` |
| Auth / CORS | Partially (WiFiManager headers present) |

A replacement firmware could keep these routes for compatibility while adding a
proper `GET /select?ant=N` (or `/ant/N`) endpoint and a JSON status API.
