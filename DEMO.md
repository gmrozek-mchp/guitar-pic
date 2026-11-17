# Booth demo guide

Talking points for staff running the Guitar Hero Bot at a trade-show booth. Use the pitch that
fits the visitor's time and depth, then point at the hardware as you talk. Setup is in
[SETUP.md](SETUP.md).

<!-- video: media/demo-reel.mp4 (30 s loop for the booth screen) -->

---

## The hook (10 seconds)

> "That's a robot playing Guitar Hero. Nothing's pre-recorded. It watches the TV, sees the
> notes, and plays them on a real Wii, all on Microchip chips with no PC. And every board
> you see here is on one two-wire Ethernet cable. Want to try and beat it?"

## 30-second pitch

> "The board behind the screen is a SAM9X75 microprocessor. It captures the Wii's HDMI
> video, runs computer vision to find every note, times each strum, and sends the button
> presses out to the Wii. It even drives the game's menus by reading the screen.
>
> And it isn't one board, it's a network. Seven nodes on one twisted pair: 10BASE-T1S
> automotive Ethernet. A dsPIC listens to the music and broadcasts the beat. The puppet
> head-bangs to it and the lights flash to it. A PIC32CM at the TV runs a tiny neural network
> and can play the song itself, talking peer-to-peer on the same wire. No switch, no hub, no
> IP stack."

## 2-minute walkthrough

Point at each piece as you go:

