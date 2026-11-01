# relay8_udp — UDP control for the ESP12F 8-Channel Relay Board

Arduino sketches that turn the **ESP12F_Relay_X8_V1.1** board (8 relays with an on-board ESP-12F) into a simple network relay controller. Send it a plain-text UDP packet like `1 ON` and it switches the relay and replies with the state of all eight.

The repo has two versions:

| Sketch | What it is |
|---|---|
| [`relay8_udp_v2`](#part-1--generic-udp-relay-controller-relay8_udp_v2) | **Generic UDP controller.** Individual relays, all-on/off, bitmasks, timed pulses and staggered switching. Use this for any project. |
| [`relay8_udp_v3`](#part-2--halloween-2026-edition-relay8_udp_v3) | **Halloween 2026 edition.** Everything in v2, plus a timed `MARVIN` show sequence that works with a button box and a video player. |

---

## Hardware

**Board:** ESP12F_Relay_X8_V1.1 (silkscreen `303ESP12F06`). It has eight Songle SRD-05VDC-SL-C relays with transistor drivers and a 7–28 V or 5 V power input.

![board](docs/8ch_relay_board_1.jpg) <!-- optional: add your photo -->

### Relay pin map

The relay numbers here match the numbers printed on the board.

| Relay | GPIO |
|:---:|:---:|
| 1 | 16 |
| 2 | 14 |
| 3 | 12 |
| 4 | 13 |
| 5 | 15 |
| 6 | 0 |
| 7 | 4 |
| 8 | 5 |

The relays are **active-HIGH**: a HIGH output turns the relay on, and every relay is off at boot.

> **Boot-pin notes:** relay 6 is on GPIO0 and relay 5 is on GPIO15. Both are ESP8266 boot-strap pins. They work as normal outputs once the sketch is running, but avoid using those relays for loads that can't tolerate a brief glitch at power-up.

### Programming header (P2)

`ADC · IO2 · GND · GND · IO0 · RXD · TXD · 5V`

To flash over serial, connect a 3.3 V USB-serial adapter to **TXD, RXD and GND**. Hold **IO0 to GND** while you press **RST** to enter flash mode. After the first flash you can upload over WiFi (see [OTA](#ota-updates)).

---

## Setup

1. Install the **ESP8266 board package** in the Arduino IDE (Boards Manager → *esp8266 by ESP8266 Community*).
2. Board: **Generic ESP8266 Module**, Flash Size **4MB**.
3. Edit the settings at the top of the sketch:
   ```cpp
   const char* WIFI_SSID = "your-ssid";
   const char* WIFI_PASS = "your-password";
   const char* HOSTNAME  = "relay8";
   const uint16_t UDP_PORT = 8888;
   ```
4. Upload. The serial monitor (115200) shows the board's IP address.

> **Tip:** set up a DHCP reservation on your router for the board's MAC address so its IP never changes. Other ESP devices that send it commands need a fixed IP because they can't resolve `.local` names.

> ⚠️ Don't commit your real WiFi password. Replace it with a placeholder before you push.

### OTA updates

Both sketches include ArduinoOTA. After the first serial flash, the board appears in the IDE as the network port **`relay8`**. There is no OTA password, but Arduino IDE 2 asks for one anyway: type anything and click **Upload**.

---

## Part 1 — Generic UDP relay controller (`relay8_udp_v2`)

### Commands

Send these as UDP packets to port **8888**. They're plain text and not case-sensitive.

| Command | Action |
|---|---|
| `1 ON` | Relay 1 on (relays `1`–`8`; `R1 ON` also works) |
| `1 OFF` | Relay 1 off |
| `1 TOGGLE` | Flip relay 1 |
| `1 PULSE 500` | Relay 1 on for 500 ms, then off (default 500) |
| `ALL ON` / `ALL OFF` / `ALL TOGGLE` | All relays, **staggered** |
| `MASK 0xA5` | Set all relays from a bitmask (bit 0 = relay 1), **staggered**. Decimal, hex and octal all work. |
| `DELAY 150` | Set the stagger delay in ms (`0` = all at once) |
| `STATUS` | Report only |

### Reply

Every command gets a reply sent back to the sender:

```
R1=1 R2=0 R3=0 R4=0 R5=0 R6=0 R7=0 R8=0 MASK=0x01 DELAY=100
```

The reply ends with `PENDING=n` while a staggered sequence is still running.

### Staggered switching

`ALL` and `MASK` switch **one relay at a time**, `STAGGER_MS` apart (default **100 ms**). This spreads out the inrush current when the relays drive real loads.

- Relays that are already in the wanted state are skipped, so there's no wasted delay.
- The board keeps answering commands during a sequence, so nothing blocks.
- A single-relay command overrides any queued change for that relay.
- `DELAY` changes the stagger at runtime until the next reboot. Change `STAGGER_MS` in the code to set the default.

### Testing from Linux

```bash
echo -n "1 ON"    | nc -u -w1 relay8.local 8888
echo -n "ALL OFF" | nc -u -w1 relay8.local 8888
echo -n "STATUS"  | nc -u -w1 relay8.local 8888
```

---

## Part 2 — Halloween 2026 edition (`relay8_udp_v3`)

v3 includes **everything in Part 1** and adds a timed show sequence for the 2026 Halloween *Magrathea space station* display. A visitor presses a button, there's a short pause, and then a lighting change and the *Marvin* video start together.

### System overview

```
 ┌───────────────────────┐   "MARVIN"    ┌──────────────────────┐   "PLAY"   ┌───────────────────┐
 │  Button box           │ ────────────▶ │  relay8 (this board) │ ─────────▶ │  BMAX (Ubuntu NUC) │
 │  Wemos D1 Mini        │   UDP :8888   │  relay8_udp_v3       │  UDP :4210 │  plays marvin.mp4  │
 │  marvin_button.ino    │ ── ALL ON/OFF▶│                      │            │                    │
 │                       │ ── "STOP" ────┼──────────────────────┼──────────▶ │  closes the video  │
 └───────────────────────┘               └──────────────────────┘            └───────────────────┘
```

All show timing lives on the relay board. The button box only sends `MARVIN`, and the relay board starts the relay and the video at the same moment.

### The Marvin sequence

| Time | Relay 1 | Relay 2 | BMAX |
|---|:---:|:---:|---|
| `MARVIN` received | – | – | – |
| + `MARVIN_DELAY_MS` (default **3 s**) | **ON** | **OFF** | receives `PLAY` → video starts |
| + `MARVIN_MS` (default **10 s**) | **OFF** | **ON** | – |

### Added commands

| Command | Action |
|---|---|
| `MARVIN` | Start the show after `MARVIN_DELAY_MS`. Repeats during the wait are ignored. Sending it during a show restarts the show (after the wait). |
| `MARVIN STOP` | End the show now (relay 1 off, relay 2 on). Also cancels a show that's waiting to start. |
| `MARVINDELAY 3000` | Set the wait before the show starts, in ms (0–60000) |
| `MARVINTIME 10000` | Set how long relay 1 stays on, in ms (max 1 hour) |

`MARVINDELAY` and `MARVINTIME` last until the next reboot. Change `MARVIN_DELAY_MS` and `MARVIN_MS` in the code to set new defaults.

**Manual override:** a single-relay command on relay 1 or 2, or any `ALL` or `MASK` command, cancels a show that's waiting or running. The timer never fights a manual change.

### Added reply fields

```
R1=1 R2=0 ... MASK=0x01 DELAY=100 MARVIN_MS=10000 MARVIN_DELAY=3000 MARVIN_START_IN=1840
```

- `MARVIN_START_IN=n`: ms until the show starts, shown during the wait
- `MARVIN_LEFT=n`: ms until the show ends, shown while it's running

### Added settings

```cpp
const IPAddress BMAX_IP(192, 168, 1, 102);   // video player
const uint16_t  BMAX_PORT    = 4210;
const uint8_t   PLAY_REPEATS = 3;            // PLAY sent 3x; BMAX ignores duplicates
unsigned long   MARVIN_MS       = 10000;     // relay 1 on time
unsigned long   MARVIN_DELAY_MS = 3000;      // wait before relay 1 + video
```

### Button box (`marvin_button.ino`, Wemos D1 Mini)

Wire each button between its pin and GND. The sketch uses the internal pull-ups, so no resistors are needed.

| Pin | Button | Sends |
|:---:|---|---|
| D5 | Marvin | `MARVIN` → relay board |
| D1 | All On | `ALL ON` → relay board |
| D2 | All Off | `ALL OFF` → relay board |
| D7 | Stop | `STOP` → BMAX (closes the video) |
| D6 | – | Heartbeat LED (with a 220–330 Ω resistor) |

Set `RELAY_IP` in `marvin_button.ino` to the relay board's reserved IP address.

### Testing the show from Linux

```bash
echo -n "MARVINDELAY 3000" | nc -u -w1 relay8.local 8888
echo -n "MARVIN"           | nc -u -w1 relay8.local 8888   # reply shows MARVIN_START_IN
echo -n "STATUS"           | nc -u -w1 relay8.local 8888   # watch MARVIN_LEFT count down
```

---

## License

MIT. Use it, change it, scare your neighbors with it.
