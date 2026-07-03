# Physical Buttons

The board has **four momentary push-buttons**, all wired active-low with the
ESP8266 internal pull-ups enabled (each pin driven HIGH in `setup()` — see
[`hardware-pinout.md`](hardware-pinout.md)). Pressing a button pulls its GPIO LOW.

| Button | GPIO | Role | Read at (disasm) | Behaviour |
|--------|:----:|------|------------------|-----------|
| **UP**    | GPIO3 | Next antenna | `0x40204c59`, `0x40204961` | `position++` (wraps to GROUND past max) |
| **DOWN**  | GPIO1 | Previous antenna | `0x40204c47`, `0x4020494b` | `position--` (wraps to max below GROUND) |
| **SET**   | GPIO0 | Set max antenna count | `0x402048d5` | Displays `MAX`; increments/stores the max selectable antenna limit (default 7, DRAM `0x3ffe84e0`) |
| **ERASE** | GPIO2 | Erase WiFi config (hold) | `0x402045f9` | Prompts `pres ERASE for erase stored information`, `hold the button`; on long-press erases stored WiFi credentials and reboots |

## Evidence

### UP / DOWN (GPIO3 / GPIO1)

The disassembly at `0x40204c44` shows the classic increment/decrement pattern
(active-low: the branch is taken — i.e. no change — when the pin reads HIGH):

```
digitalRead(1)  -> if LOW (pressed): position = position - 1     ; DOWN
digitalRead(3)  -> if LOW (pressed): position = position + 1     ; UP
if (position == -1) position = <stored max>                      ; wrap-around
```

The same pair appears again at `0x40204948`/`0x4020495e`.

### SET / max antennas (GPIO0)

At `0x402048d5`, `digitalRead(0)` gates a counter that wraps at 8 (`bgei a2,8`).
Its handler calls the display routine at `0x40204750`, which draws the string
**`MAX`** (`0x3ffe8735`) — i.e. this button configures the maximum number of
antennas the UP/DOWN cycle will reach.

### ERASE (GPIO2)

At `0x402045f9`, `digitalRead(2)` enters a branch that displays
**`pres ERASE for erase stored information`** (`0x3ffe86b0`),
**`ERASE`** (`0x3ffe86d9`) and **`hold the button`** (`0x3ffe86df`), with delay
loops of ~2500 ms / 3000 ms (`0x9c4`, `0xbb8`) — a hold-to-confirm gesture that
erases the saved WiFi configuration (equivalent to the web `/erase` action).

## Reading the buttons in new firmware

```c
pinMode(0, INPUT_PULLUP);   // SET
pinMode(1, INPUT_PULLUP);   // DOWN
pinMode(2, INPUT_PULLUP);   // ERASE
pinMode(3, INPUT_PULLUP);   // UP

bool up    = (digitalRead(3) == LOW);
bool down  = (digitalRead(1) == LOW);
bool set   = (digitalRead(0) == LOW);
bool erase = (digitalRead(2) == LOW);
```

> Because GPIO1/GPIO3 are the UART0 TX/RX pins, use them as inputs only (or disable
> the serial console) to avoid contention.
