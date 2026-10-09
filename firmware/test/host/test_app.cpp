// test_app.cpp — the whole firmware (src/main.cpp) on a PC.
//
// main.cpp is compiled as-is against the stand-ins in stub/: a fake clock, pins, WiFi,
// LittleFS (loaded with the real data/ web assets), EEPROM, OLED, MQTT broker and web
// server. Each boot() is a fresh power-on in its own process; what the device persists
// (EEPROM, LittleFS, saved WiFi) carries over to the next boot(), so reboots are real.
//
// Covered: boot + WiFi paths, boot gestures, hostname, startup port, every REST route,
// the stock web routes and pages, buttons and their holds, the lock, the screen saver,
// the scheduler, last-port restore, MQTT (native + Tasmota) and persistence.
#include "main.cpp"

#include <dirent.h>

#include <fstream>
#include <sstream>

#include "harness.h"

#ifndef DATA_DIR
#define DATA_DIR "../../data"
#endif

// ---- device <-> fakes ---------------------------------------------------------------
void loadDevice(const Device &d) {
    memcpy(EEPROM.mem, d.eeprom.data(), sizeof EEPROM.mem);
    g_fs = d.fs;
    WiFi.savedSsid = d.savedSsid; WiFi.savedPass = d.savedPass;
}
void saveDevice(Device &d) {
    d.eeprom.assign(EEPROM.mem, EEPROM.mem + sizeof EEPROM.mem);
    d.fs = g_fs;
    d.savedSsid = WiFi.savedSsid.s; d.savedPass = WiFi.savedPass.s;
}

// A freshly flashed unit: the real data/ image on LittleFS (+ wifi.json, as flash.sh
// writes it), erased EEPROM.
static Device flashed(bool withWifiJson = true) {
    Device d;
    if (DIR *dir = opendir(DATA_DIR)) {
        while (dirent *e = readdir(dir)) {
            std::string n = e->d_name;
            if (n == "." || n == ".." || n == "wifi.json") continue;
            std::ifstream f(std::string(DATA_DIR) + "/" + n, std::ios::binary);
            std::stringstream ss; ss << f.rdbuf();
            d.fs["/" + n] = ss.str();
        }
        closedir(dir);
    }
    if (withWifiJson) d.fs["/wifi.json"] = "{\"ssid\":\"Shack\",\"pass\":\"secret\"}";
    return d;
}

// ---- driving the device -------------------------------------------------------------
static bool g_restarted = false;
static void powerOn() {
    g_restarted = false;
    try { setup(); } catch (const EspRestart &) { g_restarted = true; }
}
// Run loop() for `ms` of fake time. Returns true if the firmware called ESP.restart().
static bool pump(unsigned long ms, unsigned long step = 10) {
    for (unsigned long t = 0; t < ms; t += step) {
        g_millis += step;
        try { loop(); } catch (const EspRestart &) { return true; }
    }
    return false;
}
static HttpResponse req(HTTPMethod m, const std::string &uri, const std::string &body = "") { return g_portal.request(m, uri, body); }
static HttpResponse GET(const std::string &uri) { return req(HTTP_GET, uri); }
static HttpResponse POST(const std::string &uri, const std::string &body = "") { return req(HTTP_POST, uri, body); }
static J status() { return json(GET("/api/status").body); }
static int pos() { return (int)status()["position"].n; }
static int lines() { return (g_pinOut[PIN_SEL_A2] << 2) | (g_pinOut[PIN_SEL_A1] << 1) | g_pinOut[PIN_SEL_A0]; }
static const OledFrame &screen() { static OledFrame none; return g_frames.empty() ? none : g_frames.back(); }
static bool shown(const std::string &t, size_t since = 0) {
    for (size_t i = since; i < g_frames.size(); i++) if (g_frames[i].has(t)) return true;
    return false;
}
// Hold a button (pin LOW) for `ms`, then release. Returns true if it caused a restart.
static bool press(uint8_t pin, unsigned long ms) {
    g_pinLevel[pin] = LOW;
    bool r = pump(ms);
    g_pinLevel[pin] = HIGH;
    return r || pump(100);
}
static J events() { return json(GET("/api/events").body); }
static time_t utc(int y, int mo, int d, int h, int mi) {
    struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; return timegm(&t);
}

