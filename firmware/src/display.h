// display.h — Heltec WiFi Kit 8 (HTIT-W8266) 0.91" 128x32 I2C OLED rendering
// (Adafruit SSD1306 + GFX).
//
// Reproduces the stock on-screen behaviour (docs/manual-setup.md, docs/buttons.md):
//
//   * Normal:   antenna label ("GROUND" / "ANT: n") + the device web address
//               ("http://(IP unset)" until an IP is assigned).
//   * SET:      "MAX n"
//   * ERASE:    "press ERASE for erase stored information" / "ERASE" / "hold the button"
//               with a countdown, and the AP-mode SSID/URL screen.
//
// HARDWARE (Heltec WiFi Kit 8 / HTIT-W8266):
//   * Controller : SSD1306 (0.91" 128x32 panel).
//   * I2C        : SDA = GPIO4, SCL = GPIO5, address 0x3C.
//   * RESET      : GPIO16. The Heltec board wires the SSD1306 RES line to GPIO16, and
//                  the panel stays FULLY BLANK unless GPIO16 is pulsed LOW then HIGH
//                  before begin() (see hwReset()). Treating the panel as a "no reset
//                  pin" module is the number-one cause of a dead Heltec OLED.
//   * GEOMETRY   : 128x32. Initialising it as 128x64 produces a garbled/blank display
//                  and pushes half the text off-screen.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "config.h"
#include "debuglog.h"

// Heltec WiFi Kit 8: 0.91" 128x32 SSD1306 over I2C, RESET on GPIO16 (see config.h).
#define OLED_W    128
#define OLED_H    32
#define OLED_RST  PIN_OLED_RST

