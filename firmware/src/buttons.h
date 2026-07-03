// buttons.h — Four active-low push-buttons (UP / DOWN / SET / ERASE).
//
// Replicates the stock button wiring and behaviour (docs/buttons.md,
// docs/hardware-pinout.md): all pins driven HIGH in setup() to enable the internal
// pull-ups; a press pulls the line LOW.
//
//   UP    = GPIO3  (next antenna)
//   DOWN  = GPIO1  (previous antenna)
//   SET   = GPIO0  (set max antenna count)
//   ERASE = GPIO2  (hold to wipe WiFi config)
//
// This class provides debounced level reads plus rising/falling edge helpers so the
// main loop can implement press (edge) and hold (level + timer) gestures exactly like
// the stock firmware.

#pragma once

#include <Arduino.h>
#include "config.h"

enum class Btn : uint8_t { UP, DOWN, SET, ERASE, COUNT };

class Buttons {
public:
    void begin() {
        _pin[(int)Btn::UP]    = PIN_BTN_UP;
        _pin[(int)Btn::DOWN]  = PIN_BTN_DOWN;
        _pin[(int)Btn::SET]   = PIN_BTN_SET;
        _pin[(int)Btn::ERASE] = PIN_BTN_ERASE;

        for (int i = 0; i < (int)Btn::COUNT; i++) {
            // Match the stock setup(): drive HIGH to enable the pull-up. Using
            // INPUT_PULLUP is the Arduino-core equivalent for GPIO0/2/3; GPIO1 (TX)
            // also supports the internal pull-up when used as an input.
#ifdef REPLICA_DEBUG
            // In the serial-debug build, DON'T reconfigure GPIO1/GPIO3 (UART0 TX/RX)
            // as button inputs — doing so kills the serial console. The UP/DOWN
            // buttons won't work in this build, but that's the documented trade-off
            // for having serial logs. (Normal builds configure all four buttons.)
            if (_pin[i] == 1 || _pin[i] == 3) {
                _stable[i] = true; _lastRead[i] = true;
                _prevStable[i] = true; _changedAt[i] = 0;
                continue;
            }
#endif
            pinMode(_pin[i], INPUT_PULLUP);
            _stable[i]    = true;   // released (HIGH) at boot
            _lastRead[i]  = true;
            _prevStable[i]= true;
            _changedAt[i] = 0;
        }
    }

    // Sample all buttons; call once per loop iteration.
    void update() {
        uint32_t now = millis();
        for (int i = 0; i < (int)Btn::COUNT; i++) {
            bool raw = (digitalRead(_pin[i]) == HIGH);  // true = released
            if (raw != _lastRead[i]) {
                _lastRead[i]  = raw;
                _changedAt[i] = now;
            }
            if ((now - _changedAt[i]) >= BTN_DEBOUNCE_MS) {
                _prevStable[i] = _stable[i];
                _stable[i]     = raw;
            } else {
                _prevStable[i] = _stable[i];
            }
        }
    }

    // Debounced level: true while the button is held down (active-low pressed).
    bool pressed(Btn b) const { return _stable[(int)b] == false; }

    // Rising edge of a press (released -> pressed), reported once.
    bool justPressed(Btn b) const {
        return _prevStable[(int)b] == true && _stable[(int)b] == false;
    }

    // Falling edge of a press (pressed -> released), reported once.
    bool justReleased(Btn b) const {
        return _prevStable[(int)b] == false && _stable[(int)b] == true;
    }

    // Raw (undebounced) read — used at boot to sample a held button before the
    // debounce history exists (e.g. UP/ERASE held during power-on).
    bool rawPressed(Btn b) const { return digitalRead(_pin[(int)b]) == LOW; }

private:
    uint8_t  _pin[(int)Btn::COUNT]        = {0};
    bool     _stable[(int)Btn::COUNT]     = {true, true, true, true};
    bool     _prevStable[(int)Btn::COUNT] = {true, true, true, true};
    bool     _lastRead[(int)Btn::COUNT]   = {true, true, true, true};
    uint32_t _changedAt[(int)Btn::COUNT]  = {0};
};
