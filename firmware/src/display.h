// display.h — Heltec WiFi Kit 8 (HTIT-W8266) 0.91" 128x32 I2C OLED rendering
// (Adafruit SSD1306 + GFX).
//
// Reproduces the stock on-screen behaviour (docs/manual-setup.md, docs/buttons.md):
//
//   * Normal:   antenna label ("GROUND" / "ANT: n") + device name + clock underneath.
//   * SET:      "MAX n"
//   * ERASE:    "press ERASE for erase stored information" / "ERASE" / "hold the button"
//               with a countdown, and the AP-mode SSID/URL screen.
//
// Three display modes (selectable in Settings → Display):
//   Port mode  (default): antenna label is the large text; device name + clock is small.
//   Clock mode:           time is the large text; antenna label + device name is small.
//   Cycle mode:           alternates between Port and Clock every CYCLE_INTERVAL_MS (5 s).
//                         A custom message via /api/display pauses the cycle for its
//                         duration; the cycle resumes (from the same phase) afterwards.
//
// In all modes, when the antenna position changes, the display temporarily switches to
// Port layout for PORT_CHANGE_SHOW_MS (2 s) so the user sees clear confirmation of the
// switch, then reverts to the configured mode.
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
//
// FONTS:
//   Large/prominent text (splash, antenna label, clock, banner, MAX) uses proportional
//   TrueType-derived GFX fonts for a smoother appearance:
//     FreeSansBold12pt7b — ~18 px cap-height, used for short strings (≤6 chars)
//     FreeSansBold9pt7b  — ~13 px cap-height, used for longer strings or when 12pt
//                          doesn't fit the available vertical space
//   Both are included with the Adafruit GFX Library (no extra dependency).
//   Small/status text (bottom row, AP screen, erase prompts) keeps the built-in 6×8
//   bitmap font (size 1) — blockiness is imperceptible at that scale.
//
//   drawLarge() measures actual pixel width via getTextBounds() so centering is exact
//   for proportional glyphs, then restores the default font for subsequent small text.

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "config.h"
#include "debuglog.h"
#include "settings.h"

// Heltec WiFi Kit 8: 0.91" 128x32 SSD1306 over I2C, RESET on GPIO16 (see config.h).
#define OLED_W    128
#define OLED_H    32
#define OLED_RST  PIN_OLED_RST

// How long (ms) to show the port-prominent layout after an antenna change, even in
// clock mode, so the user gets clear visual confirmation of the switch.
static const uint32_t PORT_CHANGE_SHOW_MS = 2000;

