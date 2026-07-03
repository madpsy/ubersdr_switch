// settings.h — persistent device settings (MQTT config + NTP config + device name)
// stored in the shared EEPROM sector, AFTER the antenna block. Because it lives in
// EEPROM (not LittleFS) the configuration survives a filesystem reflash (`uploadfs`),
// like the antenna names and max value. A single EEPROM.begin(EEPROM_TOTAL_SIZE) is
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

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// NTP configuration (host + port, persisted in EEPROM).
struct NtpConfig {
    char     host[NTP_HOST_MAXLEN + 1] = {0};
    uint16_t port = NTP_PORT_DEFAULT;
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

    const MqttConfig &mqtt() const { return _mqtt; }
    const NtpConfig  &ntp()  const { return _ntp;  }

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

    MqttConfig _mqtt;
    NtpConfig  _ntp;
    char       _deviceName[DEVICE_NAME_MAXLEN + 1] = {0};
};
