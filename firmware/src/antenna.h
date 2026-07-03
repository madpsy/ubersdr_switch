// antenna.h — Antenna position model + 74HCT138 decoder drive.
//
// Replicates the stock firmware's selection logic exactly (docs/74hct138-truth-table.md,
// docs/hardware-pinout.md, docs/buttons.md, docs/manual-setup.md):
//
//   * position index 0 = GROUND, 1..7 = ANT1..ANT7
//   * selectAntenna() writes the index as binary across A0/A1/A2 (GPIO12/13/14)
//   * UP steps +1, DOWN steps -1, wrapping between GROUND and the stored max
//   * SET increments a stored "max antennas" value that bounds the UP/DOWN range
//     (wraps at 8, default 7). This value is PERSISTENT: the manual states the chosen
//     value is "remembered (stored in the device)" and to "power-cycle to confirm",
//     and the stock FW keeps it in the retained DRAM word at 0x3ffe84e0. We persist
//     it to the ESP8266 EEPROM area so it survives reboots and power-cycles.

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// Custom antenna names (ANT1..ANT7; GROUND is never nameable). Capped so they fit both
// storage and the OLED's auto-sizing text renderer.
static const uint8_t ANT_NAME_MAXLEN = 16;

// EEPROM layout. Names are stored HERE (not in LittleFS) so they survive a filesystem
// reflash (`uploadfs`) exactly like the max value and WiFi credentials do — the EEPROM
// sector is outside the LittleFS partition.
//   [0]            magic 0xA6  (bumped: layout now includes names)
//   [1]            maxAntennas (0..7)
//   [2 ..]         7 name slots of (ANT_NAME_MAXLEN+1) bytes each, NUL-terminated,
//                  for positions 1..7 (slot i-1). An empty first byte = no name.
static const int     EEPROM_ADDR_MAGIC = 0;
static const int     EEPROM_ADDR_MAX   = 1;
static const int     EEPROM_ADDR_NAMES = 2;
static const int     EEPROM_NAME_SLOT  = ANT_NAME_MAXLEN + 1;   // 17 bytes/slot
static const int     EEPROM_SIZE       = EEPROM_ADDR_NAMES + 7 * EEPROM_NAME_SLOT; // 121
static const uint8_t EEPROM_MAGIC      = 0xA6;   // bumped from 0xA5 (added names)
static const uint8_t EEPROM_MAGIC_OLD  = 0xA5;   // previous layout (max only)

class Antenna {
public:
    void begin() {
        // Match setup() init order from the disassembly (0x40204504):
        //   digitalWrite(14/12/13, HIGH) — select lines preset to 111 (pos 7)
        // We configure the pins as outputs and preset to 111 to mirror the stock
        // power-on state before the saved selection is applied.
        pinMode(PIN_SEL_A2, OUTPUT);
        pinMode(PIN_SEL_A0, OUTPUT);
        pinMode(PIN_SEL_A1, OUTPUT);
        digitalWrite(PIN_SEL_A2, HIGH);  // A2
        digitalWrite(PIN_SEL_A0, HIGH);  // A0
        digitalWrite(PIN_SEL_A1, HIGH);  // A1

        // Restore the persistent "max antennas" value (default 7 if never set).
        loadMax();
        // Restore custom antenna names from LittleFS (if any).
        loadNames();

        // Stock powers up showing GROUND ("wait a while until GROUND appears").
        _position = POS_GROUND;
        apply();
    }

    // Write the current position across the three decoder select lines.
    // pos: 0 = GROUND, 1..7 = ANT1..ANT7  (docs/74hct138-truth-table.md).
    void apply() {
        digitalWrite(PIN_SEL_A0, (_position >> 0) & 1);  // A0
        digitalWrite(PIN_SEL_A1, (_position >> 1) & 1);  // A1
        digitalWrite(PIN_SEL_A2, (_position >> 2) & 1);  // A2
    }

    // UP button / GET /5/on : next antenna, wrapping to GROUND past the max.
    void up() {
        _position++;
        if (_position > _maxAntennas) _position = POS_GROUND;
        apply();
    }