1. **The screen (marvin's touch UI).** "This is the robot's dashboard: live video, the
   song, scores. It's a 10.1-inch LVDS touch panel driven straight off the SAM9X75."
2. **The video path.** "The Wii's video comes in over HDMI and goes through a MIPI CSI-2
   bridge into the SAM9X75's image sensor controller, 60 frames a second, into DDR. The CV
   runs on the ARM9 core under FreeRTOS. No Linux, no OpenCV."
3. **Playing.** "It sees notes a few hundred milliseconds before they reach the strike
   line, so it schedules each press to land exactly on the line. The presses go out over
   the bus, and the guitar node's LEDs light up with every note."
4. **Menus.** "Press START and it navigates GH3 by itself. It classifies about 20 different
   screens and verifies each menu step before taking the next one."
5. **The bus.** "Seven nodes, one twisted pair, three Microchip CPU families, the same
   LAN8651 on each. Here's live traffic." Open the **Bus** screen. See
   [The T1S story](#the-t1s-story) for the full version.
6. **The show.** "The dsPIC33AK is doing FFT beat detection on the audio. It broadcasts a
   beat frame about 23 times a second, and the puppet and the light show both use it."
7. **The neural net.** "That board at the TV has five light sensors over the note lanes.
   It runs a 3,000-parameter int8 network on a 24 MHz Cortex-M0+, trained from the
   SAM9X75's own play. Switch the detector to NEURAL NETWORK and the little chip plays the
   song itself."

## The T1S story

The 10BASE-T1S network grew from one replacement cable into the backbone of the whole
build. It's the best story here for engineers. Lead with the **Bus** screen.

**What to show**
- **Bus** screen during a song: per-node TX/RX rates, heartbeats, CRC/symbol error counts,
  and measured bus utilization. ⚠ TBD: note the typical in-song utilization to quote.
- **System** screen: one card per node, showing the board, part numbers and QR links.
- ⚠ TBD (rehearse before the show): unplug a node's T1S cable and watch it drop off the
  **Bus** screen, then plug it back in and watch it rejoin.

**Talking points**
- **One pair, multidrop, deterministic.** PLCA (802.3cg) gives each node a transmit slot in
  turn, so there are no collisions, no switch and no hub. marvin is the coordinator; an idle
  bus cycle is about 29 µs.
- **Three Microchip CPU families, one PHY, one driver.** The ARM926 SAM9X75 MPU, the
  Cortex-M0+ PIC32CM PL10 MCUs and the dsPIC33AK DSC all use the same LAN8651 MAC-PHY over
  SPI, with the same OPEN Alliance TC6 driver. The LAN8651 does the MAC, PHY and CRC in
  hardware.
- **It fits a tiny MCU.** On a 64 KB flash / 8 KB RAM PIC32CM, the whole T1S stack is about
  6–10 KB of flash and 1–2 KB of RAM, statically allocated.
- **Raw Ethernet, no IP.** Static addressing (MAC = node id) and one ethertype per purpose:
  commands, heartbeats, beat broadcast, per-node control.
- **Three traffic patterns on one wire:**
  - *Coordinator → node:* marvin sends button masks and per-node control.
  - *Peer-to-peer:* the fretboard's neural net commands the guitar node directly, without
    marvin. The estimated worst case is about 5 ms end to end, and the bus adds well under
    1 ms of that.
  - *Broadcast:* the dsPIC beatbox sends one beat frame about 23 times a second, and lemmy
    and lightshow both use it.
- **Self-reporting network.** Every node sends a 20-byte heartbeat twice a second, carrying
  its TX/RX counts and CRC and symbol errors. marvin listens to the whole segment and
  measures utilization exactly. Each node also enforces its own output gate and reports it
  in its heartbeat, so marvin can correct any disagreement within a second.
- **Bandwidth was never the point.** The whole show uses a small slice of 10 Mbit/s. The
  win is wiring: one pair to every node instead of a star of point-to-point UARTs.
- **Next: PoDL**, power over the same pair. The sink board is in KiCad: 5 V buck plus
  3.3 V LDOs. Then each node needs only one two-wire cable for both power and data.
- **Where this goes:** automotive zonal architectures, industrial sensor and actuator
  networks, building automation. Anywhere CAN or LIN wiring is turning into Ethernet.

Deep reference: [`docs/t1s-podl-link.md`](docs/t1s-podl-link.md).

## Let them play: SHOWDOWN

Robot vs. human. This is the best crowd draw.

1. In **Song select**, enter the visitor's name on the on-screen keyboard and pick client or
   employee. Their score goes on the **TOP SCORES** board.
2. Hand them the real Wii guitar (player 2).
3. Tap **SHOWDOWN**. marvin navigates to a 2-player PRO FACE-OFF on the configured song and
   difficulty (`showdown.cfg` on the SD card; edits take effect on the next tap).
4. ROBOT and HUMAN score cards update live. Results are saved to the SD card.

⚠ TBD: a tip on which difficulty makes a fair match.

## Deep-dive points by part

For engineers who want to know why each part is there.

| Part | Where | Why it's interesting |
|---|---|---|
| **SAM9X75** (ARM926 MPU) | marvin | MIPI CSI-2 camera input, LVDS display, and DDR on one MPU. It runs real-time CV + UI on bare-metal Harmony + FreeRTOS, statically allocated. |
| **LAN8651** 10BASE-T1S MAC-PHY | every node | Ethernet on any MCU with SPI, even one with no MAC. See [The T1S story](#the-t1s-story). |
| **PIC32CM PL10** (Cortex-M0+, 24 MHz, 5 V) | fretboard, guitar, lemmy, lightshow | One small low-cost MCU, four very different jobs. fretboard runs on-device ML. lightshow drives WS2812 straight from the 5 V I/O domain, with no level shifter. |
| **dsPIC33AK512MPS512** (200 MHz DSC) | beatbox | 48 kHz stereo ADC into a 512-point FFT beat detector, with headroom to spare. |
| **Legato / MPLAB Harmony** | marvin UI | The whole dashboard is Legato, composited onto the SAM9X75's hardware layers, with video on its own overlay. |

## FAQ

- **Is it cheating, i.e. reading the game data?** No. It only sees the same video you do.
- **How does it press the buttons?** Not mechanically. The button presses travel over the
  T1S bus to a bridge node that delivers them to the Wii as a wireless controller. The guitar
  node's LEDs show what it's pressing. If asked, the bridge is an off-the-shelf
  Bluetooth module, because the Wii only speaks Bluetooth Classic. Answer honestly, then steer
  back to the SAM9X75 and the bus, where the interesting work is.
- **How good is it?** It has been tested on Easy through Expert. ⚠ TBD: typical scores / accuracy to quote.
- **Why not a PC?** Version 1 was a PC with a USB capture card and OpenCV. The point is doing it embedded, in real
  time, on parts you can buy.
- **Can I get the code?** ⚠ TBD: what to say about availability.
- **What is 10BASE-T1S for, really?** Automotive and industrial: many sensors and actuators on
  one pair, replacing CAN/LIN-style harnesses with Ethernet.
- **How far can the bus run, and how many nodes?** The 802.3cg multidrop spec targets at least
  25 m and at least 8 nodes. This build uses 8 PLCA slots (7 used).
- **Why not CAN?** CAN would carry these messages. T1S makes every node a plain Ethernet
  endpoint: standard frames, MAC addresses and tooling, at 10 Mbit/s, on the same pair.

## Don't promise / known quirks

- **GH3 drops the robot's controller when the game boots from the Wii home screen.** Fix:
  open Wiimotes → **RECONNECT** once GH3 has finished booting. It only happens at game
  boot, so do it during setup rather than in front of visitors.
- **The neural-net detector is hard, 1-player only.** Its peer-to-peer play is built but
  not yet confirmed on hardware, so rehearse it before showing it live.
- **lemmy and lightshow only perform during a song**, and only if they're enabled under
  ACTUATORS.
- There is no pause. If you need to stop, use **STOP** on the dashboard.

<!-- photo: media/demo-booth.jpg -->
