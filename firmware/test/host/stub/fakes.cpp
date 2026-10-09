// fakes.cpp — the state behind the stand-ins in stub/. Linked into every test.
#include "Arduino.h"
#include "EEPROM.h"
#include "ESP8266WiFi.h"
#include "ESP8266mDNS.h"
#include "LittleFS.h"
#include "Wire.h"
#include "Adafruit_SSD1306.h"
#include "PubSubClient.h"
#include "fakes.h"

unsigned long g_millis = 1000;
std::function<void()> g_delayHook;
int g_pinLevel[32], g_pinOut[32], g_pinMode[32];
HardwareSerial Serial;
EspClass ESP;
time_t g_epoch = 0;
static unsigned long g_epochSetAt = 0;
int g_configTimeCalls = 0;
std::string g_configTimeServer;

EEPROMClass EEPROM;
WiFiClass WiFi;
MDNSResponder MDNS;
std::map<std::string, std::string> g_fs;
bool g_fsMountOk = true;
LittleFSClass LittleFS;
uint8_t g_i2cDevice = 0x3C;
TwoWire Wire;
std::vector<OledFrame> g_frames;
bool g_oledBeginOk = true;
uint8_t g_oledBeginAddr = 0;

int g_connects = 0;
bool g_brokerUp = true;
void (*g_cb)(char *, uint8_t *, unsigned int) = nullptr;
std::vector<Pub> g_pubs;
std::vector<std::string> g_subs;
std::string g_will, g_willMsg, g_mqttHost, g_mqttUser, g_mqttPass, g_mqttClientId;
uint16_t g_mqttPort = 0;

// libc time(), as the NTP code sees it: a 1970 value until a test "syncs" the clock.
extern "C" time_t time(time_t *t) noexcept {
    time_t v = g_epoch ? g_epoch + (time_t)((g_millis - g_epochSetAt) / 1000) : (time_t)1000;
    if (t) *t = v;
    return v;
}
void setEpoch(time_t e) { g_epoch = e; g_epochSetAt = g_millis; }

void fakesReset() {
    g_millis = 1000; g_delayHook = nullptr;
    for (int i = 0; i < 32; i++) { g_pinLevel[i] = HIGH; g_pinOut[i] = 0; g_pinMode[i] = -1; }
    g_epoch = 0; g_epochSetAt = 0; g_configTimeCalls = 0; g_configTimeServer.clear();
    EEPROM = EEPROMClass();
    WiFi = WiFiClass(); MDNS = MDNSResponder();
    g_fs.clear(); g_fsMountOk = true;
    g_i2cDevice = 0x3C; g_frames.clear(); g_oledBeginOk = true; g_oledBeginAddr = 0;
    g_connects = 0; g_brokerUp = true; g_cb = nullptr; g_pubs.clear(); g_subs.clear();
    g_will.clear(); g_willMsg.clear(); g_mqttHost.clear(); g_mqttUser.clear(); g_mqttPass.clear(); g_mqttClientId.clear(); g_mqttPort = 0;
}

void mqttInject(const std::string &t, const std::string &p) {
    std::string tt = t, pp = p;
    if (g_cb) g_cb(&tt[0], (uint8_t *)pp.data(), (unsigned)pp.size());
}