    // DOWN button / GET /4/on : previous antenna, wrapping to max below GROUND.
    // (Disassembly: if (position == -1) position = <stored max>.)
    void down() {
        _position--;
        if (_position < POS_GROUND) _position = _maxAntennas;
        apply();
    }

    // SET button : bump the stored "max antennas" value. Counter wraps at 8
    // (bgei a2,8 in the FW) — i.e. cycles 0..7 then back to 0. The new value is
    // persisted so it survives a power-cycle (as the manual describes).
    void bumpMax() {
        _maxAntennas++;
        if (_maxAntennas >= MAX_ANTENNAS_WRAP) _maxAntennas = 0;
        // Keep the current position within the new bound.
        if (_position > _maxAntennas) {
            _position = POS_GROUND;
            apply();
        }
        saveMax();
    }

    int8_t position()    const { return _position; }
    int8_t maxAntennas() const { return _maxAntennas; }

    void setMaxAntennas(int8_t m) {
        if (m < 0) m = 0;
        if (m > POS_MAX_HW) m = POS_MAX_HW;
        _maxAntennas = m;
        if (_position > _maxAntennas) { _position = POS_GROUND; apply(); }
        saveMax();
    }

    void setPosition(int8_t p) {
        if (p < POS_GROUND) p = POS_GROUND;
        if (p > _maxAntennas) p = POS_GROUND;
        _position = p;
        apply();
    }

    // OLED / status selection label. If the current antenna has a custom name it is
    // shown INSTEAD of "ANT: n"; GROUND is never nameable. The OLED renderer
    // auto-sizes text, so long names simply shrink to fit (display.h showStatus()).
    String label() const {
        if (_position == POS_GROUND) return String(LABEL_GROUND);
        if (_names[_position].length()) return _names[_position];
        return String("ANT: ") + String((int)_position);
    }

    // Custom name for a position (empty String if unset / GROUND).
    String name(int8_t pos) const {
        if (pos <= POS_GROUND || pos > POS_MAX_HW) return String();
        return _names[pos];
    }
    bool hasName(int8_t pos) const { return name(pos).length() > 0; }

    // Set / clear a custom name (GROUND cannot be named). Empty string clears it.
    // Names are sanitised (control chars, backslash and quotes removed) and capped at
    // ANT_NAME_MAXLEN, then persisted to EEPROM. Returns the stored value.
    String setName(int8_t pos, const String &raw) {
        if (pos <= POS_GROUND || pos > POS_MAX_HW) return String();
        _names[pos] = sanitizeName(raw);
        saveNames();
        return _names[pos];
    }

    // JSON object of all set names: {"1":"...","3":"..."} (only non-empty entries).
    String namesJson() const {
        String j = "{";
        bool first = true;
        for (int8_t i = 1; i <= POS_MAX_HW; i++) {
            if (!_names[i].length()) continue;
            if (!first) j += ",";
            first = false;
            j += "\""; j += String((int)i); j += "\":\"";
            j += jsonEscape(_names[i]); j += "\"";
        }
        j += "}";
        return j;
    }

    // Web-page selection fragment (substituted for %LABEL% in data/old.html).
    //
    // The stock app page formats the two states DIFFERENTLY — verified verbatim
    // against a real device at http://<ip>/ (docs/web-interface.md §1.3):
    //   * GROUND (pos 0): "<p>GROUND</p>\n"  (word "GROUND", wrapped in <p>)
    //   * ANTn   (pos n): "n"                (bare digit, no <p>, no newline)
    // (The legacy /old page keeps the bare-number form; custom names appear on the
    // modern UI and OLED.)
    String webLabel() const {
        if (_position == POS_GROUND) return String("<p>") + LABEL_GROUND + "</p>\n";
        return String((int)_position);
    }

private:
    // The EEPROM image is opened once in begin() (via loadMax) and holds the max value
    // AND the 7 name slots, so it survives a LittleFS reflash.
    void loadMax() {
        // Cover the WHOLE shared sector (antenna block + settings block) so the
        // Settings class can use the same EEPROM image (see config.h EEPROM_TOTAL_SIZE).
        EEPROM.begin(EEPROM_TOTAL_SIZE);
        uint8_t magic = EEPROM.read(EEPROM_ADDR_MAGIC);
        uint8_t m     = EEPROM.read(EEPROM_ADDR_MAX);
        // Accept the current layout OR the older max-only layout (0xA5): in both the
        // max byte is valid; only the new layout has names.
        if ((magic == EEPROM_MAGIC || magic == EEPROM_MAGIC_OLD) && m <= POS_MAX_HW) {
            _maxAntennas = (int8_t)m;
        } else {
            _maxAntennas = DEFAULT_MAX_ANTENNAS;   // never set → factory default 7
        }
    }

