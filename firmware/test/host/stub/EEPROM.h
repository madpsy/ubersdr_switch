#pragma once
// Host-side EEPROM stand-in: a 4 KB sector that starts erased (0xFF), like fresh flash.
// Records the size begin() opened, commits, and any access outside that size (on the
// ESP8266 such an access is silently lost — here it is counted so a test can fail).
#include "Arduino.h"
struct EEPROMClass {
    uint8_t mem[4096];
    int size = 0, commits = 0, outOfRange = 0;
    EEPROMClass() { memset(mem, 0xFF, sizeof mem); }
    void begin(int n) { size = n; }
    uint8_t read(int a) { if (a < 0 || a >= size) { outOfRange++; return 0; } return mem[a]; }
    void write(int a, uint8_t v) { if (a < 0 || a >= size) { outOfRange++; return; } mem[a] = v; }
    bool commit() { commits++; return true; }
};
extern EEPROMClass EEPROM;
