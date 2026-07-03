# Hardware Pinout — ANTENNA SELECTOR V2

Board: **Heltec WiFi Kit 8 (HTIT-W8266)** — an ESP8266 module with an on-board
0.91" 128×32 SSD1306 OLED (MAC `cc:50:e3:45:ca:21`) — driving a **74HCT138** 3-to-8
line decoder, which in turn switches the antenna relays (RL1–RL5) on the "ANTENNAS
webSWITCH control 1 to 5" board (ANTENI.NET Ltd.).

All assignments were recovered by disassembling the flash image and tracing calls
to the Arduino core primitives `__digitalWrite(pin,val)` (`0x401002fc`) and
`__digitalRead(pin)` (`0x40100360`). See
[`firmware-analysis.md`](firmware-analysis.md) for the method.

## GPIO summary

| GPIO | Dir | Net / Function | Notes |
|------|-----|----------------|-------|
| GPIO12 | OUT | 74HCT138 **A0** (LSB) | decoder select bit 0 |
| GPIO13 | OUT | 74HCT138 **A1** | decoder select bit 1 |
| GPIO14 | OUT | 74HCT138 **A2** (MSB) | decoder select bit 2 |
| GPIO0  | IN  | **SET** button (max antenna count) | active-low, internal pull-up, shows `MAX` |
| GPIO1  | IN  | **DOWN** button | active-low, internal pull-up (also UART0 TX pin) |
| GPIO2  | IN  | **ERASE** button (hold) | active-low, internal pull-up |
| GPIO3  | IN  | **UP** button | active-low, internal pull-up (also UART0 RX pin) |
| GPIO4  | I2C | OLED **SDA** | 0.91" 128×32 SSD1306 (I²C addr `0x3C`) |
| GPIO5  | I2C | OLED **SCL** | 0.91" 128×32 SSD1306 (I²C addr `0x3C`) |
| GPIO16 | OUT | OLED **RESET** | Heltec WiFi Kit 8 wires the SSD1306 RES to GPIO16; must be pulsed LOW→HIGH before init or the panel stays blank |

> GPIO1/GPIO3 double as the UART0 TX/RX pins. The stock firmware repurposes them
> as button inputs, which is why serial console use and the buttons can interfere.

## `setup()` GPIO initialization

Recovered verbatim from the disassembly at `0x40204504`:

```c
digitalWrite(14, HIGH);   // 74HCT138 A2
digitalWrite(12, HIGH);   // 74HCT138 A0
digitalWrite(13, HIGH);   // 74HCT138 A1
digitalWrite(1,  HIGH);   // DOWN  pull-up
digitalWrite(3,  HIGH);   // UP    pull-up
digitalWrite(2,  HIGH);   // ERASE pull-up
digitalWrite(0,  HIGH);   // SET   pull-up
```

(The three select lines start at `111` = position 7 before the saved selection is
applied. The four button pins are driven HIGH to enable pull-ups; buttons pull the
line LOW when pressed.)

## Signal chain

```
ESP8266 GPIO12/13/14  ─►  74HCT138  ─►  relay drivers (R11–R15 + transistors)
   (3-bit select)         (1-of-8)      ─►  relays RL1–RL5 (+ flyback D1–D5)
                                        ─►  one SMA antenna (A1–A5) → common "Radio" SMA
```

The 74HCT138 asserts exactly one active-low output for the selected 3-bit code, so
only one relay — hence one antenna — can ever be connected to the radio port at a
time. See [`74hct138-truth-table.md`](74hct138-truth-table.md) for the codes.

## Power

- 12 VDC input (0.5 A) powers the relay coils directly.
- An on-board regulator (per schematic) supplies 3.3 V/5 V to the ESP8266 and logic.
