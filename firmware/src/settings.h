// settings.h — persistent device settings (MQTT config + NTP config + device name
//              + display blank config + timezone offset + display mode) stored in
// the shared EEPROM sector, AFTER the antenna block. Because it lives in EEPROM
// (not LittleFS) the configuration survives a filesystem reflash (`uploadfs`), like
// the antenna names and max value. A single EEPROM.begin(EEPROM_TOTAL_SIZE) is
// issued by Antenna::begin(); this class only reads/writes its own region and commits.
//
// Layout of the settings region (base = EEPROM_SETTINGS_BASE):
//   +0        magic (SETTINGS_MAGIC)
//   +1        flags: bit0 mqtt_enabled, bit1 retain, bit2 commands
//   +2..3     MQTT port (little-endian uint16)
//   +4        host        (MQTT_HOST_MAXLEN+1, NUL-padded)
//   +...      user        (MQTT_USER_MAXLEN+1)
//   +...      pass        (MQTT_PASS_MAXLEN+1)
//   +...      prefix      (MQTT_PREFIX_MAXLEN+1)
//   +...      device_name (DEVICE_NAME_MAXLEN+1)
//   +...      ntp_host    (NTP_HOST_MAXLEN+1)
//   +...      ntp_port    (2 bytes LE)
//   +...      display_blank_flags  (1 byte: bit0 = enabled)
//   +...      display_blank_secs   (1 byte: timeout in seconds, 1-255)
//   +...      tz_offset_minutes    (2 bytes LE signed int16: minutes east of UTC)
//   +...      display_mode         (1 byte: 0=port-prominent, 1=clock-prominent)
//   +...      default_port         (1 byte: 0=GROUND, 1-7=ANT1-ANT7, 0xFF=unset→GROUND)
//   +...      startup_port_mode    (1 byte: 0=use default_port, 1=restore last used port)

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// NTP configuration (host + port, persisted in EEPROM).
struct NtpConfig {
    char     host[NTP_HOST_MAXLEN + 1] = {0};
    uint16_t port = NTP_PORT_DEFAULT;
};

// Display blank (screen-saver) configuration.
struct DisplayBlankConfig {
    bool    enabled     = false;
    uint8_t timeoutSecs = DISPLAY_BLANK_TIMEOUT_DEFAULT;
};

// Display mode: which value is shown prominently on the OLED status screen.
enum class DisplayMode : uint8_t {
    Port  = 0,   // antenna label is the large text (default)
    Clock = 1,   // time is the large text
    Cycle = 2,   // alternates between Port and Clock every 5 seconds
};

