# marvin — System Specification

> **Read this first** for any non-trivial marvin task. The journal ([`journal.md`](journal.md)) is the running diary of decisions and work-in-progress; this spec is the durable description of *what marvin is*. When the two disagree, fix the spec.

**Status:** living spec. Settled sections are marked ✅; unbuilt or open ones 🚧 (linked to §9).

---

## 1. Purpose & scope

### 1.1 What marvin is

marvin is the firmware running on a **SAM9X75 Curiosity** board. Its job, in the guitar-playing-robot system, is to:

1. Capture live HDMI video of Guitar Hero III (Wii).
2. Run computer-vision note detection on the captured frames.
3. Decide *what* to play and *when* (chord grouping, fret-early timing, strum scheduling).
4. Send the corresponding fret/strum commands over the 10BASE-T1S bus to **fauxmote** (the Wiimote emulator that actually plays the Wii) and the **guitar** indicator node, and coordinate the other bus nodes (fretboard detector, lemmy, lightshow, beatbox).
5. Provide an on-device operator UI (10.1″ LVDS panel + maXTouch) for live view, run control, song selection, scores and bus diagnostics.
6. Produce **reference data** — annotated frames + detector state — for off-board use (training low-fidelity detectors, side-by-side detector comparison, replay).
7. **Observe game state from the screen** — recognize current menu / song-select / gameplay / pause / score-summary contexts via CV, expose high-level control verbs ("start single-player playthrough of song X", "advance menu", "exit to main menu") that translate into fret/strum command sequences sent through the same fretboard link.

In short: marvin is the runtime brain *and* the reference-detector data source for the project.

### 1.2 What marvin is *not*

- **Not the actuator.** The Wii is played by fauxmote (emulated Wiimote + guitar extension over Bluetooth); the guitar node mirrors the mask on status LEDs (ATE_2026 board). marvin sends bitmask commands; the nodes act on them.
- **Not a sensor / detection-only device.** marvin owns timing and command emission. It is not a passive vision peripheral that some other host orchestrates.
- **Not a router for a PC.** The previous PC-based architecture (Elgato + OpenCV + USB to a Pi) is retired. marvin replaces the PC in the runtime path.
- **Not a generic camera platform.** The video source is a fixed HDMI signal from a game console. Suggestions like "just use a USB webcam" or "output to HDMI directly" miss the point.

### 1.3 North-star one-paragraph summary

A SAM9X75 captures Wii HDMI video at 720×480 / 1280×720 @ 60 Hz over a TC358743 → MIPI CSI-2 → ISC path into DDR (BGR888 packed, 3 B/pixel). It runs reference-quality CV note detection on those frames, schedules chord/strum commands through a timing pipeline, and sends them over a 10BASE-T1S bus (marvin is the PLCA coordinator) to the guitar node and fauxmote. A closed-loop game controller reads the GH3 screens and navigates the menus to start a run (1-player practice, or a 2-player face-off against a human). An operator UI runs on a 10.1″ LVDS touch panel. marvin is also the ground-truth source for lighter-weight detectors — the fretboard node's phototransistors and its on-MCU neural network (edge-ai) are trained against marvin's captures.

### 1.4 What's done, what's next

| Subsystem | Status |
|---|---|
| HDMI capture (TC358743 → CSI-2 → ISC → DDR, BGR888 packed) | ✅ working at 480p60 and 720p60. See [`capture_pipeline.md`](capture_pipeline.md). |
| Display path (DDR → XLCDC HEO → LVDSC → panel) | ✅ active-picture crop scaled into a window or fullscreen, per-frame pointer swap. See [`display_path.md`](display_path.md). |
| CV detection pipeline | ✅ `cv_marvin_v1` plays songs (1p centred and 2p-left highway geometry; observation lead per highway × difficulty). The fretboard node is the second detector (`DETECTOR_FRETBOARD`, phototransistors + on-MCU NN, offered for hard 1-player only). §4.2. |
| T1S bus (actuator + detector links) | ✅ **T1S is the default** for both the fretboard/guitar link and the fauxmote link (`user.cmake` sets `MARVIN_FRETBOARD_TRANSPORT=1`, `MARVIN_FAUXMOTE_TRANSPORT=1`). marvin is PLCA coordinator (id 0); followers fauxmote 1, guitar 3, fretboard 4, beatbox 5, lemmy 6, lightshow 7 ([`t1s-podl-link.md`](../../../docs/t1s-podl-link.md) §7.1). Fallbacks: FLEXCOM1 USART (guitar/fretboard), FLEXCOM5 USART (fauxmote). §4.3. |
| Timing pipeline (chord FIFO, strum) | ✅ working; end-to-end gameplay on hardware. §4.4. |
| Operator UI (Legato) | ✅ eleven layer-screens: dashboard, navigation drawer, song-select dialog + album-art strip, wiimotes/manual override, on-screen keyboard, player-affiliation modal, bus statistics, system info (grid + node detail), activity log; plus the boot splash and tap-to-fullscreen video. 🚧 calibration/tuning surface not started. §4.5, [`ui_compositor.md`](ui_compositor.md). |
| Reference-data recording & export (SD) | 🚧 SD recorder not started. Perf-log USB CDC export (snapshots, strips, region streams for `tools/marvin-perf`) complete. §4.6. |
| System services (config, time, watchdog) | 🚧 Logging (+ RAM log ring), FreeRTOS analytics, health monitor, RTC (UTC) and QSPI settings done; watchdog not started. §4.7. |
| Game-state awareness & high-level game control | ✅ M9 observer: 20 GH3 screen classes incl. the 2-player setup and face-off results screens, menu-selection and song readers, 1p score/multiplier and 2p amp-scoreboard readers, end-of-song probe. ✅ M10 controller: 1-player practice and 2-player PRO FACE-OFF plans (a full 2p game ran end to end on hardware 2026-08-10), plus `play attach` for a manually-started game. §4.8, [`gh3_navigation.md`](gh3_navigation.md). |

---

## 2. System context

### 2.1 System at a glance

```
 Wii (GH3) ──component──► ElectronWarp ──HDMI──► TC358743 ──CSI-2──► marvin (SAM9X75)
     ▲                                                             ISC → DDR → CV detect → timing
     │ Bluetooth                                                   game observer / controller
     │                                                             HEO → 10.1″ LVDS panel + maXTouch UI
     │                                                                  │ LAN8651 (FLEXCOM3 SPI)
     │                                                                  │
     │        10BASE-T1S bus (PLCA; marvin = coordinator, id 0) ────────┴──────────────────────────────┐
     │             │                │                │                 │              │            │
     └──── fauxmote (id 1)    guitar (id 3)    fretboard (id 4)    beatbox (id 5)   lemmy (id 6)  lightshow (id 7)
           ESP32 Wiimote      PIC32CM PL10     PIC32CM PL10        dsPIC33AK        PIC32CM PL10  PIC32CM PL10
           emulator           status LEDs      phototransistors    audio beats      puppet servos LEDs
                                               + NN model
```

The fretboard node (phototransistors on the TV + NN) drives fauxmote and the guitar node peer-to-peer when it is the active detector inside a song. Wire-level detail: [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md). Off-band: a dev PC on the FLEXCOM2 console and the USB CDC perf-log (`tools/marvin-perf`); not in the runtime path.

### 2.2 Who owns what