// =====================================================================================
static void testBoot() {
    section("BOOT: freshly flashed, wifi.json present");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(!g_restarted && !g_apMode && WiFi.status() == WL_CONNECTED, "joins the network from wifi.json, station mode");
        check(WiFi.savedSsid == "Shack", "…using the baked-in SSID");
        check(shown("ANTENNA") && shown("SELECTOR V2"), "power-on banner");
        check(shown("UberANT"), "splash shows the device name");
        check(shown("press ERASE for") && shown("5") && shown("0"), "boot erase prompt counts down 5..0");
        check(shown("WiFi OK") && shown("192.168.9.109"), "\"WiFi OK\" + the IP after joining");
        check(g_hostname == "uberant" && WiFi.hostname() == "uberant", "hostname from the device name: uberant");
        check(MDNS.running && MDNS.name == "uberant" && MDNS.services.size() == 1 && MDNS.services[0] == "http/tcp:80", "mDNS uberant.local with http/tcp:80");
        check(g_portal.begun() && GET("/api/status").code == 200, "web server up");
        check(pos() == 0 && lines() == 0, "antenna at GROUND");
        check(g_configTimeCalls >= 1 && g_configTimeServer == "time.cloudflare.com", "NTP started against time.cloudflare.com (the default)");
        check(g_wm.autoConnectCalls == 0 && g_wm.startPortalCalls == 0, "WiFiManager not needed");
        pump(300);
        check(screen().has("GROUND"), "then the status screen");
    });

    section("BOOT: saved credentials, no wifi.json");
    d = flashed(false); d.savedSsid = "Shack"; d.savedPass = "secret";
    boot(d, [] {
        powerOn();
        check(!g_apMode && WiFi.status() == WL_CONNECTED && GET("/api/status").code == 200, "rejoins with the saved network");
        check(g_wm.autoConnectCalls == 0, "no portal");
    });

    section("BOOT: saved network unreachable -> bounded retry -> portal");
    d = flashed(false); d.savedSsid = "Shack"; d.savedPass = "wrong";
    boot(d, [] {
        unsigned long t0 = g_millis;
        powerOn();
        check(g_wm.autoConnectCalls == 1 && g_apMode, "after the retry budget, WiFiManager's portal");
        check(g_millis - t0 >= WIFI_CONNECT_BUDGET_MS, "retried for the whole " + std::to_string(WIFI_CONNECT_BUDGET_MS / 1000) + " s budget first");
        check(g_wm.connectTimeout == WIFI_CONNECT_ATTEMPT_S && g_wm.connectRetries == WIFI_CONNECT_RETRIES && g_wm.portalTimeout == 0 && g_wm.blocking,
              "portal: 15 s attempts, 3 retries, no timeout, blocking");
        check(g_wm.apName == "AutoConnectAP" && g_wm.hostName == "uberant", "AP SSID AutoConnectAP, hostname passed on");
    });

    section("BOOT: no network at all -> config portal (AP mode)");
    d = flashed(false);
    boot(d, [] {
        powerOn();
        check(g_apMode && g_wm.autoConnectCalls == 1, "AP / config mode");
        check(shown("ssid: AutoConnectAP") && screen().has("http://192.168.4.1"), "OLED: ssid: AutoConnectAP / http://192.168.4.1");
        check(GET("/api/status").code == 404, "the station web server is not started");
        pump(100);
        check(g_wm.processCalls > 0, "loop() services the portal");
        WiFi.st = WL_CONNECTED; WiFi.ssid = "Shack";   // the user saved credentials in the portal
        pump(50);
        check(!g_apMode && GET("/api/status").code == 200 && MDNS.running, "once WiFi connects, station services start from loop()");
    });

    section("BOOT: portal used to save credentials");
    d = flashed(false);
    boot(d, [] {
        g_wm.portalResult = true;
        powerOn();
        check(!g_apMode && WiFi.savedSsid == "Shack" && GET("/api/status").code == 200, "connects with what the user entered");
    });

    section("BOOT: wifi.json with bad credentials");
    d = flashed(); d.fs["/wifi.json"] = "{\"ssid\":\"Shack\",\"pass\":\"nope\"}";
    boot(d, [] {
        powerOn();
        check(g_apMode && g_wm.autoConnectCalls == 1, "falls back to the portal");
    });
    d = flashed(); d.fs["/wifi.json"] = "{\"pass\":\"x\"}";
    boot(d, [] { powerOn(); check(g_apMode, "wifi.json without an ssid is ignored"); });

    section("BOOT: LittleFS not flashed");
    d = flashed();
    boot(d, [] {
        g_fsMountOk = false; g_fs.erase("/app.html"); g_fs.erase("/app.html.gz"); g_fs.erase("/old.html");
        powerOn();
        HttpResponse r = GET("/");
        check(r.code == 200 && r.body.find("Filesystem image not uploaded") != std::string::npos, "\"/\" explains the filesystem image is missing");
        HttpResponse o = GET("/old");
        check(o.body.find("Web ANTENNAS switch") != std::string::npos && o.body.find("<p>GROUND</p>") != std::string::npos, "/old falls back to a built-in page with the label");
    });

    section("BOOT: no OLED attached");
    d = flashed();
    boot(d, [] {
        g_i2cDevice = 0; g_oledBeginOk = false;
        powerOn();
        check(!g_restarted && GET("/api/status").code == 200, "boots and serves without a panel");
    });
}

static void testGestures() {
    section("BOOT GESTURES");
    Device d = flashed();
    boot(d, [] {
        g_pinLevel[PIN_BTN_UP] = LOW;   // UP held from power-on
        g_delayHook = [] { if (g_millis > 5000) g_pinLevel[PIN_BTN_UP] = HIGH; };
        powerOn();
        check(g_wm.startPortalCalls == 1 && g_wm.autoConnectCalls == 0, "UP held at boot: forces the config portal, even with wifi.json");
        check(shown("AutoConnect (+)"), "OLED: AutoConnect (+)");
    });
    d = flashed();
    boot(d, [] {
        unsigned long until = g_millis + 4000;
        g_pinLevel[PIN_BTN_UP] = LOW;
        g_delayHook = [until] { if (g_millis >= until - 3900) g_pinLevel[PIN_BTN_UP] = HIGH; };   // ~100 ms blip
        powerOn();
        check(g_wm.startPortalCalls == 0 && !g_apMode, "a 100 ms blip on UP (UART RX noise) does not force the portal");
    });
    d = flashed();
    { Device pre = d;
      boot(pre, [] { powerOn(); POST("/api/name?position=1&name=Keep"); });
      d = pre; d.savedSsid = "Shack"; d.savedPass = "secret"; }
    boot(d, [] {
        g_pinLevel[PIN_BTN_ERASE] = LOW;
        powerOn();
        check(g_restarted, "ERASE held at boot: restarts");
        bool wiped = true;
        for (int i = 0; i < EEPROM_TOTAL_SIZE; i++) wiped = wiped && EEPROM.mem[i] == 0xFF;
        check(wiped, "…after wiping the whole settings sector");
        check(g_wm.resetCalls == 1 && WiFi.savedSsid == "", "…and the saved WiFi");
        check(shown("ERASE") && shown("hold the button"), "OLED: ERASE / hold the button");
    });
    boot(d, [] {
        powerOn();
        check(json(GET("/api/names").body).o.empty(), "next boot: names gone (factory state)");
    });
    d = flashed();
    boot(d, [] {
        g_pinLevel[PIN_BTN_ERASE] = LOW;
        g_delayHook = [] { static int n = 0; if (++n > 20) g_pinLevel[PIN_BTN_ERASE] = HIGH; };
        powerOn();
        check(!g_restarted, "ERASE tapped (not held 800 ms) at boot: nothing wiped");
    });
}

static void testHostname() {
    section("HOSTNAME from the device name");
    struct Case { const char *name, *host; };
    for (Case c : {Case{"My Shack #1!", "my-shack-1"}, Case{"--Ant--Box--", "ant-box"}, Case{"!!!", "ESP-45CA21"},
                   Case{"ABCDEFGHIJKLMNOPQRSTUVWX", "abcdefghijklmnopqrstuvwx"}}) {
        Device d = flashed();
        std::string n = c.name, h = c.host;
        boot(d, [n] { powerOn(); POST("/api/mqtt", "{\"device_name\":\"" + n + "\"}"); });
        boot(d, [n, h] {
            powerOn();
            check(g_hostname == h.c_str() && MDNS.name == h.c_str(), "\"" + n + "\" -> " + h);
        });
    }
    Device d = flashed();
    boot(d, [] {
        powerOn();
        POST("/api/mqtt", "{\"device_name\":\"Roof Box\"}");
        check(g_hostname == "roof-box" && MDNS.name == "roof-box" && WiFi.hostname() == "roof-box", "renaming re-registers mDNS at once (no reboot)");
        check(status()["device_name"].s == "Roof Box" && status()["hostname"].s == "roof-box", "status reports both");
    });
}

