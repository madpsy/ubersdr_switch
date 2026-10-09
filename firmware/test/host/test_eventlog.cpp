// test_eventlog.cpp — src/eventlog.h: the in-RAM ring of the last 25 antenna changes.
#include "eventlog.h"

#include "harness.h"

int main() {
    section("event log");
    EventLog e;
    check(e.count() == 0 && e.toJson() == "[]", "empty: []");
    e.push("2026-10-09T10:00:00Z", "button", 3, "ANT: 3");
    check(e.toJson() == "[{\"ts\":\"2026-10-09T10:00:00Z\",\"source\":\"button\",\"position\":3,\"label\":\"ANT: 3\"}]", "one event: ts, source, position, label");
    e.push("", "api", 0, "GROUND");
    J j; check(parseJson(e.toJson().s, j) && j.a.size() == 2, "valid JSON array");
    check(j[0]["source"].s == "api" && j[0]["ts"].s == "" && j[1]["source"].s == "button", "newest first; ts empty before NTP sync");

    EventLog q;
    q.push("", "web", 1, "say \"hi\" \\ there");
    J k; check(parseJson(q.toJson().s, k) && k[0]["label"].s == "say \"hi\" \\ there", "quotes and backslashes in a label are escaped");

    EventLog f;
    for (int i = 0; i < 30; i++) f.push(String(i), "api", (int8_t)(i % 8), String("L") + i);
    J m; parseJson(f.toJson().s, m);
    check(f.count() == 25 && m.a.size() == 25, "capped at 25 (EVENT_LOG_CAP)");
    check(m[0]["ts"].s == "29" && m[24]["ts"].s == "5", "oldest dropped: holds 29 down to 5");
    for (const char *src : {"button", "api", "web", "mqtt", "schedule"}) f.push("", src, 1, "x");
    J m2; parseJson(f.toJson().s, m2);
    check(m2[0]["source"].s == "schedule" && m2[4]["source"].s == "button", "every source string is kept as given");
    return finish("eventlog");
}
