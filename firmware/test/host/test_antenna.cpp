// test_antenna.cpp — src/antenna.h: the position model, the 74HCT138 select lines,
// UP/DOWN/SET behaviour, labels, custom names and their EEPROM persistence.
#include "antenna.h"

#include "harness.h"

static int lines() {   // the position currently driven onto A2..A0
    return (g_pinOut[PIN_SEL_A2] << 2) | (g_pinOut[PIN_SEL_A1] << 1) | g_pinOut[PIN_SEL_A0];
}
static Antenna boot() { Antenna a; a.begin(); return a; }

static void testDecoder() {
    section("decoder select lines (docs/74hct138-truth-table.md)");
    fakesReset();
    Antenna a = boot();
    check(g_pinMode[PIN_SEL_A0] == OUTPUT && g_pinMode[PIN_SEL_A1] == OUTPUT && g_pinMode[PIN_SEL_A2] == OUTPUT, "A0/A1/A2 (GPIO14/12/13) are outputs");
    check(a.position() == POS_GROUND && lines() == 0, "boots at GROUND, lines 000");
    a.setMaxAntennas(7);
    bool all = true;
    for (int p = 0; p <= 7; p++) {
        a.setPosition(p);
        all = all && lines() == p
                  && g_pinOut[14] == (p & 1) && g_pinOut[12] == ((p >> 1) & 1) && g_pinOut[13] == ((p >> 2) & 1);
    }
    check(all, "positions 0..7 drive GPIO14=A0 (LSB), GPIO12=A1, GPIO13=A2 (MSB)");
}

static void testStepping() {
    section("UP / DOWN stepping and wrap");
    fakesReset();
    Antenna a = boot();
    check(a.maxAntennas() == DEFAULT_MAX_ANTENNAS && DEFAULT_MAX_ANTENNAS == 5, "factory max is 5 (this board has 5 relays)");
    std::string seq;
    for (int i = 0; i < 7; i++) { a.up(); seq += std::to_string(a.position()); }
    check(seq == "1234501", "UP: 1..5 then wraps to GROUND (" + seq + ")");
    a.setPosition(0); seq.clear();
    for (int i = 0; i < 7; i++) { a.down(); seq += std::to_string(a.position()); }
    check(seq == "5432105", "DOWN from GROUND wraps to max (" + seq + ")");
    check(lines() == a.position(), "the select lines follow every step");
    a.setMaxAntennas(0);
    a.up(); bool g1 = a.position() == 0; a.down();
    check(g1 && a.position() == 0, "max 0: UP and DOWN stay on GROUND");
}

static void testMax() {
    section("SET button: bumpMax / setMaxAntennas");
    fakesReset();
    Antenna a = boot();
    std::string seq;
    for (int i = 0; i < 4; i++) { a.bumpMax(); seq += std::to_string(a.maxAntennas()); }
    check(seq == "6701", "bump: 5 -> 6 -> 7 -> wraps to 0 -> 1 (" + seq + ")");
    a.setMaxAntennas(5); a.setPosition(5); a.setMaxAntennas(3);
    check(a.position() == POS_GROUND && lines() == 0, "lowering max below the selection drops to GROUND");
    a.setPosition(2); a.setMaxAntennas(4);
    check(a.position() == 2, "raising max keeps the selection");
    a.setMaxAntennas(-3); bool lo = a.maxAntennas() == 0;
    a.setMaxAntennas(12);
    check(lo && a.maxAntennas() == 7, "setMaxAntennas clamps to 0..7");
    a.setMaxAntennas(7); a.setPosition(7); a.bumpMax();
    check(a.maxAntennas() == 0 && a.position() == 0, "bump past 7 wraps to 0 and the selection to GROUND");
    a.setMaxAntennas(4);
    Antenna b = boot();
    check(b.maxAntennas() == 4 && EEPROM.mem[EEPROM_ADDR_MAX] == 4 && EEPROM.mem[EEPROM_ADDR_MAGIC] == 0xA6, "max persists (EEPROM[1], magic 0xA6)");
}

static void testSetPosition() {
    section("setPosition bounds");
    fakesReset();
    Antenna a = boot();
    a.setPosition(3); bool ok3 = a.position() == 3;
    a.setPosition(6);
    check(ok3 && a.position() == POS_GROUND, "a position above max selects GROUND");
    a.setPosition(-1);
    check(a.position() == POS_GROUND, "a negative position selects GROUND");
    a.setPosition(5);
    Antenna b = boot();
    check(b.position() == POS_GROUND, "the selection itself is not persisted: every boot starts at GROUND");
}

