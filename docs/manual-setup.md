# Antenna Switch — Manual Setup Guide

This document reproduces and organises the vendor's printed manual for the antenna
switch, page by page, describing how to set the device up and what the on-screen
(OLED) interface looks like. Technical details cross-reference the reverse-engineering
notes in [`wifi-ap-config.md`](wifi-ap-config.md) and [`buttons.md`](buttons.md).

---

## WiFi Setup (WiFiManager)

The device uses **WiFiManager** to change its network credentials (SSID and
password). On boot the device will either automatically join a known network or
set up its own Access Point that you use to configure the network credentials.

### How the process works

1. **Boot → Station (STA) mode.**
   When the device boots, it is set up in **Station mode** and tries to connect to a
   previously saved Access Point (a known SSID + password combination).

2. **Switching to Configuration mode requires deleting the old configuration.**
   During startup, for a short time, the following prompt appears on the OLED:

   ```
   pres ERASE for erase
   stored information
   6
   ```

   > The number (e.g. `6`) is a countdown timer showing how long you have to act.

   Deleting the stored credentials is done by pressing the button labelled
   **"ERASE WiFi ssid and pwd"**. On-screen this button/action is shown as:

   ```
   ERASE
   ```

3. **Force the device into Access Point (AP) mode.**
   To cause the device to go to AP mode, hold the **UP + (AutoConnect)** button
   during the bootstrap (power-on). The OLED prompt is:

   ```
   hold the button
   AutoConnect (+)
   ```

4. **AP mode is active — connect to configure.**
   Once in AP mode the OLED shows the Access Point SSID and the portal address:

   ```
   ssid: AutoConnectAP
   http://192.168.4.1
   ```

   Using any Wi-Fi enabled device with a browser, connect to the newly created
   Access Point (default name **`AutoConnectAP`**), then browse to
   **`http://192.168.4.1`** to enter the configuration portal.

5. **Join the AutoConnectAP network from your phone.**
   In your device's Wi-Fi settings, under **AVAILABLE NETWORKS**, select
   **`AutoConnectAP`** (it appears as an open network):

   ```
   Wi-Fi                              ON
   CURRENT NETWORK
     mm                               Connected
   AVAILABLE NETWORKS
     ( AutoConnectAP )   <- select this
     Rammstein
     Rammstein_5GHz
     + Add network
   ```

   After establishing a connection with the **AutoConnectAP**, it moves to the
   **CURRENT NETWORK** section and typically shows *"Internet may not be available"*
   (this is normal — the switch's AP has no internet uplink):

   ```
   Wi-Fi                              ON
   CURRENT NETWORK
     ( AutoConnectAP )
       Internet may not be available
   AVAILABLE NETWORKS
     mm
     Rammstein
     Rammstein_5GHz
     + Add network
   ```

   > "Internet may not be available" is expected while connected to the config AP.
   > Once connected, open a browser to `http://192.168.4.1` to configure WiFi.

6. **Open the config portal.**
   Browse to the default IP address **`http://192.168.4.1`** to configure your SSID
   and password. The **AutoConnectAP / WiFiManager** menu appears:

   ```
   AutoConnectAP
   WiFiManager
     [ Configure WiFi ]            <- tap this
     [ Configure WiFi (No Scan) ]
     [ Info ]
     [ Reset ]
   ```

   | Menu item | Function |
   |-----------|----------|
   | **Configure WiFi** | Scan for networks and enter credentials |
   | **Configure WiFi (No Scan)** | Enter SSID/password manually (no scan) |
   | **Info** | Device information page |
   | **Reset** | Reset the device |

7. **Choose your network.**
   Tap **Configure WiFi**. A list of scanned networks (with signal strength) is shown;
   tap your desired network by name and the **SSID field is filled instantly**
   (in the manual's example, `mm`):

   ```
   192.168.4.1
     mm             🔒 100%
     Rammstein      🔒  82%
     TP-LINK_9660   🔒  14%

     SSID:     [ mm            ]
     Password: [ •••••••••     ]

           [   save   ]
              Scan
   ```

8. **Enter password and save.**
   Type your WiFi password into the password field and press **save**. The device
   stores the credentials, reboots, and connects to your network as a station.

### Summary of buttons used

| Manual label | Action | On-screen prompt |
|--------------|--------|------------------|
| **ERASE WiFi ssid and pwd** | Press during startup to delete saved credentials | `pres ERASE for erase stored information` / `ERASE` |
| **UP + (AutoConnect)** | Hold during boot to force AP / configuration mode | `hold the button AutoConnect (+)` |

### Access Point details

| Property | Value |
|----------|-------|
| **AP SSID** | `AutoConnectAP` |
| **Portal URL / IP** | `http://192.168.4.1` |

> Note: the original manual and stock firmware write "pres" (press), "AutoConect" and
> similar typos; these are reproduced above where they appeared literally on the stock
> device's OLED. **The replica firmware corrects these typos** (e.g. it displays
> "press ERASE for erase stored information").

---

## Web Interface (browser control page)

Once the device has joined your network as a station, it serves a simple web page you
use to control the antenna switch. Browse to the device's IP address on your LAN (in
the manual's example this is **`http://192.168.100.5`** — your address will differ and
is assigned by your router's DHCP; the device can also be reached via its mDNS
hostname, see [`wifi-ap-config.md`](wifi-ap-config.md)).

