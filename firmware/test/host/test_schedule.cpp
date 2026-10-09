// test_schedule.cpp — src/schedule.h: firing rules, next-firing preview, JSON and the
// EEPROM image.
#include "schedule.h"

#include "harness.h"

enum { SUN, MON, TUE, WED, THU, FRI, SAT };
static ScheduleEntry E(bool en, uint8_t days, uint8_t h, uint8_t m, uint8_t pos) {
    ScheduleEntry e; e.enabled = en; e.daysMask = days; e.hour = h; e.minute = m; e.position = pos; return e;
}
static Schedule fresh() { Schedule s; s.begin(); return s; }

static void testTick() {
    section("tick: what fires");
    fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE);
    Schedule s = fresh();
    check(s.schedEnabled() && !s.respectLock(), "erased EEPROM: scheduler on, respect_lock off, no entries");
    check(s.tick(8, 0, MON) == -1, "nothing enabled: nothing fires");
    s.setEntry(0, E(true, SCHED_DAY_MON | SCHED_DAY_WED, 8, 30, 3));
    check(s.tick(8, 29, MON) == -1 && s.tick(9, 30, MON) == -1, "wrong minute / hour: no");
    check(s.tick(8, 30, TUE) == -1, "wrong day: no");
    check(s.tick(8, 30, MON) == 3, "08:30 Monday: position 3");
    check(s.tick(8, 30, MON) == -1, "same minute again: does not re-fire");
    check(s.tick(8, 30, WED) == 3, "Wednesday 08:30 fires too (a new minute re-arms the entry)");
    check(s.tick(8, 30, WED) == -1, "…once");
    s.tick(8, 31, WED);
    s.setEntry(0, E(true, SCHED_DAY_MON | SCHED_DAY_WED, 8, 30, 3));
    check(s.tick(8, 30, WED) == 3, "editing an entry re-arms it within the same minute");

    section("tick: day after day (the device runs for weeks)");
    Schedule d;
    d.setEntry(0, E(true, SCHED_DAY_ALL, 6, 15, 2));
    int fires = 0, extra = 0;
    for (int day = 0; day < 14; day++)
        for (int h = 0; h < 24; h++)
            for (int m = 0; m < 60; m++)
                for (int t = 0; t < 4; t++) {          // loop() ticks every 250 ms
                    int r = d.tick(h, m, (uint8_t)(day % 7));
                    if (r == 2 && h == 6 && m == 15) fires++; else if (r >= 0) extra++;
                }
    check(fires == 14 && extra == 0, "a daily entry fires exactly once a day for two weeks (" + std::to_string(fires) + ")");
    Schedule w;
    w.setEntry(0, E(true, SCHED_DAY_TUE, 22, 0, 1));
    fires = 0;
    for (int day = 0; day < 21; day++) {
        for (int t = 0; t < 4; t++) w.tick(21, 59, (uint8_t)(day % 7));
        for (int t = 0; t < 4; t++) fires += w.tick(22, 0, (uint8_t)(day % 7)) == 1;
    }
    check(fires == 3, "a weekly entry fires once a week for three weeks (" + std::to_string(fires) + ")");
    s.setEntry(1, E(false, SCHED_DAY_ALL, 12, 0, 1));
    check(s.tick(12, 0, FRI) == -1, "a disabled entry never fires");

    s.setEntry(2, E(true, SCHED_DAY_ALL, 20, 0, 2));
    s.setEntry(3, E(true, SCHED_DAY_ALL, 20, 0, 5));
    check(s.tick(20, 0, SAT) == 2 && s.tick(20, 0, SAT) == 5, "two entries in the same minute: lowest id first, the other on the next tick");
    check(s.tick(20, 0, SAT) == -1, "…then nothing");

    s.setEntry(4, E(true, SCHED_DAY_ALL, 6, 0, 0));
    check(s.tick(6, 0, SUN) == 0, "position 0 (GROUND) can be scheduled");
    s.setEntry(4, E(true, SCHED_DAY_ALL, 6, 0, 1));
    check(s.tick(6, 0, SUN) == 1, "editing an entry resets its re-fire guard");

    section("tick: master switch and lock");
    auto rearm = [&]() { s.setEntry(5, E(true, SCHED_DAY_ALL, 7, 0, 4)); };
    rearm();
    s.setSchedEnabled(false);
    check(s.tick(7, 0, MON) == -1, "scheduler disabled: nothing fires");
    s.setSchedEnabled(true);
    check(s.tick(7, 0, MON, true) == 4, "locked but respect_lock off: fires");
    rearm();
    s.setRespectLock(true);
    check(s.tick(7, 0, MON, true) == -1, "locked with respect_lock: does not fire");
    check(s.tick(7, 0, MON, false) == 4, "…and fires once unlocked, in the same minute");
}