static void testStartupPort() {
    section("STARTUP PORT");
    Device d = flashed();
    boot(d, [] { powerOn(); POST("/api/default-port", "{\"default_port\":3}"); check(pos() == 0, "setting the default does not move the antenna now"); });
    boot(d, [] { powerOn(); check(pos() == 3 && lines() == 3, "default port 3: boots on ANT3"); });
    boot(d, [] { powerOn(); POST("/api/max?value=2"); });
    boot(d, [] { powerOn(); check(pos() == 0 && status()["default_port"].n == 0, "lowering max below it resets the default to GROUND"); });

    d = flashed();
    boot(d, [] {
        powerOn();
        POST("/api/startup-port", "{\"mode\":\"last\"}");
        POST("/api/antenna?position=4");
        pump(1500);
        check(!g_fs.count("/last_port.txt") || g_fs["/last_port.txt"] != "4", "last port is written after a 2 s debounce, not at once");
        pump(1000);
        check(g_fs["/last_port.txt"] == "4", "…then /last_port.txt = 4");
        POST("/api/antenna/up"); POST("/api/antenna/up"); POST("/api/antenna/down");
        pump(2500);
        check(g_fs["/last_port.txt"] == "5", "several quick changes: one write of the final port");
    });
    boot(d, [] { powerOn(); check(pos() == 5, "mode 'last': boots on the last used port (5)"); });
    d.fs["/last_port.txt"] = "9";
    boot(d, [] { powerOn(); check(pos() == 0, "an invalid last port falls back to the default"); });
    d.fs.erase("/last_port.txt");
    boot(d, [] { powerOn(); check(pos() == 0, "a missing last port falls back to the default"); });
    boot(d, [] {
        powerOn(); POST("/api/startup-port", "{\"mode\":\"default\"}");
        POST("/api/antenna?position=2"); pump(3000);
        check(!g_fs.count("/last_port.txt"), "mode 'default': changes are not written to flash");
    });
}

// =====================================================================================
static void testApiStatus() {
    section("API: /api/status, /api/info, CORS");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        HttpResponse r = GET("/api/status");
        J s; bool ok = parseJson(r.body, s);
        check(r.code == 200 && r.type == "application/json" && ok, "200, application/json, valid JSON");
        check(r.headers["Access-Control-Allow-Origin"] == "*" && r.headers["Access-Control-Allow-Methods"] == "GET, POST, OPTIONS"
              && r.headers["Access-Control-Allow-Headers"] == "Content-Type", "permissive CORS headers");
        const char *keys[] = {"position", "label", "ground", "max", "count", "name", "names", "url", "hostname", "device_name",
                              "mqtt_enabled", "mqtt_connected", "mqtt_prefix", "locked", "scheduler_enabled", "scheduler_active",
                              "scheduler_next", "time", "local_time", "tz_offset", "startup_port_mode", "default_port"};
        bool all = true; std::string missing;
        for (const char *k : keys) if (!s.get(k)) { all = false; missing += k; missing += ' '; }
        check(all && s.o.size() == sizeof keys / sizeof *keys, "exactly the documented fields " + missing);
        check(s["position"].n == 0 && s["label"].s == "GROUND" && s["ground"].b && s["max"].n == 5 && s["count"].n == 5, "position/label/ground/max/count");
        check(s["url"].s == "http://192.168.9.109" && s["hostname"].s == "uberant" && s["device_name"].s == "UberANT", "url, hostname, device name");
        check(!s["mqtt_enabled"].b && s["mqtt_prefix"].s == "uberant/uberant" && !s["locked"].b, "MQTT off, default prefix, unlocked");
        check(s["scheduler_enabled"].b && !s["scheduler_active"].b && s["scheduler_next"].s == "", "scheduler on, nothing scheduled");
        check(s["time"].s == "" && s["local_time"].s == "" && s["tz_offset"].n == 0, "no time before NTP sync");
        check(s["startup_port_mode"].s == "default" && s["default_port"].n == 0, "startup defaults");
        setEpoch(utc(2026, 10, 5, 7, 59)); pump(300);
        check(status()["time"].s == "2026-10-05T07:59:00Z", "time after sync (UTC)");

        J i = json(GET("/api/info").body);
        const char *ik[] = {"hostname", "sta_mac", "sta_ip", "gateway", "netmask", "dns", "ssid", "bssid", "ap_mac", "rssi", "ap_mode", "sdk",
                            "core", "chip_id", "flash_id", "flash_size", "boot", "reset", "heap", "sketch", "free_sketch", "uptime",
                            "device_name", "time", "local_time", "tz_offset"};
        all = true; for (const char *k : ik) all = all && i.get(k);
        check(all && i["sta_mac"].s == "CC:50:E3:45:CA:21" && i["ssid"].s == "Shack" && i["chip_id"].s == "45ca21" && !i["ap_mode"].b,
              "/api/info: valid JSON, every field");
    });
}

static void testApiAntenna() {
    section("API: antenna");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        J r = json(POST("/api/antenna/up").body);
        check(r["position"].n == 1 && lines() == 1, "up -> 1, decoder follows, returns status");
        POST("/api/antenna/up"); POST("/api/antenna/down");
        check(pos() == 1, "down -> 1");
        POST("/api/antenna/ground");
        check(pos() == 0, "ground -> 0");
        POST("/api/antenna/down");
        check(pos() == 5, "down from GROUND wraps to max");
        POST("/api/antenna?position=3");
        check(pos() == 3 && lines() == 3, "?position=3");
        POST("/api/antenna", "{\"position\": 2}");
        check(pos() == 2, "JSON {\"position\":2}");
        GET("/api/antenna?position=4");
        check(pos() == 4, "GET with ?position also selects (convenience)");
        HttpResponse e = POST("/api/antenna?position=6");
        check(e.code == 422 && json(e.body)["error"].s == "position out of range (0..max)" && pos() == 4, "beyond max: 422, no change");
        check(POST("/api/antenna?position=-1").code == 422, "negative: 422");
        HttpResponse m = POST("/api/antenna");
        check(m.code == 400 && json(m.body)["error"].s == "missing 'position'", "POST without position: 400");
        check(GET("/api/antenna").code == 200 && json(GET("/api/antenna").body)["position"].n == 4, "plain GET: status");
        J ev = events();
        check(ev.a.size() == 8 && ev[0]["source"].s == "api" && ev[0]["position"].n == 4 && ev[0]["label"].s == "ANT: 4", "every change logged, source api");
        size_t f = g_frames.size();
        POST("/api/antenna?position=1");
        check(shown("ANT: 1", f), "the OLED shows the new antenna at once");
    });
}

