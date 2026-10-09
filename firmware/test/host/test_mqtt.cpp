// test_mqtt.cpp — host-side tests for src/mqtt.h (run with ./run.sh).
//
// The real mqtt.h, config.h and debuglog.h, compiled on a PC against the stand-ins in
// stub/ (Arduino String, WiFi, a PubSubClient that records what is sent). Two groups:
//
//   NATIVE  — the uberant/<host>/… topics with Tasmota discovery OFF: unchanged by
//             discovery (verified against the firmware from before it existed).
//   TASMOTA — discovery ON, checked against Tasmota's own behaviour (arendst/Tasmota):
//             TasDiscoverMessage(), MqttConnected(), MqttPublishPowerState(),
//             ExecuteCommandPower() with Interlock, CmndPower(), CmndStatus().
#include "mqtt.h"

#include <algorithm>
#include <memory>

#include "harness.h"

static std::vector<Pub> take() { auto v = g_pubs; g_pubs.clear(); return v; }
static const Pub *find(const std::vector<Pub> &v, const std::string &t) {
    for (auto &p : v) if (p.t == t) return &p;
    return nullptr;
}
static bool has(const std::vector<Pub> &v, const std::string &t, const std::string &p) {
    const Pub *x = find(v, t); return x && x->p == p;
}
static void inject(const std::string &t, const std::string &p) { mqttInject(t, p); }
static bool subscribed(const std::string &t) {
    return std::find(g_subs.begin(), g_subs.end(), t) != g_subs.end();
}
static bool parse(const std::string &s, J &out) { return parseJson(s, out); }

// ---- the app side the callbacks stand in for ----------------------------------------
static int pos = 0, maxA = 5;
static bool locked = false;
static std::string names[8];
static std::string devName = "UberANT";
static std::vector<std::string> calls;   // what the native callbacks were asked to do
static std::unique_ptr<Mqtt> m;

static String statusFn() { return String("{\"position\":") + pos + ",\"label\":\"x\"}"; }
static String infoFn() { return String("{\"hostname\":\"ubersdr\"}"); }
static String timeFn() { return String("2026-10-09T22:10:00Z"); }
static void setFn(const String &c) { calls.push_back("set " + c.s); }
static void maxFn(int v) { calls.push_back("max " + std::to_string(v)); }
static void nameFn(int p, const String &n) { calls.push_back("name " + std::to_string(p) + " " + n.s); }
static void restartFn() { calls.push_back("restart"); }
static void displayFn(const String &t, char a, uint32_t ms, bool) { calls.push_back("display " + t.s + " " + a + " " + std::to_string(ms)); }
static void lockFn(bool l) { calls.push_back(l ? "lock" : "unlock"); }
static void lockToggleFn() { calls.push_back("locktoggle"); }

static MqttConfig cfg(bool commands, bool retain = true, const char *prefix = "") {
    MqttConfig c; c.enabled = true; strcpy(c.host, "192.168.9.1"); c.commands = commands; c.retain = retain;
    strcpy(c.prefix, prefix);
    return c;
}
static void start(const MqttConfig &c) {
    m->begin(c, "ubersdr", statusFn, infoFn, timeFn, setFn, maxFn, nameFn, restartFn,
             displayFn, lockFn, lockToggleFn);
}
static void fresh() {
    m.reset(new Mqtt());
    fakesReset(); WiFi.st = WL_CONNECTED; WiFi.ssid = "Shack"; WiFi.hostname("ubersdr"); calls.clear();
    g_millis = 100000; pos = 0; maxA = 5; locked = false; devName = "UberANT";
    for (auto &n : names) n.clear();
}