class Settings {
public:
    // Load settings from EEPROM. Antenna::begin() must have already run
    // EEPROM.begin(EEPROM_TOTAL_SIZE). If the region is uninitialised, defaults apply.
    void begin() {
        int base = EEPROM_SETTINGS_BASE;
        if (EEPROM.read(base) != SETTINGS_MAGIC) {
            _mqtt = MqttConfig();   // factory defaults (disabled)
            _ntp  = NtpConfig();
            strncpy(_ntp.host, NTP_HOST_DEFAULT, NTP_HOST_MAXLEN);
            _ntp.host[NTP_HOST_MAXLEN] = '\0';
            strncpy(_deviceName, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAXLEN);
            _deviceName[DEVICE_NAME_MAXLEN] = '\0';
            return;
        }
        uint8_t flags = EEPROM.read(base + 1);
        _mqtt.enabled  = flags & 0x01;
        _mqtt.retain   = flags & 0x02;
        _mqtt.commands = flags & 0x04;
        _mqtt.port     = (uint16_t)EEPROM.read(base + 2)
                       | ((uint16_t)EEPROM.read(base + 3) << 8);
        int p = base + 4;
        p = readField(p, _mqtt.host,   MQTT_HOST_MAXLEN);
        p = readField(p, _mqtt.user,   MQTT_USER_MAXLEN);
        p = readField(p, _mqtt.pass,   MQTT_PASS_MAXLEN);
        p = readField(p, _mqtt.prefix, MQTT_PREFIX_MAXLEN);
        p = readField(p, _deviceName,  DEVICE_NAME_MAXLEN);
        p = readField(p, _ntp.host,    NTP_HOST_MAXLEN);
        _ntp.port = (uint16_t)EEPROM.read(p) | ((uint16_t)EEPROM.read(p + 1) << 8);
        p += 2;
        // Display blank config (appended after ntp_port).
        // Both bytes default to 0xFF on erased flash — treat that as uninitialised.
        uint8_t blankFlags = EEPROM.read(p);
        uint8_t blankSecs  = EEPROM.read(p + 1);
        if (blankFlags == 0xFF) {
            // Uninitialised: apply defaults (disabled, 5 s).
            _blank.enabled     = false;
            _blank.timeoutSecs = DISPLAY_BLANK_TIMEOUT_DEFAULT;
        } else {
            _blank.enabled     = blankFlags & 0x01;
            _blank.timeoutSecs = (blankSecs < DISPLAY_BLANK_TIMEOUT_MIN) ?
                                  DISPLAY_BLANK_TIMEOUT_DEFAULT : blankSecs;
        }
        p += 2;
        // Timezone offset (signed int16 LE, minutes east of UTC). 0xFFFF = uninitialised.
        uint8_t tzLo = EEPROM.read(p);
        uint8_t tzHi = EEPROM.read(p + 1);
        uint16_t tzRaw = (uint16_t)tzLo | ((uint16_t)tzHi << 8);
        if (tzRaw == 0xFFFF) {
            _tzOffsetMinutes = 0;   // default UTC
        } else {
            _tzOffsetMinutes = (int16_t)tzRaw;
        }
        p += 2;
        // Display mode (1 byte: 0=port, 1=clock, 2=cycle). 0xFF = uninitialised → default port.
        uint8_t dm = EEPROM.read(p);
        if      (dm == (uint8_t)DisplayMode::Clock) _displayMode = DisplayMode::Clock;
        else if (dm == (uint8_t)DisplayMode::Cycle) _displayMode = DisplayMode::Cycle;
        else                                         _displayMode = DisplayMode::Port;
        p += 1;
        // Default port (1 byte: 0=GROUND, 1-7=ANT, 0xFF=unset→GROUND).
        uint8_t dp = EEPROM.read(p);
        _defaultPort = (dp <= 7) ? dp : 0;
        p += 1;
        // Startup port mode (1 byte: 0=default_port, 1=last_port). 0xFF=unset→default.
        uint8_t spm = EEPROM.read(p);
        _startupPortMode = (spm == 1) ? 1 : 0;

        if (_mqtt.port == 0) _mqtt.port = MQTT_DEFAULT_PORT;
        if (_deviceName[0] == '\0') {
            strncpy(_deviceName, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAXLEN);
            _deviceName[DEVICE_NAME_MAXLEN] = '\0';
        }
        if (_ntp.host[0] == '\0') {
            strncpy(_ntp.host, NTP_HOST_DEFAULT, NTP_HOST_MAXLEN);
            _ntp.host[NTP_HOST_MAXLEN] = '\0';
        }
        if (_ntp.port == 0) _ntp.port = NTP_PORT_DEFAULT;
    }

    const MqttConfig        &mqtt()  const { return _mqtt;  }
    const NtpConfig         &ntp()   const { return _ntp;   }
    const DisplayBlankConfig &blank() const { return _blank; }

    // Display mode: which value is shown prominently on the OLED status screen.
    DisplayMode displayMode() const { return _displayMode; }
    void setDisplayMode(DisplayMode m) { _displayMode = m; saveAll(); }

    // Startup port mode: 0 = use stored default_port on boot, 1 = restore last used port.
    uint8_t startupPortMode() const { return _startupPortMode; }
    void setStartupPortMode(uint8_t m) {
        _startupPortMode = (m == 1) ? 1 : 0;
        saveAll();
    }

    // Default port: the antenna position selected on boot (0=GROUND, 1-7=ANT1-ANT7).
    // Clamped to [0, maxAntennas] at call time so callers must pass the current max.
    uint8_t defaultPort() const { return _defaultPort; }
    uint8_t defaultPortClamped(int8_t maxAntennas) const {
        return (_defaultPort > (uint8_t)maxAntennas) ? 0 : _defaultPort;
    }
    void setDefaultPort(uint8_t port) {
        if (port > 7) port = 0;
        _defaultPort = port;
        saveAll();
    }
    // Called when max antennas is reduced: if the stored default is now out of range,
    // reset it to GROUND (0) and persist.
    void clampDefaultPort(int8_t maxAntennas) {
        if (_defaultPort > (uint8_t)maxAntennas) {
            _defaultPort = 0;
            saveAll();
        }
    }

    // Timezone offset in minutes east of UTC (e.g. +60 = UTC+1, -300 = UTC-5).
    // Range: -720..+840. Stored as signed int16 LE in EEPROM.
    int16_t tzOffsetMinutes() const { return _tzOffsetMinutes; }

    void setTzOffset(int16_t minutes) {
        if (minutes < -720) minutes = -720;
        if (minutes >  840) minutes =  840;
        _tzOffsetMinutes = minutes;
        saveAll();
    }

    // The device display name (shown on OLED splash + web UI header).
    const char *deviceName() const { return _deviceName; }

