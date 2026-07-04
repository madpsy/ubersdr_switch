// mqtt.h — optional, plaintext MQTT client (knolleary PubSubClient).
//
// Publishes device state (retained, if configured) and availability (LWT), and — when
// enabled — accepts inbound command topics to change the antenna. Configuration lives
// in EEPROM (see settings.h / config.h MqttConfig) so it survives a filesystem reflash.
// TLS is intentionally NOT supported (heap/complexity cost on the ESP8266); the design
// keeps the client type local so TLS could be added later behind a flag.
//
// Topics (prefix defaults to "uberant/<hostname>" when the config prefix is empty):
//   PUBLISH  <prefix>/availability   "online" / "offline"   (retained, LWT)
//   PUBLISH  <prefix>/status         full status JSON       (retained if cfg.retain)
//   PUBLISH  <prefix>/antenna        bare position number   (retained if cfg.retain)
//   PUBLISH  <prefix>/info           device/network info JSON (on connect + every 60s)
//   PUBLISH  <prefix>/event          JSON incl. change source (not retained)
//   SUBSCRIBE (if cfg.commands)
//     <prefix>/set        "0".."7" | "up" | "down" | "ground"
//     <prefix>/max/set    "0".."7"
//     <prefix>/name/set   {"position":N,"name":"..."}
//     <prefix>/restart    (any payload)

#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "config.h"
#include "debuglog.h"

class Mqtt {
public:
    // Callbacks supplied by main.cpp so this module stays decoupled from the app:
    //   statusJson  -> returns the current status JSON string
    //   infoFn      -> returns device/network info JSON
    //   timeFn      -> returns current ISO8601 UTC timestamp (or "" if NTP not synced)
    //   onSet       -> apply an antenna command ("up"/"down"/"ground"/"<n>")
    //   onMax       -> set max antennas (0..7)
    //   onName      -> set a name (position, name)
    //   onRestart   -> reboot the device
    typedef String (*StatusFn)();
    typedef String (*InfoFn)();
    typedef String (*TimeFn)();
    typedef void   (*SetFn)(const String &cmd);
    typedef void   (*MaxFn)(int value);
    typedef void   (*NameFn)(int position, const String &name);
    typedef void   (*RestartFn)();
    // text, align char ('L'/'R'/'C'), duration ms, wasBlank (re-blank after message)
    typedef void   (*DisplayFn)(const String &text, char align, uint32_t durationMs, bool wasBlank);
    // lock: called with true=lock, false=unlock
    typedef void   (*LockFn)(bool locked);

    typedef void (*LockToggleFn)();

    void begin(const MqttConfig &cfg, const String &hostname,
               StatusFn statusFn, InfoFn infoFn, TimeFn timeFn,
               SetFn setFn, MaxFn maxFn, NameFn nameFn, RestartFn restartFn,
               DisplayFn displayFn = nullptr, LockFn lockFn = nullptr,
               LockToggleFn lockToggleFn = nullptr) {
        _cfg          = cfg;
        _statusFn     = statusFn;
        _infoFn       = infoFn;
        _timeFn       = timeFn;
        _setFn        = setFn;
        _maxFn        = maxFn;
        _nameFn       = nameFn;
        _restartFn    = restartFn;
        _displayFn    = displayFn;
        _lockFn       = lockFn;
        _lockToggleFn = lockToggleFn;

        // Resolve the topic prefix: explicit config, else "uberant/<hostname>".
        _prefix = strlen(cfg.prefix) ? String(cfg.prefix)
                                     : (String("uberant/") + hostname);
        while (_prefix.endsWith("/")) _prefix.remove(_prefix.length() - 1);

        _client.setClient(_net);
        if (strlen(cfg.host)) _client.setServer(cfg.host, cfg.port);
        _client.setBufferSize(1024);  // info JSON is ~768 bytes; status ~360 bytes
        _self = this;
        _client.setCallback(&Mqtt::trampoline);
        _lastAttempt = 0;
        LOGF("MQTT: %s (host=%s:%u prefix=%s cmds=%d retain=%d)",
             cfg.enabled ? "enabled" : "disabled",
             cfg.host, cfg.port, _prefix.c_str(), cfg.commands, cfg.retain);
    }