static void testApiLock() {
    section("API: lock");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(GET("/api/lock").body == "{\"locked\":false}", "GET: unlocked");
        check(POST("/api/lock", "{\"locked\":true}").body == "{\"locked\":true}" && status()["locked"].b, "POST {\"locked\":true}");
        pump(2500);   // after the "LOCKED" message
        check(screen().fills == 1, "OLED padlock on the status screen");
        for (const char *r : {"/api/antenna/up", "/api/antenna/down", "/api/antenna/ground", "/api/antenna?position=2"}) {
            HttpResponse x = POST(r);
            check(x.code == 423 && json(x.body)["error"].s == "locked" && pos() == 0, std::string("locked: ") + r + " -> 423");
        }
        check(POST("/api/lock", "{\"locked\":true}").body == "{\"locked\":true}", "locking again: no change");
        check(POST("/api/lock", "{\"locked\":false}").body == "{\"locked\":false}", "POST {\"locked\":false}");
        pump(2500);
        check(screen().fills == 0, "padlock gone");
        check(POST("/api/lock?locked=1").body == "{\"locked\":true}" && POST("/api/lock?locked=0").body == "{\"locked\":false}", "query ?locked=1 / 0");
        check(POST("/api/lock/toggle").body == "{\"locked\":true}" && POST("/api/lock/toggle").body == "{\"locked\":false}", "toggle");
        check(POST("/api/lock", "{}").body == "{\"locked\":false}", "a POST without a value changes nothing");
        J ev = events();
        check(ev[0]["source"].s == "unlock" && ev[1]["source"].s == "lock", "lock/unlock are logged as events");
        POST("/api/lock/toggle");
        HttpResponse up = GET("/5/on");
        check(pos() == 0 && up.body.find("<p>GROUND</p>") != std::string::npos, "locked: /5/on does not move the antenna (page re-rendered)");
        POST("/api/lock/toggle"); POST("/api/antenna?position=2"); POST("/api/lock/toggle");
        GET("/4/on");
        check(pos() == 2, "locked: nor does /4/on");
        POST("/api/lock/toggle"); POST("/api/antenna/ground"); POST("/api/lock/toggle");
        size_t f = g_frames.size();
        POST("/api/lock/toggle");
        check(!shown("LOCKED", 0) || shown("UNLOCKED", f), "OLED says UNLOCKED");
    });
    boot(d, [] { powerOn(); check(!status()["locked"].b, "the lock is not persisted: unlocked after a reboot"); });
}

static void testApiMax() {
    section("API: max");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(GET("/api/max").body == "{\"max\":5}", "GET {\"max\":5}");
        check(POST("/api/max?value=7").body == "{\"max\":7}", "?value=7");
        check(POST("/api/max", "{\"value\":3}").body == "{\"max\":3}", "JSON value 3");
        check(GET("/api/max?value=4").body == "{\"max\":4}", "GET with ?value sets too");
        check(POST("/api/max?value=8").code == 422 && POST("/api/max?value=-1").code == 422, "outside 0..7: 422");
        check(POST("/api/max").code == 400, "POST without value: 400");
        POST("/api/max?value=7");
        check(POST("/api/max/bump").body == "{\"max\":0}" && POST("/api/max/bump").body == "{\"max\":1}", "bump wraps 7 -> 0 -> 1");
        POST("/api/max?value=5");
        POST("/api/antenna?position=5");
        POST("/api/default-port", "{\"default_port\":4}");
        POST("/api/schedule", "{\"id\":0,\"enabled\":true,\"hour\":8,\"minute\":0,\"position\":5}");
        POST("/api/schedule", "{\"id\":1,\"enabled\":true,\"hour\":9,\"minute\":0,\"position\":2}");
        POST("/api/max?value=3");
        J s = status(); J sc = json(GET("/api/schedule").body);
        check(s["position"].n == 0 && s["max"].n == 3, "lowering max below the selection: GROUND");
        check(s["default_port"].n == 0, "…the default port is reset to GROUND");
        check(!sc["entries"][0]["enabled"].b && sc["entries"][0]["position"].n == 5 && sc["entries"][1]["enabled"].b,
              "…schedule entries above the new max are paused, not deleted");
    });
    boot(d, [] { powerOn(); check(status()["max"].n == 3, "max persists"); });
}

static void testApiPorts() {
    section("API: default port / startup mode");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(GET("/api/default-port").body == "{\"default_port\":0}", "GET default port");
        check(POST("/api/default-port", "{\"default_port\":2}").body == "{\"default_port\":2}", "POST 2");
        check(POST("/api/default-port?default_port=8").code == 422, "8: 422");
        check(POST("/api/default-port").body == "{\"default_port\":2}", "POST without a value: unchanged");
        check(GET("/api/startup-port").body == "{\"mode\":\"default\"}", "GET mode default");
        check(POST("/api/startup-port", "{\"mode\":\"LAST\"}").body == "{\"mode\":\"last\"}", "POST LAST (any case)");
        HttpResponse c = POST("/api/default-port", "{\"default_port\":1}");
        check(c.code == 409 && GET("/api/default-port").body == "{\"default_port\":2}", "mode last: setting a default port is 409");
        check(POST("/api/startup-port", "{\"mode\":\"bogus\"}").body == "{\"mode\":\"default\"}", "anything but 'last' is 'default'");
        check(status()["startup_port_mode"].s == "default", "status agrees");
    });
}