    // Write magic + max, then commit (shared by saveMax/saveNames).
    void commitHeaderAndFlush() {
        EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
        EEPROM.write(EEPROM_ADDR_MAX, (uint8_t)_maxAntennas);
        EEPROM.commit();
    }

    void saveMax() { commitHeaderAndFlush(); }

    // Strip control chars, backslashes and quotes; collapse to <= ANT_NAME_MAXLEN,
    // and trim surrounding whitespace. Keeps names safe for JSON and OLED rendering.
    static String sanitizeName(const String &raw) {
        String s;
        for (size_t i = 0; i < raw.length() && s.length() < ANT_NAME_MAXLEN; i++) {
            char c = raw[i];
            if (c == '"' || c == '\\') continue;
            if ((uint8_t)c < 0x20) continue;      // control chars
            s += c;
        }
        s.trim();
        return s;
    }

    static String jsonEscape(const String &s) {
        // Names are already sanitised (no quotes/backslashes/control), so this is a
        // pass-through; kept for clarity and future-proofing.
        return s;
    }

    // EEPROM address of the name slot for position `pos` (1..7).
    static int nameSlotAddr(int8_t pos) {
        return EEPROM_ADDR_NAMES + (pos - 1) * EEPROM_NAME_SLOT;
    }

    // Persist all name slots to EEPROM (fixed 17-byte NUL-terminated records).
    void saveNames() {
        for (int8_t i = 1; i <= POS_MAX_HW; i++) {
            int base = nameSlotAddr(i);
            const String &n = _names[i];
            int len = n.length();
            if (len > ANT_NAME_MAXLEN) len = ANT_NAME_MAXLEN;
            for (int j = 0; j < len; j++) EEPROM.write(base + j, (uint8_t)n[j]);
            for (int j = len; j < EEPROM_NAME_SLOT; j++) EEPROM.write(base + j, 0);
        }
        commitHeaderAndFlush();   // ensures magic is the new (name-aware) value + flush
    }

    // Load names from the EEPROM slots. If the stored layout predates names
    // (magic 0xA5) the slots read as uninitialised; we treat non-printable/empty
    // first bytes as "no name" so a legacy image simply yields blank names.
    void loadNames() {
        for (int8_t i = 0; i <= POS_MAX_HW; i++) _names[i] = String();
        uint8_t magic = EEPROM.read(EEPROM_ADDR_MAGIC);
        if (magic != EEPROM_MAGIC) return;        // names only exist in the new layout
        char buf[ANT_NAME_MAXLEN + 1];
        for (int8_t i = 1; i <= POS_MAX_HW; i++) {
            int base = nameSlotAddr(i);
            int k = 0;
            for (; k < ANT_NAME_MAXLEN; k++) {
                uint8_t c = EEPROM.read(base + k);
                if (c == 0 || c == 0xFF) break;   // NUL or erased byte ends the string
                buf[k] = (char)c;
            }
            buf[k] = '\0';
            _names[i] = sanitizeName(String(buf));
        }
    }

    int8_t _position    = POS_GROUND;
    int8_t _maxAntennas = DEFAULT_MAX_ANTENNAS;
    String _names[POS_MAX_HW + 1];   // [0] GROUND unused; [1..7] custom names
};
