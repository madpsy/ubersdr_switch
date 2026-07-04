// schedule.h — Time-based antenna scheduler (up to SCHED_MAX_ENTRIES entries).
//
// Each entry fires once per matching minute (local time, using the stored
// tz_offset) on the selected day(s) of the week, switching to the configured
// antenna position. Entries can be individually enabled/disabled.
//
// EEPROM layout (base = EEPROM_SCHED_BASE):
//   [0]          magic SCHED_MAGIC (0xC3)
//   [1]          global_flags:
//                  bit0 = respect_lock  (scheduler won't fire while locked)
//                  bit1 = sched_disabled (inverted: 0 = enabled, 1 = disabled)
//                         Stored inverted so erased flash (0xFF) → enabled.
//   [2 + i*5]    flags:     bit0 = enabled
//   [3 + i*5]    days_mask: bits 0..6 = Sun..Sat (0x7F = every day)
//   [4 + i*5]    hour       (0–23, local time)
//   [5 + i*5]    minute     (0–59)
//   [6 + i*5]    position   (0–7: 0=GROUND, 1..7=ANT1..ANT7)
//
// The "last fired" state is kept in RAM only — it resets on reboot, which is
// fine: the worst case is one extra fire on boot if the current minute matches.
//
// The scheduler will not fire if NTP is not synced (caller must check
// g_ntp.synced() before calling tick()). If respect_lock is true and the
// switch is locked, tick() returns -1 without firing.
//
// Usage:
//   g_sched.begin();                                    // load from EEPROM
//   // in loop() 250 ms tick, only when NTP synced:
//   int pos = g_sched.tick(localHour, localMin, localWday, g_locked);
//   if (pos >= 0) { g_ant.setPosition(pos); logEvent("schedule"); }

#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"
#include "debuglog.h"

// Bitmask helpers for days_mask.
// Bit positions: 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat.
static const uint8_t SCHED_DAY_SUN = 0x01;
static const uint8_t SCHED_DAY_MON = 0x02;
static const uint8_t SCHED_DAY_TUE = 0x04;
static const uint8_t SCHED_DAY_WED = 0x08;
static const uint8_t SCHED_DAY_THU = 0x10;
static const uint8_t SCHED_DAY_FRI = 0x20;
static const uint8_t SCHED_DAY_SAT = 0x40;
static const uint8_t SCHED_DAY_ALL = 0x7F;   // every day

struct ScheduleEntry {
    bool    enabled   = false;
    uint8_t daysMask  = SCHED_DAY_ALL;  // which days to fire (bitmask)
    uint8_t hour      = 0;              // local hour (0–23)
    uint8_t minute    = 0;              // local minute (0–59)
    uint8_t position  = 0;             // antenna position (0–7)
};

