// test_ntp.cpp — src/ntp.h: sync detection, retry/re-sync, and the ISO 8601 formatters.
#include "ntp.h"

#include "harness.h"

static const time_t T = 1720019045;   // 2024-07-03T15:04:05Z (docs' example)

int main() {
    section("formatting");
    check(Ntp::isoOf(T) == "2024-07-03T15:04:05Z", "isoOf: 2024-07-03T15:04:05Z");
    check(Ntp::isoOf(0) == "", "isoOf(0): empty");
    check(Ntp::localIsoOf(T, 0) == "2024-07-03T15:04:05Z", "localIsoOf, offset 0: Z");
    check(Ntp::localIsoOf(T, 60) == "2024-07-03T16:04:05+01:00", "+60: +01:00");
    check(Ntp::localIsoOf(T, -300) == "2024-07-03T10:04:05-05:00", "-300: -05:00");
    check(Ntp::localIsoOf(T, 330) == "2024-07-03T20:34:05+05:30", "+330: +05:30 (half-hour zone)");
    check(Ntp::localIsoOf(T, -570) == "2024-07-03T05:34:05-09:30", "-570: -09:30");
    check(Ntp::localIsoOf(T, 540) == "2024-07-04T00:04:05+09:00", "crosses midnight into the next day");
    check(Ntp::localIsoOf(T, -720) == "2024-07-03T03:04:05-12:00" && Ntp::localIsoOf(T, 840) == "2024-07-04T05:04:05+14:00", "extremes -12:00 / +14:00");
    check(Ntp::isoOf(1704067199) == "2023-12-31T23:59:59Z" && Ntp::isoOf(1709208000) == "2024-02-29T12:00:00Z", "year end and leap day");

    section("sync");
    fakesReset();
    Ntp n;
    check(!n.synced() && n.isoNow() == "" && n.localIsoNow(60) == "" && n.now() == 0, "before begin: unsynced, empty strings, now() 0");
    n.begin("time.example", 123);
    check(g_configTimeCalls == 1 && g_configTimeServer == "time.example", "begin() configures SNTP with the given server");
    n.loop();
    check(!n.synced(), "no time yet: still unsynced");
    int calls = g_configTimeCalls;
    g_millis += 29000; n.loop(); bool early = g_configTimeCalls == calls;
    g_millis += 2000; n.loop();
    check(early && g_configTimeCalls == calls + 1, "unsynced: SNTP prodded again every 30 s");
    setEpoch(T);
    n.loop();
    check(n.synced() && n.isoNow() == "2024-07-03T15:04:05Z" && n.now() == T, "a plausible time (after 2023-11-14): synced");
    check(n.localIsoNow(60) == "2024-07-03T16:04:05+01:00", "localIsoNow applies the offset");
    calls = g_configTimeCalls;
    g_millis += 3599000; n.loop(); bool noResync = g_configTimeCalls == calls;
    g_millis += 2000; n.loop();
    check(noResync && g_configTimeCalls == calls + 1, "synced: re-sync once an hour");
    check(n.isoNow() == Ntp::isoOf(time(nullptr)), "the clock keeps running between syncs");

    fakesReset();
    Ntp e; e.begin("", 123);
    check(g_configTimeServer == "time.cloudflare.com", "an empty host uses the default server, time.cloudflare.com");
    setEpoch(T); e.loop();
    check(e.synced(), "…and syncs");
    fakesReset();
    Ntp s; s.begin("pool.ntp.org"); setEpoch(1600000000); s.loop();
    check(!s.synced(), "a time before 2023-11-14 is not taken as synced");
    Ntp idle; g_millis += 100000; idle.loop();
    check(!idle.synced(), "loop() before begin() does nothing");
    return finish("ntp");
}