// How long (ms) each sub-mode is shown in Cycle mode before switching.
static const uint32_t CYCLE_INTERVAL_MS = 5000;

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

    // Boot splash: display the device name in the largest smooth font that fills the
    // 128x32 panel. Cascades: FreeSansBold12pt7b → FreeSansBold9pt7b → default size 2
    // → default size 1, picking the first that fits the panel width.
    void showSplash(const char *name = "UberSDR") {
        if (!_oled) return;
        if (!name || !name[0]) name = "UberSDR";

        _oled->clearDisplay();

        // Try each font/size in descending preference; use the first that fits width.
        // GFX custom fonts: measure with getTextBounds (proportional).
        // Fallback default font: measure with strlen * 6 * size (monospace).
        struct { const GFXfont *font; uint8_t defSize; } cascade[] = {
            { &FreeSansBold12pt7b, 0 },
            { &FreeSansBold9pt7b,  0 },
            { nullptr,             2 },
            { nullptr,             1 },
        };

        int16_t bx, by; uint16_t bw, bh;
        for (auto &c : cascade) {
            int textW, textH;
            if (c.font) {
                _oled->setFont(c.font);
                _oled->getTextBounds(name, 0, 0, &bx, &by, &bw, &bh);
                textW = (int)bw;
                textH = (int)bh;
            } else {
                _oled->setFont(nullptr);
                textW = (int)strlen(name) * 6 * c.defSize;
                textH = 8 * c.defSize;
                _oled->setTextSize(c.defSize);
            }
            if (textW <= OLED_W) {
                int x, y;
                if (c.font) {
                    // GFX custom fonts: cursor is at the baseline, not the top.
                    // bx is the left-edge offset of the bounding box from cursor x=0.
                    // Centre the bounding box; cursor x = centreX - bx.
                    x = (OLED_W - textW) / 2 - bx;
                    if (x < 0) x = 0;
                    // by is negative (distance from baseline up to top of bounding box).
                    int centreY = (OLED_H - textH) / 2;
                    if (centreY < 0) centreY = 0;
                    y = centreY - by;
                } else {
                    x = (OLED_W - textW) / 2;
                    if (x < 0) x = 0;
                    y = (OLED_H - textH) / 2;
                    if (y < 0) y = 0;
                }
                _oled->setCursor(x, y);
                _oled->print(name);
                break;
            }
        }

        _oled->setFont(nullptr);   // restore default font
        _oled->setTextSize(1);
        _oled->display();
    }

    // Power-on self-test frame so a blank/mis-wired panel is obviously distinguishable
    // from a working one: a border + banner text (sized for 128x32).
    // "ANTENNA" uses FreeSansBold9pt7b; "SELECTOR V2" uses the default size-1 font.
    void showBanner() {
        if (!_oled) return;
        _oled->clearDisplay();
        _oled->drawRect(0, 0, OLED_W, OLED_H, SSD1306_WHITE);

        // "ANTENNA" — smooth font, centred in the top ~22px.
        drawLarge("ANTENNA", 3, 20, &FreeSansBold9pt7b);

        // "SELECTOR V2" — small default font on the bottom row.
        drawSmall("SELECTOR V2", 23);

        _oled->display();
    }

    // Inform the display of the current lock state so the padlock indicator can be
    // drawn. Call whenever the lock state changes (from main.cpp).
    void setLocked(bool locked) {
        if (_locked != locked) {
            _locked = locked;
            // Force a redraw on the next tick so the padlock appears/disappears.
            if (!_blanked && !customActive()) {
                _redrawStatus();
            }
        }
    }

    // Notify the display that the antenna position just changed. In clock mode this
    // triggers a 2-second override showing the port-prominent layout so the user gets
    // clear visual confirmation of the switch. In port mode this is a no-op (the label
    // is already prominent). Call at every antenna-change site in main.cpp.
    void notifyPortChange() {
        _portChangedAt = millis();
        // Force an immediate redraw so the new label appears without waiting for the
        // next tickStatus() call.
        if (!_blanked && !customActive()) {
            _redrawStatus();
        }
    }

    // Main status screen: antenna label + device name/clock underneath.
    // Stores the label for use by tickStatus() and redraws immediately.
    void showStatus(const String &label, const String &url) {
        if (!_oled) return;
        // Store for the tick loop (url kept for potential future use but not shown).
        _statusLabel = label;
        _statusUrl   = url;
        // Reset scroll state.
        _scrollPx     = 0;
        _lastScrollMs = millis();
        _redrawStatus();
    }

    // Display a custom message on the OLED for `durationMs` milliseconds (default 1000).
    //
    // Multi-line: split on '\n' (or the two-char sequence "\\n" from JSON strings).
    // Auto-sizes to the largest font where ALL lines fit the panel width AND the total
    // block height fits the 32px panel. Font cascade (largest to smallest):
    //   FreeSansBold12pt7b: ~18px/line — fits 1 line
    //   FreeSansBold9pt7b:  ~13px/line — fits 1–2 lines
    //   default size 2:     16px/line  — fits 2 lines, max 10 chars/line
    //   default size 1:      8px/line  — fits 4 lines, max 21 chars/line
    // Lines that are still too wide at default size 1 are truncated with '>'.
    // `align`: 'L' = left, 'R' = right, anything else = centre (default).
    // `wasBlank`: pass true if the display was blanked when this call was triggered
    // (checked by the caller before any unblank side-effects). When true, the display
    // re-blanks immediately after the message duration expires.
    void showCustomMessage(const String &text, char align = 'C', uint32_t durationMs = 1000,
                           bool wasBlank = false) {
        if (!_oled) return;
        if (durationMs < 100) durationMs = 100;
        _reblankAfterCustom = wasBlank;
        _blanked    = false;              // ensure display is active while message shows
        _customUntil = millis() + durationMs;

        // Normalise "\\n" (two chars: backslash + n) -> '\n' so JSON payloads work.
        String src = text;
        src.replace("\\n", "\n");

        // Split into lines (up to 4 — the max at default size 1).
        String lines[4];
        uint8_t nLines = 0;
        int start = 0;
        for (int i = 0; i <= (int)src.length() && nLines < 4; i++) {
            if (i == (int)src.length() || src[i] == '\n') {
                lines[nLines++] = src.substring(start, i);
                start = i + 1;
            }
        }
        if (nLines == 0) nLines = 1;

        _oled->clearDisplay();

        // --- Font cascade for custom messages ---
        // Try GFX fonts first (proportional), then default bitmap font sizes.
        // For GFX fonts we measure each line with getTextBounds.
        struct FontOption {
            const GFXfont *font;
            uint8_t        defSize;   // 0 = use GFX font
            int            lineH;     // approximate line height in px
        };
        FontOption opts[] = {
            { &FreeSansBold12pt7b, 0, 20 },
            { &FreeSansBold9pt7b,  0, 15 },
            { nullptr,             2, 16 },
            { nullptr,             1,  8 },
        };

        int16_t bx, by; uint16_t bw, bh;
        bool rendered = false;

        for (auto &opt : opts) {
            // Check all lines fit width and total block fits height.
            int blockH = (int)nLines * opt.lineH;
            if (blockH > OLED_H) continue;

            bool allFit = true;
            for (uint8_t i = 0; i < nLines; i++) {
                int lw;
                if (opt.font) {
                    _oled->setFont(opt.font);
                    _oled->getTextBounds(lines[i].c_str(), 0, 0, &bx, &by, &bw, &bh);
                    lw = (int)bw;
                } else {
                    lw = (int)lines[i].length() * 6 * opt.defSize;
                }
                if (lw > OLED_W) { allFit = false; break; }
            }
            if (!allFit) continue;

            // This option fits — render all lines.
            if (!opt.font) {
                _oled->setFont(nullptr);
                _oled->setTextSize(opt.defSize);
            }
            int startY = (OLED_H - blockH) / 2;
            if (startY < 0) startY = 0;

            for (uint8_t i = 0; i < nLines; i++) {
                String &ln = lines[i];
                // Truncate with '>' if still too wide at default size 1.
                if (!opt.font && opt.defSize == 1 && (int)ln.length() * 6 > OLED_W) {
                    ln = ln.substring(0, 20) + ">";
                }
                int lw, cursorY;
                if (opt.font) {
                    _oled->setFont(opt.font);
                    _oled->getTextBounds(ln.c_str(), 0, 0, &bx, &by, &bw, &bh);
                    lw = (int)bw;
                    // Cursor y = top of line area - by (by is negative = offset to top).
                    cursorY = startY + (int)i * opt.lineH - by;
                } else {
                    lw = (int)ln.length() * 6 * opt.defSize;
                    cursorY = startY + (int)i * opt.lineH;
                }
                int x;
                if      (align == 'L') x = 0;
                else if (align == 'R') x = OLED_W - lw;
                else                   x = (OLED_W - lw) / 2;
                if (x < 0) x = 0;
                _oled->setCursor(x, cursorY);
                _oled->print(ln.c_str());
            }
            rendered = true;
            break;
        }

        _oled->setFont(nullptr);
        _oled->setTextSize(1);

        if (!rendered) {
            // Absolute fallback: print first line truncated at size 1.
            String ln = lines[0].substring(0, 20);
            int lw = (int)ln.length() * 6;
            int x = (OLED_W - lw) / 2;
            _oled->setCursor(x < 0 ? 0 : x, 12);
            _oled->print(ln.c_str());
        }

        _oled->display();
    }

    // Returns true while a custom message is being shown.
    bool customActive() const {
        return _customUntil != 0 && (int32_t)(millis() - _customUntil) < 0;
    }

    // Returns true while the display is blanked (screen-saver active).
    bool isBlank() const { return _blanked; }

    // Blank the display immediately (screen-saver on). A single pixel dot flashes
    // in the bottom-left corner at ~1 Hz so the user knows the device is alive.
    void blankDisplay() {
        if (!_oled) return;
        _blanked    = true;
        _dotOn      = false;
        _lastDotMs  = millis();
        _oled->clearDisplay();
        _oled->display();
    }

    // Wake the display from blank. Redraws the status screen immediately.
    void unblankDisplay() {
        if (!_blanked) return;
        _blanked = false;
        _redrawStatus();
    }

    // Call every ~250 ms from loop() while blanked. Flashes a 2×2 dot in the
    // bottom-left corner at ~1 Hz to show the device is alive.
    void tickBlank() {
        if (!_oled || !_blanked) return;
        uint32_t now = millis();
        if ((int32_t)(now - _lastDotMs) < 500) return;
        _lastDotMs = now;
        _dotOn = !_dotOn;
        _oled->clearDisplay();
        if (_dotOn) {
            // 2×2 pixel dot at bottom-left (x=0, y=30).
            _oled->drawPixel(0, OLED_H - 2, SSD1306_WHITE);
            _oled->drawPixel(1, OLED_H - 2, SSD1306_WHITE);
            _oled->drawPixel(0, OLED_H - 1, SSD1306_WHITE);
            _oled->drawPixel(1, OLED_H - 1, SSD1306_WHITE);
        }
        _oled->display();
    }

    // Call every ~250 ms from loop() while in normal station mode.
    //
    // Port mode:  large antenna label on top; "deviceName  HH:MM:SS" on the bottom row.
    // Clock mode: large "HH:MM:SS" on top; "antLabel  deviceName" on the bottom row.
    // Cycle mode: alternates between Port and Clock every CYCLE_INTERVAL_MS (5 s).
    //             A custom message pauses the cycle; it resumes from the same phase.
    //
    // Port-change override: for PORT_CHANGE_SHOW_MS after notifyPortChange() is called,
    // always renders in port-prominent layout regardless of the configured mode, so the
    // user sees clear confirmation of the switch.
    //
    // `deviceName` and `timeStr` ("HH:MM:SS" local, or "" if NTP not synced) are
    // passed in from main.cpp so display.h stays dependency-free.
    // `displayMode`: the current DisplayMode (Port / Clock / Cycle).
    // No-ops while a custom message is active or the display is blanked.
    void tickStatus(const String &deviceName, const String &timeStr, DisplayMode displayMode) {
        if (!_oled) return;
        if (_blanked) return;

        uint32_t now = millis();

        // Determine whether a port-change override is active (forces Port layout for 2 s).
        bool portOverrideActive = (_portChangedAt != 0 &&
                                   (int32_t)(now - _portChangedAt) < (int32_t)PORT_CHANGE_SHOW_MS);

        // Freeze the cycle timer whenever the display is "interrupted" by something that
        // takes over the screen: a custom message OR a port-change override. This ensures
        // the 5 s cycle doesn't silently consume time while the user is reading something
        // else, so each phase always gets a full 5 s of uninterrupted display time.
        if (displayMode == DisplayMode::Cycle && (customActive() || portOverrideActive)) {
            _cycleStartMs = now - _cyclePhaseMs;
        }

        if (customActive()) {
            return;
        }
        // Custom message just expired.
        if (_customUntil) {
            _customUntil = 0;
            if (_reblankAfterCustom) {
                _reblankAfterCustom = false;
                blankDisplay();
                return;
            }
            _redrawStatus(deviceName, timeStr, displayMode);
        }

        // --- Cycle mode: advance phase and detect sub-mode transitions ---
        bool cycleJustFlipped = false;
        if (displayMode == DisplayMode::Cycle) {
            uint32_t elapsed = now - _cycleStartMs;
            bool newPhase = (elapsed / CYCLE_INTERVAL_MS) & 1;  // 0=Port, 1=Clock
            if (newPhase != _cyclePhase) {
                _cyclePhase = newPhase;
                cycleJustFlipped = true;
                // Reset scroll state on phase flip so the new layout starts clean.
                _scrollPx      = 0;
                _lastScrollMs  = now;
                _clockScrollPx = 0;
                _clockScrollMs = now;
            }
            // Keep _cyclePhaseMs updated so freeze logic stays accurate.
            _cyclePhaseMs = elapsed % CYCLE_INTERVAL_MS;
        }

        // Resolve the effective clock-mode flag from the display mode.
        bool clockMode = _effectiveClockMode(displayMode);

        // Port-change override suppresses clock layout (portOverrideActive already computed above).
        bool effectiveClockMode = clockMode && !portOverrideActive;

        // Decide whether to redraw this tick.
        bool redraw = cycleJustFlipped;

        // Clock ticks every second (both modes show the time somewhere).
        if (timeStr.length()) {
            if ((int32_t)(now - _lastClockMs) >= 1000) {
                _lastClockMs = now;
                redraw = true;
            }
        }

        // Port-change override expiry: redraw once when the 2s window closes.
        if (_portChangedAt != 0 && !portOverrideActive) {
            _portChangedAt = 0;
            redraw = true;
        }

        // Bottom-line scroll — both modes may need it (default font, fixed 6px/char).
        {
            String bottom = effectiveClockMode
                ? _clockBottomLine(_statusLabel, deviceName)
                : _bottomLine(deviceName, timeStr);
            int textW = (int)bottom.length() * 6;
            int &scrollPx  = effectiveClockMode ? _clockScrollPx  : _scrollPx;
            uint32_t &scrollMs = effectiveClockMode ? _clockScrollMs : _lastScrollMs;
            if (textW > OLED_W) {
                if ((int32_t)(now - scrollMs) >= 40) {
                    scrollMs = now;
                    scrollPx++;
                    if (scrollPx > 25) {
                        int travel = textW - OLED_W + 4;
                        if (scrollPx - 25 >= travel + 25) scrollPx = 0;
                    }
                    redraw = true;
                }
            } else {
                // Text fits — reset scroll so it starts from the left if it grows.
                scrollPx = 0;
            }
        }

        if (!redraw) return;
        _redrawStatus(deviceName, timeStr, displayMode);
    }

    // SET button: "MAX n" — rendered with FreeSansBold9pt7b for a smooth look.
    void showMax(int n) {
        if (!_oled) return;
        _oled->clearDisplay();
        String s = String(STR_MAX) + " " + String(n);
        // Centre vertically in the full 32px panel.
        drawLarge(s.c_str(), 0, OLED_H, &FreeSansBold9pt7b);
        _oled->display();
    }

    // ERASE prompt with countdown (docs/manual-setup.md):
    //   "press ERASE for" / "erase stored info" / "<countdown>"
    void showErasePrompt(int countdown) {
        if (!_oled) return;
        _oled->clearDisplay();
        drawSmall("press ERASE for", 0);
        drawSmall("erase stored info", 10);
        drawSmall(String(countdown).c_str(), 20);
        _oled->display();
    }

    // Shown while the ERASE button is held: "ERASE" / "hold the button".
    void showEraseHold() {
        if (!_oled) return;
        _oled->clearDisplay();
        // "ERASE" in smooth font, centred in the top 22px.
        drawLarge(STR_ERASE, 0, 22, &FreeSansBold9pt7b);
        drawSmall(STR_HOLD_BUTTON, 23);
        _oled->display();
    }

    // AP / config-portal screen: "ssid: AutoConnectAP" / "http://192.168.4.1".
    void showAP(const String &ssid, const String &url) {
        if (!_oled) return;
        _oled->clearDisplay();
        drawSmall((String("ssid: ") + ssid).c_str(), 6);
        drawSmall(url.c_str(), 20);
        _oled->display();
    }

    // Shown while UP is held at boot to force AP/config mode (docs/manual-setup.md
    // step 3): "hold the button" / "AutoConnect (+)".
    void showForceAP() {
        if (!_oled) return;
        _oled->clearDisplay();
        drawSmall("hold the button", 6);
        drawSmall("AutoConnect (+)", 20);
        _oled->display();
    }

    void showMessage(const String &line1, const String &line2 = "") {
        if (!_oled) return;
        _oled->clearDisplay();
        if (line2.length()) {
            drawSmall(line1.c_str(), 6);
            drawSmall(line2.c_str(), 20);
        } else {
            drawSmall(line1.c_str(), 12);
        }
        _oled->display();
    }