    void setDeviceName(const char *name) {
        strncpy(_deviceName, name, DEVICE_NAME_MAXLEN);
        _deviceName[DEVICE_NAME_MAXLEN] = '\0';
        for (int i = 0; _deviceName[i]; i++) {
            if ((uint8_t)_deviceName[i] < 0x20) _deviceName[i] = ' ';
        }
        int len = strlen(_deviceName);
        while (len > 0 && _deviceName[len-1] == ' ') _deviceName[--len] = '\0';
        saveAll();
    }

    void setMqtt(const MqttConfig &c) {
        _mqtt = c;
        if (_mqtt.port == 0) _mqtt.port = MQTT_DEFAULT_PORT;
        saveAll();
    }

    void setBlank(const DisplayBlankConfig &c) {
        _blank = c;
        if (_blank.timeoutSecs < DISPLAY_BLANK_TIMEOUT_MIN)
            _blank.timeoutSecs = DISPLAY_BLANK_TIMEOUT_DEFAULT;
        saveAll();
    }

    void setNtp(const NtpConfig &c) {
        _ntp = c;
        if (_ntp.port == 0) _ntp.port = NTP_PORT_DEFAULT;
        if (_ntp.host[0] == '\0') {
            strncpy(_ntp.host, NTP_HOST_DEFAULT, NTP_HOST_MAXLEN);
            _ntp.host[NTP_HOST_MAXLEN] = '\0';
        }
        saveAll();
    }

    void saveAll() {
        int base = EEPROM_SETTINGS_BASE;
        EEPROM.write(base, SETTINGS_MAGIC);
        uint8_t flags = 0;
        if (_mqtt.enabled)  flags |= 0x01;
        if (_mqtt.retain)   flags |= 0x02;
        if (_mqtt.commands) flags |= 0x04;
        EEPROM.write(base + 1, flags);
        EEPROM.write(base + 2, (uint8_t)(_mqtt.port & 0xFF));
        EEPROM.write(base + 3, (uint8_t)(_mqtt.port >> 8));
        int p = base + 4;
        p = writeField(p, _mqtt.host,   MQTT_HOST_MAXLEN);
        p = writeField(p, _mqtt.user,   MQTT_USER_MAXLEN);
        p = writeField(p, _mqtt.pass,   MQTT_PASS_MAXLEN);
        p = writeField(p, _mqtt.prefix, MQTT_PREFIX_MAXLEN);
        p = writeField(p, _deviceName,  DEVICE_NAME_MAXLEN);
        p = writeField(p, _ntp.host,    NTP_HOST_MAXLEN);
        EEPROM.write(p,     (uint8_t)(_ntp.port & 0xFF));
        EEPROM.write(p + 1, (uint8_t)(_ntp.port >> 8));
        p += 2;
        // Display blank config.
        uint8_t blankFlags = _blank.enabled ? 0x01 : 0x00;
        EEPROM.write(p,     blankFlags);
        EEPROM.write(p + 1, _blank.timeoutSecs);
        p += 2;
        // Timezone offset (signed int16 LE).
        uint16_t tzRaw = (uint16_t)_tzOffsetMinutes;
        EEPROM.write(p,     (uint8_t)(tzRaw & 0xFF));
        EEPROM.write(p + 1, (uint8_t)(tzRaw >> 8));
        p += 2;
        // Display mode (1 byte).
        EEPROM.write(p, (uint8_t)_displayMode);
        p += 1;
        // Default port (1 byte).
        EEPROM.write(p, _defaultPort);
        p += 1;
        // Startup port mode (1 byte).
        EEPROM.write(p, _startupPortMode);
        EEPROM.commit();
    }

    // Backward-compat alias.
    void saveMqtt() { saveAll(); }

private:
    int readField(int addr, char *dst, uint8_t maxlen) {
        int k = 0;
        for (; k < maxlen; k++) {
            uint8_t c = EEPROM.read(addr + k);
            if (c == 0 || c == 0xFF) break;
            dst[k] = (char)c;
        }
        dst[k] = '\0';
        return addr + maxlen + 1;
    }

    int writeField(int addr, const char *src, uint8_t maxlen) {
        int len = strnlen(src, maxlen);
        for (int k = 0; k < len; k++)       EEPROM.write(addr + k, (uint8_t)src[k]);
        for (int k = len; k <= maxlen; k++) EEPROM.write(addr + k, 0);
        return addr + maxlen + 1;
    }

    MqttConfig         _mqtt;
    NtpConfig          _ntp;
    DisplayBlankConfig _blank;
    int16_t            _tzOffsetMinutes = 0;          // default UTC
    DisplayMode        _displayMode     = DisplayMode::Port;
    uint8_t            _defaultPort     = 0;          // 0=GROUND (default)
    uint8_t            _startupPortMode = 0;          // 0=default_port, 1=last_port
    char               _deviceName[DEVICE_NAME_MAXLEN + 1] = {0};
};
