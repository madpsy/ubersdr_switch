// test_display.cpp — src/display.h: what the OLED shows. The SSD1306 stand-in records
// every frame (texts, positions, fonts, pixels), so these checks read the screen.
#include "display.h"

#include "harness.h"

static const OledFrame &last() { static OledFrame none; return g_frames.empty() ? none : g_frames.back(); }
static Display *started() {
    static Display *d = nullptr;
    fakesReset();
    delete d; d = new Display(); d->begin();
    return d;
}
static void advance(Display &d, int ms, const String &dev, const String &tm, DisplayMode m) {
    for (int t = 0; t < ms; t += 250) { g_millis += 250; d.tickStatus(dev, tm, m); }
}

static void testBoot() {
    section("power-on: reset pulse, I2C, banner");
    Display *d = started();
    check(g_pinMode[PIN_OLED_RST] == OUTPUT && g_pinOut[PIN_OLED_RST] == HIGH, "GPIO16 reset pulsed and released HIGH (Heltec panel stays blank otherwise)");
    check(g_oledBeginAddr == 0x3C, "the panel found by the I2C scan (0x3C) is the one started");
    check(last().has("ANTENNA") && last().has("SELECTOR V2") && last().rects == 1, "banner: border, ANTENNA, SELECTOR V2");
    check(last().find("ANTENNA")->font == "9pt" && last().find("SELECTOR V2")->font == "size1", "ANTENNA in the 9pt font, the subtitle small");
    fakesReset(); g_i2cDevice = 0x3D;
    Display e; e.begin();
    check(g_oledBeginAddr == 0x3D, "a panel at another address is found too");
    fakesReset(); g_i2cDevice = 0;
    Display f; f.begin();
    check(g_oledBeginAddr == 0x3C, "nothing answers the scan: falls back to 0x3C");
    (void)d;
    Display none;   // begin() never called: every call is a safe no-op
    none.showStatus("GROUND", ""); none.showCustomMessage("x"); none.blankDisplay(); none.tickBlank();
    none.tickStatus("a", "b", DisplayMode::Port); none.showMax(3); none.showSplash("x");
    check(true, "a Display that was never begun ignores every call");
}

static void testSplash() {
    section("boot splash: largest font that fits");
    Display *d = started();
    d->showSplash("UberANT");
    check(last().texts.size() == 1 && last().texts[0].text == "UberANT" && last().texts[0].font == "12pt", "short name: 12pt");
    d->showSplash("Antenna Switch");          // 14 chars: 196 px at 12pt, 140 at 9pt
    check(last().texts[0].font == "size1", "14 characters: too wide for 12pt/9pt/size 2 -> size 1");
    d->showSplash("Shack Ant");               // 9 chars: 126 px at 12pt
    check(last().texts[0].font == "12pt", "9 characters still fit 12pt");
    d->showSplash("Shack Ants");              // 10 chars: 140 / 100
    check(last().texts[0].font == "9pt", "10 characters: 9pt");
    d->showSplash("");
    check(last().texts[0].text == "UberSDR", "empty name: \"UberSDR\"");
    d->showSplash(nullptr);
    check(last().texts[0].text == "UberSDR", "null name: \"UberSDR\"");
}

static void testStatus() {
    section("status screen");
    Display *d = started();
    d->showStatus("GROUND", "http://192.168.9.109");
    check(last().has("GROUND") && last().find("GROUND")->font == "12pt", "label in 12pt");
    check(last().find("GROUND")->y > 0 && last().find("GROUND")->y <= 24, "…in the top 24 px");
    d->showStatus("80m Vertical", "");
    check(last().find("80m Vertical") && last().find("80m Vertical")->font != "12pt", "a long label steps down a font");
    d->tickStatus("UberANT", "", DisplayMode::Port);
    g_millis += 3000; d->tickStatus("UberANT", "12:34:56", DisplayMode::Port);
    check(last().has("80m Vertical") && last().has("UberANT  12:34:56"), "port mode: label on top, \"name  HH:MM:SS\" below");
    check(last().find("UberANT  12:34:56")->y == 24 && last().find("UberANT  12:34:56")->font == "size1", "bottom row, small font");
    g_millis += 1000; d->tickStatus("UberANT", "", DisplayMode::Port);
    size_t n = g_frames.size();
    for (int i = 0; i < 8; i++) { g_millis += 250; d->tickStatus("UberANT", "", DisplayMode::Port); }
    check(g_frames.size() == n, "no clock and nothing changing: no redraws");
    size_t before = g_frames.size();
    g_millis += 1000; d->tickStatus("UberANT", "12:34:57", DisplayMode::Port);
    g_millis += 400; d->tickStatus("UberANT", "12:34:57", DisplayMode::Port);
    check(g_frames.size() == before + 1, "with a clock: redrawn once a second");

    section("padlock");
    d->setLocked(true);
    check(last().lines == 3 && last().fills == 1, "locking draws the padlock (3 lines + body)");
    g_millis += 1000; d->tickStatus("UberANT", "12:34:58", DisplayMode::Port);
    check(last().fills == 1, "…and keeps it on every redraw");
    d->setLocked(false);
    check(last().lines == 0 && last().fills == 0, "unlocking removes it");
}