static void testApiNames() {
    section("API: names");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(GET("/api/names").body == "{}" && GET("/api/name").body == "{}", "no names");
        check(POST("/api/name?position=1&name=Sky%20Loop").body == "{\"position\":1,\"name\":\"Sky Loop\"}", "query form");
        check(POST("/api/name", "{\"position\":3,\"name\":\"80m Vertical\"}").body == "{\"position\":3,\"name\":\"80m Vertical\"}", "JSON form");
        check(POST("/api/name", "{\"position\":2,\"name\":\"A name far too long\"}").body == "{\"position\":2,\"name\":\"A name far too l\"}", "capped at 16");
        check(GET("/api/names").body == "{\"1\":\"Sky Loop\",\"2\":\"A name far too l\",\"3\":\"80m Vertical\"}", "GET /api/names");
        check(POST("/api/name?position=0&name=x").code == 422 && POST("/api/name?position=8&name=x").code == 422, "0 and 8: 422");
        check(POST("/api/name?position=2").code == 400, "POST without name: 400");
        check(req(HTTP_DELETE, "/api/name?position=2").body == "{\"position\":2,\"name\":\"\"}", "DELETE clears");
        check(POST("/api/name", "{\"position\":1,\"name\":\"\"}").body == "{\"position\":1,\"name\":\"\"}", "an empty name clears");
        POST("/api/antenna?position=3");
        J s = status();
        check(s["label"].s == "80m Vertical" && s["name"].s == "80m Vertical" && s["names"]["3"].s == "80m Vertical", "status: label, name, names");
        pump(300);
        check(screen().has("80m Vertical"), "OLED shows the name");
    });
    boot(d, [] { powerOn(); check(GET("/api/names").body == "{\"3\":\"80m Vertical\"}", "names persist"); });
}

static void testApiDisplay() {
    section("API: display");
    Device d = flashed();
    boot(d, [] {
        powerOn(); pump(300);
        HttpResponse r = POST("/api/display", "{\"text\":\"Hello\",\"duration\":3,\"align\":\"left\"}");
        check(r.body == "{\"text\":\"Hello\",\"duration\":3,\"align\":\"left\"}" && screen().has("Hello") && screen().find("Hello")->x == 0, "show text, left aligned, 3 s");
        pump(2500);
        check(screen().has("Hello"), "still showing at 2.5 s");
        pump(1000);
        check(screen().has("GROUND"), "status back after 3 s");
        check(json(POST("/api/display?text=x&duration=0").body)["duration"].n == 0, "duration 0 -> 100 ms minimum");
        check(json(POST("/api/display?text=x&duration=500").body)["duration"].n == 60, "duration capped at 60 s");
        check(json(POST("/api/display?text=x&align=RIGHT").body)["align"].s == "right" && json(POST("/api/display?text=x&align=up").body)["align"].s == "center",
              "align right; anything else centre");
        check(POST("/api/display").code == 400 && POST("/api/display", "{\"text\":\"\"}").code == 400, "missing or empty text: 400");

        check(GET("/api/display/mode").body == "{\"mode\":\"port\"}", "mode: port");
        check(POST("/api/display/mode", "{\"mode\":\"Clock\"}").body == "{\"mode\":\"clock\"}", "mode: clock");
        check(POST("/api/display/mode", "{\"mode\":\"cycle\"}").body == "{\"mode\":\"cycle\"}", "mode: cycle");
        check(POST("/api/display/mode", "{\"mode\":\"weird\"}").body == "{\"mode\":\"port\"}", "unknown -> port");

        check(GET("/api/display/blank").body == "{\"enabled\":false,\"timeout\":5}", "blank: off, 5 s");
        check(POST("/api/display/blank", "{\"enabled\":true,\"timeout\":10}").body == "{\"enabled\":true,\"timeout\":10}", "enable, 10 s");
        check(POST("/api/display/blank?timeout=0").body == "{\"enabled\":true,\"timeout\":1}", "timeout 0 -> 1");
        check(POST("/api/display/blank?timeout=999").body == "{\"enabled\":true,\"timeout\":255}", "timeout 999 -> 255");
        check(POST("/api/display/blank?enabled=0").body == "{\"enabled\":false,\"timeout\":255}", "?enabled=0");
    });
    boot(d, [] {
        powerOn();
        check(GET("/api/display/mode").body == "{\"mode\":\"port\"}" && GET("/api/display/blank").body == "{\"enabled\":false,\"timeout\":255}", "display settings persist");
    });
}

static void testApiNtp() {
    section("API: NTP / timezone");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        J n = json(GET("/api/ntp").body);
        check(n["host"].s == "time.cloudflare.com" && n["port"].n == 123 && !n["synced"].b && n["time"].s == "" && n["tz_offset"].n == 0, "GET: defaults, unsynced");
        int calls = g_configTimeCalls;
        check(POST("/api/ntp", "{\"host\":\"time.lan\",\"port\":1123,\"tz_offset\":60}").body == "{\"saved\":true}", "POST host/port/tz");
        check(g_configTimeCalls > calls && g_configTimeServer == "time.lan", "NTP restarted with the new server");
        setEpoch(utc(2026, 10, 5, 23, 30)); pump(300);
        n = json(GET("/api/ntp").body);
        check(n["synced"].b && n["host"].s == "time.lan" && n["port"].n == 1123 && n["time"].s == "2026-10-05T23:30:00Z"
              && n["local_time"].s == "2026-10-06T00:30:00+01:00" && n["tz_offset"].n == 60, "synced: UTC and local time");
        POST("/api/ntp?tz_offset=-2000");
        check(json(GET("/api/ntp").body)["tz_offset"].n == -720, "tz clamped to -720");
        POST("/api/ntp", "{\"tz_offset\":60}");
        POST("/api/display/mode", "{\"mode\":\"clock\"}");
        pump(1500);
        check(screen().has("00:30") || screen().has("00:31"), "clock mode shows LOCAL HH:MM");
    });
    boot(d, [] { powerOn(); J n = json(GET("/api/ntp").body); check(n["host"].s == "time.lan" && n["tz_offset"].n == 60, "persists"); });
}