| Concern | Owner | Notes |
|---|---|---|
| HDMI capture into memory | marvin | TC358743 + CSI-2 + ISC; 720×480 / 1280×720 @ 60 Hz, BGR888 packed (3 B/pixel). |
| Reference-quality CV note detection | marvin | Ground truth; multiple algorithms may coexist on the bus. |
| Phototransistor / NN note detection | fretboard node | fretboard scans 5 phototransistors @ 240 Hz and runs its own model; streams raw ADC + its command to marvin, and drives the guitar node directly when it is the active detector. |
| Detector arbitration | marvin | One active detector at a time (`Detector_SetActive`); dashboard / `active` console command select it. |
| Chord-window FIFO + strum scheduling | marvin | `game/game_timing.c` (§4.4). |
| Fret/strum output | guitar node, fauxmote | Open-drain GPIO (guitar) / emulated Wiimote guitar extension (fauxmote); both receive the 1-byte bitmask over T1S. |
| Operator UI (live view, run control, diagnostics) | marvin | 10.1″ LVDS + maXTouch, Legato + marvin compositor (§4.5). |
| Reference-data recording & export | marvin | USB CDC perf-log today; SD recorder planned (§4.6). |
| Game-state observation (menu / song-select / gameplay / pause / score) | marvin | CV on captured frames; emits typed game-state events on its own bus. §4.8. |
| Game catalog + menu metadata (song list, menu graph, recognized icons) | marvin | Recognizer tables compiled in (`gameplay_metadata.h`); song labels on SD (`songs.csv`). §4.8.3. |
| High-level game control (e.g., "start single-player song X") | marvin | Translates verbs into fret/strum sequences sent over the same fretboard link as gameplay commands. §4.8. |
| Calibration / tuning UI authoring | dev PC (fret-tuner) | Optional; not required at runtime. |
| Training data ingest / replay analysis | dev PC | Off-band consumer of marvin's reference-data export. |

### 2.3 Data flow at a glance

1. **Frame ready** — ISC writes a BGR888 packed frame to DDR; the video task notifies subscribers.
2. **CV detect** — `cv_marvin_v1` reads the frame and publishes a `detector_state_t` (stamped with its strike-line time) if it is the active detector.
3. **Fretboard ingest** — the T1S link receives the fretboard node's frames (raw ADC + its own command) and republishes them as perf-log records; when the fretboard is the active detector inside a song, it drives the guitar node itself and marvin stays silent.
4. **Accumulate** — the timing pipeline groups presses inside the chord window and queues finished chords.
5. **Schedule** — the queue emits fret-assert and strum-pulse commands at the right ticks.
6. **Send** — the 1-byte mask goes to the guitar node over T1S and is mirrored to fauxmote.
7. **Display** — the captured frame is shown on HEO in a window (or fullscreen) over the dashboard.
8. **Observe** — in parallel the game engine classifies screens and reads scores; the controller uses it to navigate menus and to detect song end.

### 2.4 Latency budget (target, end-to-end)

| Stage | Budget | Notes |
|---|---|---|
| HDMI source → DDR (capture) | ≤ 16.7 ms | One frame @ 60 Hz; bounded by capture pipeline. |
| Detect on frame | ≤ 16.7 ms | One frame budget for CV; ADC path is separate and faster. |
| Timing pipeline → command emit | latency dominated by the detector **observation lead** (≈ 210–420 ms) — intentional, not waste; compensates for sensor placement above strike line. |
| T1S command → guitar-node GPIO | ≤ 5 ms | 1-byte mask in one PLCA cycle. |
| Total user-visible latency | ≈ 210–420 ms | Set by calibration per detector, per highway, and per play difficulty. |

