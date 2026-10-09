#pragma once
// harness.h — the shared test harness for the host-side firmware tests.
//
//   check(cond, "what")        one assertion; prints ok/FAIL
//   section("name")            a heading
//   J / parseJson()            a strict little JSON parser (is what we publish valid?)
//   Device / boot()            run a block as a FRESH POWER-ON in a forked process:
//                              main.cpp's globals start from scratch every time, and
//                              what the device persists (EEPROM, LittleFS, saved WiFi)
//                              is carried to the next boot() — so a test can reboot.
//   finish()                   print the totals; returns the process exit code
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "fakes.h"

struct Counters { int pass, fail, known; char lastFail[160]; };
inline Counters *counters() {
    static Counters *c = [] {
        void *p = mmap(nullptr, sizeof(Counters), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        memset(p, 0, sizeof(Counters));
        return (Counters *)p;
    }();
    return c;
}
inline void check(bool ok, const std::string &what) {
    std::printf("   %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    Counters *c = counters();
    if (ok) c->pass++; else { c->fail++; snprintf(c->lastFail, sizeof c->lastFail, "%s", what.c_str()); }
    std::fflush(stdout);
}
// A bug the suite has found but the firmware has not fixed yet. `stillBuggy` asserts
// TODAY's (wrong) behaviour, so the run stays green while the bug is documented. When
// someone fixes it, this check FAILS: replace it with a normal check() of the right
// behaviour.
inline void known(bool stillBuggy, const std::string &bug) {
    Counters *c = counters();
    if (stillBuggy) { std::printf("   KNOWN BUG  %s\n", bug.c_str()); c->known++; }
    else { std::printf("   FAIL  behaviour changed (fixed?): %s — turn this known() into a check()\n", bug.c_str()); c->fail++; }
    std::fflush(stdout);
}
inline void section(const char *s) { std::printf("\n-- %s --\n", s); std::fflush(stdout); }
inline int finish(const char *suite) {
    Counters *c = counters();
    if (c->known) std::printf("\n%s: %d passed, %d failed, %d known bug(s)\n", suite, c->pass, c->fail, c->known);
    else std::printf("\n%s: %d passed, %d failed\n", suite, c->pass, c->fail);
    return c->fail ? 1 : 0;
}

// ---- JSON ---------------------------------------------------------------------------
struct J {
    enum K { Null, Bool, Num, Str, Arr, Obj } k = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<J> a; std::map<std::string, J> o;
    const J *get(const std::string &key) const { auto it = o.find(key); return k == Obj && it != o.end() ? &it->second : nullptr; }
    const J &operator[](const std::string &key) const { static J none; const J *x = get(key); return x ? *x : none; }
    const J &operator[](size_t i) const { static J none; return k == Arr && i < a.size() ? a[i] : none; }
    bool isNull() const { return k == Null; }
};
struct JsonParser {
    const std::string &t; size_t i = 0;
    explicit JsonParser(const std::string &s) : t(s) {}
    void ws() { while (i < t.size() && isspace((unsigned char)t[i])) i++; }
    bool lit(const char *w) { size_t n = strlen(w); if (t.compare(i, n, w) == 0) { i += n; return true; } return false; }
    bool str(std::string &out) {
        if (i >= t.size() || t[i] != '"') return false;
        for (i++; i < t.size(); i++) {
            char c = t[i];
            if (c == '"') { i++; return true; }
            if ((unsigned char)c < 0x20) return false;
            if (c == '\\') {
                if (++i >= t.size()) return false;
                c = t[i];
                if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'u') { i += 4; c = '?'; }
                else if (!strchr("\"\\/bfr", c)) return false;
            }
            out += c;
        }
        return false;
    }
    bool val(J &v, int depth = 0) {
        if (depth > 32) return false;
        ws(); if (i >= t.size()) return false;
        char c = t[i];
        if (c == '{') {
            v.k = J::Obj; i++; ws(); if (i < t.size() && t[i] == '}') { i++; return true; }
            for (;;) { ws(); std::string key; if (!str(key)) return false; ws(); if (i >= t.size() || t[i++] != ':') return false;
                       J x; if (!val(x, depth + 1)) return false; if (v.o.count(key)) return false; v.o[key] = x; ws();
                       if (i < t.size() && t[i] == ',') { i++; continue; } if (i < t.size() && t[i] == '}') { i++; return true; } return false; }
        }
        if (c == '[') {
            v.k = J::Arr; i++; ws(); if (i < t.size() && t[i] == ']') { i++; return true; }
            for (;;) { J x; if (!val(x, depth + 1)) return false; v.a.push_back(x); ws();
                       if (i < t.size() && t[i] == ',') { i++; continue; } if (i < t.size() && t[i] == ']') { i++; return true; } return false; }
        }
        if (c == '"') { v.k = J::Str; return str(v.s); }
        if (lit("true")) { v.k = J::Bool; v.b = true; return true; }
        if (lit("false")) { v.k = J::Bool; return true; }
        if (lit("null")) { v.k = J::Null; return true; }
        size_t st = i; if (t[i] == '-') i++;
        if (i >= t.size() || !isdigit((unsigned char)t[i])) return false;
        while (i < t.size() && (isdigit((unsigned char)t[i]) || t[i] == '.' || t[i] == 'e' || t[i] == 'E' || t[i] == '+' || t[i] == '-')) i++;
        v.k = J::Num; v.n = std::stod(t.substr(st, i - st)); return true;
    }
};
// Strict: one value, nothing after it, no duplicate keys.
inline bool parseJson(const std::string &s, J &out) { out = J(); JsonParser p(s); if (!p.val(out)) return false; p.ws(); return p.i == s.size(); }
inline J json(const std::string &s) { J j; if (!parseJson(s, j)) { j = J(); j.k = J::Null; } return j; }

// ---- fresh boots in a child process -------------------------------------------------
// What survives a power cycle on the real device.
struct Device {
    std::vector<uint8_t> eeprom = std::vector<uint8_t>(4096, 0xFF);
    std::map<std::string, std::string> fs;
    std::string savedSsid, savedPass;
};
// Implemented by the test file that uses boot(): copy a Device into the fakes / back.
void loadDevice(const Device &d);
void saveDevice(Device &d);

namespace detail {
static const size_t SHM = 4 << 20;
inline char *shm() {
    static char *p = (char *)mmap(nullptr, SHM, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return p;
}
inline void put(std::string &o, const std::string &s) { uint32_t n = (uint32_t)s.size(); o.append((char *)&n, 4); o += s; }
inline std::string get(const char *&p) { uint32_t n; memcpy(&n, p, 4); p += 4; std::string s(p, n); p += n; return s; }
inline std::string pack(const Device &d) {
    std::string o; put(o, std::string(d.eeprom.begin(), d.eeprom.end()));
    put(o, d.savedSsid); put(o, d.savedPass); put(o, std::to_string(d.fs.size()));
    for (auto &kv : d.fs) { put(o, kv.first); put(o, kv.second); }
    return o;
}
inline void unpack(const char *p, Device &d) {
    std::string e = get(p); d.eeprom.assign(e.begin(), e.end());
    d.savedSsid = get(p); d.savedPass = get(p);
    size_t n = std::stoul(get(p)); d.fs.clear();
    for (size_t i = 0; i < n; i++) { std::string k = get(p); d.fs[k] = get(p); }
}
}  // namespace detail

// Run `fn` as one power-on of `dev` in a fresh process; afterwards `dev` holds what the
// device persisted. A crash or an uncaught exception is reported as a failure.
inline void boot(Device &dev, const std::function<void()> &fn) {
    counters(); detail::shm();   // map the shared regions in the PARENT, before forking
    std::fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        fakesReset();
        loadDevice(dev);
        try { fn(); }
        catch (const std::exception &e) { check(false, std::string("uncaught exception: ") + e.what()); }
        catch (...) { check(false, "uncaught exception (ESP.restart() outside a request?)"); }
        Device out; saveDevice(out);
        std::string b = detail::pack(out);
        if (b.size() + 8 < detail::SHM) { uint64_t n = b.size(); memcpy(detail::shm(), &n, 8); memcpy(detail::shm() + 8, b.data(), b.size()); }
        std::fflush(stdout);
        _exit(0);
    }
    int st = 0; waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { check(false, "device process crashed (signal " + std::to_string(WIFSIGNALED(st) ? WTERMSIG(st) : 0) + ")"); return; }
    uint64_t n; memcpy(&n, detail::shm(), 8);
    detail::unpack(detail::shm() + 8, dev);
}
