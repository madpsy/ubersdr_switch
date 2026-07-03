// config.h — Pin map, constants and verbatim strings for the ANTENNA SELECTOR V2
// replica firmware.
//
// Every value here is taken directly from the reverse-engineering documentation in
// ../../docs/ so that the replica is pin- and interface-compatible with the stock
// ANTENI.NET "ANTENNAS webSWITCH control 1 to 5" firmware.
//
//  - GPIO map / setup():            ../../docs/hardware-pinout.md
//  - 74HCT138 decoder truth table:  ../../docs/74hct138-truth-table.md
//  - Buttons + on-screen strings:   ../../docs/buttons.md
//  - Web interface (exact HTML):    ../../docs/web-interface.md
//  - WiFi / AP / mDNS / OTA:        ../../docs/wifi-ap-config.md

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// GPIO map  (docs/hardware-pinout.md)
// ---------------------------------------------------------------------------

// 74HCT138 3-to-8 decoder select lines (outputs). The firmware writes the antenna
// position index (0..7) as a plain binary number across these three lines:
//   A0 = GPIO12 (LSB), A1 = GPIO13, A2 = GPIO14 (MSB).
static const uint8_t PIN_SEL_A0 = 12;  // 74HCT138 A0 (LSB)
static const uint8_t PIN_SEL_A1 = 13;  // 74HCT138 A1
static const uint8_t PIN_SEL_A2 = 14;  // 74HCT138 A2 (MSB)

// Four momentary push-buttons, all active-low with internal pull-ups enabled.
// (GPIO1/GPIO3 double as UART0 TX/RX — used as inputs only, as in the stock FW.)
static const uint8_t PIN_BTN_SET   = 0;  // SET  — sets the max selectable antenna
static const uint8_t PIN_BTN_DOWN  = 1;  // DOWN — previous antenna
static const uint8_t PIN_BTN_ERASE = 2;  // ERASE — hold to wipe WiFi config
static const uint8_t PIN_BTN_UP    = 3;  // UP   — next antenna

// I2C OLED on the Heltec WiFi Kit 8 (HTIT-W8266): a 0.91" 128x32 SSD1306.
//   SDA = GPIO4, SCL = GPIO5, and — crucially — the panel's RESET line is wired to
//   GPIO16. The SSD1306 will NOT come out of reset (stays fully blank) unless GPIO16
//   is toggled LOW then HIGH before begin(). This is the #1 cause of a blank Heltec
//   OLED when firmware assumes a "no reset pin" module.
static const uint8_t PIN_OLED_SDA = 4;
static const uint8_t PIN_OLED_SCL = 5;
static const uint8_t PIN_OLED_RST = 16;   // Heltec WiFi Kit 8 OLED hardware reset

// ---------------------------------------------------------------------------
// Antenna / decoder model  (docs/74hct138-truth-table.md)
// ---------------------------------------------------------------------------

// Position index: 0 = GROUND, 1..7 = ANT1..ANT7. The firmware supports 8 positions
// in software (GROUND + ANT1..7); this board populates only 5 relays (ANT1..ANT5).
static const int8_t POS_GROUND = 0;
static const int8_t POS_MIN    = 0;   // GROUND
static const int8_t POS_MAX_HW = 7;   // ANT7 (hardware/decoder limit)

// The stored "max antennas" value (SET button). This board populates 5 relays
// (ANT1..ANT5), so the default is 5 — the UP/DOWN cycle runs GROUND..ANT5 then wraps.
// (The stock firmware defaulted to 7, its full 8-position software range; the SET
// button can still raise it up to 7 for boards with more relays. The SET counter
// wraps at 8 — bgei a2,8 in the disassembly.)
static const int8_t DEFAULT_MAX_ANTENNAS = 5;
static const int8_t MAX_ANTENNAS_WRAP    = 8;  // SET wraps 0..7 then back

// ---------------------------------------------------------------------------
// Button timing
// ---------------------------------------------------------------------------

static const uint16_t BTN_DEBOUNCE_MS = 40;    // debounce for edge detection
static const uint16_t ERASE_HOLD_MS   = 3000;  // hold-to-erase (~0x9c4/0xbb8 in FW)
static const uint16_t ERASE_PROMPT_MS = 2500;  // erase prompt display window

// ---------------------------------------------------------------------------
// Identity / network  (docs/wifi-ap-config.md, docs/README.md)
// ---------------------------------------------------------------------------

// Original device MAC cc:50:e3:45:ca:21 → hostname/mDNS "ESP-45CA21".
// The replica derives the same style of name from the running chip's own MAC so it
// is self-consistent on any unit ("ESP-" + last 3 MAC bytes, upper-case hex).
static const char AP_SSID[]       = "AutoConnectAP";  // WiFiManager default SoftAP
static const char HOSTNAME_PREFIX[] = "ESP-";

// WiFi connection resilience. A flaky/slow router (or one still booting after a power
// cut) can miss a single connect attempt, dropping the unit into AP/config mode. We
// therefore retry the SAVED credentials for a bounded total budget before falling back
// to the captive portal: several ~15s attempts up to WIFI_CONNECT_BUDGET_MS total.
static const uint32_t WIFI_CONNECT_BUDGET_MS  = 45000;  // total retry budget (~45s)
static const uint8_t  WIFI_CONNECT_ATTEMPT_S  = 15;     // per-attempt timeout (seconds)
static const uint8_t  WIFI_CONNECT_RETRIES    = 3;      // WiFiManager's own retries

// ---------------------------------------------------------------------------
// NTP (optional; uses ESP8266 built-in SNTP via configTime() — no extra lib)
// ---------------------------------------------------------------------------