The dominant fixed delay (the detector's observation lead) exists by design — the camera sees notes before they reach the strike line, so the detector stamps a strike-line time (§4.2.3) that far ahead and the pipeline schedules to it. Capture + detect + bus jitter is what we actually budget against.

The lead is **not one constant**: the note highway scrolls faster on harder difficulties, so it is calibrated per (highway, play difficulty) — roughly 420 ms on easy down to ~210 ms on expert. `cv_marvin_v1` owns the table and takes the active difficulty from the committed `GameSelection` (§4.8); the console `cvdiff` command reads it and retunes cells live.

---

## 3. Hardware platform

### 3.1 Boards and links

| Board | Role | Connection to marvin |
|---|---|---|
| **SAM9X75 Curiosity Development Board** (EV31H43A, SAM9X75D2G with 2 Gbit DDR3L in package) | marvin host | — |
| **Waveshare HDMI → CSI-2 adapter** (TC358743) | HDMI → MIPI CSI-2 bridge | I²C (FLEXCOM8 TWI, PB4/PB5, 400 kHz, addr `0x0F`) for control; 2-lane CSI-2 RX for data; PC15 PWD, PC19 RESET unused (software reset over I²C). |
| **ElectronWarp** | Wii component → HDMI converter | HDMI into the TC358743. |
| **Microchip 10.1″ 1280×800 LVDS panel + maXTouch** | Operator UI | LVDSC from XLCDC; maXTouch on I²C; backlight PWM on PC18. |
| **Two-Wire ETH 3 Click** (LAN8651B1 10BASE-T1S MAC-PHY) | T1S bus to every other node | FLEXCOM3 SPI (PC22–PC26) + `T1S_IRQ_N` PB18 + `T1S_RST` PA17. |
| **SST26VF064B** QSPI NOR (on board) | Settings ring + splash image | QSPI (§4.7, [`ui_compositor.md`](ui_compositor.md) §12). |
| **microSD card** | Song catalog, album art, results, SHOWDOWN config | SDMMC, mounted at `/mnt/marvin` (§4.6.2). |
| **(optional) dev PC** | Operator console + perf-log viewer | FLEXCOM2 serial console (115 200 8N1, via an FTDI channel; §4.9); USB device CDC for perf-log (`tools/marvin-perf`). No runtime dependency. |

The other bus nodes (guitar, fretboard, fauxmote, lemmy, lightshow, beatbox) have their own specs; see the top-level [`SPEC.md`](../../../SPEC.md).

Pin assignments live in `firmware/marvin/default/src/config/default/pin_configurations.csv`. The capture pipeline doc ([`capture_pipeline.md`](capture_pipeline.md)) is authoritative for CSI / ISC register state; the display path doc ([`display_path.md`](display_path.md)) is authoritative for XLCDC overlay wiring.

### 3.2 SAM9X75 resources used / unused

| Peripheral | Status | Notes |
|---|---|---|
| MIPI CSI-2 RX + ISC + CSI2DC | ✅ in use | 2 lanes, 972 Mbps/lane; BGR888 packed to DDR (CSI2DC RMS=1, 3 B/pixel). |
| XLCDC + LVDSC | ✅ in use | BASE/OVR1/OVR2 for UI canvases, HEO for video (RGB\_888\_PACKED, scaled active-picture crop). |
| FLEXCOM8 (I²C/TWI) | ✅ in use | TC358743 control + display MIPI I²C, shared bus on PB4/PB5 (`DRV_I2C_INDEX_0`). |
| DBGU (UART) | ✅ in use | `printf` retarget (`printf → xc32_monitor → DBGU`); lightweight log shim. Log/diagnostic chatter only — kept off the console channel. |
| FLEXCOM1 (USART) | ⚪ fallback only | Fretboard/guitar-link UART (PA28/PA29, 500 000 Bd) when built with `MARVIN_FRETBOARD_TRANSPORT=0` (§4.3). |
| FLEXCOM2 (USART) | ✅ in use | Operator command console — 115 200 Bd 8N1, ring-buffer mode (§4.9). |
| FLEXCOM3 (SPI) | ✅ in use | LAN8651 10BASE-T1S MAC-PHY (`net/t1s/t1s_link.c`, vendored OPEN Alliance TC6 driver). |
| FLEXCOM5 (USART) | ⚪ fallback only | fauxmote UART link (PA16/PA15, 1 Mbaud) when built with `MARVIN_FAUXMOTE_TRANSPORT=0`. |
| FreeRTOS (Harmony OSAL) | ✅ in use | Static task priorities; `health/` monitors stacks and runtime. |
| TC0 (SYS_TIME) | ✅ in use | OSAL synchronous I²C requires it. |
| GMAC (Ethernet) | ⚪ unused | Reserved for future live-stream ref-data export. |
| SDMMC | ✅ in use | Card mounted at `/mnt/marvin` (`storage/storage.c`); catalog, album art, results, SHOWDOWN config. The §4.6 recorder is not started. |
| USB device (UDPHS) | ✅ in use | Perf-log CDC ACM sink to the dev PC, up to 2.77 MB/s. |
| maXTouch I²C | ✅ in use | Touch input for the UI; bounded-init / headless-fallback fix carried as an MCC re-apply patch. |
| Watchdog | 🚧 not configured | System services (§4.7). |
| QSPI | ✅ in use | SST26 NOR: settings ring + raw splash image (`flash/`, layout in `flash/qspi_layout.h`). |
| PWM ch 0 | ✅ in use | Panel backlight (PC18). |

---

## 4. Subsystems

### 4.1 Video capture & display ✅

Working end-to-end. See [`capture_pipeline.md`](capture_pipeline.md) and [`display_path.md`](display_path.md) for register-level detail. The video task (`video/video.c`) owns the capture pipeline, a stall watchdog, active-picture detection, and a frame queue that other tasks subscribe to. HEO scanout is owned by `ui_manager` through two video-task callbacks, so capture and display intent stay decoupled.

**What this subsystem owns:**
- TC358743 init, source detection, EDID, format negotiation.
- ISC DMA configuration and frame buffer rotation in DDR.
- Frame-ready notification to subscribers (CV detect, recording, UI overlay).

**What it doesn't own:**
- CV processing (§4.2).
- Recording / export (§4.6).
- HEO layer placement, scaling and levels (owned by `ui_manager`, §4.5).

### 4.2 CV detection pipeline + detector-state bus ✅

#### 4.2.1 Role

This is what makes marvin the *reference detector*. It runs computer-vision note detection on captured frames and publishes a structured detector-state record per frame onto a shared bus. Other detectors (fretboard ADC; future Edge-AI unit) publish onto the same bus. The timing pipeline (§4.4) consumes the bus.

#### 4.2.2 Detector model

A **detector** is anything that turns observation data into a `detector_state_t` event. Two exist (`detector_id_t`):

- `DETECTOR_CV_MARVIN_V1` — the reference vision detector. Reads BGR888 frames from the video frame queue and samples a brightness sensor + colour-filtered edge sensor per fret; geometry is a config (`cvcfg 1p|2pl`).
- `DETECTOR_FRETBOARD` — the fretboard node (phototransistors + an on-MCU model, trained for hard only). marvin does not run detection on its samples: when it is active inside a song (`Detector_FretboardDriving()`), the node drives the guitar peer-to-peer and marvin cedes the guitar and fauxmote.

#### 4.2.3 detector_state record (canonical shape)

```
typedef struct {
    uint32_t frame_epoch;     // master sync token (§4.6.4)
    uint32_t strike_at_ms;    // timestamp_us/1000 + detector observation lead;
                              //   when this observation reaches the strike line.
                              //   The timing pipeline (§4.4) schedules in this base.
    uint64_t timestamp_us;    // monotonic, marvin-local
    uint8_t  detector_id;     // detector_id_t: 0=cv_marvin_v1, 1=fretboard
    uint8_t  reserved[3];
    struct {
        uint8_t  pressed;     // 0/1 hard call
        uint16_t confidence;  // 0..65535, detector-defined
        uint16_t raw_value;   // detector-defined: ADC sample, pixel mean, etc.
    } fret[5];                // FRET_GREEN..FRET_ORANGE
} detector_state_t;
```

Fields are fixed-width, naturally aligned, little-endian — this is also the on-disk record layout for the state log (§4.6.5). `frame_epoch` is the master clock for cross-detector and cross-recording sync.

#### 4.2.4 Bus mechanics

- A single FreeRTOS queue (`s_bus_queue`) carries `detector_state_t` records, one entry per detector emit. The timing pipeline task (§4.4) is the sole consumer.
- **Source arbitration is on the push side, in the detector module.** Detectors emit via `Detector_Publish`, which forwards a record onto the bus only if that detector is the *active* one (`Detector_SetActive`); a source switch drains the queue. So the bus carries a single source's stream and the consumer never filters. Non-active detectors still run and feed the recording/UI paths.
- Capture does **not** read this bus: `cv_marvin_v1` emits a perf-log detector record (`PerfLog_EmitDetector`) regardless of active state, and the fretboard's frames arrive as `PERF_REC_FRETBOARD_RAW`, so both are captured side by side for training data.
- `cv_marvin_v1` runs on its own FreeRTOS task so it cannot stall the frame pipeline.

#### 4.2.5 Reference-detector responsibilities

`cv_marvin_v1` is a *first-class* subsystem, not a debug aid. Its detector state is the ground-truth label for any reference-data recording (§4.6). Implications:

- Algorithm changes are versioned (`cv_marvin_v1`, `cv_marvin_v2`, …). The version is stamped into recordings so training data stays attributable.
- Sensor geometry (per-fret sample points in 720×480 capture space) is compiled in per highway (`cv_marvin_v1.c`).
- The **observation lead** is calibration too, but per (highway, play difficulty) rather than per detector — see §2.4. It is runtime-tunable and is stamped into every `PERF_REC_DETECTOR_CONFIG` record alongside the difficulty and highway it applied to, so a capture is self-describing.
- The detector must run within one frame-time (≤ 16.7 ms) on the ARM926.

### 4.3 Actuator & detector links (T1S) ✅

**Transport.** Both marvin links default to the **10BASE-T1S bus** — marvin is the PLCA coordinator (id 0) on a LAN8651 over FLEXCOM3 SPI (`net/t1s/t1s_link.c`). Ids, ethertypes, heartbeat and control-channel formats are canonical in [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md) §7; this section covers marvin's side only. Each link has a build-time UART fallback, selected in `cmake/marvin/default/user.cmake`:

| Link | Default | Fallback (`=0`) |
|---|---|---|
| guitar / fretboard (`actuator/fretboard_link.c`, `MARVIN_FRETBOARD_TRANSPORT`) | T1S | FLEXCOM1 USART, PA28 `GUITAR_TX` / PA29 `GUITAR_RX`, 500 000 Bd 8N1 |
| fauxmote (`net/fauxmote/fauxmote_link.c`, `MARVIN_FAUXMOTE_TRANSPORT`) | T1S controller channel (`0x88B7`) | FLEXCOM5 USART, PA16 / PA15, 1 Mbaud |

- **marvin → guitar node:** 1-byte fret/strum bitmask (`actuator/guitar_cmd.h`), latest-wins. Every producer (timing pipeline, game controller, manual control) submits through `FretboardLink_Send`, tagged with a producer id that rides the `PERF_REC_ACTUATOR` record. The link sends on change and at a floor rate, not on every producer tick.
- **marvin → fauxmote:** `FretboardLink_Send` mirrors every mask to `Fauxmote_SendGuitarMask`; fauxmote also takes Wiimote nav slices, link commands (`fauxmote` console verb, wiimotes-screen RECONNECT / 1+2 keys) and the IR pointer. Wire protocol: `net/fauxmote/mf_proto.h`.
- **fretboard → marvin:** the node's 17-byte frame (`0x03 … 0xFC`: 5 × 16-bit ADC, `sample_seq`, applied/commanded masks) at 240 Hz, republished as a `PERF_REC_FRETBOARD_RAW` perf-log record (default-disabled; host enables with `set-mask`). This is the edge-ai training capture. `FretboardLink_SendWithTeacher` lets the timing pipeline label each frame with a per-note teacher mask distinct from the wire mask.
- **fretboard control:** `fretboard arm|disarm|stream on|off|model <difficulty>`. The node is armed only while `Detector_FretboardDriving()` is true (song active **and** fretboard selected); marvin disarms it at boot because the arm bit survives a marvin reset.
- **IR pointer (`POINTER` slice):** the wiimotes/manual-override screen (§4.5) sources it — a touch on its live-video rect aims the Wii's IR cursor at that point in the picture and the release pulses **A**, so a tap activates whatever it lands on. The cursor latches where the touch left it, so a drag corrects the aim before the click. The pulse rides `Fauxmote_PulseNav`, which ORs nav-core bits over whatever a producer has latched for a fixed hold rather than writing the slice. Because the Wii derives its cursor from fauxmote's synthesized sensor-bar dots using console-side settings marvin cannot read, the touch→pointer map is a per-axis affine calibrated on the bench by `fauxmote calib` (two commanded points, the operator touches the cursor at each) and persisted in QSPI settings as `ptr_cal_t` (§4.7). Uncalibrated it is the identity. Owned by `net/fauxmote/fauxmote_pointer.c`; gated behind the screen's slide-to-unlock, like every other control there.

#### 4.3.1 Actuator output enables

Each of the three actuator nodes — guitar (buttons), lemmy (servos), lightshow (LEDs) — has an **output enable that gates its own physical output**, commanded by marvin over that node's `0x88B9` control channel. `actuator/actuator_enable.c` owns marvin's wanted state for all three (**default enabled**, so a cold boot plays untouched) and is driven from the dashboard ACTUATORS toggles and the `guitar` / `lemmy output` / `lightshow` console commands.

The gate is at the node, not on marvin's send path, because marvin is not always the one driving: inside a song with the fretboard selected marvin goes silent and the fretboard drives the guitar peer-to-peer (`Detector_FretboardDriving()`), and lemmy's servos are also driven by beatbox. Muting marvin would leave both uncovered.

The dashboard/console state is the **master** enable. What marvin commands is that ANDed with the **performance window** (`ActuatorEnable_SetPlaying`): lemmy and lightshow run only while a song is actually playing, so the puppet and the lamps are still through the menus, loading and results. The game controller opens the window on arrival at a gameplay screen and closes it when the run ends (§4.8), so it covers both a navigated run and `play attach`; a game a human starts with marvin idle leaves it shut. The guitar is exempt — it has to actuate the menus that reach a song. On the dashboard the row's **button** shows the master intent and its **dot** the node's report, so "enabled, dot dark" reads as *armed and idle*.

lemmy's **nod** is a per-song setting taken from the song catalog's `nod_trim` column (§4.8.3). An explicit `0` means no nod for that song; **a blank cell — and an unknown song — read as `5`**, a middling trim that suits most songs, so the catalog only has to carry the exceptions and an untuned song still nods. Any other value is pushed as lemmy's `trim` (his `NOD_TRIM`, an offset on the nod oscillator's half-period, clamped by marvin to ±14 because past that it only saturates) and the nod goes to **`auto`** — lemmy head-bangs in occasional bursts on a tempo his engine is confident of, rather than for the whole song ([lemmy `SPEC.md`](../../lemmy/SPEC.md) §4.7). Trim goes first so the song's first nod frame already uses it, and both are pushed on the window edges.

