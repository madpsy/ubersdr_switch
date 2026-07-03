// debuglog.h — lightweight in-memory debug log with optional serial mirror.
//
// GPIO1 (UART0 TX) and GPIO3 (UART0 RX) are used as the DOWN/UP buttons on this
// board, so ordinary Serial debugging conflicts with them. This logger keeps a
// rolling in-RAM buffer of recent messages that can be viewed over WiFi at the
// "/debug" HTTP endpoint (no pin conflict), and — only when REPLICA_DEBUG is
// defined — also mirrors each line to Serial (which will disturb the buttons).
//
// When NTP is available, log lines are prefixed with an ISO8601 UTC timestamp
// instead of the raw millis() counter. Call DebugLog::setTimeFn() once NTP is
// initialised to enable this.
//
// Usage:
//   LOG("boot");
//   LOGF("wifi status=%d ip=%s", st, ip.c_str());
//   DebugLog::setTimeFn([]() -> String { return g_ntp.isoNow(); });
//   String dump = DebugLog::instance().text();   // for the /debug page

#pragma once

#include <Arduino.h>

class DebugLog {
public:
    // Optional: supply a function that returns the current ISO8601 UTC timestamp.
    // When set and it returns a non-empty string, log lines use that instead of millis().
    typedef String (*TimeFn)();
    static void setTimeFn(TimeFn fn) { _timeFn = fn; }

    static DebugLog &instance() {
        static DebugLog inst;
        return inst;
    }

    void begin() {
#ifdef REPLICA_DEBUG
        Serial.begin(115200);
        Serial.println();
        Serial.println(F("[dbg] serial logging enabled (NOTE: conflicts with UP/DOWN buttons)"));
#endif
    }

    void log(const String &msg) {
        String prefix;
        if (_timeFn) {
            String ts = _timeFn();
            if (ts.length() > 0) {
                prefix = "[" + ts + "] ";
            }
        }
        if (prefix.length() == 0) {
            char ts[16];
            snprintf(ts, sizeof(ts), "[%7lu] ", (unsigned long)millis());
            prefix = String(ts);
        }
        String line = prefix + msg;

        _buf[_head] = line;
        _head = (_head + 1) % CAP;
        if (_count < CAP) _count++;

#ifdef REPLICA_DEBUG
        Serial.println(line);
        Serial.flush();
#endif
    }

    void logf(const char *fmt, ...) {
        char tmp[192];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
        log(String(tmp));
    }

    // Oldest-to-newest joined text (for the /debug endpoint).
    String text() const {
        String out;
        out.reserve(_count * 56);
        int idx = (_head - _count + CAP) % CAP;
        for (int i = 0; i < _count; i++) {
            out += _buf[idx];
            out += '\n';
            idx = (idx + 1) % CAP;
        }
        return out;
    }

private:
    static const int CAP = 40;
    String  _buf[CAP];
    int     _head  = 0;
    int     _count = 0;

    static TimeFn _timeFn;
};

// Definition of the static member (in header — only one TU includes this).
inline DebugLog::TimeFn DebugLog::_timeFn = nullptr;

#define LOG(msg)      DebugLog::instance().log(String(msg))
#define LOGF(...)     DebugLog::instance().logf(__VA_ARGS__)
