// test_settings.cpp — src/settings.h: every persisted setting, its defaults, its clamps,
// and the shared EEPROM sector layout (antenna | settings | schedule, no overlaps).
#include "antenna.h"
#include "schedule.h"
#include "settings.h"

#include "harness.h"

static Settings load() { Settings s; s.begin(); return s; }
static void power() { fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE); }

static void testDefaults() {
    section("erased EEPROM: factory defaults");
    power();
    Settings s = load();
    const MqttConfig &m = s.mqtt();
    check(!m.enabled && m.retain && !m.commands && !m.discovery && m.port == 1883 && !m.host[0] && !m.prefix[0], "MQTT off, retain on, commands off, discovery off, port 1883");
    check(String(s.ntp().host) == "time.cloudflare.com" && s.ntp().port == 123, "NTP time.cloudflare.com:123");
    check(String(s.deviceName()) == "UberANT", "device name UberANT");
    check(!s.blank().enabled && s.blank().timeoutSecs == 5, "screen saver off, 5 s");
    check(s.tzOffsetMinutes() == 0 && s.displayMode() == DisplayMode::Port, "UTC, port display mode");
    check(s.defaultPort() == 0 && s.startupPortMode() == 0, "boot to GROUND, 'default' startup mode");
}

static void testRoundTrip() {
    section("save + reload: every field");
    power();
    Settings s = load();
    MqttConfig m; m.enabled = true; strcpy(m.host, "broker.lan"); m.port = 8883; strcpy(m.user, "ant"); strcpy(m.pass, "pw");
    strcpy(m.prefix, "shack/ant"); m.retain = false; m.commands = true; m.discovery = true;
    s.setMqtt(m);
    NtpConfig n; strcpy(n.host, "time.lan"); n.port = 1123; s.setNtp(n);
    s.setDeviceName("Shack Switch");
    DisplayBlankConfig b; b.enabled = true; b.timeoutSecs = 30; s.setBlank(b);
    s.setTzOffset(-300);
    s.setDisplayMode(DisplayMode::Cycle);
    s.setDefaultPort(4);
    s.setStartupPortMode(1);

    Settings r = load();
    const MqttConfig &q = r.mqtt();
    check(q.enabled && String(q.host) == "broker.lan" && q.port == 8883 && String(q.user) == "ant" && String(q.pass) == "pw"
          && String(q.prefix) == "shack/ant" && !q.retain && q.commands && q.discovery, "MQTT: all fields");
    check(String(r.ntp().host) == "time.lan" && r.ntp().port == 1123, "NTP host + port");
    check(String(r.deviceName()) == "Shack Switch", "device name");
    check(r.blank().enabled && r.blank().timeoutSecs == 30, "screen saver");
    check(r.tzOffsetMinutes() == -300, "negative tz offset (stored as int16)");
    check(r.displayMode() == DisplayMode::Cycle, "display mode cycle");
    check(r.defaultPort() == 4 && r.startupPortMode() == 1, "default port + startup mode");
    r.setDisplayMode(DisplayMode::Clock);
    check(load().displayMode() == DisplayMode::Clock, "display mode clock");
}

static void testClamps() {
    section("clamps and sanitising");
    power();
    Settings s = load();
    s.setTzOffset(-900); bool lo = s.tzOffsetMinutes() == -720;
    s.setTzOffset(1000);
    check(lo && s.tzOffsetMinutes() == 840 && load().tzOffsetMinutes() == 840, "tz offset clamped to -720..+840");
    DisplayBlankConfig b; b.enabled = true; b.timeoutSecs = 0; s.setBlank(b);
    check(s.blank().timeoutSecs == 5, "screen-saver timeout 0 -> 5 s");
    s.setDefaultPort(9);
    check(s.defaultPort() == 0, "default port above 7 -> GROUND");
    s.setDefaultPort(5);
    check(s.defaultPortClamped(3) == 0 && s.defaultPortClamped(5) == 5, "defaultPortClamped: GROUND when above the current max");
    s.clampDefaultPort(5); bool kept = s.defaultPort() == 5;
    s.clampDefaultPort(4);
    check(kept && s.defaultPort() == 0 && load().defaultPort() == 0, "clampDefaultPort resets (and persists) only when out of range");
    s.setStartupPortMode(7);
    check(s.startupPortMode() == 0, "startup mode: anything but 1 is 'default'");
    s.setDeviceName("Tab\tName\n  ");
    check(String(s.deviceName()) == "Tab Name", "device name: control characters become spaces, trailing spaces trimmed");
    s.setDeviceName("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123");
    check(String(s.deviceName()) == "ABCDEFGHIJKLMNOPQRSTUVWX", "device name capped at 24");
    s.setDeviceName("");
    check(String(load().deviceName()) == "UberANT", "an empty device name loads as the default");
    NtpConfig n; n.host[0] = 0; n.port = 0; s.setNtp(n);
    check(String(s.ntp().host) == "time.cloudflare.com" && s.ntp().port == 123, "empty NTP host / port 0 -> defaults");
    MqttConfig m; m.port = 0; strcpy(m.host, "h"); s.setMqtt(m);
    check(s.mqtt().port == 1883 && load().mqtt().port == 1883, "MQTT port 0 -> 1883");
    MqttConfig big;
    memset(big.host, 'h', MQTT_HOST_MAXLEN); big.host[MQTT_HOST_MAXLEN] = 0;
    memset(big.prefix, 'p', MQTT_PREFIX_MAXLEN); big.prefix[MQTT_PREFIX_MAXLEN] = 0;
    s.setMqtt(big);
    Settings r = load();
    check(strlen(r.mqtt().host) == MQTT_HOST_MAXLEN && strlen(r.mqtt().prefix) == MQTT_PREFIX_MAXLEN && String(r.deviceName()) == "UberANT",
          "full-length fields round-trip without spilling into their neighbours");
}

