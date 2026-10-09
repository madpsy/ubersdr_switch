#pragma once
// Host-side LittleFS stand-in: an in-memory map of path -> contents.
#include "Arduino.h"
#include <map>
#include <memory>

extern std::map<std::string, std::string> g_fs;
extern bool g_fsMountOk;

class File {
public:
    File() {}
    File(const std::string &path, bool write) : _path(path), _write(write), _ok(true) {
        if (write) g_fs[path] = "";
    }
    explicit operator bool() const { return _ok; }
    String readString() { return _ok ? String(g_fs[_path]) : String(); }
    size_t size() const { return _ok ? g_fs[_path].size() : 0; }
    String name() const { return _path; }
    template <class T> size_t print(const T &v) { String s(v); if (_ok && _write) g_fs[_path] += s.s; return s.length(); }
    size_t print(const char *v) { if (_ok && _write) g_fs[_path] += v; return strlen(v); }
    size_t print(const String &v) { if (_ok && _write) g_fs[_path] += v.s; return v.length(); }
    void close() {}
    const std::string &path() const { return _path; }
private:
    std::string _path; bool _write = false, _ok = false;
};

class Dir {
public:
    bool next() { if (!_started) { _it = g_fs.begin(); _started = true; } else if (_it != g_fs.end()) ++_it; return _it != g_fs.end(); }
    String fileName() { return String(_it->first.substr(1)); }
    size_t fileSize() { return _it->second.size(); }
private:
    std::map<std::string, std::string>::iterator _it; bool _started = false;
};

struct LittleFSClass {
    bool begin() { return g_fsMountOk; }
    File open(const String &p, const char *mode) {
        bool w = mode && mode[0] == 'w';
        if (!w && !g_fs.count(p.s)) return File();
        return File(p.s, w);
    }
    bool exists(const String &p) { return g_fs.count(p.s) != 0; }
    bool remove(const String &p) { return g_fs.erase(p.s) != 0; }
    Dir openDir(const char *) { return Dir(); }
};
extern LittleFSClass LittleFS;
