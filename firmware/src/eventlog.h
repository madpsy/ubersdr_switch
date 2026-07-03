// eventlog.h — circular buffer of the last 25 antenna-change events.
//
// Each event records: ISO8601 UTC timestamp (or "" if NTP not synced), source
// ("button"/"api"/"web"/"mqtt"), antenna position, and label. The buffer is
// in-RAM only (not persisted); it resets on reboot.
//
// Usage:
//   g_events.push(g_ntp.isoNow(), "api", g_ant.position(), g_ant.label());
//   String json = g_events.toJson();   // for GET /api/events

#pragma once

#include <Arduino.h>

static const int EVENT_LOG_CAP = 25;

struct Event {
    String   ts;       // ISO8601 UTC or "" if NTP not synced
    String   source;   // "button" / "api" / "web" / "mqtt"
    int8_t   position; // 0=GROUND, 1..7=ANT1..7
    String   label;    // human label ("GROUND", "ANT: 3", custom name, …)
};

class EventLog {
public:
    // Push a new event (oldest is silently dropped when full).
    void push(const String &ts, const char *source, int8_t position, const String &label) {
        _buf[_head].ts       = ts;
        _buf[_head].source   = String(source);
        _buf[_head].position = position;
        _buf[_head].label    = label;
        _head = (_head + 1) % EVENT_LOG_CAP;
        if (_count < EVENT_LOG_CAP) _count++;
    }

    int count() const { return _count; }

    // Serialise to a JSON array, newest-first.
    // Each element: {"ts":"…","source":"…","position":N,"label":"…"}
    String toJson() const {
        String out;
        out.reserve(_count * 80 + 4);
        out += '[';
        bool first = true;
        // Walk newest-to-oldest.
        for (int i = 0; i < _count; i++) {
            int idx = (_head - 1 - i + EVENT_LOG_CAP) % EVENT_LOG_CAP;
            const Event &e = _buf[idx];
            if (!first) out += ',';
            first = false;
            out += F("{\"ts\":\"");
            out += e.ts;
            out += F("\",\"source\":\"");
            out += e.source;
            out += F("\",\"position\":");
            out += String(e.position);
            out += F(",\"label\":\"");
            // Escape label (strip quotes/backslashes for safety).
            for (unsigned int k = 0; k < e.label.length(); k++) {
                char c = e.label[k];
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += F("\"}");
        }
        out += ']';
        return out;
    }

private:
    Event _buf[EVENT_LOG_CAP];
    int   _head  = 0;
    int   _count = 0;
};