static void testApiSchedule() {
    section("API: schedule");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        J s = json(GET("/api/schedule").body);
        check(s["enabled"].b && !s["respect_lock"].b && s["entries"].a.size() == 10, "GET: on, 10 empty slots");
        J r = json(POST("/api/schedule", "{\"id\":2,\"enabled\":true,\"days\":62,\"hour\":7,\"minute\":30,\"position\":3}").body);
        const J &e = r["entries"][2];
        check(e["enabled"].b && e["days"].n == 62 && e["hour"].n == 7 && e["minute"].n == 30 && e["position"].n == 3, "set slot 2 (JSON)");
        r = json(POST("/api/schedule?id=3&enabled=1&hour=25&minute=70&position=9&days=255").body);
        const J &q = r["entries"][3];
        check(q["enabled"].b && q["hour"].n == 23 && q["minute"].n == 59 && q["position"].n == 7 && q["days"].n == 127, "query form, values clamped");
        r = json(POST("/api/schedule", "{\"id\":2,\"enabled\":false}").body);
        check(!r["entries"][2]["enabled"].b && r["entries"][2]["hour"].n == 7, "partial update keeps the other fields");
        check(POST("/api/schedule", "{\"id\":10}").code == 422 && POST("/api/schedule", "{\"hour\":1}").code == 422, "bad or missing id: 422");
        r = json(POST("/api/schedule", "{\"scheduler_enabled\":false}").body);
        check(!r["enabled"].b && !status()["scheduler_enabled"].b, "master switch off (no id needed)");
        r = json(POST("/api/schedule", "{\"respect_lock\":true,\"scheduler_enabled\":true}").body);
        check(r["respect_lock"].b && r["enabled"].b, "both global flags in one POST");
        r = json(POST("/api/schedule?scheduler_enabled=0").body);
        check(!r["enabled"].b, "query form ?scheduler_enabled=0");
        POST("/api/schedule?scheduler_enabled=1");
        r = json(req(HTTP_DELETE, "/api/schedule?id=3").body);
        check(!r["entries"][3]["enabled"].b && r["entries"][3]["hour"].n == 0, "DELETE ?id=3 clears");
        POST("/api/schedule", "{\"id\":4,\"enabled\":true,\"hour\":1,\"minute\":2,\"position\":1}");
        r = json(GET("/api/schedule?delete=1&id=4").body);
        check(!r["entries"][4]["enabled"].b, "GET ?delete=1&id=4 clears too");
        check(req(HTTP_DELETE, "/api/schedule?id=11").code == 422, "DELETE bad id: 422");
        check(status()["scheduler_active"].b == false, "nothing enabled: scheduler_active false");
        POST("/api/schedule", "{\"id\":0,\"enabled\":true,\"hour\":8,\"minute\":0,\"position\":2}");
        check(status()["scheduler_active"].b, "an enabled entry: scheduler_active true");
    });
    boot(d, [] {
        powerOn();
        J s = json(GET("/api/schedule").body);
        check(s["respect_lock"].b && s["entries"][0]["enabled"].b && s["entries"][2]["minute"].n == 30, "schedule persists");
    });
}

static void testApiMqttConfig() {
    section("API: MQTT config");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        J m = json(GET("/api/mqtt").body);
        check(!m["enabled"].b && m["host"].s == "" && m["port"].n == 1883 && m["retain"].b && !m["commands"].b && !m["discovery"].b
              && m["pass"].s == "" && !m["connected"].b && m["device_name"].s == "UberANT", "GET: defaults");
        check(m["tasmota_topic"].s == "uberant_45CA21", "Tasmota topic from the MAC");
        POST("/api/mqtt", "{\"enabled\":true,\"host\":\"192.168.9.1\",\"port\":1884,\"user\":\"u\",\"pass\":\"p\",\"prefix\":\"shack/ant\",\"retain\":false,\"commands\":true}");
        m = json(GET("/api/mqtt").body);
        check(m["enabled"].b && m["host"].s == "192.168.9.1" && m["port"].n == 1884 && m["user"].s == "u" && m["prefix"].s == "shack/ant"
              && !m["retain"].b && m["commands"].b, "POST: every field");
        check(m["pass"].s == "***", "the password is write-only (***)");
        POST("/api/mqtt", "{\"pass\":\"***\",\"port\":1885}");
        pump(100);
        check(g_mqttPass == "p" && g_mqttPort == 1885 && g_mqttUser == "u", "\"***\" keeps the stored password; partial update keeps the rest");
        check(json(GET("/api/mqtt").body)["connected"].b && status()["mqtt_connected"].b && status()["mqtt_prefix"].s == "shack/ant", "connected; status agrees");
        POST("/api/mqtt", "{\"enabled\":false}");
        pump(100);
        check(!json(GET("/api/mqtt").body)["connected"].b, "disabling disconnects");
    });
    boot(d, [] { powerOn(); J m = json(GET("/api/mqtt").body); check(!m["enabled"].b && m["host"].s == "192.168.9.1" && m["port"].n == 1885, "persists"); });
}

static void testApiMisc() {
    section("API: events, restart, unknown routes");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        check(GET("/api/events").body == "[]", "no events at boot");
        for (int i = 0; i < 30; i++) POST("/api/antenna/up");
        J e = events();
        check(e.a.size() == 25, "event log keeps the last 25");
        HttpResponse r = POST("/api/restart");
        check(r.restarted && r.body == "{\"restarting\":true}" && r.code == 200, "/api/restart answers, then restarts");
        check(GET("/api/nope").code == 404 && GET("/nothing").code == 404, "unknown paths: 404");
    });
}

// =====================================================================================
static void testWebPages() {
    section("WEB: pages and assets");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        HttpResponse r = GET("/");
        check(r.code == 200 && r.type == "text/html" && r.headers["Content-Encoding"] == "gzip" && r.body == g_fs["/app.html.gz"], "\"/\": the modern app, pre-gzipped");
        check(GET("/app").body == r.body, "/app is the same page");
        HttpResponse s = GET("/style.css");
        check(s.type == "text/css" && s.headers["Content-Encoding"] == "gzip", "/style.css (gzip)");
        check(GET("/sw.js").type == "application/javascript" && GET("/manifest.json").type == "application/manifest+json", "/sw.js, /manifest.json");
        check(GET("/favicon.ico").type == "image/x-icon" && GET("/icon-192.png").type == "image/png" && GET("/icon-512.png").body == g_fs["/icon-512.png"], "icons");
        check(GET("/manifest.json").headers.count("Content-Encoding") == 0, "a file without a .gz sibling is served as-is");
        HttpResponse m = GET("/menu");
        check(m.body == g_fs["/portal.html"], "/menu: the portal menu");
        HttpResponse i = GET("/info");
        check(i.code == 200 && i.body.find('%') == std::string::npos, "/info: every %PLACEHOLDER% filled in");
        check(i.body.find("CC:50:E3:45:CA:21") != std::string::npos && i.body.find("uberant") != std::string::npos && i.body.find("Mins") != std::string::npos,
              "/info: MAC, hostname, uptime");
        HttpResponse u = GET("/update");
        check(u.code == 200 && u.body.find("form") != std::string::npos, "/update: OTA page");
        HttpResponse dbg = GET("/debug");
        check(dbg.type == "text/plain" && dbg.body.find("hostname : uberant") != std::string::npos && dbg.body.find("--- log") != std::string::npos,
              "/debug: state + log (the first /debug handler wins)");
    });

    section("WEB: the stock routes (docs/web-interface.md)");
    boot(d, [] {
        powerOn();
        HttpResponse o = GET("/old");
        check(o.body.find("%LABEL%") == std::string::npos && o.body.find("<p>GROUND</p>") != std::string::npos, "/old: label GROUND as <p>GROUND</p>");
        HttpResponse up = GET("/5/on");
        check(pos() == 1 && up.body.find("%LABEL%") == std::string::npos && up.body.find("<p>GROUND</p>") == std::string::npos, "/5/on: Up, page shows the bare number");
        GET("/5/on"); GET("/4/on");
        check(pos() == 1, "/4/on: Dn");
        GET("/5/off"); GET("/4/off");
        check(pos() == 1, "/5/off, /4/off: re-render only");
        check(events()[0]["source"].s == "web", "logged as source web");
    });

    section("WEB: /erase and /restart");
    boot(d, [] { powerOn(); HttpResponse r = GET("/restart"); check(r.restarted && r.body.find("Restarting") != std::string::npos, "/restart"); });
    boot(d, [] { powerOn(); POST("/api/name?position=1&name=Zap"); });
    boot(d, [] {
        powerOn();
        HttpResponse r = GET("/erase");
        check(r.restarted && r.body.find("Erasing") != std::string::npos && EEPROM.mem[0] == 0xFF && g_wm.resetCalls == 1, "/erase: wipes settings + WiFi, restarts");
    });
    boot(d, [] { powerOn(); check(GET("/api/names").body == "{}", "after /erase: factory state"); });
}