### Page layout

```
+------------------------------------+
|        Web ANTENNAS switch         |   <- page title
|                                    |
|              [  Up  ]              |   <- button
|                                    |
|          ANTENNA:  GROUND          |   <- current selection
|                                    |
|              [  Dn  ]              |   <- button
|                                    |
|          Anteni.net Ltd.           |   <- footer / vendor
+------------------------------------+
```

### Controls

| Element | Type | Function |
|---------|------|----------|
| **Web ANTENNAS switch** | Heading | Page title |
| **Up** | Button | Switch to the next antenna position (step up) |
| **ANTENNA:** | Label | Shows the currently selected antenna position (e.g. `GROUND`) |
| **Dn** | Button | Switch to the previous antenna position (step down) |
| **Anteni.net Ltd.** | Footer | Manufacturer / vendor |

Press **Up** or **Dn** to switch antennas. The current position is displayed next to
the **ANTENNA:** label; `GROUND` is one of the selectable positions (used to ground /
disconnect the antenna).

> The manufacturer of the switch is **Anteni.net Ltd.**

---

## Connecting & Powering the Unit (manual p.2)

### System topology

The manual shows how the pieces fit together:

```
   Internet (http://www.)
        |
   [ WiFi Router ] ---- WiFi ----> Laptop / Smartphone / Tablet / PC  (AutoConnect)
        |         \
        |          `-- WiFi ----> KiwiSDR board (establish connection with
        |                          SSID & Password from smartphone)
        |
   [ Antenna Switch box ] -------- Configure SSID & Password
```

- Client devices (laptop, phone, tablet, PC) reach the switch over WiFi through the
  router — this is the normal **AutoConnect** / station-mode operation.
- During first-time provisioning you **configure the SSID & Password** from a
  smartphone (see the WiFiManager AP steps above).
- The switch has a **universal application** and can also be paired with a **KiwiSDR**.

### Power supply

| Requirement | Value |
|-------------|-------|
| **Supply voltage** | **12 V – 14 V DC** |
| **Minimum current** | **500 mA** |
| **Positive electrode** | the **white** cable is **positive (+)** |

> The power connector shown is a 12 V DC lead. Observe polarity: **white = positive**.

### Control methods

For independent antenna switching you can use:

- **Manual** control (front-panel buttons on the unit), or
- **WEB-browser** control from a smartphone, tablet, or PC (the *Web ANTENNAS
  switch* page described above).

### KiwiSDR integration

The antenna selector has a universal application. If you plan to use it with a
**KiwiSDR**, follow the antenna-switch-extension project:

- **Antenna switch extension:**
  <https://github.com/OH1KK/KiwiSDR-antenna-switch-extension>
- By **Kari, OH1KK** — supports the OH1KK antenna switch and others, including
  **Kiwi Beagle GPIO**.

---

## Manual Control (manual p.3)

The switch can be operated entirely from the front-panel buttons, without a browser.
The current state is shown on the OLED, along with the device's web address (which
reads `http://(IP unset)` until the unit has joined a network and been assigned an IP).

### Power-on / grounded state

After powering on, **wait a while** until `GROUND` appears on the OLED:

```
+----------------------+
|       GROUND         |
|  http://(IP unset)   |
+----------------------+
```

This means that **there is no selected antenna and all antennas are grounded**.

### Selecting an antenna (UP / DOWN)

With the **UP** and **DOWN** buttons you can alternately switch between the antennas.
The position of the switch is displayed like this:

```
+----------------------+
|       ANT: 3         |
|  http://(IP unset)   |
+----------------------+
```

- **UP** — step to the next antenna position.
- **DOWN** — step to the previous antenna position.
- The cycle includes `GROUND` (all antennas grounded) as one of the positions.

### Limiting the number of antennas (max. NUMBER of ANTENNAS)

