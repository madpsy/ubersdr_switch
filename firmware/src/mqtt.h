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
//
// TASMOTA DISCOVERY (opt-in, cfg.discovery). The switch also speaks enough of Tasmota's
// MQTT protocol to be found and driven by anything that understands a Tasmota relay
// board (Home Assistant's Tasmota integration, TasmoManager). The authority for every topic, field and payload below is the Tasmota source
// (arendst/Tasmota): xdrv_12_discovery.ino TasDiscoverMessage()/TasDiscovery(),
// xdrv_02_9_mqtt.ino MqttConnected()/MqttPublishPowerState(), support_tasmota.ino
// ExecuteCommandPower()/MqttShowState(), support_command.ino CmndPower()/CmndStatus().
//
// Each antenna port 1..max is one Tasmota relay, and the relays are INTERLOCKED: turning
// one ON turns the others OFF, which is what the 74HCT138 does in hardware anyway.
// Turning the selected port OFF selects GROUND. Topic <t> = "uberant_<last 6 MAC hex>"
// (Tasmota's PROJECT "_%06X"), full topic "%prefix%/%topic%/", prefixes cmnd/stat/tele.
//   PUBLISH  tasmota/discovery/<MAC>/config   discovery JSON          (retained)
//   PUBLISH  tasmota/discovery/<MAC>/sensors  {"sn":{"Time":…},"ver":1} (retained)
//   PUBLISH  tele/<t>/LWT                     "Online" / "Offline"    (retained, LWT)
//   PUBLISH  stat/<t>/RESULT + stat/<t>/POWER<n>   on every change (OFF before ON)
//   PUBLISH  tele/<t>/STATE                   on connect + every 300 s (TELE_PERIOD)
//   SUBSCRIBE cmnd/<t>/#
//     POWER<n>  ON|OFF|TOGGLE|1|0|2 (empty = report)   POWER0 OFF = GROUND
//     STATE                       -> stat/<t>/RESULT  (state JSON)
//     STATUS 11                   -> stat/<t>/STATUS11 {"StatusSTS":{…}}
// The lock blocks POWER changes like Tasmota's PowerLock: the state is reported, unchanged.
// ★ PubSubClient carries ONE last will. With discovery on it is Tasmota's (tele/<t>/LWT
//   "Offline") so Tasmota consumers see the switch drop; <prefix>/availability is still
//   published "online"/"offline" on connect and on a clean reconfigure, but not by the broker.

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

    // Tasmota-discovery hooks (set once from setup(); survive begin() re-inits).
    struct TasmotaHooks {
        int    (*position)()       = nullptr;   // 0 = GROUND, 1..7
        int    (*maxAntennas)()    = nullptr;   // ports in use = Tasmota relays
        String (*portName)(int)    = nullptr;   // custom name, "" if unset
        bool   (*locked)()         = nullptr;
        String (*deviceName)()     = nullptr;
        String (*localTime)()      = nullptr;   // "YYYY-MM-DDTHH:MM:SS" (Tasmota "Time"), "" if unsynced
        void   (*select)(int pos)  = nullptr;   // apply a position (0 = GROUND) from a POWER command
    };
    void setTasmotaHooks(const TasmotaHooks &h) { _tas = h; }

    void begin(const MqttConfig &cfg, const String &hostname,
               StatusFn statusFn, InfoFn infoFn, TimeFn timeFn,
               SetFn setFn, MaxFn maxFn, NameFn nameFn, RestartFn restartFn,
               DisplayFn displayFn = nullptr, LockFn lockFn = nullptr,
               LockToggleFn lockToggleFn = nullptr) {
        // A reconfigure must take effect now: the will and the subscriptions are fixed at
        // connect time. Withdraw a discovery we are turning off (Tasmota's SetOption19 1
        // clears the retained message the same way), then drop the session.
        if (_client.connected()) {
            if (_cfg.discovery && !cfg.discovery) tasClearDiscovery();
            if (_cfg.discovery) _client.publish(tasTopic("tele", "LWT").c_str(), "Offline", true);
            _client.publish(topic("availability").c_str(), "offline", true);
            _client.disconnect();
        }
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

        // Tasmota identity: NetworkUniqueId() = the full MAC without colons; the default
        // topic is PROJECT "_%06X" with the last six hex digits of the MAC.
        _tasMac = WiFi.macAddress(); _tasMac.replace(":", ""); _tasMac.toUpperCase();
        _tasT   = String("uberant_") + _tasMac.substring(6);
        _tasPower = 0; _tasSig = String();

        _client.setClient(_net);
        if (strlen(cfg.host)) _client.setServer(cfg.host, cfg.port);
        _client.setBufferSize(1536);  // Tasmota discovery ~1.1 kB; info ~768 bytes; status ~360
        _self = this;
        _client.setCallback(&Mqtt::trampoline);
        _lastAttempt = 0;
        LOGF("MQTT: %s (host=%s:%u prefix=%s cmds=%d retain=%d tasmota=%s)",
             cfg.enabled ? "enabled" : "disabled",
             cfg.host, cfg.port, _prefix.c_str(), cfg.commands, cfg.retain,
             cfg.discovery ? _tasT.c_str() : "off");
    }

    bool enabled()   const { return _cfg.enabled && strlen(_cfg.host) > 0; }
    bool connected()       { return _client.connected(); }
    // Effective topic prefix (resolved at begin() time).
    const String &prefix() const { return _prefix; }
    // Tasmota topic <t> (cmnd/<t>/POWER<n>, …) — meaningful when discovery is on.
    const String &tasmotaTopic() const { return _tasT; }

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
        if (tasOn() && now - _lastTele >= 300000UL) {   // Tasmota TELE_PERIOD default
            _lastTele = now;
            tasPublishTeleState();
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
        tasSync();
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
        // LWT: broker publishes "offline" (retained) if we drop unexpectedly — or, with
        // Tasmota discovery on, Tasmota's tele/<t>/LWT "Offline" (only one will per session).
        String tasLwt = tasTopic("tele", "LWT");
        bool ok = tasOn()
            ? _client.connect(cid.c_str(), user, pass, tasLwt.c_str(), 0, true, "Offline")
            : _client.connect(cid.c_str(), user, pass, avail.c_str(), 0, true, "offline");
        if (!ok) {
            LOGF("MQTT: connect failed rc=%d (backoff %ums)", _client.state(), _backoffMs);
            // Exponential backoff: 5s → 10s → 20s → 40s → 60s cap.
            { uint32_t nx = _backoffMs * 2; _backoffMs = (nx < 60000UL) ? nx : 60000UL; }
            return;
        }
        _backoffMs = 5000;   // reset backoff on success
        _connectCount++;
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
        if (tasOn()) {
            // MqttConnected(): LWT Online, subscribe cmnd/<t>/#; then discovery and state.
            _client.publish(tasLwt.c_str(), "Online", true);
            _client.subscribe(tasTopic("cmnd", "#").c_str());
            _tasPower = 0; _tasSig = String();   // force a full republish
            LOGF("MQTT: Tasmota discovery as %s (%s)", _tasT.c_str(), _tasMac.c_str());
        }
        publishState();   // also publishes discovery + every POWER<n> (tasSync)
        publishInfo();   // send /info immediately on (re)connect
        _lastInfo = millis();
        if (tasOn()) { tasPublishTeleState(); _lastTele = millis(); }
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

        if (tasOn()) {
            String pre = tasTopic("cmnd", "");
            if (t.startsWith(pre)) { tasCommand(t.substring(pre.length()), msg); return; }
        }

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

    // ---------------------------------------------------------------------------
    // Tasmota protocol (see the header comment for the source of each piece)
    // ---------------------------------------------------------------------------
    static const uint8_t TAS_MAX_RELAYS = 32;     // MAX_RELAYS_SET / MAX_SWITCHES_SET / MAX_KEYS_SET
    static const uint8_t TAS_MAX_FN     = 8;      // MAX_FRIENDLYNAMES

    bool tasOn() const { return enabled() && _cfg.discovery && _tas.position && _tas.maxAntennas; }

    // GetTopic_P with the default full topic "%prefix%/%topic%/".
    String tasTopic(const char *prefix, const char *cmd) const {
        return String(prefix) + "/" + _tasT + "/" + cmd;
    }

    int tasRelays() const {
        int n = _tas.maxAntennas ? _tas.maxAntennas() : 0;
        return n < 0 ? 0 : (n > 7 ? 7 : n);
    }
    // TasmotaGlobal.power: bit n-1 set when port n is the selected antenna.
    uint32_t tasPowerMask() const {
        int p = _tas.position ? _tas.position() : 0;
        return (p >= 1 && p <= tasRelays()) ? (1UL << (p - 1)) : 0;
    }
    String tasFriendly(int i) const {   // 1-based; Tasmota's FriendlyName<i>
        String n = _tas.portName ? _tas.portName(i) : String();
        return n.length() ? n : String("ANT ") + i;
    }
    // Names are sanitised by Antenna::setName (no quotes, backslashes or controls); the
    // device name is not, so escape what JSON needs.
    static String tasEsc(const String &s) {
        String o; o.reserve(s.length() + 4);
        for (size_t i = 0; i < s.length(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((uint8_t)c >= 0x20) o += c;
        }
        return o;
    }
    String tasTime() const {   // ResponseAppendTime(): "Time":"YYYY-MM-DDTHH:MM:SS"
        String t = _tas.localTime ? _tas.localTime() : String();
        return t.length() >= 19 ? t.substring(0, 19) : String("1970-01-01T00:00:00");
    }

    // TasDiscoverMessage(), field for field. Relay type 1 = simple relay; no lights,
    // shutters, switches or buttons are offered (the front-panel buttons step the
    // selector locally and are not Tasmota buttons).
    String tasDiscoveryJson() const {
        const int nRel = tasRelays();
        String dn = _tas.deviceName ? _tas.deviceName() : String("UberANT");
        String j; j.reserve(1200);
        j += F("{\"ip\":\"");  j += WiFi.localIP().toString();
        j += F("\",\"dn\":\""); j += tasEsc(dn);
        j += F("\",\"fn\":[");
        const int maxfn = nRel > TAS_MAX_FN ? TAS_MAX_FN : (nRel ? nRel : 1);
        for (int i = 0; i < TAS_MAX_FN; i++) {
            if (i) j += ',';
            if (i < maxfn) { j += '"'; j += tasEsc(tasFriendly(i + 1)); j += '"'; }
            else j += F("null");
        }
        j += F("],\"hn\":\"");  j += WiFi.hostname();
        j += F("\",\"mac\":\""); j += _tasMac;
        j += F("\",\"md\":\"MS-S5\",\"ty\":0,\"if\":0,\"cam\":0,"
               "\"ofln\":\"Offline\",\"onln\":\"Online\","
               "\"state\":[\"OFF\",\"ON\",\"TOGGLE\",\"HOLD\"],"
               "\"sw\":\"ubersdr_switch\",\"t\":\"");
        j += _tasT;
        j += F("\",\"ft\":\"%prefix%/%topic%/\",\"tp\":[\"cmnd\",\"stat\",\"tele\"],\"rl\":[");
        for (int i = 0; i < TAS_MAX_RELAYS; i++) { if (i) j += ','; j += (i < nRel) ? '1' : '0'; }
        j += F("],\"swc\":[");
        for (int i = 0; i < TAS_MAX_RELAYS; i++) { if (i) j += ','; j += F("-1"); }
        j += F("],\"swn\":[");
        for (int i = 0; i < TAS_MAX_RELAYS; i++) { if (i) j += ','; j += F("null"); }
        j += F("],\"btn\":[");
        for (int i = 0; i < TAS_MAX_RELAYS; i++) { if (i) j += ','; j += '0'; }
        j += F("],\"so\":{\"4\":0,\"11\":0,\"13\":0,\"17\":0,\"20\":0,\"30\":0,\"68\":0,"
               "\"73\":0,\"82\":0,\"114\":0,\"117\":0},"
               "\"lk\":0,\"lt_st\":0,\"bat\":0,\"dslp\":0,\"sho\":[],\"sht\":[],\"ver\":1}");
        return j;
    }

    // TasDiscovery(): config + sensors, both retained.
    void tasPublishDiscovery() {
        String base = String("tasmota/discovery/") + _tasMac;
        String cfg = tasDiscoveryJson();
        bool ok = _client.publish((base + "/config").c_str(), cfg.c_str(), true);
        String sn = String("{\"sn\":{\"Time\":\"") + tasTime() + "\"},\"ver\":1}";
        _client.publish((base + "/sensors").c_str(), sn.c_str(), true);
        LOGF("MQTT: Tasmota discovery published (%u bytes, %s)", cfg.length(), ok ? "ok" : "FAILED");
    }
    // SetOption19 1: an empty retained message removes the discovery.
    void tasClearDiscovery() {
        String base = String("tasmota/discovery/") + _tasMac;
        _client.publish((base + "/config").c_str(), "", true);
        _client.publish((base + "/sensors").c_str(), "", true);
        LOG("MQTT: Tasmota discovery withdrawn");
    }

    // MqttPublishPowerState(n): stat/<t>/RESULT {"POWERn":"ON"} then stat/<t>/POWERn "ON".
    // Not retained (PowerRetain defaults off).
    void tasPublishPower(int n, bool on) {
        String cmd = String("POWER") + n;
        const char *st = on ? "ON" : "OFF";
        String res = String("{\"") + cmd + "\":\"" + st + "\"}";
        _client.publish(tasTopic("stat", "RESULT").c_str(), res.c_str(), false);
        _client.publish(tasTopic("stat", cmd.c_str()).c_str(), st, false);
    }

    // Called from publishState() at every change: republish discovery if the relay set
    // or a name changed, then each POWER<n> that changed — OFF before ON, as an
    // interlocked Tasmota reports it.
    void tasSync() {
        if (!tasOn() || !_client.connected()) return;
        const int nRel = tasRelays();
        String sig = String(nRel) + "|" + (_tas.deviceName ? _tas.deviceName() : String());
        for (int i = 1; i <= nRel; i++) { sig += '|'; sig += tasFriendly(i); }
        bool all = false;
        if (sig != _tasSig) { _tasSig = sig; tasPublishDiscovery(); all = true; }
        uint32_t now = tasPowerMask();
        uint32_t diff = all ? 0x7F : (now ^ _tasPower);
        for (int pass = 0; pass < 2; pass++) {               // pass 0: OFFs, pass 1: ONs
            for (int i = 1; i <= nRel; i++) {
                uint32_t m = 1UL << (i - 1);
                if (!(diff & m)) continue;
                bool on = now & m;
                if (on == (pass == 1)) tasPublishPower(i, on);
            }
        }
        _tasPower = now;
    }

    // MqttShowState() body (no braces): Time, Uptime, Heap, MqttCount, POWER<n>, Wifi.
    String tasStateBody() const {
        uint32_t up = millis() / 1000;
        char ut[20];
        snprintf(ut, sizeof(ut), "%uT%02u:%02u:%02u", (unsigned)(up / 86400),
                 (unsigned)(up / 3600 % 24), (unsigned)(up / 60 % 60), (unsigned)(up % 60));
        String j; j.reserve(400);
        j += F("\"Time\":\""); j += tasTime();
        j += F("\",\"Uptime\":\""); j += ut;
        j += F("\",\"UptimeSec\":"); j += String(up);
        j += F(",\"Heap\":"); j += String(ESP.getFreeHeap() / 1024);
        j += F(",\"MqttCount\":"); j += String(_connectCount);
        uint32_t mask = tasPowerMask();
        for (int i = 1; i <= tasRelays(); i++) {
            j += F(",\"POWER"); j += String(i); j += F("\":\"");
            j += (mask & (1UL << (i - 1))) ? F("ON") : F("OFF"); j += '"';
        }
        int32_t rssi = WiFi.RSSI();
        int q = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : 2 * (rssi + 100));   // WifiGetRssiAsQuality
        j += F(",\"Wifi\":{\"AP\":1,\"SSId\":\""); j += tasEsc(WiFi.SSID());
        j += F("\",\"BSSId\":\""); j += WiFi.BSSIDstr();
        j += F("\",\"Channel\":"); j += String(WiFi.channel());
        j += F(",\"RSSI\":"); j += String(q);
        j += F(",\"Signal\":"); j += String(rssi);
        j += F("}");
        return j;
    }
    void tasPublishTeleState() {
        if (!_client.connected()) return;
        String j = String("{") + tasStateBody() + "}";
        _client.publish(tasTopic("tele", "STATE").c_str(), j.c_str(), false);
    }

    // GetStateNumber(): OFF/FALSE/STOP/0 -> 0, ON/TRUE/START/1 -> 1, TOGGLE/2 -> 2,
    // BLINK/3, BLINKOFF/4; anything else -> -1 (CmndPower turns that into "show state").
    static int tasStateNumber(String p) {
        p.trim(); p.toUpperCase();
        if (p == "OFF" || p == "FALSE" || p == "STOP" || p == "0") return 0;
        if (p == "ON"  || p == "TRUE"  || p == "START" || p == "1") return 1;
        if (p == "TOGGLE" || p == "2") return 2;
        if (p == "BLINK" || p == "3") return 3;
        if (p == "BLINKOFF" || p == "4") return 4;
        return -1;
    }

    // cmnd/<t>/<command>. Commands are case-insensitive, as in Tasmota.
    void tasCommand(String cmd, const String &payload) {
        cmd.toUpperCase();
        if (cmd.startsWith("POWER")) {
            String idx = cmd.substring(5);
            for (size_t i = 0; i < idx.length(); i++) if (!isDigit(idx[i])) return;  // POWERLOCK, POWERONSTATE…
            int n = idx.length() ? idx.toInt() : 1;            // "Power" is Power1
            int st = tasStateNumber(payload);
            const int nRel = tasRelays();
            const bool locked = _tas.locked && _tas.locked();
            if (n == 0) {                                      // SetAllPower: only OFF is meaningful
                if (st == 0 && !locked && _tas.select) _tas.select(0);
                for (int i = 1; i <= nRel; i++) tasPublishPower(i, tasPowerMask() & (1UL << (i - 1)));
                return;
            }
            if (n > nRel) return;                              // no such relay: ignored, as CmndPower does
            const bool isOn = tasPowerMask() & (1UL << (n - 1));
            int want = -1;                                     // -1 = no change (show state)
            if      (st == 1)            want = n;
            else if (st == 0)            want = isOn ? 0 : -1;  // OFF on an unselected port: nothing to do
            else if (st == 2)            want = isOn ? 0 : n;
            // BLINK/BLINKOFF have no meaning for a coax selector: report state.
            if (want >= 0 && locked) { LOGF("MQTT: POWER%d refused, locked", n); want = -1; }
            if (want == (_tas.position ? _tas.position() : 0)) want = -1;  // ON to the selected port
            if (want >= 0 && _tas.select) _tas.select(want);  // -> logEvent -> publishState -> tasSync
            // Nothing changed: still answer, as ExecuteCommandPower always publishes the state.
            if (want < 0) tasPublishPower(n, isOn);
            return;
        }
        if (cmd == "STATE") {                                  // CmndState(): stat/<t>/RESULT
            String j = String("{") + tasStateBody() + "}";
            _client.publish(tasTopic("stat", "RESULT").c_str(), j.c_str(), false);
            return;
        }
        if (cmd == "STATUS") {                                 // only Status 11 (StatusSTS)
            String p = payload; p.trim();
            if (p == "11") {
                String j = String("{\"StatusSTS\":{") + tasStateBody() + "}}";
                _client.publish(tasTopic("stat", "STATUS11").c_str(), j.c_str(), false);
            }
            return;
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
    uint32_t     _lastTele    = 0;
    uint32_t     _connectCount = 0;

    TasmotaHooks _tas;
    String       _tasMac;          // full MAC, no colons (NetworkUniqueId)
    String       _tasT;            // Tasmota topic
    uint32_t     _tasPower = 0;    // last published POWER<n> bits
    String       _tasSig;          // relay count + names last put in discovery

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
