#pragma once
// Host-side PubSubClient stand-in: records the will, subscriptions and publishes, and
// lets a test deliver an inbound message through the registered callback (g_cb).
#include "ESP8266WiFi.h"
#include <vector>
struct Pub { std::string t, p; bool r; };
extern int g_connects;
extern bool g_brokerUp;                                   // false = connect() fails
extern void (*g_cb)(char *, uint8_t *, unsigned int);
extern std::vector<Pub> g_pubs;
extern std::vector<std::string> g_subs;
extern std::string g_will, g_willMsg, g_mqttHost, g_mqttUser, g_mqttPass, g_mqttClientId;
extern uint16_t g_mqttPort;
class PubSubClient {
public:
    bool conn = false;
    void setClient(WiFiClient &) {}
    void setServer(const char *h, uint16_t p) { g_mqttHost = h; g_mqttPort = p; }
    void setBufferSize(int) {}
    void setCallback(void (*c)(char *, uint8_t *, unsigned int)) { g_cb = c; }
    bool connected() { return conn; }
    bool connect(const char *id, const char *u, const char *pw, const char *wt, int, bool, const char *wm) {
        if (!g_brokerUp) return false;
        g_mqttClientId = id; g_mqttUser = u ? u : ""; g_mqttPass = pw ? pw : "";
        g_will = wt; g_willMsg = wm; conn = true; g_connects++; return true;
    }
    void disconnect() { conn = false; }
    int state() { return conn ? 0 : -2; }
    void loop() {}
    bool publish(const char *t, const char *p, bool r) { g_pubs.push_back({t, p, r}); return true; }
    bool subscribe(const char *t) { g_subs.push_back(t); return true; }
};
