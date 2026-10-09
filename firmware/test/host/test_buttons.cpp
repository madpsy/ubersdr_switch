// test_buttons.cpp — src/buttons.h: pin setup, debouncing and edge detection.
#include "buttons.h"

#include "harness.h"

static Buttons b;
static void tick(int ms, int step = 5) { for (int t = 0; t < ms; t += step) { g_millis += step; b.update(); } }
static void level(Btn which, int v) {
    const uint8_t pins[] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_SET, PIN_BTN_ERASE};
    g_pinLevel[pins[(int)which]] = v;
}
// Count edges while the clock runs.
static void run(int ms, Btn w, int &presses, int &releases) {
    for (int t = 0; t < ms; t += 5) { g_millis += 5; b.update(); presses += b.justPressed(w); releases += b.justReleased(w); }
}

int main() {
    section("setup (docs/hardware-pinout.md)");
    fakesReset(); b = Buttons(); b.begin();
    check(PIN_BTN_UP == 3 && PIN_BTN_DOWN == 1 && PIN_BTN_SET == 0 && PIN_BTN_ERASE == 2, "UP=GPIO3, DOWN=GPIO1, SET=GPIO0, ERASE=GPIO2");
    check(g_pinMode[3] == INPUT_PULLUP && g_pinMode[1] == INPUT_PULLUP && g_pinMode[0] == INPUT_PULLUP && g_pinMode[2] == INPUT_PULLUP,
          "all four are inputs with pull-ups (active low)");
    b.update();
    check(!b.pressed(Btn::UP) && !b.justPressed(Btn::UP) && !b.justReleased(Btn::UP), "released at boot, no edges");

    section("debounce (BTN_DEBOUNCE_MS = 40)");
    int p = 0, r = 0;
    level(Btn::UP, LOW); run(35, Btn::UP, p, r);
    check(p == 0 && !b.pressed(Btn::UP), "LOW for 35 ms: not yet a press");
    run(10, Btn::UP, p, r);
    check(p == 1 && b.pressed(Btn::UP), "LOW for 40 ms: one press");
    run(500, Btn::UP, p, r);
    check(p == 1 && r == 0 && b.pressed(Btn::UP), "held: the press is reported once, pressed() stays true");
    level(Btn::UP, HIGH); run(60, Btn::UP, p, r);
    check(r == 1 && !b.pressed(Btn::UP), "release: one release edge");

    p = r = 0;
    for (int i = 0; i < 10; i++) { level(Btn::DOWN, i & 1 ? HIGH : LOW); run(15, Btn::DOWN, p, r); }
    level(Btn::DOWN, HIGH); run(60, Btn::DOWN, p, r);
    check(p == 0 && r == 0, "contact bounce shorter than 40 ms: no press at all");

    p = r = 0;
    level(Btn::SET, LOW); run(60, Btn::SET, p, r);
    level(Btn::SET, HIGH); run(20, Btn::SET, p, r);
    level(Btn::SET, LOW); run(60, Btn::SET, p, r);
    check(p == 1 && r == 0 && b.pressed(Btn::SET), "a 20 ms gap while held is ignored (one press)");
    level(Btn::SET, HIGH); run(60, Btn::SET, p, r);

    section("independence and raw reads");
    p = r = 0; int pu = 0, ru = 0;
    level(Btn::ERASE, LOW);
    for (int t = 0; t < 60; t += 5) { g_millis += 5; b.update(); p += b.justPressed(Btn::ERASE); pu += b.justPressed(Btn::UP); }
    check(p == 1 && pu == 0 && b.pressed(Btn::ERASE) && !b.pressed(Btn::UP), "one button does not affect another");
    (void)ru;
    level(Btn::UP, LOW);
    check(b.rawPressed(Btn::UP) && !b.pressed(Btn::UP), "rawPressed() is immediate (boot gestures), pressed() is debounced");
    level(Btn::UP, HIGH); level(Btn::ERASE, HIGH); tick(60);
    check(!b.pressed(Btn::ERASE) && !b.rawPressed(Btn::ERASE), "all released");
    return finish("buttons");
}