If the number of active antennas is **less than** the maximum number the switch
supports, you can use the **"max. NUMBER of ANTENNAS"** button to limit how many
antennas are included in the sequential switching. On-screen this is shown as:

```
+----------------------+
|        MAX 6         |
+----------------------+
```

The purpose of this button is to limit the number of antennas. For example, if your
switch is a **5-position** unit but you only use **4 antennas**, then when you limit
it to **4 antennas** the sequential switching after position 4 will go to `GROUND`.

**How to set it:**

1. Configuration is done with a **sequence of short presses** of the button until you
   reach the desired value. The chosen value is **remembered** (stored in the device).
2. It is a good idea to **turn off** the power after this setting and **turn it on**
   again to check the effect.

### Summary of manual-control buttons

| Manual label | Action | On-screen prompt |
|--------------|--------|------------------|
| **UP** | Step to the next antenna position | `ANT: n` (or `GROUND`) |
| **DOWN** | Step to the previous antenna position | `ANT: n` (or `GROUND`) |
| **max. NUMBER of ANTENNAS** | Short-press repeatedly to limit the number of antennas in the switching sequence | `MAX n` |

> After changing the maximum-antenna limit, power-cycle the unit (off then on) to
> confirm the new setting has taken effect.

---

## Rear Panel & Connections (manual p.4)

### Connector layout

The rear panel carries the power input, the radio port, the antenna ports, and the
two configuration buttons:

```
+-----------------------------------------------------------------+
|                                                                 |
|  [12V DC]   ( R )    ( 1 ) ( 2 ) ( 3 ) ( 4 ) ( 5 )   (o)  (o)   |
|                                                                 |
|   POWER    to Radio  \________ ANTENNAS ________/    |    |     |
|  12v DC 0.5A                                          |    |     |
|                                        max.number ---'    |     |
|                                        of ANTENNAS        |     |
|                                        erase WiFi --------'     |
|                                        ssid and pwd            |
+-----------------------------------------------------------------+
```

| Connector / control | Label | Function |
|---------------------|-------|----------|
| **Power jack** | `12V DC` | Power input — **12 V DC, 0.5 A** (see polarity note below) |
| **R** | `to Radio` | Common / radio port — connects to your receiver/transceiver |
| **1 – 5** | `ANTENNAS` | Antenna ports (the numbered outputs selected by UP/DOWN) |
| **max. NUMBER of ANTENNAS** button | `MAX NUM` | Short-press to limit the number of antennas in the switching sequence (see p.3) |
| **erase WiFi ssid and pwd** button | `ERASE` | Press during startup to delete the stored WiFi credentials (see WiFi Setup) |

> **Power:** 12 V DC, 0.5 A (500 mA). Observe polarity — the **white** cable is
> **positive (+)** (see p.2).
>
> The photographed unit shows **5 antenna ports (1–5)** plus the radio port **R**;
> your model may have a different number of ports.

### Managing over WiFi

The control device can make a connection to your WiFi wireless network and be managed
by a **browser, PC, tablet, or smartphone**.

**How to connect to the Local network:**

WiFiManager is used, with **AutoConnect**, **Custom Parameter**, and management of your
**SSID** and **Password**.

### What WiFiManager does

**WiFiManager** allows you to connect your remote-control unit to different Access
Points (APs). Additionally, you can:

- Add **custom parameters** (variables), and
- Manage **multiple SSID connections** with WiFiManager.

For the step-by-step WiFiManager procedure (forcing AP mode, joining `AutoConnectAP`,
and using the `http://192.168.4.1` config portal), see the
[WiFi Setup (WiFiManager)](#wifi-setup-wifimanager) section above.

---

## Schematic (manual — final page)

The last page of the manual is the manufacturer's full circuit diagram, titled
**"ANTENNAS webSWITCH control 1 to 5"** (drawing size A4, dated on the sheet). It shows
the complete electronics: the ESP8266 module, the **74HCT138** decoder, the five relays
(RL1–RL5) with their series resistors (R11–R15) and flyback diodes (D1–D5), the five
antenna SMA jacks (A1–A5) plus the **Radio** SMA output, the **AMS1117** regulator
block, and the **12 V / 0.5 A** power input.

The manual introduces this page with the note:

> *"The set is absolutely ready for use. Quality checked, tested and functioning.*
> *Necessary: 12 VDC power supply."*

A fully redrawn and annotated version of this schematic — cross-referenced with the
GPIO assignments recovered from the firmware, a decoder truth table, the RF relay
cascade, and a complete bill of materials — is maintained in
[`schematic.md`](schematic.md). That document also includes a section directly
comparing the redrawing against this manufacturer scan.
