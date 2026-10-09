#pragma once
// Host-side ESP8266WebServer stand-in. Routes are matched like the real server: the
// first registered handler whose path matches exactly (and whose method matches, or is
// HTTP_ANY) wins. A test calls request() to play a client.
#include "Arduino.h"
#include "LittleFS.h"
#include <map>
#include <vector>

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS };

struct HttpResponse {
    int code = 0;
    std::string type, body;
    std::map<std::string, std::string> headers;
    bool restarted = false;   // the handler called ESP.restart()
    bool matched = false;
};

class ESP8266WebServer {
public:
    explicit ESP8266WebServer(int port = 80) : _port(port) {}
    typedef std::function<void()> Handler;
    void on(const String &uri, Handler h) { _routes.push_back({uri.s, HTTP_ANY, h}); }
    void on(const String &uri, HTTPMethod m, Handler h) { _routes.push_back({uri.s, m, h}); }
    void begin() { _begun = true; }
    void handleClient() {}
    bool begun() const { return _begun; }

    HTTPMethod method() const { return _method; }
    bool hasArg(const String &k) const { return _args.count(k.s) != 0; }
    String arg(const String &k) const { auto it = _args.find(k.s); return it == _args.end() ? String() : String(it->second); }
    String uri() const { return _uri; }

    void sendHeader(const String &k, const String &v) { _resp.headers[k.s] = v.s; }
    void send(int code, const char *type, const String &body) { _resp.code = code; _resp.type = type; _resp.body = body.s; }
    void send(int code, const String &type, const String &body) { send(code, type.c_str(), body); }
    void send(int code) { _resp.code = code; }
    size_t streamFile(File &f, const String &type) {
        _resp.code = 200; _resp.type = type.s; _resp.body = f.readString().s;
        if (f.path().size() > 3 && f.path().compare(f.path().size() - 3, 3, ".gz") == 0) _resp.headers["Content-Encoding"] = "gzip";
        return _resp.body.size();
    }

    // ---- test side ----
    // uri may carry a query string ("/api/max?value=3"); body becomes arg("plain").
    HttpResponse request(HTTPMethod m, const std::string &uri, const std::string &body = "") {
        _resp = HttpResponse(); _args.clear(); _method = m;
        std::string path = uri, q;
        size_t qm = uri.find('?');
        if (qm != std::string::npos) { path = uri.substr(0, qm); q = uri.substr(qm + 1); }
        _uri = path;
        while (!q.empty()) {
            size_t amp = q.find('&');
            std::string kv = q.substr(0, amp);
            size_t eq = kv.find('=');
            _args[decode(kv.substr(0, eq))] = eq == std::string::npos ? "" : decode(kv.substr(eq + 1));
            if (amp == std::string::npos) break;
            q = q.substr(amp + 1);
        }
        if (!body.empty()) _args["plain"] = body;
        for (auto &r : _routes) {
            if (r.path != path || (r.method != HTTP_ANY && r.method != m)) continue;
            _resp.matched = true;
            try { r.h(); } catch (const EspRestart &) { _resp.restarted = true; }
            return _resp;
        }
        _resp.code = 404;
        return _resp;
    }
    size_t routeCount(const std::string &path) const {
        size_t n = 0; for (auto &r : _routes) n += r.path == path; return n;
    }

private:
    static std::string decode(const std::string &s) {
        std::string o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '+') o += ' ';
            else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
            else o += s[i];
        }
        return o;
    }
    struct Route { std::string path; HTTPMethod method; Handler h; };
    int _port;
    bool _begun = false;
    std::vector<Route> _routes;
    std::map<std::string, std::string> _args;
    HTTPMethod _method = HTTP_GET;
    String _uri;
    HttpResponse _resp;
};