// =====================================================================================
static void testButtons() {
    section("BUTTONS (docs/buttons.md)");
    Device d = flashed();
    boot(d, [] {
        powerOn(); pump(300);
        press(PIN_BTN_UP, 100);
        check(pos() == 1 && events()[0]["source"].s == "button", "UP: next antenna, logged as button");
        press(PIN_BTN_UP, 100); press(PIN_BTN_DOWN, 100);
        check(pos() == 1, "DOWN: previous");
        size_t f = g_frames.size();
        press(PIN_BTN_SET, 100);
        check(status()["max"].n == 6 && shown("MAX 6", f), "SET: bumps max, shows MAX 6");
        pump(1500);
        check(screen().has("ANT: 1"), "…then back to the status screen");
        press(PIN_BTN_UP, 3200);
        check(status()["locked"].b && pos() == 1, "UP held 3 s: locks, no step on release");
        f = g_frames.size();
        press(PIN_BTN_UP, 100);
        check(pos() == 1 && shown("LOCKED", f) && shown("Hold UP 3s", f), "locked: UP is refused with a hint");
        press(PIN_BTN_DOWN, 100);
        check(pos() == 1, "locked: DOWN refused");
        press(PIN_BTN_SET, 100);
        check(status()["max"].n == 7, "SET still works while locked (it changes the range, not the antenna)");
        press(PIN_BTN_UP, 3200);
        check(!status()["locked"].b, "UP held 3 s again: unlocks");
        f = g_frames.size();
        press(PIN_BTN_DOWN, 3200);
        check(pos() == 1 && shown("192.168.9.109", f), "DOWN held 3 s: shows the IP, no step");
        f = g_frames.size();
        press(PIN_BTN_ERASE, 1000);
        check(shown("ERASE", f) && EEPROM.mem[0] != 0xFF, "ERASE pressed briefly: prompt only, nothing wiped");
        bool r = press(PIN_BTN_ERASE, 3500);
        check(r && EEPROM.mem[0] == 0xFF && g_wm.resetCalls == 1, "ERASE held 3 s: wipe + restart");
    });
    boot(d, [] {
        powerOn(); pump(300);
        POST("/api/max?value=5");
        POST("/api/schedule", "{\"id\":0,\"enabled\":true,\"hour\":8,\"minute\":0,\"position\":5}");
        POST("/api/max?value=7");
        for (int i = 0; i < 2; i++) press(PIN_BTN_SET, 100);   // 7 -> 0 -> 1
        check(status()["max"].n == 1 && !json(GET("/api/schedule").body)["entries"][0]["enabled"].b, "SET past an entry's port pauses it");
    });
}

static void testScreenSaver() {
    section("SCREEN SAVER");
    Device d = flashed();
    boot(d, [] {
        powerOn(); pump(300);
        POST("/api/display/blank", "{\"enabled\":true,\"timeout\":2}");
        pump(2000);   // boot left 2 s of "WiFi OK" + activity before this
        pump(1000);
        check(screen().blank() || screen().pixels > 0, "idle for the timeout: blanked");
        pump(1000);
        bool dot = false; for (size_t i = g_frames.size() - 4; i < g_frames.size(); i++) dot = dot || g_frames[i].pixels == 4;
        check(dot, "the alive-dot blinks while blank");
        press(PIN_BTN_UP, 100);
        check(screen().has("ANT: 1"), "a button press wakes it");
        pump(3000);
        size_t f = g_frames.size();
        POST("/api/display", "{\"text\":\"Ping\",\"duration\":1}");
        check(shown("Ping", f), "a message shows on a blanked display");
        pump(1500);
        check(screen().blank() || screen().pixels > 0, "…and it re-blanks afterwards");
        POST("/api/display/blank", "{\"enabled\":false}");
        check(screen().has("ANT: 1"), "disabling the saver wakes the display at once");
    });
}

static void testScheduler() {
    section("SCHEDULER in loop() (local time, day after day)");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        POST("/api/ntp", "{\"tz_offset\":60}");
        POST("/api/schedule", "{\"id\":0,\"enabled\":true,\"days\":127,\"hour\":9,\"minute\":0,\"position\":3}");
        pump(65000, 250);
        check(pos() == 0, "NTP not synced: never fires");
        setEpoch(utc(2026, 10, 5, 8, 0) - 30);    // Mon 07:59:30 UTC = 08:59:30 local
        pump(20000, 250);
        check(pos() == 0 && status()["scheduler_next"].s == "09:00 ANT 3", "08:59:50 local: not yet; scheduler_next says 09:00 ANT 3");
        pump(20000, 250);
        check(pos() == 3 && events()[0]["source"].s == "schedule", "09:00 local (08:00 UTC): ANT3, logged as schedule");
        POST("/api/antenna/ground");
        pump(30000, 250);
        check(pos() == 0, "fires once in its minute (a manual change in the same minute sticks)");
        g_millis += (24UL * 3600 - 120) * 1000;    // next day, 08:58 local
        pump(150000, 250);
        check(pos() == 3, "and again the next day");
        POST("/api/antenna/ground");
        POST("/api/lock/toggle");
        POST("/api/schedule", "{\"respect_lock\":true}");
        g_millis += (24UL * 3600 - 120) * 1000;
        pump(150000, 250);
        check(pos() == 0, "respect_lock + locked: does not fire");
        POST("/api/schedule", "{\"respect_lock\":false}");
        g_millis += (24UL * 3600 - 120) * 1000;
        pump(150000, 250);
        check(pos() == 3, "respect_lock off: fires even while locked");
    });
}