// =====================================================================================
static void testNative() {
    section("NATIVE: connect, will, state (discovery off)");
    fresh(); start(cfg(false));
    m->loop();
    auto v = take();
    check(m->connected(), "connects when enabled with a host");
    check(g_will == "uberant/ubersdr/availability" && g_willMsg == "offline", "will: uberant/<host>/availability \"offline\"");
    const Pub *av = find(v, "uberant/ubersdr/availability");
    check(av && av->p == "online" && av->r, "availability \"online\", retained");
    check(has(v, "uberant/ubersdr/status", "{\"position\":0,\"label\":\"x\"}") && find(v, "uberant/ubersdr/status")->r, "status JSON, retained");
    check(has(v, "uberant/ubersdr/antenna", "0"), "bare antenna number");
    check(has(v, "uberant/ubersdr/info", "{\"hostname\":\"ubersdr\"}"), "info on connect");
    check(g_subs.empty(), "commands off: no subscriptions");
    bool anyTas = false;
    for (auto &p : v) if (p.t.rfind("tasmota/", 0) == 0 || p.t.rfind("stat/", 0) == 0 || p.t.rfind("tele/", 0) == 0) anyTas = true;
    check(!anyTas, "nothing Tasmota-shaped is published");
    check(v.size() == 4, "exactly availability, status, antenna, info (" + std::to_string(v.size()) + ")");

    section("NATIVE: state and events");
    pos = 3; m->publishState(); v = take();
    check(v.size() == 2 && has(v, "uberant/ubersdr/antenna", "3"), "publishState: status + antenna only");
    m->publishEvent("button"); v = take();
    check(v.size() == 1 && v[0].t == "uberant/ubersdr/event" && !v[0].r
          && v[0].p == "{\"source\":\"button\",\"ts\":\"2026-10-09T22:10:00Z\",\"position\":3,\"label\":\"x\"}",
          "event: source + ts spliced into status, not retained");
    g_millis += 59000; m->loop(); v = take();
    check(v.empty(), "no info heartbeat before 60 s");
    g_millis += 1000; m->loop(); v = take();
    check(v.size() == 1 && v[0].t == "uberant/ubersdr/info", "info heartbeat at 60 s");

    section("NATIVE: retain off, custom prefix");
    fresh(); start(cfg(false, false, "shack/ant/")); m->loop(); v = take();
    check(m->prefix() == "shack/ant", "trailing slash trimmed from the prefix");
    check(find(v, "shack/ant/status") && !find(v, "shack/ant/status")->r, "retain off: status not retained");
    check(find(v, "shack/ant/availability") && find(v, "shack/ant/availability")->r, "availability is always retained");

    section("NATIVE: disabled");
    fresh(); { MqttConfig c = cfg(true); c.enabled = false; start(c); }
    m->loop(); m->publishState();
    check(!m->connected() && take().empty(), "disabled: never connects, publishes nothing");
    fresh(); { MqttConfig c = cfg(true); c.host[0] = 0; start(c); }
    m->loop();
    check(!m->connected(), "no host: never connects");

    section("NATIVE: commands");
    fresh(); start(cfg(true)); m->loop(); take();
    for (const char *t : {"set", "max/set", "name/set", "restart", "display", "lock"})
        check(subscribed(std::string("uberant/ubersdr/") + t), std::string("subscribes ") + t);
    check(g_subs.size() == 6, "and nothing else");
    inject("uberant/ubersdr/set", " 3 ");
    inject("uberant/ubersdr/set", "ground");
    inject("uberant/ubersdr/max/set", "5");
    inject("uberant/ubersdr/name/set", "{\"position\":2,\"name\":\"Dipole\"}");
    inject("uberant/ubersdr/display", "{\"text\":\"hi\",\"duration\":4,\"align\":\"left\"}");
    inject("uberant/ubersdr/display", "{\"text\":\"\"}");
    inject("uberant/ubersdr/lock", "LOCK");
    inject("uberant/ubersdr/lock", "0");
    inject("uberant/ubersdr/lock", "toggle");
    inject("uberant/ubersdr/lock", "bogus");
    inject("uberant/ubersdr/restart", "1");
    inject("cmnd/uberant_45CA21/POWER1", "ON");   // Tasmota topic: ignored with discovery off
    std::vector<std::string> want = {"set 3", "set ground", "max 5", "name 2 Dipole", "display hi L 4000",
                                     "lock", "unlock", "locktoggle", "restart"};
    check(calls == want, "set/max/name/display/lock/restart dispatched; empty display, bad lock and Tasmota topics ignored");
    if (calls != want) for (auto &c : calls) std::printf("        got: %s\n", c.c_str());

    section("NATIVE: reconfigure takes effect at once");
    fresh(); start(cfg(false)); m->loop(); take();
    int before = g_connects;
    start(cfg(true));
    auto v2 = take();
    check(has(v2, "uberant/ubersdr/availability", "offline"), "old session says offline before it is dropped");
    check(!m->connected(), "…and is dropped");
    m->loop();
    check(m->connected() && g_connects == before + 1 && subscribed("uberant/ubersdr/set"), "the next loop reconnects with the new settings");
}