static const uint8_t  NTP_HOST_MAXLEN   = 48;
static const char     NTP_HOST_DEFAULT[] = "pool.ntp.org";
static const uint16_t NTP_PORT_DEFAULT  = 123;

// ---------------------------------------------------------------------------
// MQTT (optional; plaintext only — see src/mqtt.h, docs/mqtt.md)
// ---------------------------------------------------------------------------

// Field length caps (bytes, excluding NUL) for the persisted MQTT config.
static const uint8_t MQTT_HOST_MAXLEN   = 48;
static const uint8_t MQTT_USER_MAXLEN   = 32;
static const uint8_t MQTT_PASS_MAXLEN   = 32;
static const uint8_t MQTT_PREFIX_MAXLEN = 40;
static const uint16_t MQTT_DEFAULT_PORT = 1883;

// Device display name (shown on OLED splash and in the web UI header).
// Default "UberSDR" is used when the stored name is empty.
static const uint8_t DEVICE_NAME_MAXLEN = 24;
static const char    DEVICE_NAME_DEFAULT[] = "UberSDR";

// Persisted MQTT configuration (stored in the settings EEPROM region, so it survives a
// filesystem reflash). `prefix` empty => default "ubersdr/<hostname>" is used at runtime.
struct MqttConfig {
    bool     enabled  = false;
    char     host[MQTT_HOST_MAXLEN + 1]     = {0};
    uint16_t port     = MQTT_DEFAULT_PORT;
    char     user[MQTT_USER_MAXLEN + 1]     = {0};
    char     pass[MQTT_PASS_MAXLEN + 1]     = {0};
    char     prefix[MQTT_PREFIX_MAXLEN + 1] = {0};
    bool     retain   = true;    // publish retained state/availability messages
    bool     commands = false;   // accept inbound command topics (<prefix>/set, ...)
};

// EEPROM region for the settings block. Kept AFTER the antenna block
// (max + names) in the shared EEPROM sector. A single EEPROM.begin(EEPROM_TOTAL_SIZE)
// in setup() covers both. Layout inside the settings region:
//   [0]   magic 0x5A
//   [1]   flags: bit0 mqtt_enabled, bit1 retain, bit2 commands
//   [2-3] mqtt port (LE)
//   [4..] host / user / pass / prefix / device_name / ntp_host, each a fixed NUL-padded field
//   [..] ntp_port (LE, 2 bytes)
static const uint8_t  SETTINGS_MAGIC = 0x5A;

// Shared EEPROM sector layout. The antenna block (magic+max+7 name slots = 121 bytes)
// occupies [0..120]; the settings block follows. A single EEPROM.begin() in
// setup() must cover EEPROM_TOTAL_SIZE so both classes can read/write their regions.
static const int EEPROM_ANTENNA_SIZE = 2 + 7 * 17;   // = 121 (mirror of antenna.h)
static const int EEPROM_SETTINGS_BASE = EEPROM_ANTENNA_SIZE;
static const int EEPROM_SETTINGS_SIZE =
    1 /*magic*/ + 1 /*flags*/ + 2 /*mqtt_port*/
    + (MQTT_HOST_MAXLEN + 1) + (MQTT_USER_MAXLEN + 1)
    + (MQTT_PASS_MAXLEN + 1) + (MQTT_PREFIX_MAXLEN + 1)
    + (DEVICE_NAME_MAXLEN + 1)
    + (NTP_HOST_MAXLEN + 1) + 2 /*ntp_port*/;
static const int EEPROM_TOTAL_SIZE = EEPROM_SETTINGS_BASE + EEPROM_SETTINGS_SIZE;

// ---------------------------------------------------------------------------
// On-screen / web selection labels  (docs/web-interface.md §1.4, docs/buttons.md)
// Shared between the OLED and the served HTML, verbatim from the flash image.
// ---------------------------------------------------------------------------

static const char LABEL_GROUND[] = "GROUND";
// ANT labels are formatted as "ANT: n" at runtime (see label helpers).

// OLED / erase-flow strings. (The original stock firmware displayed "pres" — a typo;
// this replica corrects it to "press".)
static const char STR_MAX[]        = "MAX";
static const char STR_ERASE[]      = "ERASE";
static const char STR_ERASE_PROMPT[] = "press ERASE for erase stored information";
static const char STR_HOLD_BUTTON[]  = "hold the button";
static const char STR_IP_UNSET[]     = "http://(IP unset)";

// ---------------------------------------------------------------------------
// Application web-server routes  (docs/web-interface.md §1.1)
// The stock server matches literal substrings of the request line.
// ---------------------------------------------------------------------------

static const char ROUTE_UP_ON[]   = "GET /5/on";   // Up   (antenna +1)
static const char ROUTE_UP_OFF[]  = "GET /5/off";  // Up "released"
static const char ROUTE_DN_ON[]   = "GET /4/on";   // Dn   (antenna -1)
static const char ROUTE_DN_OFF[]  = "GET /4/off";  // Dn "released"

// ---------------------------------------------------------------------------
// Debug logging (off by default — GPIO1/3 are the buttons, see platformio.ini)
// ---------------------------------------------------------------------------

#ifdef REPLICA_DEBUG
  #define DBG_BEGIN(baud) Serial.begin(baud)
  #define DBG(...)        Serial.print(__VA_ARGS__)
  #define DBGLN(...)      Serial.println(__VA_ARGS__)
#else
  #define DBG_BEGIN(baud) do {} while (0)
  #define DBG(...)        do {} while (0)
  #define DBGLN(...)      do {} while (0)
#endif