static void testMqttApp() {
    section("MQTT end to end (native topics)");
    Device d = flashed();
    boot(d, [] {
        powerOn();
        POST("/api/name?position=2&name=Dipole");
        POST("/api/mqtt", "{\"enabled\":true,\"host\":\"192.168.9.1\",\"commands\":true}");
        pump(100);
        check(g_will == "uberant/uberant/availability" && g_mqttClientId == "uberant-uberant", "connects as uberant-uberant with its will");
        J st; bool got = false;
        for (auto &p : g_pubs) if (p.t == "uberant/uberant/status") got = parseJson(p.p, st);
        check(got && st["position"].n == 0 && st["names"]["2"].s == "Dipole", "status published (same JSON as /api/status)");
        g_pubs.clear();
        mqttInject("uberant/uberant/set", "2");
        check(pos() == 2 && events()[0]["source"].s == "mqtt", "<prefix>/set 2 -> ANT2, logged as mqtt");
        bool pub = false; for (auto &p : g_pubs) pub = pub || (p.t == "uberant/uberant/antenna" && p.p == "2");
        check(pub, "…and the new state is published");
        mqttInject("uberant/uberant/set", "up"); mqttInject("uberant/uberant/set", "down"); mqttInject("uberant/uberant/set", "ground");
        check(pos() == 0, "up / down / ground");
        mqttInject("uberant/uberant/set", "9");
        check(pos() == 0, "out of range: ignored");
        mqttInject("uberant/uberant/max/set", "3");
        check(status()["max"].n == 3, "max/set");
        mqttInject("uberant/uberant/name/set", "{\"position\":1,\"name\":\"Loop\"}");
        check(json(GET("/api/names").body)["1"].s == "Loop", "name/set");
        mqttInject("uberant/uberant/lock", "lock");
        check(status()["locked"].b, "lock");
        mqttInject("uberant/uberant/set", "1");
        check(pos() == 0, "locked: <prefix>/set is refused like every other source");
        mqttInject("uberant/uberant/lock", "unlock");
        size_t f = g_frames.size();
        mqttInject("uberant/uberant/display", "{\"text\":\"From MQTT\",\"duration\":2}");
        check(shown("From MQTT", f), "display");
        bool r = false;
        try { mqttInject("uberant/uberant/restart", "1"); } catch (const EspRestart &) { r = true; }
        check(r, "restart");
    });
    boot(d, [] {
        powerOn(); pump(100);
        check(g_connects == 1, "MQTT settings persist: reconnects after a reboot");
        POST("/api/mqtt", "{\"prefix\":\"\"}");   // re-begins the client from the API path
        pump(100);
        mqttInject("uberant/uberant/name/set", "{\"position\":4,\"name\":\"Beam\"}");
        bool pub = false; for (auto &p : g_pubs) pub = pub || (p.t == "uberant/uberant/status" && p.p.find("Beam") != std::string::npos);
        check(pub, "after a config save, an MQTT rename still republishes the status");
        mqttInject("uberant/uberant/lock", "lock");
        mqttInject("uberant/uberant/set", "2");
        check(pos() == 0, "after a config save the lock still applies to <prefix>/set");
    });

    section("MQTT end to end (Tasmota discovery)");
    boot(d, [] {
        powerOn();
        POST("/api/name?position=1&name=Sky%20Loop");
        POST("/api/max?value=5");
        POST("/api/mqtt", "{\"enabled\":true,\"commands\":false,\"discovery\":true}");
        pump(100);
        J cfg; bool got = false;
        for (auto &p : g_pubs) if (p.t == "tasmota/discovery/CC50E345CA21/config") got = parseJson(p.p, cfg);
        check(got && cfg["t"].s == "uberant_45CA21" && cfg["fn"][0].s == "Sky Loop" && cfg["dn"].s == "UberANT" && cfg["hn"].s == "uberant",
              "discovery: topic, port names, device and host name");
        int relays = 0; for (auto &x : cfg["rl"].a) relays += x.n == 1;
        check(relays == 5, "five relays (max 5)");
        mqttInject("cmnd/uberant_45CA21/POWER4", "ON");
        check(pos() == 4 && lines() == 4 && events()[0]["source"].s == "mqtt", "POWER4 ON -> ANT4 on the decoder");
        g_pubs.clear();
        press(PIN_BTN_UP, 100);
        bool off4 = false, on5 = false;
        for (auto &p : g_pubs) { off4 = off4 || (p.t == "stat/uberant_45CA21/POWER4" && p.p == "OFF"); on5 = on5 || (p.t == "stat/uberant_45CA21/POWER5" && p.p == "ON"); }
        check(off4 && on5, "a button press is reported as POWER4 OFF, POWER5 ON");
        POST("/api/lock/toggle");
        mqttInject("cmnd/uberant_45CA21/POWER1", "ON");
        check(pos() == 5, "locked: POWER refused");
        POST("/api/lock/toggle");
        g_pubs.clear();
        POST("/api/name?position=3&name=Vert");
        got = false;
        for (auto &p : g_pubs) if (p.t == "tasmota/discovery/CC50E345CA21/config") got = parseJson(p.p, cfg);
        check(got && cfg["fn"][2].s == "Vert", "renaming via the API republishes discovery");
        setEpoch(utc(2026, 10, 5, 12, 0)); g_pubs.clear(); pump(300);
        bool info = false; for (auto &p : g_pubs) info = info || (p.t == "uberant/uberant/info" && p.p.find("2026-10-05T12:00") != std::string::npos);
        check(info, "info is republished once NTP syncs (with the time)");
    });
}

int main() {
    testBoot();
    testGestures();
    testHostname();
    testStartupPort();
    testApiStatus();
    testApiAntenna();
    testApiLock();
    testApiMax();
    testApiPorts();
    testApiNames();
    testApiDisplay();
    testApiNtp();
    testApiSchedule();
    testApiMqttConfig();
    testApiMisc();
    testWebPages();
    testButtons();
    testScreenSaver();
    testScheduler();
    testMqttApp();
    return finish("app");
}