    bool enabled()   const { return _cfg.enabled && strlen(_cfg.host) > 0; }
    bool connected()       { return _client.connected(); }
    // Effective topic prefix (resolved at begin() time).
    const String &prefix() const { return _prefix; }

    // Call frequently from loop(): services MQTT, reconnects with exponential backoff,
    // and republishes /info once a minute while connected.
    void loop() {
        if (!enabled()) return;
        if (WiFi.status() != WL_CONNECTED) return;   // wait for WiFi; no false backoff

        if (!_client.connected()) {
            uint32_t now = millis();
            if (now - _lastAttempt < _backoffMs) return;
            _lastAttempt = now;
            reconnect();
            return;
        }

        _client.loop();

        // Periodic /info heartbeat (every 60s).
        uint32_t now = millis();
        if (now - _lastInfo >= 60000UL) {
            _lastInfo = now;
            publishInfo();
        }
    }

    // Publish device/network info JSON (on connect + periodically).
    void publishInfo() {
        if (!enabled() || !_client.connected() || !_infoFn) return;
        _client.publish(topic("info").c_str(), _infoFn().c_str(), _cfg.retain);
    }

    // Publish the current state (status + antenna). Called on every change.
    void publishState() {
        if (!enabled() || !_client.connected()) return;
        String status = _statusFn ? _statusFn() : String("{}");
        _client.publish(topic("status").c_str(), status.c_str(), _cfg.retain);
        // Bare antenna number for simple consumers.
        int pos = extractInt(status, "\"position\":");
        _client.publish(topic("antenna").c_str(), String(pos).c_str(), _cfg.retain);
    }

    // Publish a change event (not retained) with its cause and optional timestamp.
    void publishEvent(const char *source) {
        if (!enabled() || !_client.connected()) return;
        String status = _statusFn ? _statusFn() : String("{}");
        // Splice "source" and "ts" into the status object.
        String ev = String("{\"source\":\"") + source + "\"";
        if (_timeFn) {
            String ts = _timeFn();
            if (ts.length() > 0) {
                ev += F(",\"ts\":\"");
                ev += ts;
                ev += '"';
            }
        }
        ev += ',';
        int brace = status.indexOf('{');
        if (brace >= 0) ev += status.substring(brace + 1);
        else            ev += "}";
        _client.publish(topic("event").c_str(), ev.c_str(), false);
    }

private:
    String topic(const char *leaf) const { return _prefix + "/" + leaf; }

    void reconnect() {
        String cid = _prefix; cid.replace("/", "-");
        String avail = topic("availability");
        const char *user = strlen(_cfg.user) ? _cfg.user : nullptr;
        const char *pass = strlen(_cfg.pass) ? _cfg.pass : nullptr;
        // LWT: broker publishes "offline" (retained) if we drop unexpectedly.
        bool ok = _client.connect(cid.c_str(), user, pass,
                                  avail.c_str(), 0, true, "offline");
        if (!ok) {
            LOGF("MQTT: connect failed rc=%d (backoff %ums)", _client.state(), _backoffMs);
            // Exponential backoff: 5s → 10s → 20s → 40s → 60s cap.
            { uint32_t nx = _backoffMs * 2; _backoffMs = (nx < 60000UL) ? nx : 60000UL; }
            return;
        }
        _backoffMs = 5000;   // reset backoff on success
        LOG("MQTT: connected");
        _client.publish(avail.c_str(), "online", true);
        if (_cfg.commands) {
            _client.subscribe(topic("set").c_str());
            _client.subscribe(topic("max/set").c_str());
            _client.subscribe(topic("name/set").c_str());
            _client.subscribe(topic("restart").c_str());
            _client.subscribe(topic("display").c_str());
            _client.subscribe(topic("lock").c_str());
            LOG("MQTT: subscribed to command topics");
        }
        publishState();
        publishInfo();   // send /info immediately on (re)connect
        _lastInfo = millis();
    }