class Schedule {
public:
    // Load entries from EEPROM. Antenna::begin() must have already called
    // EEPROM.begin(EEPROM_TOTAL_SIZE). If the region is uninitialised, all
    // entries default to disabled and the scheduler defaults to enabled.
    void begin() {
        int base = EEPROM_SCHED_BASE;
        if (EEPROM.read(base) != SCHED_MAGIC) {
            LOGF("Schedule: EEPROM uninitialised (magic=0x%02X), using defaults",
                 EEPROM.read(base));
            return;
        }
        // Global flags byte (byte 1).
        // bit0 = respect_lock
        // bit1 = sched_disabled (inverted: bit1=0 → enabled, bit1=1 → disabled)
        uint8_t gflags = EEPROM.read(base + 1);
        _respectLock  = (gflags & 0x01) != 0;
        _schedEnabled = !(gflags & 0x02);   // bit1=0 → enabled (default on erased flash)
        // Entries start at byte 2.
        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            int p = base + 2 + i * SCHED_ENTRY_SIZE;
            uint8_t flags = EEPROM.read(p);
            uint8_t days  = EEPROM.read(p + 1);
            uint8_t hr    = EEPROM.read(p + 2);
            uint8_t mn    = EEPROM.read(p + 3);
            uint8_t pos   = EEPROM.read(p + 4);
            _entries[i].enabled  = (flags & 0x01) != 0;
            _entries[i].daysMask = (days <= 0x7F) ? days : SCHED_DAY_ALL;
            _entries[i].hour     = (hr  <= 23) ? hr  : 0;
            _entries[i].minute   = (mn  <= 59) ? mn  : 0;
            _entries[i].position = (pos <=  7) ? pos : 0;
        }
        LOGF("Schedule: loaded from EEPROM (enabled=%d respectLock=%d)",
             (int)_schedEnabled, (int)_respectLock);
    }

    // Master on/off switch. When disabled, tick() always returns -1.
    // Persisted in the global flags byte (bit1, inverted).
    bool schedEnabled() const { return _schedEnabled; }
    void setSchedEnabled(bool v) { _schedEnabled = v; saveAll(); }

    // Global "respect lock" flag: when true, the scheduler will not fire while
    // the antenna switch is locked. Persisted in the schedule EEPROM header.
    bool respectLock() const { return _respectLock; }
    void setRespectLock(bool v) { _respectLock = v; saveAll(); }

    void saveAll() {
        int base = EEPROM_SCHED_BASE;
        EEPROM.write(base, SCHED_MAGIC);
        // bit0 = respect_lock, bit1 = sched_disabled (inverted)
        uint8_t gflags = 0;
        if (_respectLock)  gflags |= 0x01;
        if (!_schedEnabled) gflags |= 0x02;   // disabled → set bit
        EEPROM.write(base + 1, gflags);
        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            int p = base + 2 + i * SCHED_ENTRY_SIZE;
            EEPROM.write(p,     _entries[i].enabled ? 0x01 : 0x00);
            EEPROM.write(p + 1, _entries[i].daysMask & 0x7F);
            EEPROM.write(p + 2, _entries[i].hour);
            EEPROM.write(p + 3, _entries[i].minute);
            EEPROM.write(p + 4, _entries[i].position);
        }
        EEPROM.commit();
        LOG("Schedule: saved to EEPROM");
    }

    // Read/write a single entry (0-indexed). setEntry() saves immediately.
    const ScheduleEntry &entry(uint8_t i) const { return _entries[i]; }

    void setEntry(uint8_t i, const ScheduleEntry &e) {
        if (i >= SCHED_MAX_ENTRIES) return;
        _entries[i] = e;
        // Clamp values.
        if (_entries[i].daysMask > 0x7F) _entries[i].daysMask = SCHED_DAY_ALL;
        if (_entries[i].hour     > 23)   _entries[i].hour     = 0;
        if (_entries[i].minute   > 59)   _entries[i].minute   = 0;
        if (_entries[i].position >  7)   _entries[i].position = 0;
        // Reset the "last fired" state for this slot so it can fire immediately
        // if the new time matches the current minute.
        _lastFiredMin[i] = 0xFFFF;
        saveAll();
    }

    void deleteEntry(uint8_t i) {
        if (i >= SCHED_MAX_ENTRIES) return;
        _entries[i] = ScheduleEntry();   // reset to defaults (disabled)
        _lastFiredMin[i] = 0xFFFF;
        saveAll();
    }

    // Disable any enabled entries whose position exceeds `maxPos`.
    // Called whenever the max-antennas value is reduced so that schedule entries
    // targeting ports that are no longer selectable are silently paused (not deleted).
    // Returns the number of entries that were disabled.
    uint8_t disableEntriesAbove(uint8_t maxPos) {
        uint8_t count = 0;
        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (_entries[i].enabled && _entries[i].position > maxPos) {
                _entries[i].enabled = false;
                count++;
            }
        }
        if (count) {
            saveAll();
            LOGF("Schedule: disabled %u entr%s with position > %u (max reduced)",
                 count, count == 1 ? "y" : "ies", maxPos);
        }
        return count;
    }

    // Call once per minute (or more often — it is idempotent within a minute).
    // `localHour`  : 0–23 (local time, tz offset already applied by caller)
    // `localMinute`: 0–59
    // `localWday`  : 0=Sun … 6=Sat (struct tm tm_wday convention)
    // `locked`     : current lock state — if respectLock() is true and locked
    //                is true, the scheduler will not fire.
    //
    // Returns the position to switch to (0–7) if any enabled entry fires this
    // minute, or -1 if nothing fires. If multiple entries match the same minute,
    // the lowest-indexed one wins (caller fires once).
    int tick(uint8_t localHour, uint8_t localMinute, uint8_t localWday,
             bool locked = false) {
        if (!_schedEnabled) return -1;
        if (_respectLock && locked) return -1;

        // Pack hour+minute into a 16-bit key to detect re-fires within the same minute.
        uint16_t minKey = (uint16_t)localHour * 60 + localMinute;

        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            const ScheduleEntry &e = _entries[i];
            if (!e.enabled) continue;
            if (e.hour   != localHour)   continue;
            if (e.minute != localMinute) continue;
            // Check day-of-week bitmask.
            if (!(e.daysMask & (1 << localWday))) continue;
            // Suppress re-fire within the same minute.
            if (_lastFiredMin[i] == minKey) continue;
            _lastFiredMin[i] = minKey;
            LOGF("Schedule: entry %u fired -> position %u (wday=%u %02u:%02u)",
                 i, e.position, localWday, localHour, localMinute);
            return (int)e.position;
        }
        return -1;
    }

    // Find the next enabled entry that will fire, given the current local time.
    // Returns a compact string "HH:MM ANT n" / "HH:MM GROUND" or "" if none.
    // Scans forward up to 7 days to find the next match.
    String nextFiring(uint8_t localHour, uint8_t localMinute, uint8_t localWday) const {
        if (!_schedEnabled) return String();
        int bestMins = -1;
        uint8_t bestIdx = 0;
        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            const ScheduleEntry &e = _entries[i];
            if (!e.enabled) continue;
            for (int dayOffset = 0; dayOffset <= 6; dayOffset++) {
                uint8_t wday = (localWday + dayOffset) % 7;
                if (!(e.daysMask & (1 << wday))) continue;
                int entryMins = (int)e.hour * 60 + (int)e.minute;
                int nowMins   = (int)localHour * 60 + (int)localMinute;
                int delta;
                if (dayOffset == 0) {
                    delta = entryMins - nowMins;
                    if (delta <= 0) continue;
                } else {
                    delta = dayOffset * 1440 - nowMins + entryMins;
                }
                if (bestMins < 0 || delta < bestMins) {
                    bestMins = delta;
                    bestIdx  = i;
                }
                break;
            }
        }
        if (bestMins < 0) return String();
        const ScheduleEntry &e = _entries[bestIdx];
        char buf[24];
        if (e.position == 0) {
            snprintf(buf, sizeof(buf), "%02u:%02u GROUND", e.hour, e.minute);
        } else {
            snprintf(buf, sizeof(buf), "%02u:%02u ANT %u", e.hour, e.minute, e.position);
        }
        return String(buf);
    }

    // JSON object: {"enabled":true,"respect_lock":false,"entries":[{"id":0,...}, ...]}
    String toJson() const {
        String j = F("{\"enabled\":");
        j += (_schedEnabled ? F("true") : F("false"));
        j += F(",\"respect_lock\":");
        j += (_respectLock ? F("true") : F("false"));
        j += F(",\"entries\":[");
        for (uint8_t i = 0; i < SCHED_MAX_ENTRIES; i++) {
            if (i) j += ",";
            const ScheduleEntry &e = _entries[i];
            j += F("{\"id\":");        j += String(i);
            j += F(",\"enabled\":");   j += (e.enabled ? F("true") : F("false"));
            j += F(",\"days\":");      j += String(e.daysMask);
            j += F(",\"hour\":");      j += String(e.hour);
            j += F(",\"minute\":");    j += String(e.minute);
            j += F(",\"position\":"); j += String(e.position);
            j += "}";
        }
        j += "]}";
        return j;
    }

private:
    bool          _schedEnabled = true;    // master on/off (default enabled)
    bool          _respectLock  = false;
    ScheduleEntry _entries[SCHED_MAX_ENTRIES];
    // Last minute-key (hour*60+min) at which each entry fired. 0xFFFF = never.
    uint16_t      _lastFiredMin[SCHED_MAX_ENTRIES] = {
        0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF,
        0xFFFF,0xFFFF,0xFFFF,0xFFFF,0xFFFF
    };
};
