// main.cpp — ANTENNA SELECTOR V2 replica firmware (ESP8266).
//
// A pin- and interface-compatible re-implementation of the ANTENI.NET
// "ANTENNAS webSWITCH control 1 to 5" stock firmware, reconstructed from the
// reverse-engineering documentation in ../../docs/.
//
// It mimics the stock device exactly:
//
//   * Antenna selection via the 74HCT138 on GPIO12/13/14 (index 0=GROUND, 1..7=ANTn).
//   * Four active-low buttons UP/DOWN/SET/ERASE on GPIO3/1/0/2.
//       - UP / DOWN : step antenna (wrap between GROUND and stored max).
//       - SET       : bump the "max antennas" value, shows "MAX n" (wraps at 8).
//       - ERASE     : hold to wipe WiFi config and reboot.
//   * SSD1306 OLED on GPIO4/5 showing "GROUND"/"ANT: n" + the web URL.
//   * WiFiManager captive portal: SoftAP "AutoConnectAP" @ http://192.168.4.1.
//   * Station-mode mDNS hostname "ESP-XXXXXX" (from the MAC), reachable at
//     http://ESP-XXXXXX.local.
//   * Application web server: GET /5/on (Up), GET /4/on (Dn) with the exact stock HTML.
//   * OTA firmware update at /update -> POST /u.
//   * Boot-time gestures: hold ERASE to wipe WiFi; hold UP to force the config portal.
//
// See ../../docs/web-interface.md, wifi-ap-config.md, buttons.md, manual-setup.md.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <LittleFS.h>

#include "config.h"
#include "debuglog.h"
#include "antenna.h"
#include "buttons.h"
#include "display.h"
#include "settings.h"
#include "ntp.h"
#include "eventlog.h"
#include "mqtt.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

Antenna       g_ant;
Buttons       g_btn;
Display       g_oled;
Settings      g_settings;
Ntp           g_ntp;
EventLog      g_events;
Mqtt          g_mqtt;

// A single ESP8266WebServer on port 80 serves EVERYTHING: the antenna control page
// (/, /5/on, /4/on — the stock routes), the WiFiManager-style portal pages (/info,
// /erase, /restart), OTA (/update, /u), and /debug. Using the core web server (rather
// than a hand-rolled raw WiFiClient) gives correct Content-Length + connection
// handling, so browsers render the page instead of showing an empty response.
WiFiManager             g_wm;
ESP8266WebServer        g_portal(80);
ESP8266HTTPUpdateServer g_ota;

String g_hostname;    // "ESP-XXXXXX"
bool   g_apMode = false;

// UI transient-screen management: after showing MAX/erase screens, revert to status.
uint32_t g_revertStatusAt = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void setupStationPortal();
static void startStationServices();
static bool tryProvisionedWiFi();
static bool retrySavedWiFi();
static void serveAntennaPage();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static String deviceHostname() {
    // Match the stock naming scheme: "ESP-" + last 3 bytes of the STA MAC, upper hex.
    // (Original unit: MAC cc:50:e3:45:ca:21 -> "ESP-45CA21".)
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%02X%02X%02X",
             HOSTNAME_PREFIX, mac[3], mac[4], mac[5]);
    return String(buf);
}

static String webURL() {
    if (g_apMode) return String("http://192.168.4.1");
    if (WiFi.status() == WL_CONNECTED) {
        return String("http://") + WiFi.localIP().toString();
    }
    return String();  // -> displayed as STR_IP_UNSET
}

static void refreshStatus() {
    // In AP/config-portal mode the stock unit keeps showing the SSID + portal URL
    // ("ssid: AutoConnectAP" / "http://192.168.4.1"). Otherwise show the antenna
    // status screen ("GROUND"/"ANT: n" + web URL or "http://(IP unset)").
    if (g_apMode) {
        g_oled.showAP(String(AP_SSID), String("http://192.168.4.1"));
    } else {
        g_oled.showStatus(g_ant.label(), webURL());
    }
    // NOTE: MQTT publishState() is NOT called here — refreshStatus() is also used for
    // periodic OLED redraws (every 2s) and should not spam the broker. Call
    // g_mqtt.publishState() explicitly only at sites where state actually changes.
}

// Push an event to the in-RAM event log AND publish it over MQTT.
// Call this at every antenna-change site instead of calling publishEvent() directly.
static void logEvent(const char *source) {
    g_events.push(g_ntp.isoNow(), source, g_ant.position(), g_ant.label());
    g_mqtt.publishState();
    g_mqtt.publishEvent(source);
}

// Read a web asset from LittleFS (returns "" if missing / FS not flashed).
static String readAsset(const char *path) {
    File f = LittleFS.open(path, "r");
    if (!f) return String();
    String s = f.readString();
    f.close();
    return s;
}

// Serve a LittleFS file by streaming it directly to the client — no heap copy.
// This is essential for large files like app.html (36 KB) on a device with 80 KB RAM.
static void servePortalAsset(const char *path, const char *mime = "text/html") {
    File f = LittleFS.open(path, "r");
    if (!f) {
        g_portal.send(200, "text/html",
            F("<html><body><h1>ESP</h1><p>Filesystem image not uploaded.<br>"
              "Run: <code>pio run -t uploadfs</code></p></body></html>"));
        return;
    }
    g_portal.streamFile(f, mime);
    f.close();
}