private:
    // Draw text horizontally centred using the built-in GFX bitmap font at size 1
    // (6×8 px per glyph). Used for all small/status text. GFX draws from the cursor
    // as the TOP-LEFT corner, so X is computed from glyph count.
    void drawSmall(const char *text, int y, char align = 'C') {
        _oled->setFont(nullptr);
        _oled->setTextSize(1);
        int textW = (int)strlen(text) * 6;
        int x;
        if      (align == 'L') x = 0;
        else if (align == 'R') x = OLED_W - textW;
        else                   x = (OLED_W - textW) / 2;
        if (x < 0) x = 0;
        _oled->setCursor(x, y);
        _oled->print(text);
    }

    // Draw text horizontally centred using a GFX custom (proportional) font.
    // `areaY`  : top of the vertical area to centre within (pixels from top of panel).
    // `areaH`  : height of that area in pixels.
    // The cursor is placed at the baseline so the bounding box is vertically centred
    // within [areaY, areaY+areaH). After drawing, restores the default font + size 1.
    void drawLarge(const char *text, int areaY, int areaH, const GFXfont *font) {
        _oled->setFont(font);
        int16_t bx, by; uint16_t bw, bh;
        _oled->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
        // Horizontal: centre the bounding box.
        int x = (OLED_W - (int)bw) / 2 - bx;
        if (x < 0) x = 0;
        // Vertical: centre the bounding box within the area.
        // by is negative (distance from baseline up to the top of the bounding box).
        int centreY = areaY + (areaH - (int)bh) / 2;
        if (centreY < areaY) centreY = areaY;
        int y = centreY - by;   // cursor y = top of box - by (by is negative)
        _oled->setCursor(x, y);
        _oled->print(text);
        _oled->setFont(nullptr);
        _oled->setTextSize(1);
    }

    // Build the bottom-line string for port mode: "deviceName  HH:MM:SS".
    String _bottomLine(const String &deviceName, const String &timeStr) const {
        String s = deviceName;
        if (timeStr.length()) {
            s += "  ";
            s += timeStr;
        }
        return s;
    }

    // Build the bottom-line string for clock mode: "antLabel  deviceName".
    String _clockBottomLine(const String &label, const String &deviceName) const {
        String s = label;
        if (deviceName.length()) {
            s += "  ";
            s += deviceName;
        }
        return s;
    }

    // Resolve the effective clock-mode boolean from a DisplayMode.
    // In Cycle mode, _cyclePhase 0 = Port, 1 = Clock.
    bool _effectiveClockMode(DisplayMode dm) const {
        if (dm == DisplayMode::Clock) return true;
        if (dm == DisplayMode::Cycle) return _cyclePhase != 0;
        return false;
    }

    // Redraw the status screen using stored _statusLabel and the supplied runtime
    // strings. Overload with no args uses empty strings (for lock-state redraws).
    void _redrawStatus() {
        _drawStatusImpl(_statusLabel, String(), String(), false, 0);
    }
    void _redrawStatus(const String &deviceName, const String &timeStr, DisplayMode displayMode) {
        uint32_t now = millis();
        bool portOverride = (_portChangedAt != 0 &&
                             (int32_t)(now - _portChangedAt) < (int32_t)PORT_CHANGE_SHOW_MS);
        bool effectiveClock = _effectiveClockMode(displayMode) && !portOverride;
        int scrollOff = 0;
        if (effectiveClock) {
            String bottom = _clockBottomLine(_statusLabel, deviceName);
            int textW = (int)bottom.length() * 6;
            if (textW > OLED_W && _clockScrollPx > 25) {
                int travel = textW - OLED_W + 4;
                scrollOff = _clockScrollPx - 25;
                if (scrollOff > travel) scrollOff = travel;
            }
        } else {
            String bottom = _bottomLine(deviceName, timeStr);
            int textW = (int)bottom.length() * 6;
            if (textW > OLED_W && _scrollPx > 25) {
                int travel = textW - OLED_W + 4;
                scrollOff = _scrollPx - 25;
                if (scrollOff > travel) scrollOff = travel;
            }
        }
        _drawStatusImpl(_statusLabel, deviceName, timeStr, effectiveClock, scrollOff);
    }

    // Core renderer. Two layouts:
    //
    // Port mode (effectiveClock=false):
    //   Top 24px: antenna label, smooth font cascade 12pt→9pt→default size 2→1
    //   Bottom 8px: "deviceName  HH:MM:SS", default size 1, scrolled if too wide
    //
    // Clock mode (effectiveClock=true, timeStr non-empty):
    //   Top 24px: "HH:MM", FreeSansBold12pt7b, centred
    //   Bottom 8px: "antLabel  deviceName", default size 1, centred (scrolled if needed)
    //   Falls back to port mode if timeStr is empty (NTP not synced).
    void _drawStatusImpl(const String &label, const String &deviceName,
                         const String &timeStr, bool effectiveClock, int scrollOff) {
        if (!_oled) return;
        _oled->clearDisplay();

        const int urlH = 8;
        const int urlY = OLED_H - urlH;   // y=24 — bottom row for small status text

        if (effectiveClock && timeStr.length()) {
            // --- Clock-prominent layout ---
            // Show HH:MM only (no seconds). timeStr is "HH:MM:SS"; take chars 0..4.
            String hhmm = timeStr.length() >= 5 ? timeStr.substring(0, 5) : timeStr;

            // Top 24px: time in smooth font, vertically centred.
            drawLarge(hhmm.c_str(), 0, urlY, &FreeSansBold12pt7b);

            // Bottom 8px: "antLabel  deviceName", default size 1, scrolled if too wide.
            String bottom = _clockBottomLine(label, deviceName);
            _oled->setFont(nullptr);
            _oled->setTextSize(1);
            int bw = (int)bottom.length() * 6;
            int bx = (bw <= OLED_W) ? (OLED_W - bw) / 2 : -scrollOff;
            _oled->setCursor(bx, urlY);
            _oled->print(bottom.c_str());
        } else {
            // --- Port-prominent layout ---
            // Top 24px: antenna label, smooth font cascade 12pt→9pt→default 2→1.
            _drawLabelAutoSize(label.c_str(), 0, urlY);

            // Bottom 8px: "deviceName  HH:MM:SS", default size 1, scrolled if too wide.
            String bottom = _bottomLine(deviceName, timeStr);
            _oled->setFont(nullptr);
            _oled->setTextSize(1);
            int bw = (int)bottom.length() * 6;
            int bx = (bw <= OLED_W) ? (OLED_W - bw) / 2 : -scrollOff;
            _oled->setCursor(bx, urlY);
            _oled->print(bottom.c_str());
        }

        // Padlock indicator: pixel-drawn 7×9 padlock in the top-right corner.
        if (_locked) {
            const int px = OLED_W - 7;  // x=121
            const int py = 0;
            _oled->drawLine(px+1, py+1, px+1, py+3, SSD1306_WHITE);
            _oled->drawLine(px+5, py+1, px+5, py+3, SSD1306_WHITE);
            _oled->drawLine(px+2, py,   px+4, py,   SSD1306_WHITE);
            _oled->fillRect(px, py+4, 7, 5, SSD1306_WHITE);
            _oled->drawPixel(px+3, py+6, SSD1306_BLACK);
        }

        _oled->display();
    }

    // Draw a label string into a vertical area [areaY, areaY+areaH) using the largest
    // font that fits the panel width. Cascade:
    //   FreeSansBold12pt7b → FreeSansBold9pt7b → default size 2 → default size 1
    // Leaves the font/size set to default size 1 on exit.
    void _drawLabelAutoSize(const char *text, int areaY, int areaH) {
        struct FontOption { const GFXfont *font; uint8_t defSize; };
        FontOption opts[] = {
            { &FreeSansBold12pt7b, 0 },
            { &FreeSansBold9pt7b,  0 },
            { nullptr,             2 },
            { nullptr,             1 },
        };
        int16_t bx, by; uint16_t bw, bh;
        for (auto &opt : opts) {
            int textW, textH;
            if (opt.font) {
                _oled->setFont(opt.font);
                _oled->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
                textW = (int)bw;
                textH = (int)bh;
            } else {
                _oled->setFont(nullptr);
                _oled->setTextSize(opt.defSize);
                textW = (int)strlen(text) * 6 * opt.defSize;
                textH = 8 * opt.defSize;
            }
            if (textW <= OLED_W && textH <= areaH) {
                int x, y;
                if (opt.font) {
                    x = (OLED_W - textW) / 2 - bx;
                    if (x < 0) x = 0;
                    int centreY = areaY + (areaH - textH) / 2;
                    if (centreY < areaY) centreY = areaY;
                    y = centreY - by;
                } else {
                    x = (OLED_W - textW) / 2;
                    if (x < 0) x = 0;
                    y = areaY + (areaH - textH) / 2;
                    if (y < areaY) y = areaY;
                }
                _oled->setCursor(x, y);
                _oled->print(text);
                break;
            }
        }
        _oled->setFont(nullptr);
        _oled->setTextSize(1);
    }

    Adafruit_SSD1306 *_oled = nullptr;
    uint8_t  _addr    = 0;
    uint8_t  _i2caddr = 0x3C;
    bool     _ok      = false;

    // Status screen state.
    String   _statusLabel;
    String   _statusUrl;   // stored but not currently displayed (reserved for future use)

    // Clock tick.
    uint32_t _lastClockMs = 0;

    // Bottom-line scroll — port mode.
    int      _scrollPx     = 0;
    uint32_t _lastScrollMs = 0;
    // Bottom-line scroll — clock mode.
    int      _clockScrollPx  = 0;
    uint32_t _clockScrollMs  = 0;

    // Port-change override: non-zero for PORT_CHANGE_SHOW_MS after notifyPortChange().
    uint32_t _portChangedAt = 0;

    // Custom message: non-zero while a showCustomMessage() display is active.
    uint32_t _customUntil          = 0;
    bool     _reblankAfterCustom   = false;

    // Screen-saver / blank state.
    bool     _blanked    = false;
    bool     _dotOn      = false;
    uint32_t _lastDotMs  = 0;

    // Lock indicator.
    bool     _locked     = false;

    // Cycle mode state.
    // _cycleStartMs: millis() when the current cycle epoch began (reset on mode change).
    // _cyclePhase:   0 = Port sub-mode, 1 = Clock sub-mode.
    // _cyclePhaseMs: elapsed ms within the current CYCLE_INTERVAL_MS window; used to
    //                freeze the timer while a custom message is displayed.
    uint32_t _cycleStartMs  = 0;
    bool     _cyclePhase    = false;   // false=Port, true=Clock
    uint32_t _cyclePhaseMs  = 0;
};
