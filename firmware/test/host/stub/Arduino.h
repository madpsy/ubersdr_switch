#pragma once
// Host-side stand-in for the parts of the ESP8266 Arduino core the firmware uses.
// Enough to compile src/*.h and src/main.cpp on a PC and drive them from tests:
// a controllable clock (millis/delay), pin levels (digitalRead/digitalWrite), the
// Arduino String class, and ESP.* (restart() throws EspRestart so a test can see it).
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>

// ---- flash strings ------------------------------------------------------------------
struct __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))
#define PSTR(x) (x)
#define PROGMEM

// ---- String -------------------------------------------------------------------------
#define HEX 16
#define DEC 10
class String {
public:
    std::string s;
    String() {}
    String(const char *c) : s(c ? c : "") {}
    String(const std::string &x) : s(x) {}
    String(const __FlashStringHelper *f) : s(reinterpret_cast<const char *>(f)) {}
    explicit String(char c) : s(1, c) {}
    String(int v, unsigned char base = 10) { fmt((long long)v, base); }
    String(unsigned v, unsigned char base = 10) { fmtu(v, base); }
    String(long v, unsigned char base = 10) { fmt(v, base); }
    String(unsigned long v, unsigned char base = 10) { fmtu(v, base); }
    String(long long v) { fmt(v, 10); }
    String(unsigned long long v) { fmtu(v, 10); }
    String(unsigned char v, unsigned char base = 10) { fmtu(v, base); }
    String(double v, unsigned char dp = 2) { char b[64]; snprintf(b, sizeof b, "%.*f", dp, v); s = b; }