static void testClockModes() {
    section("clock and cycle modes");
    Display *d = started();
    d->showStatus("ANT: 2", "");
    g_millis += 1000; d->tickStatus("Shack", "09:05:07", DisplayMode::Clock);
    check(last().has("09:05") && last().find("09:05")->font == "12pt" && last().has("ANT: 2  Shack"), "clock mode: HH:MM large, \"label  name\" below");
    g_millis += 1000; d->tickStatus("Shack", "", DisplayMode::Clock);
    d->showStatus("ANT: 2", "");
    check(last().has("ANT: 2"), "no time yet (NTP not synced): falls back to the port layout");

    d->notifyPortChange();
    g_millis += 1000; d->tickStatus("Shack", "09:05:08", DisplayMode::Clock);
    check(last().has("ANT: 2") && !last().has("09:05"), "after an antenna change: port layout for 2 s even in clock mode");
    advance(*d, 1500, "Shack", "09:05:10", DisplayMode::Clock);
    check(last().has("09:05"), "…then back to the clock");

    Display *c = started();
    c->showStatus("GROUND", "");
    c->tickStatus("Shack", "10:00:00", DisplayMode::Cycle);
    std::string phases;
    for (int s = 0; s < 4; s++) { advance(*c, 5000, "Shack", "10:00:00", DisplayMode::Cycle); phases += last().has("10:00") ? 'C' : 'P'; }
    check(phases == "CPCP", "cycle mode: alternates port and clock every 5 s (" + phases + ")");
}

static void testScroll() {
    section("bottom-line scroll");
    Display *d = started();
    d->showStatus("GROUND", "");
    String dev = "A Very Long Device Name Here";   // + "  12:00:00" = 38 chars = 228 px
    d->tickStatus(dev, "12:00:00", DisplayMode::Port);
    int x0 = last().find((dev + "  12:00:00").s)->x;
    auto run40 = [&](int ms) { for (int t = 0; t < ms; t += 40) { g_millis += 40; d->tickStatus(dev, "12:00:00", DisplayMode::Port); } };
    run40(1000);   // 25 steps of 40 ms = the pause
    int x1 = last().find((dev + "  12:00:00").s)->x;
    run40(1000);
    int x2 = last().find((dev + "  12:00:00").s)->x;
    check(x0 == 0 && x1 == 0 && x2 < 0, "too wide: holds at the left for ~1 s, then scrolls left (" + std::to_string(x2) + ")");
    int minX = 0; bool wrapped = false;
    for (int i = 0; i < 400; i++) {
        g_millis += 40; d->tickStatus(dev, "12:00:00", DisplayMode::Port);
        int x = last().find((dev + "  12:00:00").s)->x;
        if (minX < 0 && x == 0) wrapped = true;
        minX = std::min(minX, x);
    }
    check(minX == -(228 - 128 + 4), "scrolls exactly to the end of the text (" + std::to_string(minX) + ")");
    check(wrapped, "…pauses there, then wraps back to the start");
    Display *e = started();
    e->showStatus("GROUND", "");
    e->tickStatus("Short", "12:00:00", DisplayMode::Port);
    check(last().find("Short  12:00:00")->x == (128 - 15 * 6) / 2, "text that fits is centred and never scrolls");
}

