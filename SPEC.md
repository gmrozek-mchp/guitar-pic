# Guitar Hero Bot — System Specification

> Cross-subproject overview. Each subproject owns its own spec (§3); this document does not
> duplicate them. When this and a subproject spec disagree, the subproject spec wins and this
> file should be fixed.

## 1. Mission

Play Guitar Hero III on a real Nintendo Wii, end to end with no PC in the runtime path. The
system watches the game's video, detects notes, presses the right buttons on time, and
navigates the game's menus itself. It does this on Microchip silicon: a SAM9X75 host plus a
10BASE-T1S bus of PIC32CM and dsPIC nodes. The same platform drives a booth show: a
head-banging puppet and a light show running in time to the music, and a robot-vs-human
SHOWDOWN mode.

## 2. Architecture

```
 Wii (GH3) ──component──► ElectronWarp ──HDMI──► TC358743 ──CSI-2──► marvin (SAM9X75)
     ▲                                                     capture → CV detect → timing
     │ Bluetooth (emulated Wiimote + guitar)               game observer / menu controller
     │                                                     10.1″ LVDS touch UI · microSD
     │                                                              │ LAN8651
     │        10BASE-T1S bus (PLCA; marvin = coordinator, id 0) ────┴──────────────────────┐
     │             │               │                │               │            │         │
     └──── fauxmote (1)      guitar (3)      fretboard (4)    beatbox (5)   lemmy (6)  lightshow (7)
           ESP32             PIC32CM PL10    PIC32CM PL10     dsPIC33AK     PIC32CM    PIC32CM
           plays the Wii     status LEDs     photo-ADC + NN   audio → beats  2 servos   WS2812
```

- **marvin** captures the Wii's video and runs the reference CV note detector and the timing
  pipeline. It reads the GH3 screens to navigate menus, owns the operator UI, and coordinates
  the bus. It sends each button mask to fauxmote (which plays it) and to the guitar node
  (which mirrors it on LEDs).
- **fretboard** is an alternate detector. Five phototransistors sit over the strike line of its own
  small HDMI display, and an int8 neural net runs on-MCU. When it is selected (`active fretboard`, hard 1-player)
  it drives fauxmote and the guitar node directly, peer-to-peer, and marvin stays silent.
- **beatbox** listens to line-in audio and runs FFT beat detection. It broadcasts a beat frame
  (ethertype `0x88B8`, ~23 Hz) that **lemmy** and **lightshow** both consume. marvin gates
  both of them to the song window and the dashboard ACTUATORS toggles.

### Node table

The definitive table, with ethertypes and frame formats, is
[`docs/t1s-podl-link.md`](docs/t1s-podl-link.md) §7.1. MACs are `02:00:00:00:00:<id>`.

| Node | T1S id | `node_type` | Role | Status |
|---|---|---|---|---|
| marvin | 0 | — | PLCA coordinator, CV, timing, UI | working |
| fauxmote | 1 | 3 | Wiimote + guitar emulator; the Wii's input | working |
| guitar | 3 | 2 | mirrors the button mask on status LEDs (ATE_2026 board) | working |
| fretboard | 4 | 1 | phototransistor + NN detector | working |
| beatbox | 5 | 6 | audio beat source, broadcasts beat frames | working |
| lemmy | 6 | 4 | puppet: neck nod + jaw servos | working; post-L2 behaviour unverified on hardware |
| lightshow | 7 | 5 | 2 × 33 px WS2812 strands | working; power/timing validation open |

Id 2 is unused. The bus is separately powered today. PoDL is a design direction only
([`hardware/t1s-sink/`](hardware/t1s-sink/) is in progress).

## 3. Subprojects

| Subproject | Spec | Journal |
|---|---|---|
| marvin | [`firmware/marvin/docs/spec.md`](firmware/marvin/docs/spec.md) (deep dives: [capture](firmware/marvin/docs/capture_pipeline.md), [display](firmware/marvin/docs/display_path.md), [UI compositor](firmware/marvin/docs/ui_compositor.md), [GH3 navigation](firmware/marvin/docs/gh3_navigation.md)) | [journal](firmware/marvin/docs/journal.md) |
| fauxmote | [`firmware/fauxmote/SPEC.md`](firmware/fauxmote/SPEC.md); link protocol [`docs/marvin-fauxmote-link.md`](docs/marvin-fauxmote-link.md) | [journal](firmware/fauxmote/docs/journal.md) |
| guitar | [`firmware/guitar/SPEC.md`](firmware/guitar/SPEC.md) | [journal](firmware/guitar/docs/journal.md) |
| fretboard | [`firmware/fretboard/SPEC.md`](firmware/fretboard/SPEC.md) | [journal](firmware/fretboard/docs/journal.md) |
| beatbox | [`firmware/beatbox/SPEC.md`](firmware/beatbox/SPEC.md) | [journal](firmware/beatbox/docs/journal.md) |
| lemmy | [`firmware/lemmy/SPEC.md`](firmware/lemmy/SPEC.md) | [journal](firmware/lemmy/docs/journal.md) |
| lightshow | [`firmware/lightshow/SPEC.md`](firmware/lightshow/SPEC.md) | [journal](firmware/lightshow/docs/journal.md) |
| edge-ai | [`tools/edge-ai/docs/SPEC.md`](tools/edge-ai/docs/SPEC.md): trains the fretboard's model from marvin's commands | [journal](tools/edge-ai/docs/journal.md) |
| gameplay | [`tools/gameplay/README.md`](tools/gameplay/README.md): offline prototype of marvin's screen readers + navigator (largely ported to firmware) | [journal](tools/gameplay/docs/journal.md) |
| marvin-perf | [`tools/marvin-perf/README.md`](tools/marvin-perf/README.md): perf-log decoder + live browser viewer | — |
| gh3-cover-art, node-photos, qr-verify | asset pipelines for marvin's SD card / System screen (each has a README) | — |
| fret-tuner | [`tools/fret-tuner/SPEC.md`](tools/fret-tuner/SPEC.md): **dormant** (speaks the retired UART link) | — |
| hardware | [`hardware/README.md`](hardware/README.md): Sensor-LCD5 sensor board, T1S PoDL sink (WIP), enclosures, unbuilt actuator studies | — |

