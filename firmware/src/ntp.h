// ntp.h — NTP time synchronisation using the ESP8266 Arduino core's built-in SNTP.
//
// Uses configTime() / time() from <time.h> — no extra library required.
// All times are UTC. Syncs on begin(), then re-syncs every NTP_RESYNC_MS (1 hour).
// Gracefully handles an unreachable server: synced() returns false until the first
// successful sync, and all timestamp helpers return "" / 0 until then.
//
// Usage:
//   g_ntp.begin("pool.ntp.org", 123);
//   g_ntp.loop();                        // call from loop()
//   if (g_ntp.synced()) Serial.println(g_ntp.isoNow());

#pragma once

#include <Arduino.h>
#include <time.h>
#include "debuglog.h"

static const uint32_t NTP_RESYNC_MS    = 3600000UL;  // re-sync every 1 hour
static const uint32_t NTP_RETRY_MS     = 30000UL;    // retry every 30s if not synced
static const uint32_t NTP_SYNC_WAIT_MS = 10000UL;    // wait up to 10s for first sync

class Ntp {
public:
    // Start SNTP with the given host and port. Safe to call again to reconfigure.
    void begin(const char *host, uint16_t port = 123) {
        if (!host || !host[0]) host = "pool.ntp.org";
        _host = String(host);
        _port = port;
        _synced = false;
        _lastSync = 0;
        _lastAttempt = 0;
        // ESP8266 SNTP: timezone offset 0 (UTC), no DST server, NTP server.
        // configTime(gmtOffset_sec, daylightOffset_sec, server1)
        configTime(0, 0, _host.c_str());
        LOGF("NTP: configured host=%s port=%u", _host.c_str(), _port);
        // Note: port is informational — configTime() always uses UDP/123 internally.
        // A custom port would require a raw UDP implementation; 123 is standard.
    }

    // Call from loop(). Checks sync status and triggers periodic re-sync.
    void loop() {
        if (_host.length() == 0) return;
        uint32_t now = millis();

        if (!_synced) {
            // Poll for first sync (time() returns > some epoch threshold when synced).
            time_t t = time(nullptr);
            if (t > 1700000000UL) {   // sanity: after 2023-11-14
                _synced = true;
                _lastSync = now;
                LOGF("NTP: synced — %s", isoNow().c_str());
            } else if (now - _lastAttempt > NTP_RETRY_MS) {
                _lastAttempt = now;
                // Re-issue configTime to prod the SNTP stack.
                configTime(0, 0, _host.c_str());
                LOG("NTP: waiting for sync...");
            }
        } else {
            // Periodic re-sync: re-issue configTime every hour.
            if (now - _lastSync > NTP_RESYNC_MS) {
                _lastSync = now;
                configTime(0, 0, _host.c_str());
                LOGF("NTP: re-sync triggered — %s", isoNow().c_str());
            }
        }
    }

    bool synced() const { return _synced; }

    // Current UTC time as a Unix timestamp (0 if not synced).
    time_t now() const {
        if (!_synced) return 0;
        return time(nullptr);
    }

    // Current UTC time as ISO 8601 string "2024-07-03T15:04:05Z".
    // Returns "" if not yet synced.
    String isoNow() const {
        if (!_synced) return String();
        return isoOf(time(nullptr));
    }

    // Current local time as ISO 8601 string with UTC offset suffix,
    // e.g. "2024-07-03T16:04:05+01:00" for offsetMinutes=+60.
    // Returns "" if not yet synced.
    String localIsoNow(int16_t offsetMinutes) const {
        if (!_synced) return String();
        return localIsoOf(time(nullptr), offsetMinutes);
    }

    // Format any Unix timestamp as ISO 8601 UTC ("2024-07-03T15:04:05Z").
    static String isoOf(time_t t) {
        if (t == 0) return String();
        struct tm *tm = gmtime(&t);
        if (!tm) return String();
        // Build manually to avoid -Wformat-truncation (compiler over-estimates tm_year range).
        char buf[21];   // exactly 20 chars + NUL
        int y = tm->tm_year + 1900;
        buf[ 0] = '0' + (y / 1000) % 10;
        buf[ 1] = '0' + (y /  100) % 10;
        buf[ 2] = '0' + (y /   10) % 10;
        buf[ 3] = '0' + (y       ) % 10;
        buf[ 4] = '-';
        buf[ 5] = '0' + (tm->tm_mon + 1) / 10;
        buf[ 6] = '0' + (tm->tm_mon + 1) % 10;
        buf[ 7] = '-';
        buf[ 8] = '0' + tm->tm_mday / 10;
        buf[ 9] = '0' + tm->tm_mday % 10;
        buf[10] = 'T';
        buf[11] = '0' + tm->tm_hour / 10;
        buf[12] = '0' + tm->tm_hour % 10;
        buf[13] = ':';
        buf[14] = '0' + tm->tm_min / 10;
        buf[15] = '0' + tm->tm_min % 10;
        buf[16] = ':';
        buf[17] = '0' + tm->tm_sec / 10;
        buf[18] = '0' + tm->tm_sec % 10;
        buf[19] = 'Z';
        buf[20] = '\0';
        return String(buf);
    }

    // Format any Unix timestamp as ISO 8601 with a fixed UTC offset,
    // e.g. "2024-07-03T16:04:05+01:00". offsetMinutes is signed (east = positive).
    static String localIsoOf(time_t t, int16_t offsetMinutes) {
        if (t == 0) return String();
        // Shift the timestamp by the offset before breaking into components.
        t += (time_t)offsetMinutes * 60;
        struct tm *tm = gmtime(&t);
        if (!tm) return String();
        // "2024-07-03T16:04:05+01:00" = 25 chars + NUL
        char buf[26];
        int y = tm->tm_year + 1900;
        buf[ 0] = '0' + (y / 1000) % 10;
        buf[ 1] = '0' + (y /  100) % 10;
        buf[ 2] = '0' + (y /   10) % 10;
        buf[ 3] = '0' + (y       ) % 10;
        buf[ 4] = '-';
        buf[ 5] = '0' + (tm->tm_mon + 1) / 10;
        buf[ 6] = '0' + (tm->tm_mon + 1) % 10;
        buf[ 7] = '-';
        buf[ 8] = '0' + tm->tm_mday / 10;
        buf[ 9] = '0' + tm->tm_mday % 10;
        buf[10] = 'T';
        buf[11] = '0' + tm->tm_hour / 10;
        buf[12] = '0' + tm->tm_hour % 10;
        buf[13] = ':';
        buf[14] = '0' + tm->tm_min / 10;
        buf[15] = '0' + tm->tm_min % 10;
        buf[16] = ':';
        buf[17] = '0' + tm->tm_sec / 10;
        buf[18] = '0' + tm->tm_sec % 10;
        // Offset suffix: +HH:MM or -HH:MM or Z for UTC.
        if (offsetMinutes == 0) {
            buf[19] = 'Z';
            buf[20] = '\0';
        } else {
            int absMin = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
            buf[19] = (offsetMinutes < 0) ? '-' : '+';
            buf[20] = '0' + (absMin / 60) / 10;
            buf[21] = '0' + (absMin / 60) % 10;
            buf[22] = ':';
            buf[23] = '0' + (absMin % 60) / 10;
            buf[24] = '0' + (absMin % 60) % 10;
            buf[25] = '\0';
        }
        return String(buf);
    }

private:
    String   _host;
    uint16_t _port       = 123;
    bool     _synced     = false;
    uint32_t _lastSync   = 0;
    uint32_t _lastAttempt = 0;
};
