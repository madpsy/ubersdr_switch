#pragma once
// Host-side I2C stand-in: one device (the OLED) answers at g_i2cDevice.
#include "Arduino.h"
extern uint8_t g_i2cDevice;
struct TwoWire {
    uint8_t _addr = 0;
    void begin(int, int) {}
    void setClock(uint32_t) {}
    void beginTransmission(uint8_t a) { _addr = a; }
    uint8_t endTransmission() { return (g_i2cDevice && _addr == g_i2cDevice) ? 0 : 2; }
};
extern TwoWire Wire;