## 4. Hardware

| Component | Role |
|---|---|
| SAM9X75 Curiosity board (EV31H43A) | marvin host; on-board FT4232H gives JTAG + DBGU console over USB |
| Waveshare HDMI→CSI-2 adapter (TC358743) | HDMI into marvin's ISC |
| ElectronWarp | Wii component video → HDMI |
| 10.1″ 1280×800 LVDS panel + maXTouch | operator UI |
| Two-Wire ETH 3 Click / LAN8651 | 10BASE-T1S MAC-PHY: one per node, over SPI + the OPEN Alliance TC6 driver |
| Adafruit ESP32 Feather V2 | fauxmote (original ESP32: has Bluetooth Classic) |
| PIC32CM6408PL10048 boards | guitar (ATE_2026), fretboard (+ [Sensor-LCD5](hardware/Sensor-LCD5/) sensor board), lemmy, lightshow |
| dsPIC33AK512MPS512 GP DIM (EV80L65A) on EV74H48A | beatbox |

No physical button-pressing actuator has been built. The three candidate designs in
[`hardware/actuators/`](hardware/actuators/) are unbuilt studies.

## 5. Cross-cutting decisions

- **No PC at runtime.** CV runs natively on the SAM9X75. The Python tools are bench-only.
- **Wireless bridge instead of mechanical actuators.** Button presses reach the Wii through
  the fauxmote bridge node rather than a button-pressing mechanism. The guitar node mirrors
  them on LEDs.
- **10BASE-T1S with a minimal L2 header, no IP stack.** Frames use per-purpose ethertypes
  (`0x88B5` command/data, `0x88B6` heartbeat, `0x88B7` fauxmote, `0x88B8` beat broadcast,
  `0x88B9` per-node control). Every follower sends a 20-byte v2 heartbeat that feeds marvin's
  Bus screen. UART fallbacks remain as build options. Detail:
  [`docs/t1s-podl-link.md`](docs/t1s-podl-link.md).
- **Output gating lives on the nodes.** marvin sets each node's output enable over `0x88B9`
  and reconciles it against the heartbeat. Gating still holds when the fretboard drives
  peer-to-peer and marvin is silent.
- **`frame_epoch` is the sync token.** A per-captured-frame counter that detector records,
  commands, and perf-log data key off ([marvin spec §4.6](firmware/marvin/docs/spec.md)).
- **Static allocation only** in all firmware. No malloc.
- **Archive.** The original PC / Elgato / PS2 design is retired. Its docs are kept unmaintained
  in [`docs/archive/`](docs/archive/).

## 6. Status

| | Item |
|---|---|
| ✅ | HDMI capture 480p60 / 720p60 → DDR → LVDS panel |
| ✅ | CV detector + timing pipeline play songs (1P and 2P-left highway geometry) |
| ✅ | Game observer (20 GH3 screen classes, score readers) + menu controller: 1P practice, 2P PRO FACE-OFF, SHOWDOWN |
| ✅ | Operator UI: dashboard, song select with album art, Wiimotes manual override, Bus stats, System info, Activity log |
| ✅ | T1S bus: all seven nodes on the bus with v2 heartbeats; per-node output gating |
| ✅ | fauxmote plays GH3 on a real Wii, driven by marvin over T1S |
| 🚧 | fretboard NN detector (hard difficulty only) driving fauxmote peer-to-peer: firmware done, on-hardware confirmation pending |
| ✅ | beatbox → lemmy nod / jaw and lightshow effects |
| 🚧 | SD reference-data recorder; calibration UI; replay; watchdog |
| 🚧 | Active-guitar selection / source arbitration on the bus |
| 🚧 | PoDL (power on the T1S pair) |

Per-milestone detail: [marvin spec §8](firmware/marvin/docs/spec.md) and each node's spec.

## 7. License

MIT. Hobby / educational project. Guitar Hero is a trademark of Activision; Wii is a trademark
of Nintendo.