static void testEdit() {
    section("editing, clamping, deleting");
    fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE);
    Schedule s = fresh();
    s.setEntry(0, E(true, 0xFF, 24, 60, 9));
    const ScheduleEntry &e = s.entry(0);
    check(e.daysMask == SCHED_DAY_ALL && e.hour == 0 && e.minute == 0 && e.position == 0, "out-of-range days/hour/minute/position are clamped");
    s.setEntry(10, E(true, 1, 1, 1, 1));
    check(true, "an id past the 10 slots is ignored (no crash)");
    s.setEntry(9, E(true, SCHED_DAY_SAT, 23, 59, 7));
    s.deleteEntry(9);
    const ScheduleEntry &d = s.entry(9);
    check(!d.enabled && d.daysMask == SCHED_DAY_ALL && d.hour == 0 && d.minute == 0 && d.position == 0, "delete resets the slot to defaults");

    s.setEntry(1, E(true, SCHED_DAY_ALL, 1, 0, 5));
    s.setEntry(2, E(true, SCHED_DAY_ALL, 2, 0, 3));
    s.setEntry(3, E(false, SCHED_DAY_ALL, 3, 0, 7));
    check(s.disableEntriesAbove(4) == 1, "lowering max to 4 pauses the one enabled entry above it");
    check(!s.entry(1).enabled && s.entry(1).position == 5 && s.entry(2).enabled && s.entry(3).position == 7, "…paused, not deleted; others untouched");
    check(s.disableEntriesAbove(4) == 0, "nothing more to pause");
}

static void testNext() {
    section("nextFiring (status 'scheduler_next')");
    fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE);
    Schedule s = fresh();
    check(s.nextFiring(10, 0, MON) == "", "nothing scheduled: empty");
    s.setEntry(0, E(true, SCHED_DAY_ALL, 18, 5, 2));
    s.setEntry(1, E(true, SCHED_DAY_ALL, 9, 0, 0));
    check(s.nextFiring(10, 0, MON) == "18:05 ANT 2", "later today wins");
    check(s.nextFiring(19, 0, MON) == "09:00 GROUND", "after the last one today: tomorrow's first, GROUND spelled out");
    check(s.nextFiring(18, 5, MON) == "09:00 GROUND", "an entry at the current minute is not 'next'");
    Schedule w; w.setEntry(0, E(true, SCHED_DAY_SUN, 8, 0, 1));
    check(w.nextFiring(9, 0, SUN) == "08:00 ANT 1", "weekly entry already passed today: found next week");
    check(w.nextFiring(7, 0, SUN) == "08:00 ANT 1", "…and later today when it has not");
    check(w.nextFiring(9, 0, SAT) == "08:00 ANT 1", "Saturday -> Sunday wraps the week");
    w.setSchedEnabled(false);
    check(w.nextFiring(9, 0, SAT) == "", "scheduler disabled: empty");
}

static void testPersistence() {
    section("JSON and EEPROM");
    fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE);
    Schedule s = fresh();
    s.setEntry(3, E(true, SCHED_DAY_MON | SCHED_DAY_FRI, 7, 45, 4));
    s.setRespectLock(true);
    J j; check(parseJson(s.toJson().s, j), "toJson is valid JSON");
    check(j["enabled"].b && j["respect_lock"].b && j["entries"].a.size() == 10, "enabled, respect_lock, 10 entries");
    const J &e3 = j["entries"][3];
    check(e3["id"].n == 3 && e3["enabled"].b && e3["days"].n == 34 && e3["hour"].n == 7 && e3["minute"].n == 45 && e3["position"].n == 4, "entry fields");

    Schedule r = fresh();
    check(r.respectLock() && r.schedEnabled() && r.entry(3).enabled && r.entry(3).minute == 45 && r.entry(3).daysMask == 34, "everything survives a reboot");
    check(EEPROM.mem[EEPROM_SCHED_BASE] == SCHED_MAGIC && EEPROM.mem[EEPROM_SCHED_BASE + 1] == 0x01, "header: magic 0xC3, flags bit0 = respect_lock");
    int p = EEPROM_SCHED_BASE + 2 + 3 * SCHED_ENTRY_SIZE;
    check(EEPROM.mem[p] == 1 && EEPROM.mem[p + 1] == 34 && EEPROM.mem[p + 2] == 7 && EEPROM.mem[p + 3] == 45 && EEPROM.mem[p + 4] == 4, "entry 3 at base + 2 + 3*5: flags, days, hour, minute, position");
    r.setSchedEnabled(false);
    check(EEPROM.mem[EEPROM_SCHED_BASE + 1] == 0x03 && !fresh().schedEnabled(), "disabled is stored inverted (bit1 set), so erased flash means enabled");

    fakesReset(); EEPROM.begin(EEPROM_TOTAL_SIZE);
    EEPROM.mem[EEPROM_SCHED_BASE] = SCHED_MAGIC; EEPROM.mem[EEPROM_SCHED_BASE + 1] = 0;
    int q = EEPROM_SCHED_BASE + 2;
    EEPROM.mem[q] = 1; EEPROM.mem[q + 1] = 0xFF; EEPROM.mem[q + 2] = 30; EEPROM.mem[q + 3] = 99; EEPROM.mem[q + 4] = 12;
    Schedule c = fresh();
    check(c.entry(0).enabled && c.entry(0).daysMask == SCHED_DAY_ALL && c.entry(0).hour == 0 && c.entry(0).minute == 0 && c.entry(0).position == 0,
          "corrupt entry bytes load clamped");
    check(EEPROM_SCHED_SIZE == 52 && EEPROM_SCHED_BASE + EEPROM_SCHED_SIZE == EEPROM_TOTAL_SIZE, "schedule block is the last 52 bytes of the sector");
    check(EEPROM.outOfRange == 0, "no EEPROM access outside the opened sector");
}

int main() {
    testTick();
    testEdit();
    testNext();
    testPersistence();
    return finish("schedule");
}
