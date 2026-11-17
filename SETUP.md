# Booth setup

Checklist for assembling the Guitar Hero Bot demo at a venue. Every board arrives already
built and flashed. This guide covers unpacking, cabling, powering up, and checking that
everything is alive. Reflashing is in the [appendix](#appendix-reflashing).

> Items marked **⚠ TBD** are not yet documented anywhere in the repo. They need filling in,
> with photos from `media/`, before the next show.

---

## 1. Pack list

**Game**
- [ ] Nintendo Wii + power supply, with **Guitar Hero III** in the drive
- [ ] Wii component-video cable
- [ ] ElectronWarp (component → HDMI) + its power
- [ ] TV / monitor for the game (the fretboard sensor board mounts on it)
- [ ] One real Wiimote + GH3 guitar for the human player (SHOWDOWN), with spare AA batteries
- [ ] ⚠ TBD: HDMI splitter / how the TV and marvin both get the picture

**marvin (the brain)**
- [ ] SAM9X75 Curiosity board (EV31H43A) with the LAN8651 Click on FLEXCOM3
- [ ] Waveshare TC358743 HDMI→CSI-2 adapter + ribbon
- [ ] 10.1″ LVDS touch panel + LVDS/touch cables
- [ ] **microSD card** holding `games/` and `system/` (see [`firmware/marvin/data/README.md`](firmware/marvin/data/README.md))
- [ ] HDMI cable (ElectronWarp → TC358743)
- [ ] USB cable to the board's debug port (power + console)

**Bus nodes** (each in its printed enclosure)
- [ ] fauxmote: ESP32 Feather V2 + LAN8651 Click (USB-C)
- [ ] guitar: ATE_2026 board (status LEDs)
- [ ] fretboard: PIC32CM PL10 + Sensor-LCD5 phototransistor board
- [ ] beatbox: dsPIC33AK DIM on the EV74H48A board, plus a stereo line-in cable
- [ ] lemmy: puppet + PIC32CM PL10, plus a separate servo supply
- [ ] lightshow: PIC32CM PL10 + two WS2812 strands, plus a **5 V LED supply (size for ~4 A)**
- [ ] T1S cabling: ⚠ TBD (cable type, connectors, topology, where the bus is terminated)
- [ ] Power: ⚠ TBD (power strip, USB hub or supplies per node)

**Optional**
- [ ] Laptop with [`tools/marvin-perf`](tools/marvin-perf/) for the live performance viewer
  (USB to marvin's perf-log port), and a serial terminal for the consoles

<!-- photo: media/setup-packlist.jpg -->

## 2. Cabling

<!-- photo: media/setup-overview.jpg (whole booth, labelled) -->

1. **Video:** Wii → component cable → ElectronWarp → HDMI → TC358743 adapter → marvin.
   ⚠ TBD: path to the TV.
2. **Panel:** LVDS + touch cables from the panel to marvin.
3. **SD card** into marvin.
4. **Fretboard sensor:** mount the Sensor-LCD5 board on the game TV so its five
   phototransistors sit just above the strike line of the 1-player highway. ⚠ TBD: mounting
   method and exact position (photo).
   <!-- photo: media/setup-fretboard-mount.jpg -->
5. **T1S bus:** connect all seven nodes on the single-pair bus. ⚠ TBD: order and termination.
   <!-- photo: media/setup-bus.jpg -->
6. **Audio:** game audio → beatbox stereo line-in. ⚠ TBD: where the audio is tapped.
   <!-- photo: media/setup-audio.jpg -->
7. **lightshow:** both WS2812 strands, plus the 5 V LED rail with a common ground to the board.
8. **lemmy:** servo supply (neck on PA16, jaw on PA17 as wired in the puppet).

## 3. Power-up order

1. **TV, ElectronWarp, Wii.** Leave the Wii at the GH3 main menu or the Wii menu.
2. **marvin.** The splash screen shows a progress bar, then the dashboard appears with live
   video. marvin is the PLCA coordinator, and the bus has no beacon until it is up.
3. **Bus nodes**, in any order. Each one appears on marvin's **Bus** screen within a second
   or two of booting.
4. **fauxmote ↔ Wii.** fauxmote reconnects to a Wii it is already bonded with. If it doesn't
   (or after a Wii swap), see [Pairing](#pairing-fauxmote-with-a-new-wii).

## 4. Check it's alive

| Check | Where | Expect |
|---|---|---|
| Live video | Dashboard | Wii picture in the video window; tap it for fullscreen |
| All nodes present | Navigation drawer → **Bus** | Six followers listed with rising TX/RX counts and no CRC/symbol errors |
| Node details | **System** | Eight node cards with photos and QR codes |
| fauxmote linked | **Wiimotes** screen (slide to unlock) | Link shown as up; the on-screen fret/strum widgets register on the Wii |
| Actuators | Dashboard **ACTUATORS** | guitar / lemmy / lightshow toggles show the state each node reports back |
| Beat input | beatbox board | RGB LED flashes on kicks and the LED0–7 VU meter moves with the music |
| Song catalog | Dashboard song card / Song select | Songs listed with album art |

Then run one song: pick a song in **Song select**, press **START**, and watch marvin navigate
the menus and play. lemmy and lightshow only run while a song is playing, and only if
their ACTUATORS toggle is on.

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| Robot stops playing just after a song launches | GH3 drops the controller at launch. Open **Wiimotes** → **RECONNECT**, or run `fauxmote reconnect` on marvin's console. |
| Black video window | Check the HDMI chain and ElectronWarp power. marvin's capture watchdog re-arms itself on a stall. If the window stays black, reboot marvin. |
| A node is missing from **Bus** | Check its T1S connection and power. Run `nodes` / `t1s` on marvin's console, or `t1s` on the node's own console. |
| lemmy never nods during songs | Every `nod_trim` in `songs.csv` is currently 0. ⚠ TBD: set booth values, or use `lemmy nod on` from the console. |
| Lights flicker or the strands misbehave | 5 V LED rail undersized, or no common ground with the board. |
| Panel splash missing at boot | The QSPI splash isn't programmed (cosmetic). See the appendix. |

**Consoles.** marvin's operator console is on FLEXCOM2 at 115200 8N1, and `help` lists every
command. Each PIC node has a CLI on its debug UART: `t1s`, `id`, `plca`, plus node-specific
commands. fauxmote's console is on its USB-C port.

### Pairing fauxmote with a new Wii

On fauxmote's USB console run `pair` (marvin's console: `fauxmote pair`), then press the
red **SYNC** button on the Wii. After that, `reconnect` brings the bonded Wii back. Detail:
[`firmware/fauxmote/README.md`](firmware/fauxmote/README.md).

---

## Appendix: reflashing

Only needed if a board has lost its firmware. Each subproject's spec has the full recipe.

| Node | Toolchain | Programmer | Recipe |
|---|---|---|---|
| marvin | XC32 v5.10 (MPLAB VS Code extension) | on-board FT4232H JTAG (OpenOCD) | `openocd/program-nand.sh` for a resident build, or `openocd/make-sdcard.sh` for SD boot. Splash: `openocd/program-qspi.sh splash`. See [`firmware/marvin/openocd/README.md`](firmware/marvin/openocd/README.md). Do not use QSPI boot. |
| fauxmote | ESP-IDF v6.x | USB-C | `idf.py build flash` ([README](firmware/fauxmote/README.md)) |
| guitar, fretboard, lemmy, lightshow | XC32 v5.10 | PKOB nano (on-board) | MPLAB project `firmware/<node>/.vscode/<node>.mplab.json` |
| beatbox | XC-DSC v3.31 | PKOB4 | `firmware/beatbox/.vscode/beatbox.mplab.json` ([README](firmware/beatbox/README.md)) |

**marvin boot jumpers:** NAND boot is JP3 IN, JP4 OUT, no SD boot card. A bootable SD card
overrides NAND (useful for recovery).
