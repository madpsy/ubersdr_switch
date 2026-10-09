#pragma once
// Host-side OTA update server stand-in: registers the /update page like the real one.
#include "ESP8266WebServer.h"
class ESP8266HTTPUpdateServer {
public:
    void setup(ESP8266WebServer *s, const String &path) {
        _path = path.s;
        s->on(path, HTTP_GET, [s]() { s->send(200, "text/html", "<form method='POST' action='' enctype='multipart/form-data'>OTA</form>"); });
    }
    std::string _path;
};