static const char *T = "uberant_45CA21";
static std::string tt(const char *pre, const std::string &cmd) { return std::string(pre) + "/" + T + "/" + cmd; }

static void hooks() {
    Mqtt::TasmotaHooks h;
    h.position    = []() { return pos; };
    h.maxAntennas = []() { return maxA; };
    h.portName    = [](int i) { return String(names[i]); };
    h.locked      = []() { return locked; };
    h.deviceName  = []() { return String(devName); };
    h.localTime   = []() { return String("2026-10-09T23:10:00+01:00"); };
    h.select      = [](int p) { pos = p; m->publishState(); };   // as main.cpp: change -> logEvent -> publishState
    m->setTasmotaHooks(h);
}
static void startTas() {
    MqttConfig c = cfg(false); c.discovery = true;
    hooks(); start(c); m->loop();
}
static std::vector<std::pair<std::string, std::string>> powerPubs(const std::vector<Pub> &v) {
    std::vector<std::pair<std::string, std::string>> o;
    for (auto &p : v) if (p.t.rfind(tt("stat", "POWER"), 0) == 0) o.push_back({p.t.substr(p.t.rfind('/') + 1), p.p});
    return o;
}

static void testTasmota() {
    section("TASMOTA: connect (MqttConnected + TasDiscovery)");
    fresh(); names[1] = "40m Dipole"; names[3] = "80m Vertical"; devName = "Shack \"A\"";
    startTas();
    auto v = take();
    check(g_will == tt("tele", "LWT") && g_willMsg == "Offline", "will: tele/<t>/LWT \"Offline\"");
    check(find(v, tt("tele", "LWT")) && find(v, tt("tele", "LWT"))->p == "Online" && find(v, tt("tele", "LWT"))->r, "tele/<t>/LWT \"Online\", retained");
    check(subscribed(tt("cmnd", "#")), "subscribes cmnd/<t>/#");
    check(g_subs.size() == 1, "commands off: no native subscriptions (POWER does not need them)");
    check(find(v, "uberant/ubersdr/availability") && find(v, "uberant/ubersdr/status"), "native topics still published");
    const Pub *cf = find(v, "tasmota/discovery/CC50E345CA21/config");
    const Pub *sn = find(v, "tasmota/discovery/CC50E345CA21/sensors");
    check(cf && cf->r && sn && sn->r, "discovery config + sensors at tasmota/discovery/<full MAC>/…, retained");
    check(sn && sn->p == "{\"sn\":{\"Time\":\"2026-10-09T23:10:00\"},\"ver\":1}", "sensors: Tasmota's Time format, no zone");
    check(find(v, tt("tele", "STATE")) != nullptr, "tele/<t>/STATE on connect");

    section("TASMOTA: the discovery message (TasDiscoverMessage)");
    J d; bool ok = cf && parse(cf->p, d);
    check(ok, "valid JSON (" + std::to_string(cf ? cf->p.size() : 0) + " bytes, under the 1536-byte MQTT buffer)");
    check(cf && cf->p.size() + 64 < 1536, "fits the client buffer with its topic");
    if (ok) {
        auto S = [&](const char *k) { const J *x = d.get(k); return x && x->k == J::Str ? x->s : std::string("<missing>"); };
        auto N = [&](const char *k) { const J *x = d.get(k); return x && x->k == J::Num ? int(x->n) : -999; };
        auto A = [&](const char *k) { const J *x = d.get(k); return x && x->k == J::Arr ? int(x->a.size()) : -1; };
        check(S("t") == T && S("mac") == "CC50E345CA21" && S("hn") == "ubersdr" && S("ip") == "192.168.9.109", "t, mac, hn, ip");
        check(S("dn") == "Shack \"A\"", "dn: the device name, JSON-escaped");
        check(S("ft") == "%prefix%/%topic%/" && S("ofln") == "Offline" && S("onln") == "Online", "ft, ofln, onln");
        const J *tp = d.get("tp");
        check(tp && tp->a.size() == 3 && tp->a[0].s == "cmnd" && tp->a[1].s == "stat" && tp->a[2].s == "tele", "tp: cmnd, stat, tele");
        const J *stt = d.get("state");
        check(stt && stt->a.size() == 4 && stt->a[0].s == "OFF" && stt->a[1].s == "ON" && stt->a[2].s == "TOGGLE" && stt->a[3].s == "HOLD", "state texts");
        check(A("rl") == 32 && A("swc") == 32 && A("swn") == 32 && A("btn") == 32, "rl/swc/swn/btn: 32 entries (MAX_RELAYS_SET etc.)");
        const J *rl = d.get("rl"); int ones = 0, firstZero = -1;
        for (size_t i = 0; rl && i < rl->a.size(); i++) { if (rl->a[i].n == 1) ones++; else if (firstZero < 0) firstZero = int(i); }
        check(ones == 5 && firstZero == 5, "rl: ports 1..5 are simple relays (1), the rest 0");
        const J *fn = d.get("fn");
        check(fn && fn->a.size() == 8 && fn->a[0].s == "40m Dipole" && fn->a[1].s == "ANT 2" && fn->a[2].s == "80m Vertical"
              && fn->a[4].s == "ANT 5" && fn->a[5].k == J::Null && fn->a[7].k == J::Null,
              "fn: 8 entries (MAX_FRIENDLYNAMES) — port names, \"ANT n\" when unnamed, null past the relays");
        const J *swc = d.get("swc"); const J *btn = d.get("btn");
        check(swc && swc->a[0].n == -1 && btn && btn->a[0].n == 0, "no switches (-1) and no buttons (0) offered");
        check(N("ver") == 1 && N("ty") == 0 && N("if") == 0 && N("lk") == 0 && N("lt_st") == 0, "ver 1; not Tuya, not iFan, no lights");
        const J *so = d.get("so");
        check(so && so->o.size() == 11 && so->get("4") && so->get("114"), "so: the 11 SetOptions Tasmota reports");
        check(d.get("sho") && d.get("sht") && A("sho") == 0, "sho/sht present and empty (no shutters)");
    }
    auto pp = powerPubs(v);
    check(pp.size() == 5 && pp[0].second == "OFF", "every POWER<n> published on connect (GROUND selected: all OFF)");
    check(has(v, tt("stat", "RESULT"), "{\"POWER1\":\"OFF\"}"), "…each with its stat/<t>/RESULT JSON");
    J st; check(find(v, tt("tele", "STATE")) && parse(find(v, tt("tele", "STATE"))->p, st) && st.get("POWER5") && st.get("Wifi")
                && st.get("UptimeSec") && st.get("Time")->s == "2026-10-09T23:10:00", "tele STATE: valid JSON with Time, UptimeSec, POWER<n>, Wifi");

    section("TASMOTA: POWER commands (CmndPower / ExecuteCommandPower + Interlock)");
    for (int i : {1, 2, 4, 5}) inject(tt("cmnd", "POWER" + std::to_string(i)), "OFF");
    v = take();
    check(pos == 0, "OFF to ports that are not selected changes nothing");
    check(powerPubs(v).size() == 4, "…but each is answered with its state");
    inject(tt("cmnd", "POWER3"), "ON"); v = take();
    check(pos == 3 && has(v, tt("stat", "POWER3"), "ON") && has(v, tt("stat", "RESULT"), "{\"POWER3\":\"ON\"}"), "POWER3 ON selects port 3 and reports it");
    check(has(v, "uberant/ubersdr/antenna", "3"), "…and the native topics follow");
    inject(tt("cmnd", "POWER3"), "ON"); v = take();
    check(pos == 3 && powerPubs(v).size() == 1 && has(v, tt("stat", "POWER3"), "ON"), "ON to the selected port: no change, still answered");
    inject(tt("cmnd", "power1"), "on"); v = take(); pp = powerPubs(v);
    check(pos == 1 && pp.size() == 2 && pp[0] == std::make_pair(std::string("POWER3"), std::string("OFF"))
          && pp[1] == std::make_pair(std::string("POWER1"), std::string("ON")), "interlock: POWER3 OFF reported before POWER1 ON (case-insensitive)");
    inject(tt("cmnd", "POWER1"), "TOGGLE"); take();
    check(pos == 0, "TOGGLE on the selected port -> GROUND");
    inject(tt("cmnd", "POWER2"), "toggle"); take();
    check(pos == 2, "TOGGLE on another port selects it");
    for (const char *p : {"1", "true", "START"}) { pos = 0; inject(tt("cmnd", "POWER4"), p); take(); check(pos == 4, std::string("\"") + p + "\" means ON"); }
    for (const char *p : {"0", "false", "Stop"}) { pos = 4; inject(tt("cmnd", "POWER4"), p); take(); check(pos == 0, std::string("\"") + p + "\" means OFF"); }
    pos = 0; inject(tt("cmnd", "POWER4"), "2"); take(); check(pos == 4, "\"2\" means TOGGLE");
    inject(tt("cmnd", "POWER"), "1"); take();
    check(pos == 1, "bare POWER is POWER1");
    inject(tt("cmnd", "POWER0"), "OFF"); v = take();
    check(pos == 0 && powerPubs(v).size() >= 5, "POWER0 OFF -> GROUND, every relay reported");
    inject(tt("cmnd", "POWER0"), "ON"); take();
    check(pos == 0, "POWER0 ON: cannot turn every antenna on, ignored");
    inject(tt("cmnd", "POWER6"), "ON"); v = take();
    check(pos == 0 && v.empty(), "POWER6 beyond the relays: ignored, as CmndPower does");
    inject(tt("cmnd", "POWERONSTATE"), "1"); inject(tt("cmnd", "POWERLOCK"), "1"); v = take();
    check(pos == 0 && v.empty(), "POWERONSTATE / POWERLOCK not taken for POWER<n>");
    for (const char *p : {"", "BLINK", "garbage"}) {
        inject(tt("cmnd", "POWER2"), p); v = take();
        check(pos == 0 && has(v, tt("stat", "POWER2"), "OFF"), std::string("payload \"") + p + "\": state reported, nothing changed");
    }
    inject(tt("cmnd", "POWER2"), "ON"); take(); locked = true;
    inject(tt("cmnd", "POWER5"), "ON"); v = take();
    check(pos == 2 && has(v, tt("stat", "POWER5"), "OFF"), "locked: ON refused, state reported (PowerLock)");
    inject(tt("cmnd", "POWER2"), "OFF"); inject(tt("cmnd", "POWER0"), "OFF"); take();
    check(pos == 2, "locked: OFF and POWER0 refused too");
    locked = false;

    section("TASMOTA: STATE / STATUS");
    inject(tt("cmnd", "STATE"), ""); v = take();
    J s1; check(find(v, tt("stat", "RESULT")) && parse(find(v, tt("stat", "RESULT"))->p, s1) && s1.get("POWER2") && s1.get("POWER2")->s == "ON",
                "STATE -> stat/<t>/RESULT with POWER2 ON");
    inject(tt("cmnd", "Status"), "11"); v = take();
    J s2; check(find(v, tt("stat", "STATUS11")) && parse(find(v, tt("stat", "STATUS11"))->p, s2) && s2.get("StatusSTS") && s2.get("StatusSTS")->get("POWER2"),
                "STATUS 11 -> stat/<t>/STATUS11 {\"StatusSTS\":{…}}");
    inject(tt("cmnd", "STATUS"), "0"); inject(tt("cmnd", "FriendlyName1"), "x"); v = take();
    check(v.empty(), "other commands are not answered");
    g_millis += 299000; m->loop(); v = take();
    check(!find(v, tt("tele", "STATE")), "no tele STATE before 300 s");
    g_millis += 1000; m->loop(); v = take();
    check(find(v, tt("tele", "STATE")) != nullptr, "tele STATE every 300 s (TELE_PERIOD)");

    section("TASMOTA: changes from elsewhere (buttons, web, schedule, native MQTT)");
    pos = 4; m->publishState(); v = take(); pp = powerPubs(v);
    check(pp.size() == 2 && pp[0].first == "POWER2" && pp[0].second == "OFF" && pp[1].first == "POWER4" && pp[1].second == "ON",
          "any position change is reported as POWER OFF then ON");
    check(!find(v, "tasmota/discovery/CC50E345CA21/config"), "…without republishing discovery");
    m->publishState(); v = take();
    check(powerPubs(v).empty(), "publishState with nothing changed: no POWER noise");
    names[2] = "Sky Loop"; m->publishState(); v = take();
    J d2; check(find(v, "tasmota/discovery/CC50E345CA21/config") && parse(find(v, "tasmota/discovery/CC50E345CA21/config")->p, d2)
                && d2.get("fn")->a[1].s == "Sky Loop", "renaming a port republishes discovery with the new name");
    maxA = 3; pos = 0; m->publishState(); v = take();
    J d3; int ones = 0;
    if (find(v, "tasmota/discovery/CC50E345CA21/config") && parse(find(v, "tasmota/discovery/CC50E345CA21/config")->p, d3))
        for (auto &x : d3.get("rl")->a) ones += x.n == 1;
    check(ones == 3 && powerPubs(v).size() == 3, "lowering max to 3: rediscovered with 3 relays, 3 states");
    devName = "New"; m->publishState(); v = take();
    check(find(v, "tasmota/discovery/CC50E345CA21/config") != nullptr, "renaming the device republishes discovery");
    maxA = 0; m->publishState(); v = take();
    J d4; bool none = find(v, "tasmota/discovery/CC50E345CA21/config") && parse(find(v, "tasmota/discovery/CC50E345CA21/config")->p, d4);
    if (none) for (auto &x : d4.get("rl")->a) none = none && x.n == 0;
    check(none, "max 0: discovered with no relays");
    inject(tt("cmnd", "POWER1"), "ON"); take();
    check(pos == 0, "…and POWER1 is ignored");

    section("TASMOTA: turning discovery off / reconnecting");
    fresh(); startTas(); take();
    { MqttConfig c = cfg(false); start(c); }
    v = take();
    const Pub *gone = find(v, "tasmota/discovery/CC50E345CA21/config");
    check(gone && gone->p.empty() && gone->r, "discovery withdrawn: empty retained config (SetOption19 1)");
    check(find(v, "tasmota/discovery/CC50E345CA21/sensors") && find(v, "tasmota/discovery/CC50E345CA21/sensors")->p.empty(), "…and sensors");
    check(has(v, tt("tele", "LWT"), "Offline"), "tele LWT Offline before the session is dropped");
    g_subs.clear(); m->loop(); v = take();
    check(g_will == "uberant/ubersdr/availability" && !subscribed(tt("cmnd", "#")), "back to the native will, no cmnd/<t>/# subscription");
    check(!find(v, "tasmota/discovery/CC50E345CA21/config") && powerPubs(v).empty(), "and nothing Tasmota published");
    fresh(); startTas(); take();
    m->begin(cfg(false), "ubersdr", statusFn, infoFn, timeFn, setFn, maxFn, nameFn, restartFn);  // e.g. the broker restarted: drop
    { MqttConfig c = cfg(false); c.discovery = true; start(c); } take(); g_subs.clear();
    m->loop(); v = take();
    check(find(v, "tasmota/discovery/CC50E345CA21/config") && powerPubs(v).size() == 5,
          "re-enabling: discovery and every POWER<n> published again on connect");
    check(g_will == tt("tele", "LWT"), "…with Tasmota's will");
    fresh(); { MqttConfig c = cfg(false); c.discovery = true; start(c); } m->loop(); v = take();
    check(!find(v, "tasmota/discovery/CC50E345CA21/config") && g_will == "uberant/ubersdr/availability",
          "discovery on but no hooks set: stays native (no half-working Tasmota)");
}

int main() {
    testNative();
    testTasmota();
    return finish("mqtt");
}