Those pushes are a **starting point, not a lock**: nothing re-sends until the next edge, so `lemmy trim <n>`, `lemmy nod <on|off|auto>` and `lemmy nod conf <n>` all apply mid-song and are how the nod gets tuned by ear while it plays; the next run replaces them with the catalog's value. Each also reads back what marvin last commanded when given no value, so a live tweak starts from the song's setting rather than a guess.

Because a follower boots on its own defaults and `0x88B9` carries no ACK, the state is kept converged rather than merely sent: a send is retried until accepted, every known opcode is re-pushed when a node (re)appears or restarts, and each actuator **reports its gate back in its heartbeat** (flags bit1) so marvin re-pushes on mismatch — which also catches a node's local CLI changing it. Steady-state agreement costs no bus traffic. The dashboard dot therefore reflects what a node *reports*, with a pending state while a command is unconfirmed. Wire details: [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md) §7.1–§7.2.

### 4.4 Timing pipeline ✅

The **decide layer** — a peer of the detector (§4.2) and the actuator link (§4.3) — in `game/game_timing.c` (`GameTiming_*`), alongside `game_controller` (§4.8). It consumes the detector-state bus and schedules **in the strike-line time base**: each record carries `strike_at_ms` (§4.2.3), so the observation lead lives with the detector, not here. Owns:

- Chord-accumulation window (`TP_CHORD_WINDOW_MS` 30 ms).
- Pending-chord queue with per-chord press / strum ticks derived from `strike_at_ms`; `TP_FRET_EARLY_MS` 50 ms, `TP_STRUM_PULSE_MS` 40 ms.
- Per-fret release scheduling and strum-direction alternation.
- **Actuator advance:** subtracts `FRETBOARD_ACTUATOR_ADVANCE_MS` (0 for the open-drain guitar node) so a slower actuator still lands on the strike line; the wire byte stays a bare "assert now" mask.

`GameTiming_SetEnabled` opens the gameplay window (`Detector_SetGameActive`); the game controller enables it on arrival at a gameplay screen and the end-of-song probe can veto actuation in the frame loop. Navigation and gameplay are time-exclusive, so the controller and the pipeline never drive the wire together (Q11). With the fretboard selected as the active detector the pipeline stays silent inside the song and the fretboard drives the guitar node itself.

### 4.5 UI / operator surface ✅ (calibration 🚧)

10.1″ 1280×800 LVDS panel + maXTouch. Architecture — Legato as renderer, a marvin compositor (`ui/ui_manager.c`) over GFX Canvas mapping eleven pre-painted layer-screens onto BASE/OVR1/OVR2, with video on HEO — is in [`ui_compositor.md`](ui_compositor.md). Screens (`ui/screens/*`):

- **Splash** — QSPI image with a self-calibrating boot progress bar, cross-dissolved into the dashboard.
- **Dashboard** — windowed live video (tap → fullscreen), selected song card (album art, tier, difficulty, mode), START/STOP, run status, ROBOT and HUMAN score cards (1X–4X multiplier, TOP SCORES board), SHOWDOWN (2-player match from `showdown.cfg`), detector selection (CV / NEURAL NETWORK — NN offered only for hard 1-player), ACTUATORS toggles (guitar / lemmy / lightshow) with node-reported state.
- **Song select** — modal with scrolling song list, large album-art strip, difficulty and mode; on-screen keyboard and player-affiliation modal for entering the human player's name and role (client / employee).
- **Wiimotes** — slide-to-unlock manual override: fret/strum/whammy/tilt widgets, touch-to-aim IR pointer on the video, fauxmote link keys (RECONNECT and 1+2 are live; SWAP / UNLINK / PAIR are not yet wired).
- **Bus** — 10BASE-T1S statistics: per-node TX/RX rates, CRC/symbol errors, last heartbeat, measured bus utilisation.
- **System** — node grid (eight cards with board photo, part numbers, QR codes) and node detail.
- **Activity log** — last `LOG_RING_ENTRIES` (500) lines `log.c` sent to serial, scrollable.
- **Navigation drawer** — slides over every view; switches base views.

Custom widgets: `ui/widgets/*` (`button_aa`, `panel_aa`, `song_list`, `log_list`, `gauge`, `sparkline`, `bar`, `fret`, `whammy`, `tilt`, `slide_unlock`). Still not built: the calibration/tuning surface (per-fret ROI placement, thresholds) and an in-video detector overlay.

