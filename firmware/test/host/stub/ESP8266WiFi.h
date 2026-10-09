#pragma once
// Host-side WiFi stand-in. A "router" (routerSsid/routerPass) exists; WiFi.begin()
// with matching credentials connects. savedSsid/savedPass model the credentials the
// ESP8266 SDK keeps in flash (WiFiManager's saved network).
#include "Arduino.h"

#define WL_IDLE_STATUS 0
#define WL_DISCONNECTED 6
#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
#define WIFI_NONE_SLEEP 0

struct IPAddress {
    uint8_t b[4] = {0, 0, 0, 0};
    IPAddress() {}
    IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
    String toString() const { char t[16]; snprintf(t, sizeof t, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); return t; }
    bool operator==(const IPAddress &o) const { return !memcmp(b, o.b, 4); }
    bool operator!=(const IPAddress &o) const { return !(*this == o); }
    explicit operator uint32_t() const { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
};

struct WiFiClass {
    int st = WL_DISCONNECTED;
    String routerSsid = "Shack", routerPass = "secret";
    String savedSsid, savedPass;      // SDK-persisted credentials
    String ssid;                      // currently joined network
    String host;
    IPAddress ip{192, 168, 9, 109};
    int beginCalls = 0;

    int status() { return st; }
    bool mode(int) { return true; }
    bool setSleepMode(int) { return true; }
    void begin(const char *s, const char *p) {
        beginCalls++;
        savedSsid = s; savedPass = p;
        if (routerSsid == s && routerPass == p) { st = WL_CONNECTED; ssid = s; }
    }
    void begin() {
        beginCalls++;
        if (savedSsid.length() && routerSsid == savedSsid && routerPass == savedPass) { st = WL_CONNECTED; ssid = savedSsid; }
    }
    bool disconnect(bool wipe = false) { st = WL_DISCONNECTED; ssid = ""; if (wipe) { savedSsid = ""; savedPass = ""; } return true; }
    String macAddress() { return "CC:50:E3:45:CA:21"; }
    uint8_t *macAddress(uint8_t *m) { const uint8_t v[6] = {0xCC, 0x50, 0xE3, 0x45, 0xCA, 0x21}; memcpy(m, v, 6); return m; }
    String softAPmacAddress() { return "CE:50:E3:45:CA:21"; }
    bool hostname(const String &h) { host = h; return true; }
    String hostname() { return host; }
    IPAddress localIP() { return st == WL_CONNECTED ? ip : IPAddress(); }
    IPAddress gatewayIP() { return IPAddress(192, 168, 9, 1); }
    IPAddress subnetMask() { return IPAddress(255, 255, 255, 0); }
    IPAddress dnsIP() { return IPAddress(192, 168, 9, 1); }
    IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
    int RSSI() { return -58; }
    String SSID() { return st == WL_CONNECTED ? ssid : savedSsid; }
    String BSSIDstr() { return "AA:BB:CC:DD:EE:FF"; }
    int channel() { return 6; }
};
extern WiFiClass WiFi;
class WiFiClient {};
