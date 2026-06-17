# Fretboard Firmware Specification

> **Re-scoped to a T1S sense+actuate node (2026-06-17):** the fretboard is a 10BASE-T1S
> PLCA follower (id 1) that infers actuator commands from the phototransistor data and drives the
> [`guitar`](../guitar/SPEC.md) node over T1S, while also streaming its data to marvin. The old UART
> link, the standalone Wii-guitar GPIO outputs (`cmd_receive.c`), and the `FRETBOARD_LINK`/`FRETBOARD_MODE`
> build flags are gone — one behaviour. Node-class model: top-level [`SPEC.md`](../../SPEC.md) §2; link
> detail: [`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md).

## Overview

Firmware for PIC32CM6408PL10048 (Cortex-M0+, 24 MHz). The fretboard is a 10BASE-T1S
node that both **senses** and **drives**. Each 240 Hz tick it samples five
phototransistors above the TV strike line; an on-device int8 neural net
(`model_infer.c`, weights in the generated `model_weights.h`) infers the Wii-guitar
button bitmask from the ADC window. Over T1S it then:

- **streams** the 17-byte data frame (ADC scan + the driven bitmask) to the marvin
  coordinator — logging / edge-ai training; and
- **commands** the [`guitar`](../guitar/SPEC.md) node (id 2) directly with the
  inferred bitmask — peer-to-peer actuation; marvin coordinates/logs but is out of
  the command path.

Inference runs in the main loop (it overruns the 240 Hz tick in the ISR); the ISR
only scans + stages the data frame. SW0 arms actuation, LED0 shows armed (boots
disarmed → commands 0/released). It has **no local Wii-guitar outputs** — those pins
are the LAN8651 SPI. See [`docs/journal.md`](docs/journal.md), edge-ai
[`runtime.md`](../../tools/edge-ai/docs/runtime.md), and
[`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md).

## Hardware

### Target

| Spec | Value |
|------|-------|
| Device | PIC32CM6408PL10048 |
| Core | ARM Cortex-M0+ |
| Clock | 24 MHz |
| Toolchain | XC32 5.10 |
| Programmer | PKOB nano |

### Pin Map

#### Sensor Inputs (ADC)

Five phototransistors positioned above the TV strike line. Each
produces an analog voltage that drops when a note passes the sensor.
ADC reads 12-bit unsigned (0–4095); lower values indicate note
presence.

| Channel | ADC Input | Pin |
|---------|-----------|-----|
| Green | AIN28 | PA28 |
| Red | AIN16 | PA16 |
| Yellow | AIN19 | PA19 |
| Blue | AIN27 | PA27 |
| Orange | AIN26 | PA26 |

There are no button/strum outputs — those GPIOs are the LAN8651 SPI/CS/IRQ/RST
pins now. See [`t1s_detector.c`](t1s_detector.c).

#### SERCOM0 SPI — LAN8651 (T1S)

Mode 0 (CPOL=0/CPHA=0, MSB, 8-bit): MOSI=PA04, SCK=PA05, MISO=PA07; `T1S_CS`=PA15
(GPIO, held low across each TC6 chunk), `T1S_RST`=PA14, `T1S_IRQ_N`=PA13 (EIC
EXTINT13, falling).

#### SERCOM1 — operator console

Hosts the embedded-cli console + diagnostic log. TX PB00 / RX PB01, **500 000** 8N1,
ring-buffer mode (TX ring ≥ 512 B).

## Software Architecture

### Processing Loop

`TC0` fires a periodic callback at **240 Hz** (≈4.17 ms). The ISR scans and stages
the data frame with the currently-driven command; the heavy work (inference, TC6
service, TX flush) runs in the `main()` service loop:

```
TC0 ISR (240 Hz)                     main() service loop
  fret_scan_all()                      model_infer_* -> s_latest_cmd
  push sample to model queue           T1SDetector_SetCommand(cmd) -> guitar (T1S)
  cmd = armed ? s_latest_cmd : 0       T1SDetector_Tasks()  (service + flush data + HB)
  data_stream_send(cmd)  (stage)       CLI_Tasks()
```

The ISR stays short (the model overruns the tick if run there — see the journal /
edge-ai `runtime.md` §3). `data_stream_send()` stages the frame; `T1SDetector_Tasks()`
flushes it (and the guitar command, and the heartbeat) from the main loop.

### Module Descriptions

#### fret_scan ([fret_scan.c](fret_scan.c) / [fret_scan.h](fret_scan.h))

Reads all five ADC channels sequentially using blocking polling.
Results are stored in a static array and accessed via
`fret_scan_result(channel)`.

The channel enum defines the canonical fret ordering used throughout
the firmware:

```
FRET_GREEN = 0, FRET_RED = 1, FRET_YELLOW = 2, FRET_BLUE = 3, FRET_ORANGE = 4
```

#### data_stream ([data_stream.c](data_stream.c) / [data_stream.h](data_stream.h))