static void testFlags() {
    section("MQTT flags byte (discovery = bit 3)");
    power();
    const int base = EEPROM_SETTINGS_BASE;
    Settings s = load();
    MqttConfig c; c.enabled = true; c.retain = true; c.commands = true; s.setMqtt(c);
    check(EEPROM.mem[base + 1] == 0x07, "enabled|retain|commands = 0x07");
    EEPROM.mem[base + 1] = 0x07;
    Settings old = load();
    check(old.mqtt().enabled && old.mqtt().retain && old.mqtt().commands && !old.mqtt().discovery, "a flags byte from older firmware (0x07): discovery off");
    MqttConfig d = old.mqtt(); d.discovery = true; old.setMqtt(d);
    check(EEPROM.mem[base + 1] == 0x0F && load().mqtt().discovery, "discovery sets only bit 3");
    d.discovery = false; old.setMqtt(d);
    check(EEPROM.mem[base + 1] == 0x07, "and clears only bit 3");
}

static void testOlderImages() {
    section("settings written by older firmware (trailing fields still erased)");
    power();
    Settings s = load();
    s.setDeviceName("Old");
    // An image from before the screen-saver / tz / display / startup fields existed:
    // everything after ntp_port is still erased flash.
    int tail = EEPROM_SETTINGS_BASE + 4 + (MQTT_HOST_MAXLEN + 1) + (MQTT_USER_MAXLEN + 1) + (MQTT_PASS_MAXLEN + 1)
             + (MQTT_PREFIX_MAXLEN + 1) + (DEVICE_NAME_MAXLEN + 1) + (NTP_HOST_MAXLEN + 1) + 2;
    for (int i = tail; i < EEPROM_SETTINGS_BASE + EEPROM_SETTINGS_SIZE; i++) EEPROM.mem[i] = 0xFF;
    Settings r = load();
    check(String(r.deviceName()) == "Old", "the fields it did have still load");
    check(!r.blank().enabled && r.blank().timeoutSecs == 5 && r.tzOffsetMinutes() == 0 && r.displayMode() == DisplayMode::Port
          && r.defaultPort() == 0 && r.startupPortMode() == 0, "the newer ones take their defaults");
    check(tail + 2 + 2 + 1 + 1 + 1 == EEPROM_SETTINGS_BASE + EEPROM_SETTINGS_SIZE, "EEPROM_SETTINGS_SIZE matches what saveAll() writes");
}

static void testLayout() {
    section("one EEPROM sector, three owners, no overlaps");
    power();
    Antenna a; a.begin();
    Settings s = load();
    Schedule sc; sc.begin();
    a.setMaxAntennas(7);
    for (int p = 1; p <= 7; p++) a.setName(p, "NNNNNNNNNNNNNNNN");
    MqttConfig m; m.enabled = true; memset(m.host, 'h', MQTT_HOST_MAXLEN); memset(m.user, 'u', MQTT_USER_MAXLEN);
    memset(m.pass, 'p', MQTT_PASS_MAXLEN); memset(m.prefix, 'x', MQTT_PREFIX_MAXLEN); s.setMqtt(m);
    s.setDeviceName("DDDDDDDDDDDDDDDDDDDDDDDD");
    s.setStartupPortMode(1); s.setDefaultPort(7); s.setTzOffset(840);
    for (int i = 0; i < SCHED_MAX_ENTRIES; i++) { ScheduleEntry e; e.enabled = true; e.daysMask = 0x7F; e.hour = 23; e.minute = 59; e.position = 7; sc.setEntry(i, e); }
    sc.setRespectLock(true);

    Antenna a2; a2.begin(); Settings s2 = load(); Schedule sc2; sc2.begin();
    bool names = true; for (int p = 1; p <= 7; p++) names = names && a2.name(p) == "NNNNNNNNNNNNNNNN";
    check(names && a2.maxAntennas() == 7, "antenna block intact");
    check(strlen(s2.mqtt().pass) == MQTT_PASS_MAXLEN && String(s2.deviceName()) == "DDDDDDDDDDDDDDDDDDDDDDDD" && s2.startupPortMode() == 1
          && s2.defaultPort() == 7 && s2.tzOffsetMinutes() == 840, "settings block intact");
    bool sch = true; for (int i = 0; i < SCHED_MAX_ENTRIES; i++) sch = sch && sc2.entry(i).enabled && sc2.entry(i).minute == 59;
    check(sch && sc2.respectLock(), "schedule block intact");
    check(EEPROM_SETTINGS_BASE == EEPROM_ANTENNA_SIZE && EEPROM_SCHED_BASE == EEPROM_SETTINGS_BASE + EEPROM_SETTINGS_SIZE, "blocks are contiguous: antenna, settings, schedule");
    check(EEPROM_TOTAL_SIZE <= 4096, "the sector fits the ESP8266's 4 KB EEPROM (" + std::to_string(EEPROM_TOTAL_SIZE) + " bytes)");
    check(EEPROM.outOfRange == 0, "no access outside the opened sector");
}

int main() {
    testDefaults();
    testRoundTrip();
    testClamps();
    testFlags();
    testOlderImages();
    testLayout();
    return finish("settings");
}
