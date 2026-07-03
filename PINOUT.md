# PINOUT — moved

The pinout documentation has been reorganised into the [`docs/`](docs/) directory.

- **GPIO map / hardware:** [`docs/hardware-pinout.md`](docs/hardware-pinout.md)
- **74HCT138 truth table:** [`docs/74hct138-truth-table.md`](docs/74hct138-truth-table.md)
- **Buttons:** [`docs/buttons.md`](docs/buttons.md)
- **Documentation index:** [`docs/README.md`](docs/README.md)

## Quick reference

| GPIO | Function |
|------|----------|
| GPIO12 | 74HCT138 A0 (LSB) |
| GPIO13 | 74HCT138 A1 |
| GPIO14 | 74HCT138 A2 (MSB) |
| GPIO0  | SET button (max antenna count) |
| GPIO1  | DOWN button |
| GPIO2  | ERASE button (hold) |
| GPIO3  | UP button |
| GPIO4 / GPIO5 | I2C OLED (SDA / SCL) |

Antenna index (0 = GROUND, 1–5 = ANT1–5) is written as binary across A2/A1/A0.