**Fonts — DejaVu Sans Mono only (decided 2026-06-30).** For licensing reasons the UI uses **only the DejaVu Sans Mono family** (`DejaVuSansMono_*`, `DejaVuSansMonoBold_*`). Any new text — labels, widgets, dynamic strings — must use a DejaVu Sans Mono face; do **not** introduce other faces (e.g. the figma-imported Menlo / NotoSans assets MGS may pull in). The DejaVu mono faces are subsetted with `0x20–0x7E`, `0xA0–0xFF`, and `0x2605` (★), so Latin-1 plus the tier star are available. Some legacy MGS-imported faces remain in the generated font set and are a known cleanup item — migrate them to DejaVu over time.

### 4.6 Reference-data recording & export 🚧

#### 4.6.1 Goals (in priority order)

1. **Edge-AI training data** — produce labelled frames + ground-truth detector state suitable for training low-fidelity detectors (fretboard's photo-detector logic; future Edge-AI MCU).
2. **Replay / sanity check** — let the user (offline) scrub through a recorded session to investigate misses.
3. **Multi-detector sync** — independent detectors (fretboard ADC, future Edge-AI) record their own data alongside marvin's; everyone sync's on a shared frame epoch.

Live off-device streaming for side-by-side comparison is *not* on the MVP critical path.

#### 4.6.2 Transport — SD card

Recordings are persisted to an SD card via SDMMC + a simple filesystem (FAT32 via Harmony's FILE_SYSTEM service). The card is removed and read on a workstation; no live network path is required for MVP.

Bandwidth ceiling: SDMMC sustained write ≥ 5 MB/s on Class-10 cards is reliably achievable. The recording format below is sized to land well below that.

The card is also the runtime store for everything that changes independently of the firmware image — the song catalog, album artwork, and per-player results — alongside recordings. (Config persistence lives on QSPI flash, not the card — see §4.7.) Canonical layout (single reference for all subsystems):

```
<card root>   (mounted at /mnt/marvin)
├── games/
│   └── gh3-wii/
│       ├── songs.csv           // song catalog labels (§4.8.3)
│       ├── showdown.cfg        // song + difficulty the dashboard's SHOWDOWN button commits
│       └── art/
│           ├── small/<setlist>-<NN>.{jpg,png}   // album artwork, named by recognizer key (§4.8.7)
│           └── large/<setlist>-<NN>.{jpg,png}   // one size tier per fixed-size DDR cache
├── players/
│   └── results.csv             // append-only per-player performance records (§4.8.6)
├── health/                     // heartbeat.csv + stacks.txt (§4.7)
└── recordings/                 // planned, §4.6.3
    └── <session>/
```

Recognizer/menu-graph data is **not** on the card — it stays compile-time in flash (§4.8.3), so recognition has no card-absent or stale-data failure mode.

#### 4.6.3 Format — detector-state + sparse keyframes

A recording is a **directory** on the SD card, containing:

```
recordings/
└── 2026-05-20T14-32-08/
    ├── manifest.json          // session metadata, calibration snapshot, detector versions
    ├── state.bin              // dense detector_state_t records, all detectors, all frames
    ├── commands.bin           // emitted fret/strum commands with frame_epoch
    ├── adc_raw.bin            // raw fretboard ADC stream with frame_epoch interleave
    └── keyframes/
        ├── 000000.bgr         // raw BGR888 keyframe (no encoder), filename = frame_epoch
        ├── 000060.bgr
        └── ...
```

- **`state.bin`** — append-only, fixed-size `detector_state_t` records (§4.2.3). At 60 Hz × 2 detectors × ~28 B = ~3.4 KB/s. Trivial.
- **`commands.bin`** — fret/strum bitmask + direction + emit timestamp + frame_epoch, ~16 B per emit. Sparse.
- **`adc_raw.bin`** — fretboard 17-byte frames at 240 Hz with `frame_epoch` annotation = ~4.1 KB/s. Modest. Until the SD recorder lands, the same data is available live as `PERF_REC_FRETBOARD_RAW` records (28 B framed) in any marvin-perf capture (default-disabled; enable with `set-mask`).
- **Keyframes** — raw BGR888 packed dump every N captured frames. At 720×480 × 3 B = 1.04 MB/keyframe. Default cadence: **1 keyframe per second** (60-frame stride) → 1.04 MB/s sustained. Configurable (every 30 / 60 / 120 / 600 frames). At 1 fps a 5-minute session is ~312 MB — comfortable on any modern SD card.
- No software JPEG / video encode. SAM9X75 has no hardware JPEG; CPU encode at 60 fps is infeasible. Keyframes stay raw; offline tools can transcode if desired.

Filename convention: directory name is ISO-8601 timestamp at session start (UTC); keyframe filenames are zero-padded `frame_epoch` so chronological order matches lexical order.

#### 4.6.4 Sync token — `frame_epoch`

A 32-bit counter incremented by the video task on every captured ISC frame (rolls over after ~830 days @ 60 Hz). Every record in a session — `state.bin`, `commands.bin`, `adc_raw.bin`, keyframe filenames, off-board detector data — is keyed by `frame_epoch`. This is the master clock for the entire system, including any later-added detectors that record independently and need to align their data with marvin's.

For records that don't fall on a frame boundary (ADC samples emitted between video frames), the most-recent `frame_epoch` is used and the sub-frame offset is captured by `timestamp_us`.

#### 4.6.5 manifest.json

Minimum fields:

```json
{
  "schema_version": 1,
  "session_started_utc": "2026-05-20T14:32:08Z",
  "marvin_firmware_git": "<hash>",
  "capture": { "width": 720, "height": 480, "fps": 60, "format": "BGR888" },
  "keyframe_stride_frames": 60,
  "detectors": [
    { "id": "cv_marvin_v1", "version": "1.0.0", "calibration": { ... } },
    { "id": "fretboard", "version": "1.0.0", "model": { ... } }
  ],
  "timing": { "chord_window_ms": 20, "strum_delay_ms": 220, "fret_early_ms": 50, "strum_pulse_ms": 50 },
  "ended_utc": null
}
```

Written at session start; `ended_utc` is patched on a clean stop.

#### 4.6.6 On-device buffering

A FreeRTOS task (`record_task`) owns SD writes. State / command / ADC streams are small and write through directly. Keyframes are larger — a single keyframe at 1.04 MB cannot block the video pipeline, so the record task receives a **frame buffer pointer** (the same DDR buffer the ISC writes into; refcounted by the video task) and writes from DDR while the next frame is being captured into another buffer. We need ≥ 3 capture frame buffers (one capturing, one displaying, one being recorded) when recording is active; today the video task already has multiple frames pre-allocated — exact count is verified during implementation.

If the SD write FIFO falls behind (rare; only a sustained-throughput problem), the recorder drops keyframes (logs a `RECORD_DROP` event into `state.bin`'s metadata channel) but **never** drops detector-state records.

#### 4.6.7 Off-board detectors recording independently

When a future Edge-AI detection unit is brought online, it can:

- Subscribe to marvin's `frame_epoch` over a chosen link (UART/Ethernet/etc.).
- Record its own observations (e.g. its own sensor data + its own detector output) in its own format, *keyed by marvin's `frame_epoch`*.
- Be aligned post-hoc against marvin's reference recording during training.

The spec does not constrain how off-board detectors store their data — only the sync contract (`frame_epoch` timeline) is canonical.

#### 4.6.8 On-demand full-frame snapshot (USB CDC) ✅

Independent of the SD recording path above (not started), marvin can export a single full-resolution frame on demand over the existing perf-log USB CDC link. The host sends `PERF_CMD_SNAPSHOT`; the perf-drain task copies the current frame once into a staging buffer and streams it back as a top-to-bottom run of full-width `PERF_STRIP_SNAPSHOT` band records (each ≤ `PERF_STRIP_MAX_BYTES`), all sharing one `frame_epoch`, the last flagged `PERF_STRIP_FLAG_LAST`. Bands are written straight to the sink (bypassing the small strip pool) and paced by the wire; emit is not gated by the STRIP type mask. `marvin-perf snapshot` reassembles and saves the frame (`.bgr` + sidecar `.json`, plus `.png` via Pillow). This is the offline-study on-ramp for the game-state work (§4.8) — no in-runtime role.

### 4.7 System services 🚧

Cross-cutting services not owned by any one subsystem:

- **Logging** ✅ severity-filtered `log.c` shim on DBGU, mirrored into a 500-line RAM ring (`log_ring.c`) for the activity-log screen; the log lock refuses ISR-context callers.
- **Health** ✅ `health/health_monitor.c` — low-priority liveness task: appends `health/heartbeat.csv` and rewrites `health/stacks.txt` on the card (mirrored to DBGU), and supplies the titlebar CPU tile; `health` console command prints the stack/runtime table.
- **Time** ✅ TC0 / SYS_TIME for OSAL; RTC holds UTC wall-clock (`time` console command), persistent across power cycles.
- **Config persistence** ✅ implemented on **QSPI NOR flash**, not the SD card (resolves Q9). `flash/settings.c` maintains a power-fail-safe 256-byte-slot ring-log (`MVST` magic + CRC), exposing `Settings_Load/Get/Save/SetBacklight/SetBootStages/SetPointerCal`. Today it persists the backlight brightness (restored at splash, `ui_manager.c`), the per-stage splash boot profile, and the wiimotes screen's touch→IR-pointer calibration (`ptr_cal_t`, §4.3); the `settings` console command dumps/saves/wipes/stress-tests the store. A layout change bumps `SETTINGS_VERSION`, which invalidates every stored record (the CRC moves with the layout) and falls back to defaults for one boot. Loads at first use, before the scheduler if needed, and survives with no card present; absent/invalid record → compiled-in defaults. Additional fields (calibration values, mode toggles, recording stride) extend the same record.
- **Watchdog** 🚧 not yet enabled.
- **OTA** ⚪ out of scope for now.

### 4.8 Game-state awareness & control ✅

Numbered §4.8 only to avoid renumbering older references; logically a peer of §4.2–§4.6.

#### 4.8.1 Role

1. **Game-state observer (M9)** — `game/game_engine.c` (`game_task`): classifies which GH3 screen a frame shows and reads what is on it. Publishes `game_state_t` (screen, menu selection, 1p score + multiplier, P1 READY! badge, end-screen layout) on its own queue (`GameEngine_BusQueue`), separate from the detector-state bus. It does not free-run: it classifies on request, so its cadence is set by the controller's poll. Readers: screen fingerprint (`gameplay_classify.c`) with a probe-first fast path keyed on static scoreboard chrome (`gameplay_present.c`), menu/song selection (`gameplay_select.c`), 1p score + multiplier (`gameplay_score.c`), 2p amp scoreboards (`gameplay_amp2p.c`), end-of-song detection (`gameplay_endprobe.c`, `gameplay_endlayout.c`).
2. **Game controller (M10)** — `game/game_controller.c`: on START (dashboard / `play`) it reads the committed selection (`game_selection.c`), anchors on the main menu, walks a step plan closed-loop (verify the screen before and after every input, hold on UNKNOWN, RED-recover only on a decisive wrong screen), hands off to the timing pipeline at gameplay, and detects song end. Plans: **1-player practice** (TRAINING → PRACTICE → song → LEAD → difficulty → FULL SONG → FULL SPEED) and **2-player PRO FACE-OFF** (marvin drives P1/left, a human P2; steps that wait for the human have a 120 s budget). `play attach` skips navigation for a game started by hand. SHOWDOWN (`game_showdown.c`) commits a 2-player match off `showdown.cfg`. Screen model and edges: [`gh3_navigation.md`](gh3_navigation.md).

Recognition is bootstrapped offline: snapshots (§4.6.8) build a corpus, algorithms are proven host-side in `tools/gameplay/`, and only the proven GH3-specific logic is ported. Q10 is settled in favour of fixed-region / colour / glyph matching — GH3's screens are static.

#### 4.8.2 Command path

The controller writes fret/strum masks through `FretboardLink_Send` (so they reach both the guitar node and fauxmote) — the same wire bits as gameplay; the nodes cannot tell menu from note. The timing pipeline is enabled only while a gameplay screen is up, so the two producers never overlap (Q11).

#### 4.8.3 Game catalog & menu metadata

The recognizer and the navigator both depend on per-game data:

- **Recognizer templates.** Per-screen region-grid centroids, menu baselines, song bitmap templates, presence probes and thresholds.
- **Menu graph.** Nodes = recognized game states; edges = button sequences that move between them (e.g., "main_menu → song_select" = `[STRUM_DOWN, STRUM_DOWN, GREEN]`). Used by the navigator to plan a verb.
- **Song catalog.** Per-song labels (title, artist, album, year, genre, tier) for the UI, plus lemmy's per-song `nod_trim`.

**Storage split (decided).** The line is drawn by *what the data is coupled to*, not by convenience:

- **Compile-time, in flash:** recognizer templates/signatures (centroids, menu baselines, song bitmap templates, thresholds) **and** the menu graph. These are *algorithm-coupled* — a template is only valid paired with the exact grid geometry, sampling, and normalization compiled into the C recognizer, and is re-learned in lockstep when the algorithm changes. They are generated from the host corpus by `tools/gameplay/gameplay/export_c.py` into `gameplay_metadata.h` and consumed through pointers by `gameplay_classify.c` / `gameplay_select.c`. **Recognition never reads the SD card** — so a missing or stale card can never cause a silent misclassification.
- **On the SD card (`games/gh3-wii/songs.csv`):** the song catalog *labels* — the human-meaningful, user-editable metadata that changes independently of the firmware. A flat CSV (header + one row per song), keyed by the stable `(setlist, index)` identifier the recognizer already emits (`gp_song_t`):

```
setlist,index,title,artist,album,nod_trim,length_s,year,genre,difficulty
main,4,"Rock and Roll All Nite","Kiss","Alive!",-3,210,1975,"Hard Rock",
```

`setlist` is the string `main`/`bonus` (the same key the art filenames use); `title`/`artist`/`album`/`genre`/`difficulty` are CSV-quoted (may contain commas; any may be empty); `length_s`/`year` are integers, `0` when unknown. Rows may be sparse or out of order — only `(setlist, index)` is the key. `title`/`artist`/`album` come from the `SONGS` table in `tools/gh3-cover-art/fetch_gh3_cover_art.py`, whose `--catalog` mode also fills `year`/`genre` from MusicBrainz; `length_s`/`difficulty` are reserved-but-blank (GH3 surfaces no per-song difficulty — the only authentic signal is the career tier, deferred).

`nod_trim` is the one column that is not a label: it is **signed** (may be negative) and it is lemmy's per-song nod setting (§4.3.1). Blank means "unspecified" and reads as `5`, the default that suits most songs; an explicit `0` means don't nod to this song. The two are deliberately different, so a tool that rewrites the catalog must carry a blank cell over *as blank* rather than coercing it to 0. It occupies the slot a `bpm` column used to reserve — parsing is positional, so the header cell is documentation only.

**Why CSV (not JSON).** Same rationale that put `results.csv` (§4.8.6) on flat CSV: on-device it parses with comma-splitting + `atol` into static buffers (no malloc, no JSON tokenizer, one line at a time — fits `FF_FS_MAX_FILES=1` and the static-allocation rule), and it opens directly in pandas/Excel for editing. The on-device reader (`game/game_catalog.c`) lazy-loads it once into a fixed `[GP_N_SONGS]` cache (shares the `util/csv.h` splitter with `results.c`) and serves `(setlist, index) → labels` lookups from RAM with the file closed.

The catalog is *labels only* — recognition does not depend on it; a missing/stale catalog degrades to "Unknown song", never a functional break, and a `(setlist, index)` mismatch degrades gracefully rather than misclassifying.

> **Stale-catalog warning — deferred.** The intent is for `export_c.py` to emit a `metadata_version` `#define` into `gameplay_metadata.h` and carry the same stamp in the catalog so the UI can warn when the catalog predates a template-set regeneration. `export_c.py` does **not** emit that define yet, so the loader ships without the version check; it still degrades safely on every miss. Wire the stamp + warning once `export_c.py` produces it.

Scope today: target is **one game** — Guitar Hero (Wii) — to validate the design. Adding a second game is a metadata addition (new template set + new menu graph + new song catalog), not a structural change.

#### 4.8.6 Performance results & player profiles ✅

Per-player gameplay results, stored on the SD card as `players/results.csv` — a flat,
append-only CSV (one row per completed run, with a header row). Keyed to a song by the same
`(setlist, index)` the recognizer emits:

```
player,affiliation,setlist,index,song,difficulty,score,timestamp
GREG,employee,main,0,"Slow Ride",hard,123456,2026-08-10T23:14:00Z
```

**The row records the *human* player, so only a 2-player run writes one** (decided 2026-08-10).
1P ROBOT is marvin playing alone and 1P HUMAN is retired, so 2-player face-off is the only mode
where a human performance exists; `score` is that human's amp scoreboard, and marvin's own total
stays on the dashboard and in the run log rather than on the card. A run stopped before the song
ends writes nothing. Song + score is the whole record: peak streak, accuracy, notes hit/total and
the part are deliberately absent — marvin has no reader for the accuracy figures (they are on the
GH3 end screens), and a column that is always zero is worse than no column.

**Why CSV (not JSON/YAML or a DB).** Results have two consumers — Marvin reads `results.csv` to show
high scores on-device, and the card is taken to a PC for deeper analysis. CSV is the only format that
serves both well: on-device it parses with comma-splitting + `atol` into static buffers (no malloc,
no JSON tokenizer, stream one line at a time — fits `FF_FS_MAX_FILES=1` and the static-allocation
rule); on a PC it's a one-liner in pandas/Excel/awk. JSONL is nicer for nested/evolving schemas but
costs an on-device tokenizer for no benefit here (results are flat and tabular); YAML has no good
zero-alloc embedded parser; a DB (SQLite) needs malloc + a FatFs VFS shim — overkill for an
append-only log. Schema evolution: append columns at the end — `csv_split` stops at the field count
the reader asks for, so an added column is ignored by an older reader and a shorter legacy row still
parses; pandas keys by header. Because the on-device reader addresses fields by *index*, it
validates the file's header against the schema it was built for and returns no scores on a mismatch:
a re-ordered file would otherwise parse into a plausible-looking but wrong high-score table. The
`song` title is duplicated alongside `(setlist,index)` so the CSV is self-contained for analysis
without joining the catalog.

Scope boundary: `results.csv` is the **flat per-run summary** only. Deep per-note / per-section
telemetry belongs in the recording subsystem (§4.6 `state.bin`), keeping this file trivially
parseable on-device.

On-device high scores: scan the file, filter by `(setlist, index[, difficulty][, affiliation])`, keep a fixed
top-N array in static memory. Ranking is **one slot per player**, holding that player's best run
(names matched case-insensitively) — a visitor who plays a song five times takes one row rather than
the whole board. A full scan per display is fine at expected scale (hundreds–thousands
of rows ≈ tens–hundreds of KB); if it ever grows large, cap the log or maintain a small separate
high-score cache.

`affiliation` is `client` or `employee`, asked by the player-affiliation modal; leaderboards rank the two apart. `timestamp` is UTC from the RTC (§4.7). Player identity is the operator-entered name (keyboard modal or `player` console command); richer profiles are out of scope.

#### 4.8.7 Album artwork ✅

Per-song cover art for the operator UI, stored on the SD card as standard **JPEG or PNG** (so the files are viewable on any workstation) under `games/gh3-wii/art/`. Named by the same stable recognizer key as the catalog, so firmware derives the path directly with no dependency on the catalog:

```
art/small/<setlist>-<NN>.png     // 144×144 letterboxed thumbnail (dashboard now-playing)
art/large/<setlist>-<NN>.png     // 508×208 middle strip, difficulty-colored fade baked in (song-select detail)
```

**Size tiers = size-tier subdirectories.** The two artwork sizes live in sibling subdirs (`art/small/`, `art/large/`), each mapping 1:1 to one fixed-size DDR pre-decode cache — the firmware walks one subdir per tier and all images in it share a slot dimension, so adding a third tier is just another subdir. The filename within a tier is the recognizer key (`<setlist>-<NN>`), identical across tiers; the loader picks the PNG or JPEG decoder from the extension (both tiers ship as PNG; the small tier moved off JPEG because Legato's JPEG offscreen decode is clip-gated).

Implemented in `game/game_art.{c,h}`.

**Pre-cache at startup, not per-use.** `GameArt_LoadAll()` runs from the UI boot task during the splash (after the FAT mount + Legato decoder init, before the reveal): it walks each size subdir and decodes every image **once** into that tier's static, pre-allocated cache of fixed-size slots in DDR; runtime artwork access (`setlist,index` → `leImage*`) is then an O(1) pointer into RAM — no card I/O, no decode, and no allocation at use time (consistent with the static-allocation rule). A missing/oversized/wrong-size/corrupt file leaves its slot empty → lookup returns NULL → UI blank; boot never fails.

- **Decoder — Legato's own runtime decoders; no new library.** JPEG and PNG are already enabled (`legato_config.h`: `LE_ENABLE_JPEG_DECODER=1`, `LE_ENABLE_PNG_DECODER=1`). With `LE_STREAMING_ENABLED=0` the flow is: read the whole compressed file into a shared scratch buffer → read true dims from the header (JPEG `SOF` / PNG `IHDR`) → describe a source `leImage` (`LE_IMAGE_FORMAT_JPEG`/`_PNG`) → `leImage_Render` it into the slot, which the decoder's `render` path writes (color-converted) with no clip-rect/active-frame dependency. Source images are authored at the exact tier WxH (no on-device scaling). **Two stock-Legato gotchas for the PNG path** (both carried as MCC re-apply patches, see journal): the PNG `_render()` is buggy (copies the compressed source + skips the channel byte-swap → garbage; patched to mirror `_draw()`, bug to file upstream), and a full 508×208 PNG decode needs `LE_VARIABLEHEAP_SIZE` raised to 2 MB (default 512 KB → intermittent lodepng OOM). The JPEG path has neither issue.
- **Cache pixel format matches the display layer.** small = **RGB565** (it is drawn on the RGB565 dashboard canvas); large = **RGBA8888**, alpha forced opaque after decode (shown on its own RGBA8888 layer-screen, Marvin layer 3 / OVR2, over the RGB565 song-select dialog). RGB888 is not an option: the 2D engine (`gfx2dFormats[]`) supports only `RGB_565` and `RGBA_8888`. Slots are static, sized for `GP_N_SONGS` (70), in **cached** DDR (`.region_ram`) — small 144×144×2, large 508×208×4 — plus a 512 KB shared decode scratch.
- **Large tier is a 508×208 strip with a difficulty-colored fade, baked offline.** The song-select detail window is 508×208 and shows the cover's middle horizontal band. The crop **and** the top/bottom fade are baked offline by `tools/gh3-cover-art/process_gh3_cover_art.py`, which reads each song's `difficulty` from `songs.csv` and colors the fade from an 8-tier palette: numeric `"1".."8"` map green→red with blue/purple accents in between (the `TIER_COLORS` table, shared with the song-select TIER-label schemes so art and label match), `"bonus"`/unknown = gray. Baking (vs a runtime overlay) keeps the device module to decode→store→display and the cached array display-ready; cost is a re-bake if a difficulty changes. Small tier is a clean 144×144 letterbox, no fade.

### 4.9 Operator command console ✅

An **interactive text console** over **FLEXCOM2 USART (115 200 8N1, ring-buffer mode)** — marvin's `PA13` (FLEXCOM2_IO0, `CLI_TX`) / `PA14` (FLEXCOM2_IO1, `CLI_RX`) — deliberately on a separate channel from the DBGU log/printf chatter — log output stays on DBGU, console I/O stays on FLEXCOM2, so neither pollutes the other.

- **Why not Harmony `SYS_CONSOLE`/`SYS_COMMAND`:** those are MCC-config-coupled and live in regenerated files (this board already carries ~10 re-apply patches against clobbered generated code). The console is instead a small in-tree module (`console/console.{h,c}`) in the same shape as `fretboard_link`/`perf_log`: it owns the USART plib directly, is fully statically allocated, and is kept out of MCC via `user.cmake`.
- **Library:** [embedded-cli](https://github.com/funbiscuit/embedded-cli) (vendored under `default/src/third_party/embedded-cli/`, MIT), used in static-allocation mode (a fixed `CLI_UINT` buffer → no `malloc`). Provides line editing, history, and tab-completion.
- **Mechanics:** one FreeRTOS task drains the FLEXCOM2 RX ring (woken by a 1-byte read-threshold notification, like `fretboard_rx_task`), feeds bytes to embedded-cli, and runs the dispatcher. Output is written byte-by-byte into the TX ring.
- **Commands** (`bindings[]` in `console/console.c`; `help` lists them with usage):
  - *Run:* `play [attach|stop|status]`, `showdown [reload]`, `player [name]`, `active <cv|fretboard>`, `detect cv <on|off>`, `cvcfg <1p|2pl>`, `cvdiff`, `timing <on|off>`, `manual <on|off>`, `fret`, `strum`.
  - *Nodes:* `t1s [reset]`, `nodes`, `guitar [on|off]`, `lemmy …` (servo pos, `output`, `nod <on|off|auto>`, `nod conf`, `trim`, `osc`), `lightshow [on|off]`, `fretboard <arm|disarm|stream on|off|model <d>>`, `fauxmote …` (status, pair, reconnect, unlink, btn, pointer, `calib`).
  - *Data:* `sd`, `catalog`, `art`, `scores`, `results add`, `time [set]`, `settings`, `qspi`.
  - *Diagnostics:* `status`, `health`, `log [dump|level|probe|text]`, `perf`, `bus`, `system`, `wiimotes probe`, `titlebar`, `nav`, `backlight`, `gamma`.

---

## 5. External interfaces

- **T1S bus** (ids, ethertypes `0x88B5`–`0x88B9`, heartbeats, control opcodes): [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md).
- **marvin ↔ fauxmote** slices: `net/fauxmote/mf_proto.h`, [`docs/marvin-fauxmote-link.md`](../../../docs/marvin-fauxmote-link.md).
- **Perf-log USB CDC** records and host commands: `perf_log/perf_log_records.h`, consumed by `tools/marvin-perf`.
- **SD card files**: §4.6.2 (layout), §4.8.3 (`songs.csv`), §4.8.6 (`results.csv`).
- 🚧 Recording binary layouts (§4.6) are specified but not yet written by firmware.

## 6. Operating modes

Independent toggles rather than a mode enum. What exists today:

| Control | Effect |
|---|---|
| `active <cv\|fretboard>` / dashboard detector row | Which detector drives the song (§4.2). |
| `detect cv <on\|off>` | Run the CV detector. |
| `timing <on\|off>` | Timing-pipeline output enable (normally driven by the game controller). |
| `manual <on\|off>` / wiimotes screen unlock | Manual-control producer owns the wire. |
| `guitar` / `lemmy output` / `lightshow` / dashboard ACTUATORS | Per-node output enables, ANDed with the performance window for lemmy and lightshow (§4.3.1). |
| `fretboard arm\|disarm` | Whether the fretboard node may drive the guitar (normally automatic). |

🚧 Planned: a record toggle (§4.6) and replay of a recording through the timing pipeline.

## 7. Build & deploy

- **Toolchain:** XC32 v5.10, SAM9X7_DFP, MCC (last regenerated with MCC 5.7.1 / core 5.9.1 / Harmony 1.7.2) and MPLAB Graphics Suite for the Legato design. Build from VS Code with the MPLAB extensions; the project file is `firmware/marvin/.vscode/marvin.mplab.json` (CMake tree under `firmware/marvin/cmake/`, output `out/marvin/default.elf` + `out/harmony.bin`).
- **Hand-written vs generated:** everything under `default/src/config/default/` is MCC/Legato output — regenerate rather than edit. The few fixes carried inside it are tracked as numbered MCC re-apply patches in the journal and must be re-checked after every regeneration. In-tree sources are added via `cmake/marvin/default/user.cmake`, which also sets the transport defines (§4.3). UI assets live in the MGS design `default_design.zip`; edit it with the `mgs-legato-design` skill, then MGS **Generate**.
- **Boot / flash** (detail in [`../openocd/README.md`](../openocd/README.md), [`../binaries/README.md`](../binaries/README.md)):
  - *Dev loop:* `openocd/load-ram.sh` loads at91bootstrap + marvin into DDR over the board's FT4232H JTAG channel — no SD card, repeatable without a power cycle.
  - *Standalone:* `openocd/make-sdcard.sh` writes `boot.bin` + `harmony.bin` to a microSD (highest boot priority), or `openocd/program-nand.sh` writes NAND.
  - *QSPI assets:* `openocd/program-qspi.sh` writes the raw splash image to QSPI NOR.
- **Runtime card:** the same (or a separate) microSD carries `games/gh3-wii/` (catalog, art, `showdown.cfg`) and receives `players/results.csv` and `health/` (§4.6.2).

## 8. Milestones

| | Milestone | State |
|---|---|---|
| M1 | Reference CV detector on the bus | ✅ |
| M2 | Fretboard link (ADC stream in, commands out) | ✅ on T1S |
| M3 | Timing pipeline + actuation, end-to-end play | ✅ |
| M4 | Recording to SD | 🚧 SD substrate done (catalog, art, results, health); recorder not started |
| M5 | Operator UI v0 | ✅ (§4.5) |
| M6 | Calibration UI | 🚧 not started |
| M7 | Replay | 🚧 not started (needs M4) |
| M8 | Fretboard drives the guitar | ✅ in firmware: peer-to-peer when the fretboard is the active detector (hard, 1-player); see the fretboard journal for hardware status |
| M9 | Game-state observer | ✅ 20 screen classes + selection/score/amp/end readers |
| M10 | Game-state control | ✅ 1p practice + 2p PRO FACE-OFF plans, `play attach`, SHOWDOWN |

## 9. Open questions

Only live questions are kept here. Settled: Q3 detector bus = one queue, push-side arbitration (§4.2.4); Q4 timing on marvin (§4.4); Q5 Legato + compositor (§4.5); Q6 independent toggles (§6); Q7 fret-tuner stays off-band; Q8 brightness + colour-edge sensors (§4.2.2); Q9 settings on QSPI (§4.7); Q10 fixed-region matching (§4.8.1); Q13 album-art tiers (§4.8.7).

| # | Question | Status |
|---|---|---|
| Q1 | Reference-data format details — keyframe stride, per-fret patches. | Direction set (§4.6.3); revisit when the recorder is built. |
| Q2 | Reference-data transport beyond SD (Ethernet live stream). | Deferred. |
| Q11 | Command-path arbitration between the game controller and the timing pipeline. | Working rule: time-exclusive (pipeline only while a gameplay screen is up). Revisit if mid-song menus (pause) need driving. |
| Q12 | Player identity beyond an operator-entered name + affiliation. | Out of scope for now. |