// Serve the antenna control page: data/index.html with %LABEL% replaced by the
// current selection. The stock app page emits ONLY the bare position number (e.g.
// "4"), matching a real device — NOT the "ANT: n" OLED form (see antenna.webLabel()).
static void serveAntennaPage() {
    String html = readAsset("/old.html");
    if (html.length() == 0) {
        html = F("<!DOCTYPE html><html><body><h1>Web ANTENNAS switch</h1>"
                 "<p>ANTENNA:</p>%LABEL%"
                 "<p>Filesystem image not uploaded (run: pio run -t uploadfs)</p>"
                 "</body></html>");
    }
    html.replace("%LABEL%", g_ant.webLabel());
    g_portal.sendHeader("Content-Type", "text/html");
    g_portal.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// REST API  (docs/rest-api.md)
//
// A clean JSON API layered on top of the antenna model, served alongside the stock
// Up/Dn routes. All responses are application/json with permissive CORS so it can be
// driven from a browser, curl, or an SDR automation script.
//
//   GET  /api/status                       -> current state
//   GET  /api/antenna                       -> current state
//   POST /api/antenna/up                    -> step +1 (wraps)
//   POST /api/antenna/down                  -> step -1 (wraps)
//   POST /api/antenna/ground                -> select GROUND (position 0)
//   POST /api/antenna?position=N            -> select position N (0..max)
//   POST /api/antenna { "position": N }     -> select position N (JSON body)
//   GET  /api/max                           -> current max-antennas value
//   POST /api/max?value=N                   -> set max antennas (0..7)
//   POST /api/max { "value": N }            -> set max antennas (JSON body)
//   POST /api/max/bump                      -> bump max (wraps 0..7), like SET button
//
// GET requests are also accepted for the mutating routes (convenience for quick
// browser/curl testing); the canonical method is POST.
// ---------------------------------------------------------------------------

// Build the JSON status object shared by every API response.
static String apiStatusJson() {
    String j;
    j.reserve(360);
    j += F("{\"position\":");
    j += String(g_ant.position());
    j += F(",\"label\":\"");
    j += g_ant.label();
    j += F("\",\"ground\":");
    j += (g_ant.position() == POS_GROUND ? F("true") : F("false"));
    j += F(",\"max\":");
    j += String(g_ant.maxAntennas());
    j += F(",\"count\":");
    j += String(g_ant.maxAntennas());   // selectable antennas (GROUND + 1..max)
    j += F(",\"name\":\"");
    j += g_ant.name(g_ant.position());  // custom name of current antenna ("" if none)
    j += F("\",\"names\":");
    j += g_ant.namesJson();             // {"1":"...","3":"..."}
    j += F(",\"url\":\"");
    j += webURL();
    j += F("\",\"hostname\":\"");
    j += g_hostname;
    j += F("\",\"device_name\":\"");
    j += g_settings.deviceName();
    j += F("\",\"mqtt_enabled\":");
    j += (g_mqtt.enabled() ? F("true") : F("false"));
    j += F(",\"mqtt_connected\":");
    j += (g_mqtt.connected() ? F("true") : F("false"));
    j += F(",\"time\":\"");
    j += g_ntp.isoNow();   // "" if NTP not yet synced
    j += F("\"}");
    return j;
}

// Attach permissive CORS headers so browser fetch()/XHR from any origin works.
static void apiCors() {
    g_portal.sendHeader("Access-Control-Allow-Origin", "*");
    g_portal.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    g_portal.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void apiSendStatus(int code = 200) {
    apiCors();
    g_portal.send(code, "application/json", apiStatusJson());
}

static void apiSendError(int code, const char *msg) {
    apiCors();
    String j = String(F("{\"error\":\"")) + msg + F("\"}");
    g_portal.send(code, "application/json", j);
}

// Read an integer parameter from either the query string (?key=N) or a JSON body
// ({"key":N}). Returns true and sets `out` if found & parsed.
static bool apiIntParam(const char *key, long &out) {
    if (g_portal.hasArg(key)) {
        out = g_portal.arg(key).toInt();
        return true;
    }
    // Fall back to a minimal scan of a JSON body ({"key": N}).
    if (g_portal.hasArg("plain")) {
        String body = g_portal.arg("plain");
        String needle = String("\"") + key + "\"";
        int k = body.indexOf(needle);
        if (k >= 0) {
            int colon = body.indexOf(':', k + needle.length());
            if (colon >= 0) {
                out = body.substring(colon + 1).toInt();
                return true;
            }
        }
    }
    return false;
}

// Read a string parameter from the query string (?key=...) or a JSON body
// ({"key":"..."}). Returns true and sets `out` (may be empty to clear a name).
static bool apiStrParam(const char *key, String &out) {
    if (g_portal.hasArg(key)) {
        out = g_portal.arg(key);
        return true;
    }
    if (g_portal.hasArg("plain")) {
        String body = g_portal.arg("plain");
        String needle = String("\"") + key + "\"";
        int k = body.indexOf(needle);
        if (k >= 0) {
            int colon = body.indexOf(':', k + needle.length());
            if (colon >= 0) {
                int q1 = body.indexOf('"', colon + 1);
                if (q1 >= 0) {
                    int q2 = body.indexOf('"', q1 + 1);
                    if (q2 >= 0) { out = body.substring(q1 + 1, q2); return true; }
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// WiFiManager callbacks
// ---------------------------------------------------------------------------

static void onEnterConfigPortal(WiFiManager *wm) {
    g_apMode = true;
    LOG("AP portal started: AutoConnectAP @ 192.168.4.1");
    IPAddress apip = WiFi.softAPIP();
    LOGF("softAP IP=%s", apip.toString().c_str());
    g_oled.showAP(String(AP_SSID), String("http://") + apip.toString());
}

// Try credentials pre-provisioned in data/wifi.json (written by ./flash.sh). Returns
// true if we connect. The file is a tiny JSON object: {"ssid":"...","pass":"..."}.
// Parsed with a minimal scanner to avoid pulling in a JSON library.
static bool jsonField(const String &json, const char *key, String &out) {
    String needle = String("\"") + key + "\"";
    int k = json.indexOf(needle);
    if (k < 0) return false;
    int colon = json.indexOf(':', k + needle.length());
    if (colon < 0) return false;
    int q1 = json.indexOf('"', colon + 1);
    if (q1 < 0) return false;
    int q2 = json.indexOf('"', q1 + 1);
    if (q2 < 0) return false;
    out = json.substring(q1 + 1, q2);
    return true;
}

static bool tryProvisionedWiFi() {
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) { LOG("wifi.json: none"); return false; }
    String json = f.readString();
    f.close();

    String ssid, pass;
    if (!jsonField(json, "ssid", ssid) || ssid.length() == 0) {
        LOG("wifi.json: no ssid, skipping");
        return false;
    }
    jsonField(json, "pass", pass);   // password may be empty (open network)
    LOGF("wifi.json: connecting to SSID '%s'", ssid.c_str());
    g_oled.showMessage(F("Connecting"), ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
        delay(200);
        // keep the OLED alive with a simple animated wait
        static uint8_t dots = 0;
        dots = (dots + 1) % 4;
        g_oled.showMessage(String("Connecting") + String("...").substring(0, dots), ssid);
    }
    if (WiFi.status() == WL_CONNECTED) {
        LOGF("wifi.json: connected, IP=%s", WiFi.localIP().toString().c_str());
        return true;
    }
    LOG("wifi.json: connect FAILED (falling back to portal)");
    return false;
}

// Retry the credentials already saved in flash (from a previous WiFiManager setup)
// for a bounded total time before we hand over to the config portal. This makes the
// unit resilient to a router that is briefly unavailable/slow at boot (e.g. after a
// shared power cut), instead of dropping straight into AP mode. The OLED animates and
// the physical buttons keep working throughout; the loop is capped so it never hangs.
static bool retrySavedWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin();                          // use stored SSID/pass from flash

    uint32_t start = millis();
    uint8_t  dots  = 0;
    while ((millis() - start) < WIFI_CONNECT_BUDGET_MS) {
        if (WiFi.status() == WL_CONNECTED) {
            LOGF("wifi: reconnected to '%s', IP=%s",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            return true;
        }
        // Keep the UI alive and let the user bail into the portal by holding UP.
        g_btn.update();
        dots = (dots + 1) % 4;
        uint32_t left = (WIFI_CONNECT_BUDGET_MS - (millis() - start)) / 1000;
        g_oled.showMessage(String("WiFi") + String("...").substring(0, dots),
                           String(WiFi.SSID()) + " " + String(left) + "s");
        delay(250);
    }
    LOG("wifi: saved-credential retry exhausted, opening portal");
    return false;
}

// ---------------------------------------------------------------------------
// Portal / OTA (station-mode web portal, mirrors the stock WiFiManager routes)
// ---------------------------------------------------------------------------

static void setupStationPortal() {
    // OTA: /update (GET form) + /u (POST receiver) — the stock OTA endpoints.
    g_ota.setup(&g_portal, "/update");

    // /debug — live in-memory log + runtime state. Conflict-free debugging (viewing
    // this in a browser does not disturb the UP/DOWN buttons on GPIO1/GPIO3).
    g_portal.on("/debug", []() {
        String p;
        p.reserve(2048);
        p += F("=== ANTENNA SELECTOR V2 (replica) — debug ===\n");
        p += "hostname : " + g_hostname + "\n";
        p += "wifi     : " + String(WiFi.status() == WL_CONNECTED ? "connected" : "not connected") + "\n";
        p += "SSID     : " + WiFi.SSID() + "\n";
        p += "IP       : " + WiFi.localIP().toString() + "\n";
        p += "AP mode  : " + String(g_apMode ? "yes" : "no") + "\n";
        p += "antenna  : " + g_ant.label() + " (pos " + String(g_ant.position())
             + ", max " + String(g_ant.maxAntennas()) + ")\n";
        p += "heap     : " + String(ESP.getFreeHeap()) + "\n";
        p += "uptime   : " + String(millis() / 1000) + " s\n";
        p += "reset    : " + ESP.getResetReason() + "\n";
        p += F("\n--- log (oldest first) ---\n");
        p += DebugLog::instance().text();
        g_portal.send(200, "text/plain", p);
    });

    g_portal.on("/erase", []() {
        g_portal.send(200, "text/html",
                      "<html><body>Erasing WiFi config, rebooting...</body></html>");
        delay(200);
        WiFi.disconnect(true);   // clear stored credentials
        g_wm.resetSettings();
        delay(300);
        ESP.restart();
    });

    g_portal.on("/restart", []() {
        g_portal.send(200, "text/html",
                      "<html><body>Restarting...</body></html>");
        delay(200);
        ESP.restart();
    });

    g_portal.on("/info", []() {
        // Load data/info.html and fill in the placeholders. Mirrors the stock /info
        // field set (docs/web-interface.md §2.3).
        String h = readAsset("/info.html");
        if (h.length() == 0) { servePortalAsset("/info.html"); return; }
        long up = millis() / 1000;
        h.replace("%HOSTNAME%", g_hostname);
        h.replace("%STA_MAC%",  WiFi.macAddress());
        h.replace("%STA_IP%",   WiFi.localIP().toString());
        h.replace("%STA_GW%",   WiFi.gatewayIP().toString());
        h.replace("%STA_MASK%", WiFi.subnetMask().toString());
        h.replace("%DNS%",      WiFi.dnsIP().toString());
        h.replace("%SSID%",     WiFi.SSID());
        h.replace("%BSSID%",    WiFi.BSSIDstr());
        h.replace("%AP_MAC%",   WiFi.softAPmacAddress());
        h.replace("%SDK%",      String(ESP.getSdkVersion()));
        h.replace("%CORE%",     ESP.getCoreVersion());
        h.replace("%CHIP_ID%",  String(ESP.getChipId(), HEX));
        h.replace("%FLASH_ID%", String(ESP.getFlashChipId(), HEX));
        h.replace("%BOOT%",     String(ESP.getBootVersion()));
        h.replace("%RESET%",    ESP.getResetReason());
        h.replace("%HEAP%",     String(ESP.getFreeHeap()));
        h.replace("%SKETCH%",   String(ESP.getSketchSize()) + " / "
                                + String(ESP.getFreeSketchSpace()));
        h.replace("%UPTIME%",   String(up / 60) + " Mins " + String(up % 60) + " Secs");
        g_portal.send(200, "text/html", h);
    });

    // Shared stylesheet (data/style.css) — used by the app page and portal pages.
    g_portal.on("/style.css", []() { servePortalAsset("/style.css", "text/css"); });

    // --- Antenna control application (the stock routes) ---
    // Serve data/index.html with %LABEL% filled in. "/" is the control page (like
    // the stock firmware). /5/on = Up, /4/on = Dn; /5/off, /4/off = "released"
    // (re-render only). These use the core web server, so Content-Length / close are
    // handled correctly and browsers render the page.
    // "/" now serves the MODERN frontend (data/app.html), which drives the REST API.
    // The original stock page is preserved at /old (data/old.html), and the legacy
    // Up/Dn routes (/5/on, /4/on, ...) still return that stock page for compatibility.
    g_portal.on("/",       []() { servePortalAsset("/app.html"); });
    g_portal.on("/app",    []() { servePortalAsset("/app.html"); });   // explicit alias
    g_portal.on("/old",    []() { serveAntennaPage(); });              // stock page
    g_portal.on("/5/on",   []() { g_ant.up();   LOGF("web UP -> %s", g_ant.label().c_str());
                                  refreshStatus(); logEvent("web");
                                  serveAntennaPage(); });
    g_portal.on("/4/on",   []() { g_ant.down(); LOGF("web DOWN -> %s", g_ant.label().c_str());
                                  refreshStatus(); logEvent("web");
                                  serveAntennaPage(); });
    g_portal.on("/5/off",  []() { serveAntennaPage(); });
    g_portal.on("/4/off",  []() { serveAntennaPage(); });

    // WiFiManager-style menu moves to /menu (data/portal.html).
    g_portal.on("/menu",   []() { servePortalAsset("/portal.html"); });

    // --- REST API (docs/rest-api.md) ---
    // Status (read-only).
    g_portal.on("/api/status",  HTTP_ANY, []() { apiSendStatus(); });

    // Device info as JSON (same field set as the HTML /info page).
    g_portal.on("/api/info", HTTP_ANY, []() {
        long up = millis() / 1000;
        String j; j.reserve(768);
        j += F("{");
        j += F("\"hostname\":\"");   j += g_hostname;                         j += F("\",");
        j += F("\"sta_mac\":\"");    j += WiFi.macAddress();                  j += F("\",");
        j += F("\"sta_ip\":\"");     j += WiFi.localIP().toString();          j += F("\",");
        j += F("\"gateway\":\"");    j += WiFi.gatewayIP().toString();        j += F("\",");
        j += F("\"netmask\":\"");    j += WiFi.subnetMask().toString();       j += F("\",");
        j += F("\"dns\":\"");        j += WiFi.dnsIP().toString();            j += F("\",");
        j += F("\"ssid\":\"");       j += WiFi.SSID();                        j += F("\",");
        j += F("\"bssid\":\"");      j += WiFi.BSSIDstr();                    j += F("\",");
        j += F("\"ap_mac\":\"");     j += WiFi.softAPmacAddress();            j += F("\",");
        j += F("\"rssi\":");         j += String(WiFi.RSSI());                j += F(",");
        j += F("\"ap_mode\":");      j += (g_apMode ? F("true") : F("false")); j += F(",");
        j += F("\"sdk\":\"");        j += String(ESP.getSdkVersion());        j += F("\",");
        j += F("\"core\":\"");       j += ESP.getCoreVersion();               j += F("\",");
        j += F("\"chip_id\":\"");    j += String(ESP.getChipId(), HEX);       j += F("\",");
        j += F("\"flash_id\":\"");   j += String(ESP.getFlashChipId(), HEX);  j += F("\",");
        j += F("\"flash_size\":");   j += String(ESP.getFlashChipRealSize()); j += F(",");
        j += F("\"boot\":");         j += String(ESP.getBootVersion());       j += F(",");
        j += F("\"reset\":\"");      j += ESP.getResetReason();               j += F("\",");
        j += F("\"heap\":");         j += String(ESP.getFreeHeap());          j += F(",");
        j += F("\"sketch\":");       j += String(ESP.getSketchSize());        j += F(",");
        j += F("\"free_sketch\":");  j += String(ESP.getFreeSketchSpace());   j += F(",");
        j += F("\"uptime\":");       j += String(up);                         j += F(",");
        j += F("\"device_name\":\""); j += g_settings.deviceName();           j += F("\",");
        j += F("\"time\":\"");       j += g_ntp.isoNow();                     j += F("\"");
        j += F("}");
        apiCors();
        g_portal.send(200, "application/json", j);
    });

    // Antenna step / select. Accept any method for convenience (canonical: POST).
    g_portal.on("/api/antenna/up",     HTTP_ANY, []() {
        g_ant.up();   LOGF("API up -> %s", g_ant.label().c_str());
        refreshStatus(); logEvent("api"); apiSendStatus();
    });
    g_portal.on("/api/antenna/down",   HTTP_ANY, []() {
        g_ant.down(); LOGF("API down -> %s", g_ant.label().c_str());
        refreshStatus(); logEvent("api"); apiSendStatus();
    });
    g_portal.on("/api/antenna/ground", HTTP_ANY, []() {
        g_ant.setPosition(POS_GROUND);
        LOGF("API ground -> %s", g_ant.label().c_str());
        refreshStatus(); logEvent("api"); apiSendStatus();
    });

    // /api/antenna: plain GET (no params) = read status; otherwise select a position
    // via ?position=N or JSON {"position":N}.
    g_portal.on("/api/antenna", HTTP_ANY, []() {
        long pos;
        if (!apiIntParam("position", pos)) {
            if (g_portal.method() == HTTP_GET) { apiSendStatus(); return; }
            apiSendError(400, "missing 'position'");
            return;
        }
        if (pos < POS_MIN || pos > g_ant.maxAntennas()) {
            apiSendError(422, "position out of range (0..max)");
            return;
        }
        g_ant.setPosition((int8_t)pos);
        LOGF("API set position=%ld -> %s", pos, g_ant.label().c_str());
        refreshStatus(); logEvent("api");
        apiSendStatus();
    });

    // Bump the max-antennas value (like the SET button).
    g_portal.on("/api/max/bump", HTTP_ANY, []() {
        g_ant.bumpMax();
        LOGF("API bump max -> %d", g_ant.maxAntennas());
        refreshStatus(); g_mqtt.publishState();
        apiCors();
        g_portal.send(200, "application/json",
                      String(F("{\"max\":")) + String(g_ant.maxAntennas()) + F("}"));
    });

    // Get or set the max-antennas value: /api/max or /api/max?value=N / {"value":N}.
    g_portal.on("/api/max", HTTP_ANY, []() {
        if (g_portal.method() == HTTP_GET && !g_portal.hasArg("value")) {
            apiCors();
            g_portal.send(200, "application/json",
                          String(F("{\"max\":")) + String(g_ant.maxAntennas()) + F("}"));
            return;
        }
        long v;
        if (!apiIntParam("value", v)) {
            apiSendError(400, "missing 'value'");
            return;
        }
        if (v < 0 || v > POS_MAX_HW) {
            apiSendError(422, "value out of range (0..7)");
            return;
        }
        g_ant.setMaxAntennas((int8_t)v);
        LOGF("API set max=%ld", v);
        refreshStatus(); g_mqtt.publishState();
        apiCors();
        g_portal.send(200, "application/json",
                      String(F("{\"max\":")) + String(g_ant.maxAntennas()) + F("}"));
    });

    // Custom antenna names. GET returns {"1":"...","3":"..."}. POST sets one name via
    // /api/name?position=N&name=... (or JSON {"position":N,"name":"..."}); an empty
    // name clears it. GROUND (position 0) cannot be named. Names persist to
    // /names.json and are capped at 16 chars.
    g_portal.on("/api/names", HTTP_ANY, []() {
        apiCors();
        g_portal.send(200, "application/json", g_ant.namesJson());
    });

    // Restart the device (JSON). Responds first, then reboots shortly after.
    g_portal.on("/api/restart", HTTP_ANY, []() {
        apiCors();
        g_portal.send(200, "application/json", F("{\"restarting\":true}"));
        LOG("API restart requested");
        delay(250);
        ESP.restart();
    });
    g_portal.on("/api/name", HTTP_ANY, []() {
        long pos;
        if (!apiIntParam("position", pos)) {
            if (g_portal.method() == HTTP_GET) {
                apiCors();
                g_portal.send(200, "application/json", g_ant.namesJson());
                return;
            }
            apiSendError(400, "missing 'position'");
            return;
        }
        if (pos < 1 || pos > POS_MAX_HW) {
            apiSendError(422, "position out of range (1..7; GROUND cannot be named)");
            return;
        }
        String nm;
        // DELETE clears the name (nm stays empty). Otherwise 'name' is required.
        if (g_portal.method() != HTTP_DELETE && !apiStrParam("name", nm)) {
            apiSendError(400, "missing 'name'");
            return;
        }
        String stored = g_ant.setName((int8_t)pos, nm);
        LOGF("API name pos=%ld -> '%s'", pos, stored.c_str());
        refreshStatus(); g_mqtt.publishState();  // OLED + MQTT reflect rename immediately
        apiCors();
        String j = String(F("{\"position\":")) + String(pos)
                 + F(",\"name\":\"") + stored + F("\"}");
        g_portal.send(200, "application/json", j);
    });

    // /debug — plain-text runtime state + in-memory log (conflict-free debugging).
    g_portal.on("/debug", []() {
        String p;
        p.reserve(2048);
        p += F("=== ANTENNA SELECTOR V2 (replica) — debug ===\n");
        p += "wifi   : ";
        p += (WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
        p += "\nSSID   : "; p += WiFi.SSID();
        p += "\nIP     : "; p += WiFi.localIP().toString();
        p += "\nantenna: "; p += g_ant.label();
        p += " (pos "; p += String(g_ant.position());
        p += ", max "; p += String(g_ant.maxAntennas()); p += ")";
        p += "\nheap   : "; p += String(ESP.getFreeHeap());
        p += "\nuptime : "; p += String(millis() / 1000); p += " s";
        p += F("\n\n--- log (oldest first) ---\n");
        p += DebugLog::instance().text();
        g_portal.send(200, "text/plain", p);
    });

    // Event log: GET /api/events returns the last 25 events as a JSON array.
    g_portal.on("/api/events", HTTP_ANY, []() {
        apiCors();
        g_portal.send(200, "application/json", g_events.toJson());
    });

    // NTP config: GET returns current config + sync status; POST updates it.
    g_portal.on("/api/ntp", HTTP_ANY, []() {
        if (g_portal.method() == HTTP_GET) {
            const NtpConfig &n = g_settings.ntp();
            String j; j.reserve(128);
            j += F("{\"host\":\"");  j += n.host;
            j += F("\",\"port\":"); j += String(n.port);
            j += F(",\"synced\":"); j += (g_ntp.synced() ? F("true") : F("false"));
            j += F(",\"time\":\""); j += g_ntp.isoNow(); j += F("\"");
            j += F("}");
            apiCors();
            g_portal.send(200, "application/json", j);
            return;
        }
        // POST: update NTP config.
        NtpConfig nc = g_settings.ntp();
        String sv; long iv;
        if (apiStrParam("host", sv)) { sv.toCharArray(nc.host, sizeof(nc.host)); }
        if (apiIntParam("port", iv)) nc.port = (uint16_t)iv;
        g_settings.setNtp(nc);
        g_ntp.begin(g_settings.ntp().host, g_settings.ntp().port);
        LOGF("NTP: reconfigured host=%s port=%u", nc.host, nc.port);
        apiCors();
        g_portal.send(200, "application/json", F("{\"saved\":true}"));
    });

    // MQTT config: GET returns current config (passwords redacted); POST updates it.
    g_portal.on("/api/mqtt", HTTP_ANY, []() {
        if (g_portal.method() == HTTP_GET) {
            const MqttConfig &c = g_settings.mqtt();
            String j; j.reserve(320);
            j += F("{\"enabled\":");    j += (c.enabled  ? F("true") : F("false"));
            j += F(",\"host\":\"");     j += c.host;
            j += F("\",\"port\":");     j += String(c.port);
            j += F(",\"user\":\"");     j += c.user;
            // password is write-only: return a placeholder if set, empty if not
            j += F("\",\"pass\":\"");   j += (strlen(c.pass) ? F("***") : F(""));
            j += F("\",\"prefix\":\""); j += c.prefix;
            j += F("\",\"retain\":");   j += (c.retain   ? F("true") : F("false"));
            j += F(",\"commands\":");   j += (c.commands  ? F("true") : F("false"));
            j += F(",\"connected\":");  j += (g_mqtt.connected() ? F("true") : F("false"));
            j += F(",\"device_name\":\""); j += g_settings.deviceName(); j += F("\"");
            j += F("}");
            apiCors();
            g_portal.send(200, "application/json", j);
            return;
        }
        // POST: update config. Only fields present in the body are changed.
        MqttConfig c = g_settings.mqtt();
        String sv;
        long   iv;
        if (apiStrParam("host",   sv)) { sv.toCharArray(c.host,   sizeof(c.host));   }
        if (apiStrParam("user",   sv)) { sv.toCharArray(c.user,   sizeof(c.user));   }
        if (apiStrParam("prefix", sv)) { sv.toCharArray(c.prefix, sizeof(c.prefix)); }
        // Only update password if the client sends a real value (not the "***" placeholder).
        if (apiStrParam("pass", sv) && sv != "***") {
            sv.toCharArray(c.pass, sizeof(c.pass));
        }
        if (apiIntParam("port",     iv)) c.port     = (uint16_t)iv;
        if (apiIntParam("enabled",  iv)) c.enabled  = (iv != 0);
        if (apiIntParam("retain",   iv)) c.retain   = (iv != 0);
        if (apiIntParam("commands", iv)) c.commands = (iv != 0);
        // Boolean fields may also arrive as JSON true/false strings.
        if (g_portal.hasArg("plain")) {
            String body = g_portal.arg("plain");
            auto boolField = [&](const char *key, bool &dst) {
                String needle = String("\"") + key + "\":";
                int k = body.indexOf(needle);
                if (k < 0) return;
                int v = body.indexOf("true",  k + needle.length());
                int f = body.indexOf("false", k + needle.length());
                if (v >= 0 && (f < 0 || v < f)) dst = true;
                else if (f >= 0)                 dst = false;
            };
            boolField("enabled",  c.enabled);
            boolField("retain",   c.retain);
            boolField("commands", c.commands);
        }
        g_settings.setMqtt(c);
        // Handle device_name separately (stored outside MqttConfig).
        if (apiStrParam("device_name", sv)) {
            g_settings.setDeviceName(sv.c_str());
            LOGF("settings: device_name set to '%s'", g_settings.deviceName());
        }
        // Re-initialise the MQTT client with the new config (takes effect immediately).
        g_mqtt.begin(g_settings.mqtt(), g_hostname,
            []() -> String { return apiStatusJson(); },
            []() -> String {
                long up = millis() / 1000;
                String j; j.reserve(768);
                j += F("{");
                j += F("\"hostname\":\"");   j += g_hostname;                          j += F("\",");
                j += F("\"sta_mac\":\"");    j += WiFi.macAddress();                   j += F("\",");
                j += F("\"sta_ip\":\"");     j += WiFi.localIP().toString();           j += F("\",");
                j += F("\"gateway\":\"");    j += WiFi.gatewayIP().toString();         j += F("\",");
                j += F("\"netmask\":\"");    j += WiFi.subnetMask().toString();        j += F("\",");
                j += F("\"dns\":\"");        j += WiFi.dnsIP().toString();             j += F("\",");
                j += F("\"ssid\":\"");       j += WiFi.SSID();                         j += F("\",");
                j += F("\"bssid\":\"");      j += WiFi.BSSIDstr();                     j += F("\",");
                j += F("\"ap_mac\":\"");     j += WiFi.softAPmacAddress();             j += F("\",");
                j += F("\"rssi\":");         j += String(WiFi.RSSI());                 j += F(",");
                j += F("\"ap_mode\":");      j += (g_apMode ? F("true") : F("false")); j += F(",");
                j += F("\"sdk\":\"");        j += String(ESP.getSdkVersion());         j += F("\",");
                j += F("\"core\":\"");       j += ESP.getCoreVersion();                j += F("\",");
                j += F("\"chip_id\":\"");    j += String(ESP.getChipId(), HEX);        j += F("\",");
                j += F("\"flash_id\":\"");   j += String(ESP.getFlashChipId(), HEX);   j += F("\",");
                j += F("\"flash_size\":");   j += String(ESP.getFlashChipRealSize());  j += F(",");
                j += F("\"boot\":");         j += String(ESP.getBootVersion());        j += F(",");
                j += F("\"reset\":\"");      j += ESP.getResetReason();                j += F("\",");
                j += F("\"heap\":");         j += String(ESP.getFreeHeap());           j += F(",");
                j += F("\"sketch\":");       j += String(ESP.getSketchSize());         j += F(",");
                j += F("\"free_sketch\":");  j += String(ESP.getFreeSketchSpace());    j += F(",");
                j += F("\"uptime\":");       j += String(up);                          j += F(",");
                j += F("\"device_name\":\""); j += g_settings.deviceName();            j += F("\",");
                j += F("\"time\":\"");       j += g_ntp.isoNow();                      j += F("\"");
                j += F("}");
                return j;
            },
            []() -> String { return g_ntp.isoNow(); },
            [](const String &cmd) {
                if      (cmd == "up")     { g_ant.up();   }
                else if (cmd == "down")   { g_ant.down(); }
                else if (cmd == "ground") { g_ant.setPosition(POS_GROUND); }
                else                      { int p = cmd.toInt(); if (p >= 0 && p <= g_ant.maxAntennas()) g_ant.setPosition(p); }
                refreshStatus(); logEvent("mqtt");
            },
            [](int v)                    { g_ant.setMaxAntennas(v); refreshStatus(); g_mqtt.publishState(); },
            [](int pos, const String &n) { g_ant.setName(pos, n);  refreshStatus(); },
            []()                         { delay(200); ESP.restart(); }
        );
        LOGF("MQTT config updated: enabled=%d host=%s", c.enabled, c.host);
        apiCors();
        g_portal.send(200, "application/json", F("{\"saved\":true}"));
    });

    g_portal.begin();
}

// ---------------------------------------------------------------------------
// Button handling — replicates the stock gestures (docs/buttons.md)
// ---------------------------------------------------------------------------

static void handleButtons() {
    g_btn.update();

    // UP (GPIO3): next antenna.
    if (g_btn.justPressed(Btn::UP)) {
        g_ant.up();
        LOGF("btn UP -> %s", g_ant.label().c_str());
        refreshStatus(); logEvent("button");
    }

    // DOWN (GPIO1): previous antenna.
    if (g_btn.justPressed(Btn::DOWN)) {
        g_ant.down();
        LOGF("btn DOWN -> %s", g_ant.label().c_str());
        refreshStatus(); logEvent("button");
    }

    // SET (GPIO0): bump the "max antennas" value; show "MAX n".
    if (g_btn.justPressed(Btn::SET)) {
        g_ant.bumpMax();
        LOGF("btn SET -> MAX %d", g_ant.maxAntennas());
        g_oled.showMax(g_ant.maxAntennas());
        g_revertStatusAt = millis() + 1200;   // revert to status after a moment
    }

    // ERASE (GPIO2): hold to wipe WiFi config. Show the prompt while held, and if
    // held past ERASE_HOLD_MS, erase and reboot (matching the stock hold gesture).
    static uint32_t eraseStart = 0;
    if (g_btn.justPressed(Btn::ERASE)) {
        eraseStart = millis();
        g_oled.showEraseHold();
    }
    if (g_btn.pressed(Btn::ERASE) && eraseStart) {
        if (millis() - eraseStart >= ERASE_HOLD_MS) {
            g_oled.showMessage(F("Erasing..."), F("rebooting"));
            delay(300);
            WiFi.disconnect(true);
            g_wm.resetSettings();
            delay(300);
            ESP.restart();
        }
    }
    if (g_btn.justReleased(Btn::ERASE)) {
        eraseStart = 0;
        refreshStatus();
    }

    // Revert transient screens (MAX) back to the status screen.
    if (g_revertStatusAt && (int32_t)(millis() - g_revertStatusAt) >= 0) {
        g_revertStatusAt = 0;
        refreshStatus();
    }
}

// ---------------------------------------------------------------------------
// Boot-time gestures (docs/manual-setup.md):
//   * Hold ERASE at power-on with the countdown prompt -> wipe WiFi config.
//   * Hold UP at power-on -> force the config portal (AutoConnect +).
// ---------------------------------------------------------------------------

// Require a button to read LOW *continuously* for `ms` to count as "held". A single
// transient LOW is ignored — important for GPIO3 (UP), which is also the UART0 RX
// pin and can momentarily read LOW from serial-line activity when USB is attached
// (this previously caused a FALSE "force portal" and stopped the unit joining WiFi).
static bool heldFor(Btn b, uint16_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        if (!g_btn.rawPressed(b)) return false;   // released → not a real hold
        delay(5);
    }
    return true;
}

static bool bootGestures() {
    // Show the "press ERASE for erase / stored information / <countdown>" prompt for a
    // few seconds; if ERASE is held during that window, wipe and reboot. If UP is
    // *genuinely held* (debounced), force the config portal.
    bool forcePortal = false;
    for (int t = 5; t >= 0; t--) {           // ~5s countdown
        g_oled.showErasePrompt(t);
        uint32_t until = millis() + 1000;
        while ((int32_t)(millis() - until) < 0) {
            // ERASE: require a sustained hold before wiping (debounced).
            if (g_btn.rawPressed(Btn::ERASE) && heldFor(Btn::ERASE, 150)) {
                g_oled.showEraseHold();
                uint32_t hold = millis();
                while (g_btn.rawPressed(Btn::ERASE)) {
                    if (millis() - hold >= 800) {
                        LOG("bootGestures: ERASE held -> wiping WiFi + reboot");
                        g_oled.showMessage(F("Erasing..."), F("rebooting"));
                        delay(300);
                        WiFi.disconnect(true);
                        g_wm.resetSettings();
                        delay(300);
                        ESP.restart();
                    }
                    delay(10);
                }
            }
#ifndef REPLICA_DEBUG
            // UP: force the config portal, but ONLY if held continuously for 300 ms.
            // (Skipped entirely in the serial-debug build, where GPIO3 is UART RX and
            // is not configured as a button.)
            if (g_btn.rawPressed(Btn::UP) && heldFor(Btn::UP, 300)) {
                forcePortal = true;
            }
#endif
            delay(5);
        }
    }
    return forcePortal;
}

// ---------------------------------------------------------------------------
// setup() / loop()
// ---------------------------------------------------------------------------

void setup() {
    // NOTE on serial: GPIO1/GPIO3 are the UART0 TX/RX pins but are used here as the
    // DOWN/UP button inputs (as in the stock firmware). Serial is therefore left
    // unused unless REPLICA_DEBUG is defined (which will interfere with the buttons).
    // Regardless, all key events are recorded in the in-memory debug log, viewable
    // over WiFi at http://<device>/debug — the conflict-free way to see what's up.
    DebugLog::instance().begin();
    LOG("=== ANTENNA SELECTOR V2 (replica) boot ===");
    LOGF("reset reason: %s", ESP.getResetReason().c_str());
    LOGF("free heap: %u", ESP.getFreeHeap());

    // Mount the web-asset filesystem (index.html / style.css / portal + info pages).
    if (LittleFS.begin()) {
        LOG("LittleFS mounted");
        // List what's actually on the filesystem — diagnoses "not uploaded" issues.
        Dir dir = LittleFS.openDir("/");
        int n = 0;
        while (dir.next()) { LOGF("  fs: %s (%u bytes)", dir.fileName().c_str(),
                                  (unsigned)dir.fileSize()); n++; }
        LOGF("LittleFS: %d file(s)", n);
    } else {
        LOG("LittleFS mount FAILED (run: pio run -t uploadfs)");
    }

    // Bring up hardware first, mirroring the stock setup() init order.
    LOG("init: antenna/decoder...");
    g_ant.begin();     // GPIO12/13/14 outputs, preset then GROUND; also EEPROM.begin()
    LOG("init: buttons...");
    g_btn.begin();     // GPIO0/1/2/3 inputs w/ pull-ups (driven HIGH in stock setup)
    LOG("init: OLED (I2C)...");
    g_oled.begin();    // GPIO4/5 I2C OLED
    LOG("init: OLED done");

    // Load persisted settings (MQTT config) from EEPROM. Must run AFTER g_ant.begin()
    // which calls EEPROM.begin(EEPROM_TOTAL_SIZE) to open the shared sector.
    g_settings.begin();
    LOGF("settings: MQTT %s host='%s' port=%u prefix='%s'",
         g_settings.mqtt().enabled ? "enabled" : "disabled",
         g_settings.mqtt().host, g_settings.mqtt().port,
         g_settings.mqtt().prefix);

    // 2-second boot splash: device name in big bold text filling the screen.
    g_oled.showSplash(g_settings.deviceName());
    delay(2000);

    LOG("setup: hardware up (OLED/buttons/decoder OK)");

    // Boot-time gestures (erase countdown / force portal).
    bool forcePortal = bootGestures();
    LOGF("bootGestures: forcePortal=%d", (int)forcePortal);

    g_hostname = deviceHostname();
    WiFi.hostname(g_hostname);
    LOGF("hostname=%s  mac=%s", g_hostname.c_str(), WiFi.macAddress().c_str());

    // (1) Optional pre-provisioning: if data/wifi.json exists on LittleFS, try those
    //     credentials directly before falling back to the WiFiManager portal. This
    //     lets ./flash.sh bake in the SSID/password so the unit joins immediately.
    bool connected = false;
    if (!forcePortal && tryProvisionedWiFi()) {
        connected = true;
    }

    // (1b) If saved credentials exist, retry them for a bounded time before giving up
    //      to the portal. Flaky/slow routers (or a router still booting after a power
    //      cut) often need more than one ~15s attempt; we try several within a total
    //      budget so we "try harder" without hanging forever. Physical buttons keep
    //      working and the OLED animates during the wait.
    if (!connected && !forcePortal && WiFi.SSID().length() > 0) {
        LOGF("wifi: saved SSID '%s' present, retrying (budget %us)",
             WiFi.SSID().c_str(), WIFI_CONNECT_BUDGET_MS / 1000);
        connected = retrySavedWiFi();
    }

    // (2) WiFiManager: try saved credentials as a station; otherwise (or if UP was
    //     held) open the "AutoConnectAP" captive portal at 192.168.4.1.
    if (!connected) {
        g_wm.setHostname(g_hostname.c_str());
        g_wm.setAPCallback(onEnterConfigPortal);
        g_wm.setWebServerCallback([]() { LOG("portal web server started"); });
        g_wm.setSaveConfigCallback([]() { LOG("portal: credentials saved"); });
        // Give WiFiManager's own connect attempt more patience too: longer per-attempt
        // timeout and a few retries before it opens the config portal.
        g_wm.setConnectTimeout(WIFI_CONNECT_ATTEMPT_S);
        g_wm.setConnectRetries(WIFI_CONNECT_RETRIES);
        // Blocking portal + a tick callback that keeps the OLED lit and services the
        // physical buttons — this is what prevents the "blank screen" during setup.
        g_wm.setConfigPortalBlocking(true);
        g_wm.setConfigPortalTimeout(0);      // stay in the portal like the stock FW

        g_oled.showMessage(F("Connecting"), F("to WiFi..."));
        LOG("wifi: starting WiFiManager autoConnect / portal");

        if (forcePortal) {
            g_oled.showForceAP();
            delay(600);
            g_oled.showAP(String(AP_SSID), String("http://192.168.4.1"));
            connected = g_wm.startConfigPortal(AP_SSID);
        } else {
            connected = g_wm.autoConnect(AP_SSID);   // AP SSID "AutoConnectAP", open
        }
    }

    g_apMode = !connected;
    LOGF("wifi: connected=%d apMode=%d", (int)connected, (int)g_apMode);

    if (connected) {
        startStationServices();   // mDNS + portal + OTA + app server, refreshStatus()
    } else {
        LOG("wifi: NOT connected (AP/config mode)");
        refreshStatus();          // AP-mode / (IP unset) status on the OLED
    }
}

// Bring up the station-mode services (mDNS, portal, OTA, app server) once WiFi is up.
static void startStationServices() {
    g_apMode = false;
    // Disable WiFi modem sleep — it reduces the interrupt jitter that can corrupt
    // HW-I2C frames to the OLED.
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    LOGF("wifi: connected SSID='%s' IP=%s", WiFi.SSID().c_str(),
         WiFi.localIP().toString().c_str());
    if (MDNS.begin(g_hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        LOGF("mDNS: http://%s.local", g_hostname.c_str());
    } else {
        LOG("mDNS: begin FAILED");
    }
    setupStationPortal();   // / /5/on /4/on /info /update /u /erase /restart /debug
    LOG("HTTP server started on :80");
    LOGF("debug page: http://%s/debug", WiFi.localIP().toString().c_str());

    // Start NTP (non-blocking; syncs in background, graceful if unreachable).
    g_ntp.begin(g_settings.ntp().host, g_settings.ntp().port);
    // Wire NTP timestamp into the debug log so log lines get ISO8601 prefixes.
    DebugLog::setTimeFn([]() -> String { return g_ntp.isoNow(); });

    // Start MQTT (if configured). Lambdas capture g_hostname by value at call time.
    g_mqtt.begin(g_settings.mqtt(), g_hostname,
        // statusFn: full status JSON
        []() -> String { return apiStatusJson(); },
        // infoFn: full device/network info JSON (same field set as GET /api/info)
        []() -> String {
            long up = millis() / 1000;
            String j; j.reserve(768);
            j += F("{");
            j += F("\"hostname\":\"");   j += g_hostname;                          j += F("\",");
            j += F("\"sta_mac\":\"");    j += WiFi.macAddress();                   j += F("\",");
            j += F("\"sta_ip\":\"");     j += WiFi.localIP().toString();           j += F("\",");
            j += F("\"gateway\":\"");    j += WiFi.gatewayIP().toString();         j += F("\",");
            j += F("\"netmask\":\"");    j += WiFi.subnetMask().toString();        j += F("\",");
            j += F("\"dns\":\"");        j += WiFi.dnsIP().toString();             j += F("\",");
            j += F("\"ssid\":\"");       j += WiFi.SSID();                         j += F("\",");
            j += F("\"bssid\":\"");      j += WiFi.BSSIDstr();                     j += F("\",");
            j += F("\"ap_mac\":\"");     j += WiFi.softAPmacAddress();             j += F("\",");
            j += F("\"rssi\":");         j += String(WiFi.RSSI());                 j += F(",");
            j += F("\"ap_mode\":");      j += (g_apMode ? F("true") : F("false")); j += F(",");
            j += F("\"sdk\":\"");        j += String(ESP.getSdkVersion());         j += F("\",");
            j += F("\"core\":\"");       j += ESP.getCoreVersion();                j += F("\",");
            j += F("\"chip_id\":\"");    j += String(ESP.getChipId(), HEX);        j += F("\",");
            j += F("\"flash_id\":\"");   j += String(ESP.getFlashChipId(), HEX);   j += F("\",");
            j += F("\"flash_size\":");   j += String(ESP.getFlashChipRealSize());  j += F(",");
            j += F("\"boot\":");         j += String(ESP.getBootVersion());        j += F(",");
            j += F("\"reset\":\"");      j += ESP.getResetReason();                j += F("\",");
            j += F("\"heap\":");         j += String(ESP.getFreeHeap());           j += F(",");
            j += F("\"sketch\":");       j += String(ESP.getSketchSize());         j += F(",");
            j += F("\"free_sketch\":");  j += String(ESP.getFreeSketchSpace());    j += F(",");
            j += F("\"uptime\":");       j += String(up);                          j += F(",");
            j += F("\"device_name\":\""); j += g_settings.deviceName();            j += F("\",");
            j += F("\"time\":\"");       j += g_ntp.isoNow();                      j += F("\"");
            j += F("}");
            return j;
        },
        // timeFn: current ISO8601 UTC timestamp (or "" if NTP not synced)
        []() -> String { return g_ntp.isoNow(); },
        // setFn: handle "up"/"down"/"ground"/"<n>" commands
        [](const String &cmd) {
            if      (cmd == "up")     { g_ant.up();   }
            else if (cmd == "down")   { g_ant.down(); }
            else if (cmd == "ground") { g_ant.setPosition(POS_GROUND); }
            else {
                int p = cmd.toInt();
                if (p >= 0 && p <= g_ant.maxAntennas()) g_ant.setPosition(p);
            }
            refreshStatus(); logEvent("mqtt");
        },
        // maxFn: set max antennas
        [](int v) { g_ant.setMaxAntennas(v); refreshStatus(); g_mqtt.publishState(); },
        // nameFn: set a port name
        [](int pos, const String &n) { g_ant.setName(pos, n); refreshStatus(); g_mqtt.publishState(); },
        // restartFn
        []() { delay(200); ESP.restart(); }
    );
    LOGF("MQTT: begin done (enabled=%d)", (int)g_mqtt.enabled());

    refreshStatus();
    g_mqtt.publishState();   // publish initial state on connect
}

void loop() {
    handleButtons();
    g_ntp.loop();    // poll for NTP sync, periodic re-sync
    g_mqtt.loop();   // reconnect with backoff, service inbound commands, periodic /info

    // Re-publish /info as soon as NTP first syncs so the time field is populated.
    // (The initial /info push on MQTT connect happens before NTP has a timestamp.)
    static bool ntpInfoPublished = false;
    if (!ntpInfoPublished && g_ntp.synced() && g_mqtt.connected()) {
        ntpInfoPublished = true;
        g_mqtt.publishInfo();
        LOGF("NTP synced — re-published MQTT /info with time=%s", g_ntp.isoNow().c_str());
    }

    if (!g_apMode && WiFi.status() == WL_CONNECTED) {
        MDNS.update();
        g_portal.handleClient();          // serves /, /5/on, /4/on, /info, OTA, /debug
        // Update the OLED web-address line when the DHCP IP is first assigned or
        // changes, so the screen shows "http://<IP>" under the antenna label as soon
        // as the router hands out an address (docs/manual-setup.md).
        static IPAddress lastShownIP;
        IPAddress ip = WiFi.localIP();
        if (ip != lastShownIP && !g_revertStatusAt) {
            lastShownIP = ip;
            refreshStatus();
        }

        // Periodically re-draw the status screen (~every 2s) so any transient
        // display glitch is refreshed rather than sticking.
        static uint32_t lastRedraw = 0;
        if (!g_revertStatusAt && millis() - lastRedraw > 2000) {
            lastRedraw = millis();
            refreshStatus();
        }
    } else {
        // AP/config-portal mode: WiFiManager owns the HTTP server; keep it alive.
        g_wm.process();
        // If the user saved credentials and the device connected, switch to STA.
        if (WiFi.status() == WL_CONNECTED) {
            startStationServices();
        }
    }
}
