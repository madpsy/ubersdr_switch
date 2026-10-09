#pragma once
// Host-side mDNS stand-in: records the advertised name and services.
#include "Arduino.h"
#include <vector>
struct MDNSResponder {
    String name; bool running = false; std::vector<std::string> services;
    bool begin(const char *n) { name = n; running = true; return true; }
    void end() { running = false; services.clear(); }
    void addService(const char *s, const char *p, int port) { services.push_back(std::string(s) + "/" + p + ":" + std::to_string(port)); }
    void update() {}
};
extern MDNSResponder MDNS;