    // Static trampoline -> instance handler (PubSubClient wants a C callback).
    static void trampoline(char *topic, uint8_t *payload, unsigned int len) {
        if (_self) _self->onMessage(topic, payload, len);
    }

    void onMessage(char *rawTopic, uint8_t *payload, unsigned int len) {
        String t(rawTopic);
        String msg; msg.reserve(len);
        for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
        msg.trim();
        LOGF("MQTT rx: %s = %s", t.c_str(), msg.c_str());

        if (t == topic("set")) {
            if (_setFn) _setFn(msg);
        } else if (t == topic("max/set")) {
            if (_maxFn) _maxFn(msg.toInt());
        } else if (t == topic("name/set")) {
            int pos = extractInt(msg, "\"position\":");
            String nm = extractStr(msg, "\"name\":");
            if (_nameFn && pos >= 1) _nameFn(pos, nm);
        } else if (t == topic("restart")) {
            if (_restartFn) _restartFn();
        } else if (t == topic("display")) {
            // Payload: {"text":"...","duration":3,"align":"center"}
            // All fields optional except "text".
            String txt = extractStr(msg, "\"text\":");
            if (txt.length() == 0) return;   // ignore empty text
            long durSec = 1;
            int dk = msg.indexOf("\"duration\":");
            if (dk >= 0) durSec = msg.substring(dk + 11).toInt();
            if (durSec < 1)  durSec = 1;
            if (durSec > 60) durSec = 60;
            char aln = 'C';
            String alnStr = extractStr(msg, "\"align\":");
            if      (alnStr == "left")  aln = 'L';
            else if (alnStr == "right") aln = 'R';
            // wasBlank is determined by the caller lambda (captured from g_oled.isBlank()).
            if (_displayFn) _displayFn(txt, aln, (uint32_t)(durSec * 1000L), false);
        } else if (t == topic("lock")) {
            // Payload: "lock"/"true"/"1" = lock, "unlock"/"false"/"0" = unlock,
            // "toggle" = toggle (calls lockFn with current_state XOR true).
            String cmd = msg; cmd.toLowerCase(); cmd.trim();
            if (cmd == "toggle") {
                if (_lockToggleFn) _lockToggleFn();
            } else {
                bool doLock;
                if      (cmd == "lock"   || cmd == "true"  || cmd == "1") doLock = true;
                else if (cmd == "unlock" || cmd == "false" || cmd == "0") doLock = false;
                else return;
                if (_lockFn) _lockFn(doLock);
            }
        }
    }

    // Tiny JSON scanners (avoid a JSON library).
    static int extractInt(const String &j, const char *key) {
        int k = j.indexOf(key);
        if (k < 0) return 0;
        return j.substring(k + strlen(key)).toInt();
    }
    static String extractStr(const String &j, const char *key) {
        int k = j.indexOf(key);
        if (k < 0) return String();
        int q1 = j.indexOf('"', k + strlen(key));
        if (q1 < 0) return String();
        int q2 = j.indexOf('"', q1 + 1);
        if (q2 < 0) return String();
        return j.substring(q1 + 1, q2);
    }

    WiFiClient   _net;
    PubSubClient _client;
    MqttConfig   _cfg;
    String       _prefix;
    uint32_t     _lastAttempt = 0;
    uint32_t     _lastInfo    = 0;
    uint32_t     _backoffMs   = 5000;

    StatusFn  _statusFn  = nullptr;
    InfoFn    _infoFn    = nullptr;
    TimeFn    _timeFn    = nullptr;
    SetFn     _setFn     = nullptr;
    MaxFn     _maxFn     = nullptr;
    NameFn    _nameFn    = nullptr;
    RestartFn _restartFn = nullptr;
    DisplayFn    _displayFn     = nullptr;
    LockFn       _lockFn        = nullptr;
    LockToggleFn _lockToggleFn  = nullptr;

    static Mqtt *_self;   // for the static callback trampoline
};

Mqtt *Mqtt::_self = nullptr;