Builds one 17-byte little-endian frame per tick and stages it for TX to the marvin
coordinator over T1S (via `T1SDetector_SendFrame`):

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 1 | start = `0x03` |
| 1 | 2 | green (uint16) |
| 3 | 2 | red |
| 5 | 2 | yellow |
| 7 | 2 | blue |
| 9 | 2 | orange |
| 11 | 4 | sample_seq (uint32) — monotonic, one per tick |
| 15 | 1 | applied_mask — the bitmask driven to the guitar this scan |
| 16 | 1 | end = `0xFC` (`~start`) |

`sample_seq` lets the host reconstruct true sample order and detect dropped frames
(it advances per tick even when a send is skipped). `applied_mask` is the command
the node drove to the guitar this scan, paired atomically with the ADC scan for
edge-ai training-data export. The marvin RX side keys on this 17-byte layout.

### T1S node ([t1s_detector.c](t1s_detector.c) / [.h](t1s_detector.h))

PLCA follower **id 1**, MAC `02:00:00:00:00:01`, on the marvin-coordinated
(`02:..:00`) bus via a LAN8651 MAC-PHY over SERCOM0 SPI. Reuses the shared
`third_party/oa-tc6-lib` (OPEN Alliance TC6) + a local `tc6-conf.h`. All TC6 access
is serviced from the **main loop** (`T1SDetector_Tasks()`), never the 240 Hz ISR.

- **Data → coordinator:** the 17-byte frame rides the Ethernet payload under
  ethertype `0x88B5`, dst = coordinator MAC. The ISR stages it
  (`T1SDetector_SendFrame()`, latest-wins); the main loop flushes it, one TX in
  flight. A frame dropped while busy shows as a `sample_seq` gap.
- **Command → guitar:** `T1SDetector_SetCommand()` (main loop) hands the inferred
  1-byte bitmask to the **guitar node** (id 2, MAC `02:..:02`, ethertype `0x88B5`).
  Sent edge-triggered + re-sent every 50 ms so a dropped command self-heals; the
  guitar applies latest-wins. Peer-to-peer — marvin is not in the command path.
- **Presence:** a 500 ms heartbeat (ethertype `0x88B6`, `node_type = 1` detector) so
  marvin's `nodes` command shows the node present.
- **Operator CLI:** SERCOM1 hosts an embedded-cli console ([cli.c](cli.c), vendored
  `third_party/embedded-cli/`): `t1s` (link / sync / chipRev / PLCA / data+command tx
  counts), `adc` (latest scan), `id` / `plca` (MAC-PHY register diagnostics).
  Bare-metal — `CLI_Tasks()` drains the RX ring each main-loop pass.

**Coordination caveat:** there is no active-source arbitration yet — while the
fretboard is armed (SW0) it drives the guitar, and marvin must not also command the
guitar (both target `02:..:02`). marvin-side active-detector/active-guitar selection
is the follow-up. Link rationale, addressing, and the PoDL plan are in
[`docs/t1s-podl-link.md`](../../docs/t1s-podl-link.md).

## Observability

The data stream lands on **marvin** now (over T1S), so health is read there: the
`PERF_REC_FRETBOARD_RAW` rate via [`tools/marvin-perf`](../../tools/marvin-perf/) and
node presence via marvin's `nodes` console command. On the node itself, the SERCOM1
CLI (`t1s`, `adc`) shows link state, data/command TX counts, and the live scan.
([`tools/ds_monitor.py`](tools/ds_monitor.py) was the UART-era serial monitor — no
longer applicable now that the stream is on the bus.)

## Build System

MPLAB Extensions for VS Code. Project config is in
`.vscode/fretboard.mplab.json`. Source files are listed in the
`fileSets[0].files` array. MCC/Harmony-generated code lives under
`fretboard-mcc/` and should not be edited.

## File Map

| File | Purpose |
|------|---------|
| [main.c](main.c) | Init + TC0 scan/stage ISR + service loop (infer, drive guitar) |
| [fret_scan.c](fret_scan.c) / [.h](fret_scan.h) | ADC channel scanning |
| [data_stream.c](data_stream.c) / [.h](data_stream.h) | 17-byte data frame builder (→ T1S) |
| [t1s_detector.c](t1s_detector.c) / [.h](t1s_detector.h) | T1S node: data→coordinator, command→guitar, heartbeat |
| [cli.c](cli.c) / [.h](cli.h) | Operator CLI on SERCOM1 (t1s/adc/id/plca) |
| [model_infer.c](model_infer.c) / [model_infer_stream.c](model_infer_stream.c) | On-device int8 model (ADC window → bitmask) |
| [tc6-conf.h](tc6-conf.h) | OA TC6 driver build config |
| `third_party/embedded-cli/` | Vendored embedded-cli (CLI engine, static-alloc) |
| `fretboard-mcc/` | MCC Harmony peripheral libraries (generated) |