static void testCustom() {
    section("custom messages (/api/display, MQTT display)");
    Display *d = started();
    d->showStatus("GROUND", "");
    d->showCustomMessage("Hi", 'C', 3000);
    check(last().texts.size() == 1 && last().texts[0].font == "12pt" && d->customActive(), "short text: one line, 12pt, active");
    d->showCustomMessage("Line one\nLine two", 'C', 3000);
    check(last().texts.size() == 2 && last().texts[0].font == "9pt", "two lines: 9pt (12pt is too tall for two)");
    d->showCustomMessage("ANT 3\\n80m Vert", 'C', 3000);
    check(last().has("ANT 3") && last().has("80m Vert"), "a literal \\n (from JSON) splits lines too");
    d->showCustomMessage("a\nb\nc", 'C', 3000);
    check(last().texts.size() == 3 && last().texts[0].font == "size1", "three lines: size 1");
    d->showCustomMessage("1\n2\n3\n4\n5", 'C', 3000);
    check(last().texts.size() == 4, "at most four lines");
    d->showCustomMessage("abcdefghijklmnopqrstuvwxyz", 'C', 3000);
    check(last().texts.size() == 1 && last().texts[0].text == "abcdefghijklmnopqrst>" && last().texts[0].font == "size1",
          "too long even at size 1: cut at 20 chars with '>'");
    d->showCustomMessage("ANT 3\nA very long description here", 'C', 3000);
    check(last().texts.size() == 2 && last().has("ANT 3") && last().has("A very long descript>"),
          "one over-long line does not cost the others: both lines shown, the long one cut with '>'");
    check(last().find("A very long descript>")->x >= 0 && (int)strlen("A very long descript>") * 6 <= 128, "…and the cut line fits the panel");
    d->showCustomMessage("Hi", 'L', 3000); int xl = last().texts[0].x;
    d->showCustomMessage("Hi", 'R', 3000); int xr = last().texts[0].x;
    d->showCustomMessage("Hi", 'C', 3000); int xc = last().texts[0].x;
    check(xl == 0 && xr == 128 - 28 && xc == (128 - 28) / 2, "align left / right / centre");

    size_t n = g_frames.size();
    advance(*d, 2500, "UberANT", "", DisplayMode::Port);
    check(d->customActive() && g_frames.size() == n, "while the message shows, status ticks do not draw over it");
    advance(*d, 1000, "UberANT", "", DisplayMode::Port);
    check(!d->customActive() && last().has("GROUND"), "after its duration the status screen returns");
    d->showCustomMessage("x", 'C', 10);
    g_millis += 99; bool still = d->customActive(); g_millis += 2;
    check(still && !d->customActive(), "minimum duration 100 ms");
}

static void testBlank() {
    section("screen saver");
    Display *d = started();
    d->showStatus("GROUND", "");
    d->blankDisplay();
    check(d->isBlank() && last().blank(), "blank: an empty frame");
    d->tickStatus("UberANT", "12:00:00", DisplayMode::Port);
    check(last().blank(), "status ticks do nothing while blank");
    std::string dots;
    for (int i = 0; i < 6; i++) { g_millis += 250; d->tickBlank(); dots += last().pixels == 4 ? '*' : '.'; }
    check(dots == ".**..*" || dots == "**..**" || dots == ".**..*", "a 2x2 dot blinks at ~1 Hz while blank (" + dots + ")");
    d->unblankDisplay();
    check(!d->isBlank() && last().has("GROUND"), "unblank redraws the status");

    d->blankDisplay();
    d->showCustomMessage("Wake", 'C', 1000, true);
    check(!d->isBlank() && last().has("Wake"), "a message shows even when blanked");
    advance(*d, 1500, "UberANT", "", DisplayMode::Port);
    check(d->isBlank(), "…and the display re-blanks when it ends (wasBlank)");
    d->unblankDisplay();
    d->showCustomMessage("Stay", 'C', 1000, false);
    advance(*d, 1500, "UberANT", "", DisplayMode::Port);
    check(!d->isBlank() && last().has("GROUND"), "a message on an awake display returns to the status");
}

static void testScreens() {
    section("other screens");
    Display *d = started();
    d->showMax(5);
    check(last().has("MAX 5") && last().find("MAX 5")->font == "9pt", "SET: \"MAX 5\"");
    d->showErasePrompt(3);
    check(last().has("press ERASE for") && last().has("erase stored info") && last().has("3"), "boot erase prompt with countdown");
    d->showEraseHold();
    check(last().has("ERASE") && last().has("hold the button"), "ERASE held");
    d->showAP("AutoConnectAP", "http://192.168.4.1");
    check(last().has("ssid: AutoConnectAP") && last().has("http://192.168.4.1"), "config portal: SSID and URL");
    d->showForceAP();
    check(last().has("hold the button") && last().has("AutoConnect (+)"), "UP held at boot: force portal");
    d->showMessage("Connecting", "Shack");
    check(last().find("Connecting")->y == 6 && last().find("Shack")->y == 20, "two-line message");
    d->showMessage("Alone");
    check(last().find("Alone")->y == 12, "one-line message is centred vertically");
}

int main() {
    testBoot();
    testSplash();
    testStatus();
    testClockModes();
    testScroll();
    testCustom();
    testBlank();
    testScreens();
    return finish("display");
}