static void testLabels() {
    section("labels (OLED / status) and the stock web label");
    fakesReset();
    Antenna a = boot();
    check(a.label() == "GROUND" && a.webLabel() == "<p>GROUND</p>\n", "GROUND: \"GROUND\" / web \"<p>GROUND</p>\\n\"");
    a.setPosition(3);
    check(a.label() == "ANT: 3" && a.webLabel() == "3", "ANT3: \"ANT: 3\" / web bare \"3\"");
    a.setName(3, "80m Vertical");
    check(a.label() == "80m Vertical" && a.webLabel() == "3", "a custom name replaces the OLED label; the stock web label stays the number");
}

static void testNames() {
    section("custom names");
    fakesReset();
    Antenna a = boot();
    check(a.namesJson() == "{}", "no names: {}");
    check(a.setName(1, "40m Dipole") == "40m Dipole" && a.hasName(1), "set a name");
    check(a.setName(0, "Earth") == "" && a.name(0) == "", "GROUND cannot be named");
    check(a.setName(8, "x") == "" && a.name(8) == "", "positions above 7 cannot be named");
    check(a.setName(2, "  a\"b\\c\x01" "d  ") == "abcd", "quotes, backslashes and control characters are stripped, ends trimmed");
    check(a.setName(4, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == "ABCDEFGHIJKLMNOP", "capped at 16 characters");
    check(a.namesJson() == "{\"1\":\"40m Dipole\",\"2\":\"abcd\",\"4\":\"ABCDEFGHIJKLMNOP\"}", "namesJson: set names only, by position");
    J j; check(parseJson(a.namesJson().s, j), "namesJson is valid JSON");
    a.setName(2, "");
    check(!a.hasName(2) && a.namesJson().indexOf("\"2\"") < 0, "an empty name clears it");
    a.setName(7, "Top");

    Antenna b = boot();
    check(b.name(1) == "40m Dipole" && b.name(4) == "ABCDEFGHIJKLMNOP" && b.name(7) == "Top" && !b.hasName(2), "names survive a reboot");
    int addr = EEPROM_ADDR_NAMES + (7 - 1) * EEPROM_NAME_SLOT;
    check(memcmp(&EEPROM.mem[addr], "Top\0", 4) == 0, "slot n at 2 + (n-1)*17, NUL-terminated");
    check(EEPROM_SIZE == 121 && EEPROM_ANTENNA_SIZE == EEPROM_SIZE, "antenna block is 121 bytes and config.h's mirror agrees");
}

static void testEepromCompat() {
    section("EEPROM: erased, legacy and corrupt images");
    fakesReset();
    Antenna a = boot();
    check(a.maxAntennas() == 5 && a.namesJson() == "{}", "erased flash: max 5, no names");
    check(EEPROM.size == EEPROM_TOTAL_SIZE && EEPROM_TOTAL_SIZE <= 4096, "begin() opens the whole shared sector (" + std::to_string(EEPROM_TOTAL_SIZE) + " of 4096 bytes)");

    fakesReset();
    EEPROM.mem[0] = 0xA5; EEPROM.mem[1] = 3; memcpy(&EEPROM.mem[2], "junk", 4);
    a = boot();
    check(a.maxAntennas() == 3 && a.namesJson() == "{}", "legacy 0xA5 image (max only): max kept, slot bytes not read as names");
    a.setName(1, "New");
    check(EEPROM.mem[0] == 0xA6 && boot().name(1) == "New", "first name write upgrades the image to 0xA6");

    fakesReset();
    EEPROM.mem[0] = 0xA6; EEPROM.mem[1] = 9;
    check(boot().maxAntennas() == 5, "max byte out of range: factory default");
    fakesReset();
    EEPROM.mem[0] = 0x12; EEPROM.mem[1] = 2;
    check(boot().maxAntennas() == 5, "unknown magic: factory default");
    fakesReset();
    EEPROM.mem[0] = 0xA6; EEPROM.mem[1] = 5; memcpy(&EEPROM.mem[2], "Ab", 2);   // rest of slot 0xFF
    check(boot().name(1) == "Ab", "an erased (0xFF) byte ends a name");
    check(EEPROM.outOfRange == 0, "no EEPROM access outside the opened sector");
}

int main() {
    testDecoder();
    testStepping();
    testMax();
    testSetPosition();
    testLabels();
    testNames();
    testEepromCompat();
    return finish("antenna");
}
