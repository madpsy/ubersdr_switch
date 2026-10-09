#pragma once
// Host-side WiFiManager stand-in. autoConnect() connects when the SDK's saved
// credentials reach the router; otherwise it "opens the portal" (calls the AP
// callback) and returns portalResult — what a user did in the captive portal.
#include "ESP8266WiFi.h"
class WiFiManager {
public:
    bool portalResult = false;          // true = the user saved working credentials
    int  autoConnectCalls = 0, startPortalCalls = 0, resetCalls = 0, processCalls = 0;
    String apName, hostName;
    int connectTimeout = -1, connectRetries = -1; unsigned long portalTimeout = 1; bool blocking = false;

    void setHostname(const char *h) { hostName = h; }
    void setAPCallback(void (*cb)(WiFiManager *)) { _ap = cb; }
    void setWebServerCallback(std::function<void()> cb) { _ws = cb; }
    void setSaveConfigCallback(std::function<void()> cb) { _save = cb; }
    void setConnectTimeout(int s) { connectTimeout = s; }
    void setConnectRetries(int n) { connectRetries = n; }
    void setConfigPortalBlocking(bool b) { blocking = b; }
    void setConfigPortalTimeout(unsigned long t) { portalTimeout = t; }
    bool autoConnect(const char *ap) {
        autoConnectCalls++; apName = ap;
        WiFi.begin();
        if (WiFi.status() == WL_CONNECTED) return true;
        return portal();
    }
    bool startConfigPortal(const char *ap) { startPortalCalls++; apName = ap; return portal(); }
    void resetSettings() { resetCalls++; WiFi.savedSsid = ""; WiFi.savedPass = ""; }
    void process() { processCalls++; }
private:
    bool portal() {
        if (_ap) _ap(this);
        if (_ws) _ws();
        if (portalResult) {
            if (_save) _save();
            WiFi.begin(WiFi.routerSsid.c_str(), WiFi.routerPass.c_str());
            return WiFi.status() == WL_CONNECTED;
        }
        return false;
    }
    void (*_ap)(WiFiManager *) = nullptr;
    std::function<void()> _ws, _save;
};