class Display {
public:
    // Scan the I2C bus and return the first responding address (0 if none).
    uint8_t scanI2C() {
        for (uint8_t a = 1; a < 127; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) return a;
        }
        return 0;
    }

    // Heltec WiFi Kit 8 OLED hardware reset (GPIO16). Must run before the SSD1306
    // init or the panel never leaves reset and stays blank. Datasheet-style pulse:
    // hold RES LOW for a few ms, then release HIGH and let it settle.
    void hwReset() {
        pinMode(PIN_OLED_RST, OUTPUT);
        digitalWrite(PIN_OLED_RST, HIGH);
        delay(1);
        digitalWrite(PIN_OLED_RST, LOW);   // enter reset
        delay(10);
        digitalWrite(PIN_OLED_RST, HIGH);  // leave reset
        delay(10);
        LOG("OLED: GPIO16 hardware reset pulsed");
    }

    // I2C bus recovery. If a slave was mid-transfer when the master got reset, the
    // panel can be left holding SDA LOW — making every subsequent transaction fail.
    // Standard fix: clock SCL until the slave releases SDA, then issue a STOP.
    void busClear() {
        pinMode(PIN_OLED_SDA, INPUT_PULLUP);
        pinMode(PIN_OLED_SCL, INPUT_PULLUP);
        delayMicroseconds(20);
        if (digitalRead(PIN_OLED_SDA) == HIGH) return;   // bus is free
        LOG("I2C: SDA stuck LOW - clocking bus free");
        pinMode(PIN_OLED_SCL, OUTPUT_OPEN_DRAIN);
        digitalWrite(PIN_OLED_SCL, HIGH);
        for (int i = 0; i < 16 && digitalRead(PIN_OLED_SDA) == LOW; i++) {
            digitalWrite(PIN_OLED_SCL, LOW);  delayMicroseconds(10);
            digitalWrite(PIN_OLED_SCL, HIGH); delayMicroseconds(10);
        }
        // STOP condition: SDA LOW -> HIGH while SCL is HIGH.
        pinMode(PIN_OLED_SDA, OUTPUT_OPEN_DRAIN);
        digitalWrite(PIN_OLED_SDA, LOW);  delayMicroseconds(10);
        digitalWrite(PIN_OLED_SCL, HIGH); delayMicroseconds(10);
        digitalWrite(PIN_OLED_SDA, HIGH); delayMicroseconds(10);
        pinMode(PIN_OLED_SDA, INPUT_PULLUP);
        pinMode(PIN_OLED_SCL, INPUT_PULLUP);
        LOGF("I2C: bus clear done, SDA=%d", digitalRead(PIN_OLED_SDA));
    }

    void begin() {
        // Heltec OLED reset FIRST (GPIO16), then free a wedged bus and start I2C.
        hwReset();
        busClear();
        Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
        Wire.setClock(400000);
        _addr = scanI2C();          // usually 0x3C for these OLEDs
        LOGF("I2C scan: device at 0x%02X", _addr);
        _i2caddr = _addr ? _addr : 0x3C;

        // Pass OLED_RST to the driver too, so its own begin() reset sequence targets
        // GPIO16 rather than assuming no reset pin.
        _oled = new Adafruit_SSD1306(OLED_W, OLED_H, &Wire, OLED_RST);
        _ok = _oled->begin(SSD1306_SWITCHCAPVCC, _i2caddr, false, true);
        LOGF("OLED: SSD1306 begin() = %s @ 0x%02X (128x32)",
             _ok ? "OK" : "FAILED", _i2caddr);

        _oled->setTextColor(SSD1306_WHITE);
        _oled->cp437(true);

        showBanner();
    }

    // Boot splash: display the device name in the largest bold text that fills the
    // 128x32 panel. Auto-sizes from 3 down to 1 so long names still fit. Faux-bold
    // by over-printing with a 1px x-offset.
    void showSplash(const char *name = "UberSDR") {
        if (!_oled) return;
        if (!name || !name[0]) name = "UberSDR";

        // Pick the largest text size (3→2→1) where the text fits the panel width.
        uint8_t size = 3;
        for (; size > 1; size--) {
            int w = (int)strlen(name) * 6 * size;
            if (w <= OLED_W) break;
        }
        int glyphH = 8 * size;
        int textW  = (int)strlen(name) * 6 * size;
        int x = (OLED_W - textW) / 2;
        if (x < 0) x = 0;
        int y = (OLED_H - glyphH) / 2;
        if (y < 0) y = 0;

        _oled->clearDisplay();
        _oled->setTextSize(size);
        // Faux-bold: draw the string twice, offset by 1px horizontally.
        _oled->setCursor(x, y);      _oled->print(name);
        _oled->setCursor(x + 1, y);  _oled->print(name);
        _oled->display();
    }

    // Power-on self-test frame so a blank/mis-wired panel is obviously distinguishable
    // from a working one: a border + banner text (sized for 128x32).
    void showBanner() {
        if (!_oled) return;
        _oled->clearDisplay();
        _oled->drawRect(0, 0, OLED_W, OLED_H, SSD1306_WHITE);
        drawTextCentered("ANTENNA", 4, 2);
        drawTextCentered("SELECTOR V2", 22, 1);
        _oled->display();
    }

    // Main status screen: antenna label (as BIG as fits) + web address underneath.
    // The URL is long, so it stays at size 1 on the bottom row; the short antenna
    // label is scaled up to fill the remaining top area of the 128x32 panel.
    void showStatus(const String &label, const String &url) {
        if (!_oled) return;
        _oled->clearDisplay();

        const char *u = url.length() ? url.c_str() : STR_IP_UNSET;
        const int urlH = 8;                    // size-1 glyph height
        const int urlY = OLED_H - urlH;        // bottom row (y=24)

        // Largest label size whose width fits the panel and whose height fits the
        // space above the URL row. Try size 3 (24px) down to 1.
        uint8_t size = 3;
        for (; size > 1; size--) {
            int w = (int)label.length() * 6 * size;
            int h = 8 * size;
            if (w <= OLED_W && h <= urlY) break;   // fits width AND above the URL
        }
        int labelH = 8 * size;
        int labelY = (urlY - labelH) / 2;          // vertically centre in the top area
        if (labelY < 0) labelY = 0;

        drawTextCentered(label.c_str(), labelY, size);
        drawTextCentered(u, urlY, 1);
        _oled->display();
    }

    // SET button: "MAX n".
    void showMax(int n) {
        if (!_oled) return;
        _oled->clearDisplay();
        String s = String(STR_MAX) + " " + String(n);
        drawTextCentered(s.c_str(), 8, 3);
        _oled->display();
    }

    // ERASE prompt with countdown (docs/manual-setup.md):
    //   "press ERASE for" / "erase stored info" / "<countdown>"
    void showErasePrompt(int countdown) {
        if (!_oled) return;
        _oled->clearDisplay();
        drawTextCentered("press ERASE for", 0, 1);
        drawTextCentered("erase stored info", 10, 1);
        drawTextCentered(String(countdown).c_str(), 20, 1);
        _oled->display();
    }

    // Shown while the ERASE button is held: "ERASE" / "hold the button".
    void showEraseHold() {
        if (!_oled) return;
        _oled->clearDisplay();
        drawTextCentered(STR_ERASE, 2, 2);
        drawTextCentered(STR_HOLD_BUTTON, 22, 1);
        _oled->display();
    }

    // AP / config-portal screen: "ssid: AutoConnectAP" / "http://192.168.4.1".
    void showAP(const String &ssid, const String &url) {
        if (!_oled) return;
        _oled->clearDisplay();
        drawTextCentered((String("ssid: ") + ssid).c_str(), 6, 1);
        drawTextCentered(url.c_str(), 20, 1);
        _oled->display();
    }

    // Shown while UP is held at boot to force AP/config mode (docs/manual-setup.md
    // step 3): "hold the button" / "AutoConnect (+)".
    void showForceAP() {
        if (!_oled) return;
        _oled->clearDisplay();
        drawTextCentered("hold the button", 6, 1);
        drawTextCentered("AutoConnect (+)", 20, 1);
        _oled->display();
    }

    void showMessage(const String &line1, const String &line2 = "") {
        if (!_oled) return;
        _oled->clearDisplay();
        if (line2.length()) {
            drawTextCentered(line1.c_str(), 6, 1);
            drawTextCentered(line2.c_str(), 20, 1);
        } else {
            drawTextCentered(line1.c_str(), 12, 1);
        }
        _oled->display();
    }

private:
    // Draw text horizontally centred at the given top y, using the built-in GFX font
    // (6x8 px per glyph at size 1, scaled by `size`). GFX draws from the cursor as the
    // TOP-LEFT corner, so we compute X purely from the glyph count.
    void drawTextCentered(const char *text, int y, uint8_t size) {
        _oled->setTextSize(size);
        int glyphW = 6 * size;                 // advance width of the built-in font
        int textW  = (int)strlen(text) * glyphW;
        int x = (OLED_W - textW) / 2;
        if (x < 0) x = 0;
        _oled->setCursor(x, y);
        _oled->print(text);
    }

    Adafruit_SSD1306 *_oled = nullptr;
    uint8_t  _addr    = 0;
    uint8_t  _i2caddr = 0x3C;
    bool     _ok      = false;
};