    const char *c_str() const { return s.c_str(); }
    unsigned length() const { return (unsigned)s.size(); }
    bool reserve(unsigned) { return true; }
    char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }
    char &operator[](unsigned i) { return s[i]; }
    char charAt(unsigned i) const { return (*this)[i]; }

    String &operator+=(const String &o) { s += o.s; return *this; }
    String &operator+=(const char *o) { s += o; return *this; }
    String &operator+=(char c) { s += c; return *this; }
    String &operator+=(int v) { s += String(v).s; return *this; }
    String &operator+=(unsigned v) { s += String(v).s; return *this; }
    String &operator+=(long v) { s += String(v).s; return *this; }
    String &operator+=(unsigned long v) { s += String(v).s; return *this; }
    String &operator+=(const __FlashStringHelper *f) { s += reinterpret_cast<const char *>(f); return *this; }
    bool concat(const String &o) { s += o.s; return true; }

    bool operator==(const String &o) const { return s == o.s; }
    bool operator==(const char *o) const { return s == o; }
    bool operator!=(const String &o) const { return s != o.s; }
    bool operator!=(const char *o) const { return s != o; }
    bool operator<(const String &o) const { return s < o.s; }
    bool equals(const String &o) const { return s == o.s; }
    bool equalsIgnoreCase(const String &o) const {
        if (s.size() != o.s.size()) return false;
        for (size_t i = 0; i < s.size(); i++) if (tolower(s[i]) != tolower(o.s[i])) return false;
        return true;
    }

    int indexOf(const char *n, unsigned from = 0) const { auto p = s.find(n, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const String &n, unsigned from = 0) const { return indexOf(n.c_str(), from); }
    int indexOf(char c, unsigned from = 0) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(const char *n) const { auto p = s.rfind(n); return p == std::string::npos ? -1 : (int)p; }
    String substring(unsigned a) const { return a >= s.size() ? String() : String(s.substr(a)); }
    String substring(unsigned a, unsigned b) const {
        if (a > b) std::swap(a, b);
        if (a >= s.size()) return String();
        return String(s.substr(a, std::min<size_t>(b, s.size()) - a));
    }
    long toInt() const { return strtol(s.c_str(), nullptr, 10); }
    float toFloat() const { return strtof(s.c_str(), nullptr); }
    bool startsWith(const String &p) const { return s.compare(0, p.s.size(), p.s) == 0; }
    bool endsWith(const String &p) const { return s.size() >= p.s.size() && s.compare(s.size() - p.s.size(), p.s.size(), p.s) == 0; }
    bool endsWith(const char *p) const { return endsWith(String(p)); }
    void remove(unsigned i) { if (i < s.size()) s.erase(i); }
    void remove(unsigned i, unsigned n) { if (i < s.size()) s.erase(i, n); }
    void replace(const String &a, const String &b) {
        if (a.s.empty()) return;
        size_t p = 0;
        while ((p = s.find(a.s, p)) != std::string::npos) { s.replace(p, a.s.size(), b.s); p += b.s.size(); }
    }
    void replace(char a, char b) { for (auto &c : s) if (c == a) c = b; }
    void toUpperCase() { for (auto &c : s) c = (char)toupper((unsigned char)c); }
    void toLowerCase() { for (auto &c : s) c = (char)tolower((unsigned char)c); }
    void trim() {
        while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
        size_t i = 0; while (i < s.size() && isspace((unsigned char)s[i])) i++;
        s.erase(0, i);
    }
    void toCharArray(char *buf, unsigned size) const {
        if (!size) return;
        size_t n = std::min<size_t>(size - 1, s.size());
        memcpy(buf, s.data(), n); buf[n] = 0;
    }

private:
    void fmt(long long v, unsigned base) {
        if (base == 10) { s = std::to_string(v); return; }
        fmtu((unsigned long long)v, base);
    }
    void fmtu(unsigned long long v, unsigned base) {
        if (base == 10) { s = std::to_string(v); return; }
        const char *d = "0123456789abcdef"; std::string o;
        do { o.insert(o.begin(), d[v % base]); v /= base; } while (v);
        s = o;
    }
};
inline String operator+(const String &a, const String &b) { return String(a.s + b.s); }
inline String operator+(const String &a, const char *b) { return String(a.s + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
inline String operator+(const String &a, char b) { return String(a.s + b); }
inline String operator+(const String &a, int b) { return String(a.s + std::to_string(b)); }
inline String operator+(const String &a, const __FlashStringHelper *b) { return String(a.s + reinterpret_cast<const char *>(b)); }

// ---- clock --------------------------------------------------------------------------
// g_millis is the fake millis() clock. delay() advances it and runs g_delayHook (if set)
// so a test can change pin levels part-way through a blocking firmware loop.
extern unsigned long g_millis;
extern std::function<void()> g_delayHook;
inline unsigned long millis() { return g_millis; }
inline unsigned long micros() { return g_millis * 1000UL; }
inline void delay(unsigned long ms) { g_millis += ms; if (g_delayHook) g_delayHook(); }
inline void delayMicroseconds(unsigned) {}
inline void yield() {}

// ---- pins ---------------------------------------------------------------------------
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define OUTPUT_OPEN_DRAIN 3
extern int g_pinLevel[32];   // what digitalRead() returns (default HIGH = released)
extern int g_pinOut[32];     // the last digitalWrite() per pin
extern int g_pinMode[32];
inline void pinMode(uint8_t p, int m) { g_pinMode[p] = m; }
inline void digitalWrite(uint8_t p, int v) { g_pinOut[p] = v; }
inline int digitalRead(uint8_t p) { return g_pinLevel[p]; }

// ---- misc ---------------------------------------------------------------------------
inline bool isAlphaNumeric(char c) { return isalnum((unsigned char)c); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
template <class T, class L, class H> inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

struct HardwareSerial {
    void begin(unsigned long) {}
    template <class T> void print(const T &) {}
    template <class T> void println(const T &) {}
    void println() {}
    void flush() {}
};
extern HardwareSerial Serial;

struct EspRestart {};   // thrown by ESP.restart()
struct EspClass {
    unsigned getFreeHeap() { return 30000; }
    String getResetReason() { return "External System"; }
    const char *getSdkVersion() { return "2.2.2-dev"; }
    String getCoreVersion() { return "3.1.2"; }
    uint32_t getChipId() { return 0x45ca21; }
    uint32_t getFlashChipId() { return 0x1640ef; }
    uint32_t getFlashChipRealSize() { return 4194304; }
    uint8_t getBootVersion() { return 31; }
    uint32_t getSketchSize() { return 488000; }
    uint32_t getFreeSketchSpace() { return 2621440; }
    [[noreturn]] void restart() { throw EspRestart(); }
};
extern EspClass ESP;

// The NTP code calls libc time()/configTime(). The fake clock lives in fakes.cpp:
// g_epoch == 0 means "not synced" (time() returns a 1970 value).
extern time_t g_epoch;
extern int g_configTimeCalls;
extern std::string g_configTimeServer;   // the server the last configTime() was given
inline void configTime(long, int, const char *server) { g_configTimeCalls++; g_configTimeServer = server ? server : ""; }
