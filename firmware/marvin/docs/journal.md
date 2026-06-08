# marvin — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for marvin. Newest entries at the top. For *what marvin is* (purpose, subsystems, interfaces, milestones), read [`spec.md`](spec.md) — this journal does not duplicate it.

---

## Current focus

**Board port: SAM9X75 Curiosity → Curiosity Hybrid (in progress).** USB Host is gone on the Hybrid; the fretboard link was rewired from USB CDC host to a direct FLEXCOM2 USART on 2026-06-08 (see decision log + session log). Code-complete and syntax-checked; pending a full MPLAB build and on-hardware bring-up. Display/MIPI I²C (incl. TC358743 control) also moved off FLEXCOM6/PA24-PA25 to FLEXCOM8 TWI/PB4-PB5 during the port — `spec.md` §3.1/§3.2 reconciled on 2026-06-08. Dated 2026-05-01/05-20 journal entries and `journal-archive.md` keep their FLEXCOM6 references as historical record (true on the original Curiosity board).

**M1 — Reference detector v0.** First CV detector running on captured frames, publishing `detector_state_t` records onto the detector-state bus. No actuation yet. See [`spec.md`](spec.md) §4.2 for subsystem detail and §8 for the milestone progression.

Scaffold is in place as of 2026-05-20: `detector/detector.{h,c}` owns the bus queue + a temporary drain task that logs records-per-second; `detector/cv_marvin_v1.{h,c}` subscribes to the video frame queue and publishes one `detector_state_t` per frame with `frame_epoch = Video_FrameInfo.frame_count` and all-zero fret state. Next: pick the v0 algorithm (Q8) and start populating `fret[]`.

**Capture/display pipeline is done as of 2026-05-02:** source → TC358743 → CSI-2 @ 972 Mbps/lane → SAM9X75 → ISC DMA → DDR (BGRX32) → XLCDC OVR1 → LVDSC → 10.1″ 1280×800 LVDS panel.

> **Deeper-dive references:**
> - [`spec.md`](spec.md) — system-level spec (read first).
> - [`capture_pipeline.md`](capture_pipeline.md) — HDMI → DDR capture stage (CSI2DC, ISC, register settings, datasheet citations).
> - [`display_path.md`](display_path.md) — DDR → LCD display stage (XLCDC OVR1 wiring, pillarbox, cache coherence).

> **Earlier entries** (capture/display bring-up, 2026-05-01 → 2026-05-15) are preserved in [`journal-archive.md`](journal-archive.md).

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-08 | **Wired nSRST + RTCK into the OpenOCD config; `reset halt` and adaptive clocking verified.** Channel-A reset/clock pinout (from Greg/schematic): AD5=nRST, AD7=RTCK, AD4/AD6=N/C (no nTRST). Added `ftdi layout_signal nSRST -oe 0x0020` (open-drain), `reset_config srst_only srst_open_drain srst_nogate`, `adapter srst delay 300`. Verified on `W16-2026-413`: `reset halt` drops the core into boot ROM (pc=0x00000044, ARM, cpsr=0x…d3) vs the running-firmware SRAM pc; `adapter speed 0` engages `RCLK (adaptive)` and still scans/halts. | Supersedes the `reset_config none` placeholder in the JTAG-bring-up entry below. nSRST as open-drain via `-oe` (drive low to assert, tristate to release — board pulls up) matches the SAM9 NRST electrical behavior. `srst_nogate` because the ARM926 TAP/EmbeddedICE stay alive across a system reset, so OpenOCD can keep JTAG up and catch the core immediately after release (confirmed: TAP re-scanned fine post-reset). No nTRST is fine — ARM9 TAP reset via 5×TMS works. RTCK/adaptive clocking left available but **not** the default (`adapter speed 1000` stays default for reproducibility; the SoC runs fast enough that fixed TCK is reliable). |
| 2026-06-08 | **Programmed the FT4232H's 93LC46B EEPROM with board identity + per-channel driver flags; keeping it despite macOS not honoring the VCP bit.** Image authored via [`firmware/marvin/ftdi/ft4232h-chybrid.conf`](../ftdi/ft4232h-chybrid.conf): strings `Microchip` / `SAM9X75 cHybrid` / serial `W16-2026-359`, and per-channel driver A=D2XX, B=D2XX, C=VCP, D=D2XX. Channel map (sch sheet 14): A=JTAG (MPSSE), C=DBGU console (populated via R149/R151), B+D unconnected (B's bridge resistors are DNP). Flashed with `ftdi_eeprom --flash-eeprom`; verified write read-back at the chip and re-enumeration of the new strings/serial. JTAG re-confirmed working afterward. | The board shipped with a **blank** 93LC46B (all `0xFFFF`), so all four channels enumerated as generic "Quad RS232-HS" serial ports. Goal was to stop the JTAG channel (A) and the unconnected channels (B/D) from appearing as `/dev/cu` ports by marking them D2XX. **Outcome: macOS 26 (Tahoe) `AppleUSBFTDI` ignores the per-channel VCP/D2XX EEPROM flag and still creates a serial node for all 4 interfaces** — the port-hiding goal is not achievable via EEPROM on macOS. (The VCP/D2XX flag is really a Windows-driver concept: only Windows hides the D2XX channels; Linux `ftdi_sio` also creates ttyUSB for all four unless a udev rule suppresses them.) Kept the flash anyway because it's strictly better than blank: gives the board a real product string + unique serial (stable per-board `/dev/cu.usbserial-W16_2026_359x` names, and `adapter serial` selection in OpenOCD when multiple boards are attached), is correct for non-Mac hosts, and has zero downside on Mac (same 4 ports as before). Flashing the other boards is now optional — only worth it for unique per-board identity, not port suppression. |
| 2026-06-08 | **JTAG debug on the Curiosity Hybrid uses OpenOCD over the onboard FT4232H channel A — no driver swap needed.** Reusable config at [`firmware/marvin/openocd/sam9x75-chybrid.cfg`](../openocd/sam9x75-chybrid.cfg): FTDI MPSSE on channel 0, standard JTAG pinout (TCK=AD0/TDI=AD1/TDO=AD2/TMS=AD3), `arm926ejs` target, `reset_config none`. Verified: TAP IDCODE `0x0792603f` (ARM926EJ-S, Atmel part 0x7926), EmbeddedICE v6, 2 HW breakpoints, halt/resume work, gdb server on :3333. CPU was halted from internal SRAM (pc≈0x0030d8ec). | pyOCD was ruled out — it's Cortex-M/CMSIS-DAP only and can't drive raw FTDI MPSSE nor an ARM9. The macOS worry ("all 4 FT4232H channels show as serial ports, need to swap a driver") turned out to be a non-issue: Apple's in-kernel FTDI driver binds all four as `/dev/cu.usbserial-*`, but libusb still claims channel A for MPSSE — `libusb_claim_interface(0)` succeeds and OpenOCD only logs a harmless `libusb_detach_kernel_driver ... LIBUSB_ERROR_ACCESS` warning before proceeding. On Apple Silicon Tahoe the driver can't be `kextunload`ed anyway, and no EEPROM reflash is required. nTRST/nSRST aren't wired into the MPSSE byte layout, so reset lines aren't driven — halt/resume work over JTAG regardless; revisit if flash/reset-halt needs them. Note: OpenOCD must be run *outside* the Claude Code command sandbox — the sandbox blocks libusb USB enumeration (libusb sees 0 devices), which produced misleading "device not found" errors until diagnosed. |
| 2026-06-08 | **Fretboard link moves USB CDC host → direct FLEXCOM2 USART** (`actuator/fretboard_link.c`). The SAM9X75 Curiosity *Hybrid* board (new target, MCC ported in `598d370`/`3af09bc`) has no host-capable USB port, so the EDBG-CDC-bridge transport is gone. marvin now talks UART directly to the fretboard's SERCOM1: `PA13` FLEXCOM2_IO0 (TX) / `PA14` FLEXCOM2_IO1 (RX) ↔ fretboard `PB01` RX / `PB00` TX, 500 000 Bd 8N1. FLEXCOM2 regenerated in **ring-buffer mode** (RX ring 512 B, TX ring 64 B). The public `FretboardLink_{Initialize,Send,IsConnected}` API and all perf-log records (FBL_SEND/CDC_WRITE_COMPLETE stamps, ACTUATOR, FRETBOARD_RAW) are unchanged, so `timing_pipeline`/`manual_control` and host `marvin-perf` need no edits. | The link is unchanged at the fretboard end (always SERCOM1 UART @ 500 000); only marvin's side was USB-bridged. Ring-buffer mode chosen over the basic single-transfer plib because the fretboard streams 17-byte frames continuously at 240 Hz: the basic plib disables RXRDY between a completed Read and the next arm, risking overrun, whereas the ring fills from its own ISR with no gap. RX is now a simple drain — a persistent read-threshold (`DS_FRAME_LEN`) notification wakes `fretboard_rx_task`, which pulls bytes off the ring and runs the same `0x03 … 0xFC` resync as before; the separate `s_rx_stream` stream buffer + ISR re-arm are deleted (the ring *is* the buffer). TX drops the USB Write/ack-semaphore dance: a ring-buffer `Write` copies the byte and returns, so "ack" is the enqueue instant. No enumeration/attach/detach → `s_connected` becomes a static `s_link_up`. The `CDC_WRITE_COMPLETE` perf-stage name is now historical (it's a UART enqueue) but kept to avoid a host-decoder schema change. |
| 2026-06-03 | **Timing pipeline commands "released-style" play.** The release-suppression in `process_releases` ([timing_pipeline.c](../default/src/actuator/timing_pipeline.c)) was unbounded — it held a fret if *any* queued note/strum needed it, however far out, so across consecutive same-fret notes the fret stayed asserted the whole time. Bounded it: suppress the release only if the fret is re-needed at/before its own scheduled release time (`s_release_at_ms`), i.e. genuinely back-to-back; otherwise release for a clean per-note command. Helpers `note_q_any_needs`/`strum_q_any_needs` → `*_needs_by(bit, by_ms)`. Sustains are unaffected (continuous detector `pressed` → no release edge → never enters this path). | The legato hold was injecting marvin's note-queue lookahead into the emitted command, which polluted the edge-ai fret labels (the held fret reads `1` over a gap where the photosensor is bright). Proven by the detector-fb probe: clean per-note labels took overfit frets 0.94→0.99 (edge-ai journal 2026-06-03). Releasing per-note makes the command a function of the observable. Physical legato (avoiding release/repress churn on fast Expert runs) moves to a future fretboard *lazy actuator* layer; until then marvin's own play release-represses, which is fine at hard-mode note spacing. Validate marvin still plays hard cleanly after the change. |
| 2026-06-03 | **Perf-log schema v3→v4** (`PERF_LOG_SCHEMA_VERSION` bumped). `perf_rec_fretboard_raw_t` grows: its `reserved u16` becomes `fb_sample_seq (u32) + applied_mask (u8) + reserved (u8)` (28→32 B). `PerfLog_EmitFretboardRaw` gains `fb_sample_seq, applied_mask` params; `fretboard_link.c` RX parser reads the now-17-byte fretboard frame and passes them through. Host `marvin-perf` bumped to `EXPECTED_SCHEMA_VERSION=4` in lockstep. | Unlike the 2026-06-02 *additive* FretboardRaw add (no bump), this changes an existing record's layout, so it must bump the version — the host's `check_schema` rejects mismatched streams. The two new fields are stamped by the fretboard itself (fretboard journal 2026-06-03) so the edge-ai training label (actuator state) is paired atomically with the ADC scan at the source; marvin's previous cross-TX/RX-clock reconstruction skewed the pairing and floored strum timing at ~20 ms in Phase-2 (edge-ai journal). Old `.bin` captures no longer decode under the new host — acceptable for dev; we re-capture. Wire-format break: fretboard + marvin must flash together. |
| 2026-06-02 | New `PERF_REC_FRETBOARD_RAW` record type (`0x0B`) + `PERF_TASK_FRETBOARD_RX` task slot (17). Default-disabled at boot like the other high-rate types; host enables via `PERF_CMD_SET_TYPE_MASK`. `fretboard_link` gains a continuously-rearming `USB_HOST_CDC_Read` plus a sibling `fretboard_rx_task` that pops bytes off a 512 B stream buffer and emits one record per valid 12-byte fretboard data frame at the fretboard's 240 Hz rate. No `PERF_LOG_SCHEMA_VERSION` bump — additive only. | Step 1 of the Edge-AI training-dataset workstream: `cv_marvin_v1` provides the reference label per video frame, fretboard ADC samples are the input the future Edge-AI MCU would see. Routing both signals through the perf log lets one offline tool join them on `frame_epoch` (~4 fretboard records per video frame) and `ts_counter` (sub-frame ordering), which is the cheapest path to a labelled dataset before the SD recorder (§4.6) exists. Sibling task instead of folding RX into `fretboard_link_task` keeps the existing TX path untouched (its `xQueueReceive` blocking shape doesn't compose with a stream buffer without bigger surgery). The downstream `adc_fretboard` detector (§4.2, M2) is the natural follow-up; the bus scaffold (`DETECTOR_ADC_FRETBOARD = 1`) is already reserved. |
| 2026-06-02 | `FBL_BAUDRATE` raised 115200 → 500000 in `actuator/fretboard_link.c`, comment rewritten to reflect the EDBG-CDC-bridge reality. | The fretboard PIC32 SERCOM1 was retuned to 500 000 baud (firmware/fretboard, 2026-06-02). The link rides the on-board EDBG USB-UART bridge, so the CDC SET_LINE_CODING value is what EDBG clocks out to the PIC32 — must match exactly or every byte is corrupt. The previous comment ("fretboard side ignores baud over USB CDC") was wrong; only matters that the *application* on the PIC32 doesn't see the CDC line-coding fields, but the EDBG bridge in between very much does. `tools/fret-tuner/actuator.py` still hardcodes `ACTUATOR_BAUDRATE = 115200` for the same physical link — flagged as a follow-up; out of scope for this commit (different subproject). |
| 2026-06-01 | Perf-log v3 schema bump: add `DETECTOR_CONFIG` (cv_marvin_v1 sample coords/thresholds/color filters), `ACTUATOR` (intended/asserted masks + `producer_id` + ack timing), extend `TIMING` to a per-frame snapshot (chord window mask, note/strum queue head + deadlines, release timers, frets_active), and add `PERF_TASK_IDLE` + `PERF_TASK_OTHER` pseudo-slots so per-window CPU% adds to 100%. | Wire's job pivots from "prove bytes survived and the pipeline isn't dropping" (v1/v2 integrity story, complete) to "show the host what the detection algorithm and actuator are thinking" (tuning visibility). Bundling four additions into one schema cut avoids three rounds of firmware/host sync. Granular MCC-task breakdown deferred — not needed at this time. Causal-trace `TIMING_EVENT`, ISR latency histograms, Cortex-A5 PMU counters, free-heap reporting also out of scope this round. Plan details in 2026-06-01 session log entry. |
| 2026-05-29 | Patch the UDPHS device driver to arm DMA for queued IRPs in completion ISRs. New helper `F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp` called after queue advance in `Tasks_ISR_DMA` and the ZLP-completion path. Logged as patch #9. | Stock Harmony USB v3.16.0 driver only programs the DMA channel in `IRPSubmit`'s queue-empty branch — IRPs appended to a non-empty queue link in the linked list but never get armed for transmission. Without this, `queueSizeWrite > 1` accepts writes but only ever transmits the first of any batch — we'd accept three perf-log strips, transmit one, the other two stranded forever. The IRP-queue infrastructure is *already there* in the driver, the missing piece is the post-completion arm. Fix is additive (new helper, two new call sites, no refactor of existing IRPSubmit body), making it easy to find/maintain after MCC regen. Future Harmony versions may obviate this if upstream wires it up. |
| 2026-05-29 | Strip queue uses 1-byte slot indices into a static `s_strip_pool[depth+2]` instead of pass-by-value `perf_rec_strip_t` items. | FreeRTOS queues are pass-by-value: every `xQueueSend`/`xQueueReceive` does `memcpy(slot, src, item_size)` inside a critical section (interrupts disabled). At `sizeof(perf_rec_strip_t) = 23-61 KB`, that was 38-100 µs of interrupts-off time per queue op × ~240 ops/s. Pool-and-index pattern moves the pixel data into a static pool indexed by 1-byte handles; queue ops drop to 1 µs critical sections. Same total BSS footprint (eliminates the producer's `static r;` and drain's `static s_strip_drain;` — pool size matches old "queue + 2 staging" footprint). Pool size `depth + 2` covers "1 slot in producer's hand + 1 in drain's hand" while preserving drop-on-full semantics. |
| 2026-05-29 | Fletcher-16 (mod 255, init 0xFFFF) replaces CRC-16/CCITT-FALSE in the framing layer. `crc_*` field names renamed to `fcs_*` end-to-end. | USB hardware already CRCs every bulk packet on the wire; our framing-layer checksum's job is *firmware-side framing-bug detection* and *resync alignment* (false SOF in the middle of a corrupt stream). Fletcher-16 catches single-byte changes, adjacent swaps, and most non-adjacent swaps with ~1/65536 false-positive rate against random byte streams — sufficient for that job at ~2 cycles/byte (vs ~25 cycles for the bit-by-bit CRC, ~6 cycles for table-driven). SAM9X75 has no CRCCU peripheral (verified against `packs/SAM9X75D2G_DFP/component/`), so DMA-driven CRC isn't available. Wire format unchanged (still SOF+LEN+payload+16-bit checksum). One-shot wire-format break — old `.bin` captures aren't readable with new code; fine for dev. |
| 2026-05-29 | Single `PERF_STRIP_MAX_BYTES = 65000u` constant; `PERF_STRIP_MAX_W`/`MAX_H` removed. | The W×H pair was arbitrary — producer code only ever checks total bytes (`w * h * BPP <= PERF_STRIP_MAX_BYTES`). Real binding constraint is the wire LEN field, which is `uint16_t` → max 65535 byte payload → max ~65 KB pixel per strip. Tighter than the UDPHS DMA cap (128 KB at `DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE = 2`). Picking 65000 lands just under the LEN cap with margin. Producer accepts any (w, h) shape under that — practical envelope at 60 fps: 720×30, 480×45, 320×64, 290×72. Going beyond requires bumping LEN to u32, which is a wire-format break and not worth doing casually since wire bandwidth caps out around the same point at 60 fps. |
| 2026-05-29 | Default-disable high-rate perf-log record types at firmware boot — start mask is `DROP | TASK_HIGHWATER | TASK_RUNTIME` only; STAMP, DETECTOR, TIMING, STRIP off until host enables. | Avoids the unattended-firmware case where boot generates multi-MB/s of strip records that all get dropped at the sink (no DTR), burning producer-side CPU on framing/Fletcher work that goes nowhere. Host viewer enables higher-rate types via `PERF_CMD_SET_TYPE_MASK` on attach (the live mode's mask UI). SESSION is always emitted regardless of mask so the host gets `timer_freq_hz` on attach. |
| 2026-05-21 | `configMAX_PRIORITIES` bumped 5 → 8; tasks re-tiered into 4 active bands (UI=2, diagnostic+perf-wire=3, vision=4, strum-critical+host-wire=5) plus 3 reserve bands (1, 6, 7) and idle (0). MCC tasks re-prioritised via the per-component yml files so future regens preserve the layout. | Pre-change layout had 11 tasks bunched at priority 1 (9 MCC pollers + VideoTask + DetectorDrain) and 4 at priority 2, with no headroom. Two concrete problems: (a) VideoTask was *below* its consumer CvMarvinV1, a classic priority inversion that would bite during any priority-2 busy spell; (b) Legato (UI rendering) at the same priority as USB host driver tasks meant a heavy frame redraw could stall fretboard wire transit. The new layout puts strum-critical output above vision-real-time above diagnostic-I/O above UI, which matches the failure-cost ordering: a late strum is a missed note, a late detector tick recovers next frame, a late UI paint is jitter only. Reserve bands cost ~60 B BSS each, leave room for future watchdog or fault-recovery work to land without re-shuffling. Setting priorities in MCC yml (rather than patching `tasks.c` post-regen) avoids a re-apply patch — the source of truth and the emitted code now agree. |
| 2026-05-21 | `manual_control` lands as the second producer of `fretboard_link`, alongside `timing_pipeline`. Mode-level arbitration is two-mode (`timing_pipeline` vs `manual_control`) and embedded in `ManualControl_SetEnabled`: entering manual mode calls `TimingPipeline_SetEnabled(false)` then takes the wire; exiting reverses. | First concrete validation of the 2026-05-20 multi-producer `fretboard_link` design — `FretboardLink_Send` stays the only entry point; arbitration lives one layer up. Embedded toggle is enough for two producers; refactor to a dedicated `actuator_mode` arbiter when a third producer (game-state controller §4.8) arrives. `timing_pipeline.publish_mask` keeps updating internal state when gated so the next ≤5 ms tick after re-enable republishes a correct mask without a stale frame. |
| 2026-05-21 | Legato chosen for the manual-control surface; widgets authored in Microchip Graphics Composer (regenerated into `le_gen_screen_Screen0.{h,c}`); marvin-side code is a thin event-binding shim (`ui/manual_input.c`) | Composer makes the layout iterable in a tool rather than C source, and keeps our code out of MCC-clobbered files: only the binding shim references the Composer-generated widget pointers. **Does not commit spec Q5 for the full operator UI** — that's still M5's call. The thin-shim shape works equally well behind a future custom GFX2D UI by swapping the bind module. |
| 2026-05-21 | Manual-control input model is single-finger momentary; multi-finger chord input is out of scope for this surface | Legato's input pipeline (`legato_input.c:340/429`) routes each touch to a single focus widget, so a second simultaneous touch cancels the first button's release and strands its `pressed` state. Workarounds (custom maxtouch dispatcher; toggle-style frets) are real but not worth the complexity here — primary use case is game-menu navigation, which is single-finger by nature. Revisit only if a user-facing flow needs held chords. |
| 2026-05-21 | `fret_t` lives in `game/fret.h`, a top-level domain header. Detector / actuator / UI all include it directly; detector.h re-includes it so `detector_state_t` still compiles cleanly. | The fret colors (G/R/Y/B/O) are a Guitar Hero / Rock Band domain primitive, not a detector concept — the actuator and UI shouldn't reach into `detector/` for them. `game/` also stakes out the directory the future game-state controller (spec §4.8) will land in, so we don't have a transient home. Header-only, no cmake change. |
| 2026-05-20 | Relax Microchip's host CDC-ACM Communications-interface protocol check to accept `bInterfaceProtocol == 0x00 (None)` in addition to `0x01 (AT_V250)` | The fretboard hardware exposes its UART to marvin via the on-board EDBG debugger's CDC ACM (composite VID=0x03EB PID=0x2175: HID + CDC ACM IAD + vendor + MSC). EDBG declares the comm interface with `bInterfaceProtocol=0x00` rather than the AT-command protocol Microchip hardcodes. The TPL/IAD match accepts the device (host stack does call `interfaceAssign`), but the per-interface check inside `F_USB_HOST_CDC_InterfaceAssign` then rejects the comm interface, the interrupt pipe never opens, the CDC instance flips to STATE_ERROR, and our attach handler never fires. Both are CDC ACM in practice and Linux/macOS accept either. Two-line OR per check in `usb_host_cdc.c`; logged as patch #7 in the re-apply list. |
| 2026-05-20 | `USB_HOST_Initialize` ordering relative to `DRV_USB_EHCI/OHCI_Initialize` and `Legato_Initialize` is **not** load-bearing — keep MCC's emitted order | A prior session hand-edited `initialization.c` to move `USB_HOST_Initialize` *after* the EHCI/OHCI driver inits on the theory that the host layer needed the HCD interfaces present at init time. Empirical retest with the CDC diagnostic logging in place: MCC's original order (`USB_HOST_Initialize` → `Legato_Initialize` → driver inits) enumerates and binds the CDC IAD just as well. The host layer evidently looks up HCD interfaces lazily at `USB_HOST_BusEnable` time, not at `USB_HOST_Initialize`. Reverted the hand-edit; one fewer re-apply patch to maintain. (This is unrelated to patch #6, which is the *peripheral* init order — MMU/AIC ahead of TC0/FLEXCOM6/XLCDC — that one is still a real regression and stays in the list.) |
| 2026-05-20 | Marvin replaces the fret-tuner PC as the runtime brain | One-host architecture. fret-tuner persists as an off-band dev/calibration tool. fretboard stays a dumb relay (ADC out, bitmask in). |
| 2026-05-20 | Marvin is the *reference detector* — not a debug aid | HDMI-direct CV is ground-truth for the system. Fretboard's photo-detection and any future Edge-AI MCU are trained against marvin's output. Recording reference data is therefore a first-class subsystem (spec §4.6), not an afterthought. |
| 2026-05-20 | Multiple coexisting detectors are in scope; one canonical detector-state bus | At minimum `cv_marvin_v1` (CV) and `adc_fretboard` (over UART ingest). Bus shape: typed `detector_state_t` records on a FreeRTOS queue, single-consumer (timing pipeline). Recording snoops via tee from each producer. Spec §4.2. |
| 2026-05-20 | Timing pipeline (chord window, FIFO, strum scheduling) lives on marvin by default; fretboard-takeover is a fallback mode | Centralized = single source of truth, easier multi-detector fusion. Latency budget allows a few ms of UART jitter. Fallback mode disables marvin's pipeline; fretboard runs its own existing `fret_button.c` and feeds emitted-command telemetry up to marvin for capture/display. Spec §4.4 + §6. |
| 2026-05-20 | Reference-data export = SD card, format = detector-state + sparse keyframes | SD chosen over Ethernet/USB CDC because primary consumers are Edge-AI training (offline) and replay/sanity-check (offline). Live side-by-side comparison deferred. Format: dense `detector_state_t` records for all detectors + raw BGRX32 keyframes every 60 frames (1.38 MB/s sustained, comfortable on Class-10). No software JPEG (Cortex-A5 can't keep up; SAM9X75 has no HW encoder). Spec §4.6. |
| 2026-05-20 | `frame_epoch` (32-bit, incremented per captured ISC frame) is the master sync token across recordings, detectors, and any future off-board observers | Need single shared timeline; off-board detectors (e.g. Edge-AI device) record in their own format keyed by marvin's `frame_epoch` so post-hoc training-data alignment is trivial. Wraps after ~830 days @ 60 Hz. |
| 2026-05-20 | Operating modes are independent toggles (`detect_enable`, `marvin_timing_enable`, `actuate_enable`, `record_enable`); named modes are presets over them | Matches `tools/fret-tuner/SPEC.md`'s detect/actuate split which has worked well in practice. Avoids a brittle global state machine. Spec §6. |
| 2026-05-01 | First real driver work is the TC358743 HDMI-to-CSI bridge | It's the gate between hardware-wired and pixels-in-memory. Everything downstream (vision, actuation) depends on it. |
| 2026-05-01 | Reference source = mainline Linux tc358743 driver + community 480p dumps | Linux driver has correct state machine / EDID / IRQ handling; community dumps have verified 480p register values. |
| 2026-05-01 | Integration shape (sensor vs standalone) deferred until Phase 3 | Avoid premature structure; real answer depends on how much state the driver actually needs. |
| 2026-05-01 | Journal lives at `firmware/marvin/docs/journal.md` | Firmware docs co-located with firmware; top-level `docs/` is hardware/mechanical. |
| 2026-05-01 | TC358743 I2C address: **`0x0f`** (7-bit) | Matches the in-tree kernel DT example (`imx6q-h100.dts:190`, `tc358743@f`) and the Waveshare v1/v2 default tie-off. Confirm by I2C bus scan during Phase 1 bring-up. |
| 2026-05-01 | REFCLK: **27 MHz** | Waveshare v1/v2 on-board XTAL. Kernel driver restricts REFCLK to {26, 27, 42} MHz (`tc358743.c:717-719`); 27 MHz is the Waveshare v1/v2 value. |
| 2026-05-01 | Target CSI-2 output format: **RGB888** (MIPI data type `0x24`) | Marvin pipeline is already configured for RGB888 end-to-end: `CSI_DATA_FORMAT_TYPE = CSI2_DATA_FORMAT_RGB888`, ISC input RGB 8-bit, CSI2DC pipes RGB888, ISC output ARGB32. Emitting YUV from TC358743 would require reconfiguring ISC/CSI2DC — staying with RGB is cheaper. |
| 2026-05-01 | Register-access helpers will be a direct port from the kernel driver | `tc358743.c:131-269` has clean `i2c_rd/wr` + `i2c_rd8/wr8/rd16/wr16/rd32/wr32`. Map to FLEXCOM6 TWI underneath. |
| 2026-05-01 | J29 (22-pin MIPI) pinout confirmed; PC19=pin 17, PC15=pin 18 (not 5/6) | Per SAM9X75-Curiosity User Guide §3.4.10 (queried via Microchip MCP). Pins 5/6 are MIPI_D1_N/P — CSI data lane 1 diff pairs, not GPIOs. Correct GPIO pins for RESET/INT are pin 17 (PC19, `MIPI_CSI_GPIO0`) and pin 18 (PC15, `MIPI_CSI_GPIO1`). I2C on pins 20 (PA25, `CAM_I²C_CLK`) and 21 (PA24, `CAM_I²C_DATA`) confirmed to match marvin's FLEXCOM6 pin assignment (`pin_configurations.csv:85-86`). |
| 2026-05-01 | Phase 1 will drive RESET via software (I2C `SYSCTL.Sreset`), not via GPIO | Even if PC19 reaches the 15-pin side through the adapter, Waveshare v1/v2 typically does NOT route TC358743 `RESET_N` out to the 15-pin FFC — the chip's reset is an on-board RC/POR network. Software reset over I2C is always available and is what the kernel driver uses. Means we don't need to validate the GPIO path before Phase 1 bring-up. |
| 2026-05-01 | Delays use Harmony's `SYS_TIME` service (non-blocking shape) | Already initialized in `SYS_Initialize`; existing marvin code (`drv_isc.c`, `plib_isc.c`) uses it with `SYS_TIME_DelayUS/MS` + `SYS_TIME_DelayIsComplete`. Non-blocking pattern inside our state machine keeps `APP_Tasks()` (and Legato) from stalling during reset hold times, and scales naturally into Phase 3's longer init sequence. |
| 2026-05-01 | Our I2C open uses `DRV_IO_INTENT_READWRITE`, not `EXCLUSIVE` | `EXCLUSIVE` is rejected by Harmony's I2C driver if any other client is already open. Even after libcamera is removed we prefer `READWRITE` so our module can coexist with any future MCC-generated I2C client. `DRV_I2C_CLIENTS_NUMBER_IDX0 = 2` in `configuration.h` already permits multi-client use. |
| 2026-05-01 | Libcamera (Camera Module + Image Sensor Driver + Vision Camera Library) to be removed from MCC | Conflicts with TC358743 bridge approach: libcamera expects a directly-attached sensor (IMX219/OV5640/etc.), probes them at boot (adds bus traffic + console noise), and configures ISC/CSI/CSI2DC with sensor-style assumptions that we'll need to override in Phase 3. Keeping CSI/ISC/CSI2DC peripheral/driver layers — we'll drive those directly from the TC358743 module. Greg regenerating MCC. |
| 2026-05-01 | Post-regen: keep a minimal `drv_image_sensor.h` shim at the original path | ISC driver (`drv_isc.c:15, 246, 257, 363-365`) and `configuration.h:106-107` reference `DRV_IMAGE_SENSOR_*` enum values that MCC didn't scrub when the image-sensor component was removed. Shim defines just the enum values used (exact numeric equivalence to originals). Lives at the MCC-generated path because `drv_isc.c`'s include is hardcoded — but MCC no longer regenerates that directory, so the shim is stable. |
| 2026-05-01 | `CAMERA_ENABLE_DEBUG=0` provided via `user.cmake` compile definition | Both `drv_csi.c:45` and `drv_isc.c:18` define `debug_print(...) if (CAMERA_ENABLE_DEBUG) fprintf(...)` and the symbol used to come from the deleted `camera.h`. Define it as a compile flag instead of editing `configuration.h` (MCC-regenerated) — `user.cmake` survives regens. Value `0` elides the debug prints entirely; if we ever need them, bump to `1`. |
| 2026-05-01 | `csiBitRate = 0x14` set from `isc_capture`, not MCC | `drv_csi.c` hardcodes `csiBitRate = 0x16` (wrong — testing showed 0x14 works). Rather than modifying MCC-generated code, we treat the MCC value as a default and override in `isc_capture` alongside the already-necessary `csiFrameWidth/Height/Fps` overrides. Only one MCC-file modification stays (`plib_csi.c` `CSI_Analog_Init` bug fix). |
| 2026-05-01 | ISC Bayer blocks auto-bypass for RGB input (previously an open question) | `drv_isc.c:363-365` disables CFA/WB/Gamma/CSC/Sub422/Sub420 whenever `inputFormat == DRV_IMAGE_SENSOR_RGB` — no MCC config changes needed despite `ISC_ENABLE_DPC/GDC/WHITE_BALANCE/GAMMA` remaining `true` in `configuration.h`. Those flags are Bayer-path only and are ignored in the RGB branch. |
| 2026-05-02 | Capture pipeline outputs BGRX32 natively (CSI2DC RMS=0 + ISC RLP BYPASS + DMA PACKED32) | Datasheet §49.6.54 + §50.6.19 + §50.6.20 show this is the only in-pipeline path to 32 bpp for MIPI RGB888 bypass. ARGB32 RLP mode is unusable on this path (requires CSC module output format). Alpha=0x00 is acceptable trade for zero-CPU capture; 0xFF can be added post-hoc if needed. Full rationale in `capture_pipeline.md`. |
| 2026-05-02 | Limited→full-range RGB expansion will be done in HEO CSC block on the display side only; capture buffer stays unexpanded for vision consumers | Both Wii and Pi emit RGB limited-range (16–235). Expanding on capture would cost CPU and break vision consumers by inflating values without adding information (linear rescale carries no new signal). LCD display needs full-range 0–255 or it looks muted, so the HEO CSC matrix (programmable `M × in + offset`) will be configured as an identity RGB→RGB with limited→full gain/offset. Zero CPU, display-only, capture untouched. See `display_path.md` §5. |
| 2026-05-20 | M1 detector scaffolding: one task per detector, single shared bus queue (`xDetectorStateQueue`), temporary in-module drain task until the timing pipeline (M3) lands | Matches spec §4.2.4. Per-detector tasks isolate slow detectors from the frame pipeline; the shared queue is the hand-off point that the timing pipeline will eventually own as sole consumer. The drain task lives in `detector.c` so it travels with the bus and can be deleted in one place when M3 wires the real consumer. Records-per-second log line gives a coarse health signal during bring-up. |
| 2026-05-20 | `frame_epoch` is sourced from `Video_FrameInfo.frame_count` for M1; a persistent counter that survives ISC restart is M5's problem | Recording (M5) is the first consumer that actually needs cross-restart continuity. Until then the post-arm-monotonic counter is fine for plumbing validation, and pretending otherwise would mean threading another counter through the video module before it has a real consumer. |
| 2026-05-20 | Detector control model: per-detector `enable` (multiple may be on simultaneously for side-by-side recording) + single `active` selector (which detector_id the timing pipeline acts on) | Want side-by-side training data — every enabled detector publishes onto the bus; recording (M5) saves them all. But only one detector should ever drive actuation, so the timing pipeline (M3) filters the bus by `Detector_GetActive()` and ignores the rest. Cleaner than a global enum because it decouples "is this running?" from "is this canonical?", and lets us hot-swap the active detector without disabling its peers. Defaults: all-disabled, active=cv_marvin_v1. App explicitly calls `Detector_Enable` + `Detector_SetActive` after init. |
| 2026-05-20 | Non-detector vision tasks (menu navigation, score reading, etc.) are video-frame consumers, not detectors — `detector_state_t` stays narrow to fret-press shape | Forcing menu/score readers through `detector_state_t` would either abuse `fret[]` as untyped data or push the canonical record toward a tagged-union shape that hurts the recording layout. Cheaper to keep two separate concepts: (a) "video frame consumer" — anyone subscribing to the video module's frame queue, output type and downstream consumers are theirs to define; (b) "detector" — narrow note-timing emitter onto `xDetectorStateQueue`. cv_marvin_v1 happens to be both. Implication: `Video_SubscribeFrames` needs to go from single- to multi-subscriber before the second video CV task lands. |
| 2026-05-20 | `Video_FrameInfo.buffer` now reflects the buffer that just completed (per-frame routing); HEO is re-pointed in the ISR every frame so the panel sees every captured frame | Pre-fix: `ISC_Capture_GetBufferAddress()` returned the framebuffer base regardless of which descriptor-ring slot ISC just wrote, and HEO was bound once to the base — so HEO only ever read slot 0, and slot 1 was written but never observed. Effective behavior was ~half-rate display. Fix: ISC IRQ computes `slot = (frameIndex - 1 + N) % N` (driver increments before callback per drv_isc.c:47-50), passes `base + slot * frame_size` into the user callback, and video.c re-points HEO at that address with `XLCDC_SetLayerAddress(..., update=true)` — latches at next vsync. Also unblocks ring depth > 2 since each subscriber sees the genuinely-just-written buffer. |
| 2026-05-20 | `Video_SubscribeFrames` is multi-subscriber (max 4), each subscriber chooses its own queue depth + back-pressure policy | Need parallel video CV tasks (note detector + recording + future menu/score readers) on one capture pipeline. Static array of 4 `QueueHandle_t` slots; ISR fans out one `xQueueSendFromISR` per subscribed slot. Aligned-pointer slot reads/writes are atomic on Cortex-A5 so the IRQ can scan without locking; `taskENTER_CRITICAL` only needed in `Subscribe`/`Unsubscribe` to prevent two task-side writers racing on the same empty slot. Producer always uses `xQueueSendFromISR` (drop-on-full); consumer chooses queue depth — depth 1 = "newest only", deeper = "preserve every frame". |
| 2026-05-20 | All marvin-owned FreeRTOS objects use `xTaskCreateStatic` / `xQueueCreateStatic` / `xSemaphoreCreateMutexStatic`; FreeRTOS heap stays at heap_1 only because MCC code (XLCDC/MAXTOUCH/LEGATO/SYS_INPUT tasks, OSAL, Legato variable heap) needs it | Project rule is static-only memory allocation. MCC-generated code can't be made static without forking, so we keep `configSUPPORT_DYNAMIC_ALLOCATION = 1` for MCC and route all our own code through the static APIs. MCC reconfigured to enable `configSUPPORT_STATIC_ALLOCATION = 1` and emit `vApplicationGetIdleTaskMemory` in `freertos_hooks.c`. Per-module `Static{Task,Queue,Semaphore}_t` plus stack/storage arrays at file scope. |
| 2026-05-20 | ISC framebuffer pool moved from `.region_cache_aligned` to `.region_nocache`; max capture cap dropped 1920×1080 → 1280×720 to fit | First CPU consumer of frame data (cv_marvin_v1) needed cache-coherent reads. Two paths considered: invalidate D-cache per frame in the ISR, or move the pool to the linker's already-defined uncached region. Nocache is simpler and bug-resistant — every reader sees DMA-fresh bytes without explicit maintenance. Trade is full-DDR-fetch on every read; fine for sparse 5×5 patch sampling, would matter for a full-frame scan. `ram_nocache` is 16 MB so the 4-buffer × 1080p pool (≈24 MB) had to shrink; capping at 720p (≈11 MB) matches our current TC358743 ceiling and leaves headroom. Reverting to 1080p later requires either growing `ram_nocache` in `ddram.ld` or going back to cached + manual invalidate. |
| 2026-05-20 | cv_marvin_v1 keeps detector internals (press_count, raw color distances) private; bus record stays canonical (`pressed[] / confidence / raw_value`) | Different detectors need different per-frame data, but downstream bus consumers (timing pipeline, recorder, fretboard link) only care about "is this fret pressed at frame N." Pushing detector-specific richness onto the bus would either grow the canonical record or force a tagged-union shape that complicates recording layout. Encapsulating internals in the detector module preserves the narrow bus and lets each detector evolve its own state independently. press_count specifically lives in a private `s_press_count[FRET_COUNT]` array; if the strum scheduler later wants it, expose via a getter or a second "detail" channel. Recording-format choice (custom packed binary vs nanopb vs CBOR) deferred to M5. |
| 2026-05-20 | cv_marvin_v1 sensor coords are in native capture-frame space, no 1920×1080 reference scaling | fret-tuner detect_video.py carries a 1920×1080 reference frame and scales coords at runtime — that frame size is what the Elgato HDMI-to-USB bridge produced, not a per-camera variable. Marvin uses the TC358743 (different bridge chip, same direct-pixel topology) and captures at the source's native resolution. The Wii is the primary production source at 480p60 = 720×480, so the static defaults are scaled from Python's 1920×1080 values by (3/8, 4/9) once at port time and stored in 720×480 space. Same-pixel-strike-line geometry as the Python detector — these are real coords, not placeholders. Calibration UI (M6) will refine them. |
| 2026-05-20 | Fretboard-link submit queue belongs to `fretboard_link`, not `timing_pipeline`; producers call `FretboardLink_Send(mask)` | Game menu control (spec §4.8) needs to drive the actuator without going through the chord-window scheduler — its inputs are "navigate menu, press start", not gameplay notes. If the queue lives in `timing_pipeline`, the controller has to either reach across modules or duplicate transport. Moving ownership down to `fretboard_link` makes timing_pipeline one of N producers; the menu controller and any manual-test path call the same `Send` API. Latest-wins (depth-1 + xQueueOverwrite) already gives the right behavior — newest submitter wins on the wire. **Mode-level arbitration** (which producer is allowed to submit when, e.g. timing-only during gameplay vs controller-only in menus) lives one layer up; the link stays dumb. |
| 2026-05-20 | M2 fretboard link split into three layers — `detector → timing_pipeline → fretboard_link` — with the timing FIFO living on marvin and a 1-byte bitmask wire format | (a) Three-layer split keeps each concern small: detector emits state, timing_pipeline owns chord-window + strum scheduling, fretboard_link owns the USB CDC byte channel. Each can be replaced independently (different detector, different actuator transport) without touching the others. (b) Timing FIFO on marvin, not fretboard: marvin already runs the canonical detector and the recording pipeline, so co-locating timing keeps a single source of truth and avoids re-deriving state on the actuator side. Spec §4.4. The fretboard-takeover fallback (spec §6) is a separate mode where marvin disables its pipeline and fretboard runs `fret_button.c` standalone — that's the high-availability path, not the steady-state. (c) 1-byte bitmask matches `tools/fret-tuner/actuator.py` and `firmware/fretboard/cmd_receive.h` exactly (G/R/Y/B/O at bits 0..4, strum-down bit 5, strum-up bit 6) — no reason to invent a new framing while the protocol is this small. Latest-wins semantics on the marvin→fretboard queue (depth 1 + overwrite) so a stalled USB write can't accumulate stale chords. (d) **Actuator-latency compensation deferred until mechanical actuators arrive.** Today's actuator is direct GPIO assertion on the fretboard PCB — latency ≈ 0, so today's design is sound. Future paths considered: (1) marvin compensates by subtracting a fretboard-published latency from `assert_at`/`strum_at`; (2) fretboard compensates by accepting timestamped events and scheduling locally. Path 2 needs a richer wire format (event + abs-timestamp); path 1 keeps the byte protocol but pushes the timing math up. Pick when we know which actuator (voice coil / electromagnet / DIY solenoid) we're driving. |
| 2026-05-20 | Game-state awareness & high-level game control is a marvin subsystem (spec §4.8). Distinct from the gameplay note-detection path: a video-frame consumer (not a detector) emits typed game-state events on `xGameStateQueue`, and a controller layer translates verbs ("start single-player song X") into fret/strum sequences sent over the same fretboard link | Want marvin to drive whole-session play, not just notes once gameplay starts. Game-state observation belongs on marvin because it's CV on the same captured frames. Separating its bus from `xDetectorStateQueue` keeps `detector_state_t` narrow per the 2026-05-20 multi-detector decision. Per-game catalog (menu graph + song list) is metadata kept on marvin. Default arbitration with timing pipeline: mutually exclusive — controller runs only outside `gameplay` state. Phasing: M9 observer-only, M10 control. Open: recognizer algorithm (Q10), arbitration boundary (Q11). |

---

## Open questions

_(Questions we haven't answered yet. Move to decision log with rationale once resolved. System-level open questions are tracked in [`spec.md`](spec.md) §9 — anything tagged with `Q1..Q9` there.)_

- ~~CSI-TX minimum bitrate at 480p60~~ — **resolved 2026-05-01**: we ended up dropping to 297 Mbps/lane (below the kernel's tabulated 594 Mbps) with no problems. 297 pairs with host HSFREQRANGE band 0x14 and the kernel's 594 Mbps D-PHY timings happen to still be safe at this rate.
- ~~`TXACT` absent in CSI_STATUS after Phase 4 enable~~ — **deferred 2026-05-01**: TXACT has been observed at both 0 and 1 at various times after enable. Likely momentary (active-packet indicator, not stream-on flag). CSI_ERR=0 and HLT=0 consistently. Phase 5 frames-in-memory confirmed the chain works regardless.
- ~~Source mode variability~~ — **explained 2026-05-01**: 1440x240p observation was Wii post-reset default. Put Wii in 480p mode; we stay on 720x480p@60.

**Carried into future sessions:**

- **`vApplicationStackOverflowHook` is a silent infinite-loop.** ([freertos_hooks.c:62-76](../default/src/config/default/freertos_hooks.c#L62-L76)) MCC's default is `taskDISABLE_INTERRUPTS()` then `for (;;)`. An actual overflow is indistinguishable from any other freeze on hardware. Patch the hook to emit a `LOG_ERR` line naming the offending task before the spin (carefully — the stack is already corrupt, so the hook should avoid using locals or large stack frames). Already nearly bit us once: 2026-05-21 freeze in `open_cdc()` was first misdiagnosed as a stack overflow because we had no visibility into actual stack usage of the FBL task. (HWM/RUNTIME records reach the host now post-2026-05-29 work, so we have *steady-state* stack visibility — but a real overflow still freezes silently.)

- **Legato `LE_MEMORY_MANAGER_SIZE` adequacy.** Legato has its own internal pool (`LE_MALLOC` per touch event in `leInput_InjectTouchDown`). With the manual-control surface adding 8 buttons and frequent press/release events during menu nav, confirm `legato_config.h` `LE_MEMORY_MANAGER_SIZE` has headroom for typical event bursts. Watch for Legato heap-exhaustion symptoms (silent dropped events, widget redraw glitches) once the UI is exercised on hardware.

- **Single-screen attach assumption for `ui/manual_input`.** `screenHide_Screen0` deletes the root widget tree, taking the Composer-generated `Screen0_*` widget pointers with it. We bind callbacks once after the first `screenShow_Screen0`. If a second Legato screen is ever added, `ManualInput_Bind` must re-run inside that screen's show path (or whichever screen owns the manual-control widgets). Not an issue today — flagged for whenever the operator UI grows beyond Screen0.

- ~~**Spec ↔ implementation drift on capture format.**~~ **Resolved 2026-06-01** in doc sweep: spec.md, capture_pipeline.md, and display_path.md all updated to reflect BGR888 packed (3 B/pixel, CSI2DC RMS=1), HEO layer (not OVR1), RGB\_888\_PACKED color mode, and `.region_nocache` framebuffer. Keyframe size corrected to 1.04 MB (720×480 × 3). README.md updated with project intro.

- **MCC-file modifications maintenance risk.** A from-scratch MCC regen on 2026-05-15 confirmed **four** local modifications get clobbered. The 2026-05-20 USB-host regen added a fifth. Re-apply after every regen:

  1. **`plib_csi.c` — `CSI_Analog_Init` refactor.** MCC emits per-lane HS-RX init inline with three bugs: Lane 1 in the 2-lane branch is missing the bit-rate write, Lane 2's lane-select code is `0x24` instead of `0x64`, and the 4-lane else-if branch skips Lane 1 entirely. Replace with the `csi_phy_hs_rx_init(lane_code, bit_rate)` helper and call it for lanes 0/1/2/3 from a flat `if (nlanes >= …)` chain.

  2. **`plib_xlcdc.c` `XLCDC_EnableClocks` — `PMC_PLL_ACR`.** Set to datasheet-optimal `0x12023010` for the LVDSPLL with our 24 MHz reference (in the `fIN ∈ [20 MHz, 32 MHz]` band). Specifically: `LOOP_FILTER=0x12, LOCK_THR=0x2, UTMIBG=1, UTMIVR=1, CONTROL=0x10`. MCC's default emits a different (less-optimal) analog config (`LOOP_FILTER=0x1B, LOCK_THR=0x4, no UTMI bits`). Doesn't affect functional behavior in our use (50 Hz refresh works either way), but is the manufacturer-recommended jitter/lock optimum.

  3. **`plib_csi2dc.c` `CSI2DC_Configure_VideoPipe` — `| CSI2DC_VPCFGR_RMS_1`.** Required by the RGB888-packed capture path: byte-stream packs 4 BGR pixels across 12 bytes per CSI-2 RMS spec (Table 49.27). Without it, capture writes wrong-format bytes and the display shows garbage. *(Earlier journal note claimed this was now MCC's default — incorrect; this regen confirmed MCC still emits without RMS_1.)*

  4. **`drv_image_sensor.h` enum shim.** MCC's regen *deletes* this file when the image-sensor component is disabled. Restore the 30-line shim at `default/src/config/default/vision/drivers/image_sensor/drv_image_sensor.h` defining `DRV_IMAGE_SENSOR_RAW_BAYER…JPEG` and `DRV_IMAGE_SENSOR_8_BIT…40_BIT` enums (numeric values must match the original — `drv_isc.c` derives `bits_per_pixel = 4 - inputBits`). Without it, `drv_isc.c` and `configuration.h` won't compile.

  5. **`initialization.c` `DRV_USB_VBUSPowerEnable` — per-pin VBUS calls.** **(Obsolete on the SAM9X75 Curiosity Hybrid — no USB host; the VBUS enables were removed from `app.c` on 2026-06-08.)** MCC emits a single `VBUS_AH_*_Set/Clear()` call expecting one pin named `VBUS_AH`, but our config has VBUS on two pins (PC27 + PC31, one per USB port). The pin-macro generator produces `VBUS_AH_PC27_PowerEnable_*` and `VBUS_AH_PC31_PowerEnable_*` separately and no unified wrapper, so the MCC-emitted code fails to compile. Replace lines 162-163 (Set branch) and 169-170 (Clear branch) with explicit calls to both per-pin macros — `VBUS_AH_PC27_PowerEnable_Set(); VBUS_AH_PC31_PowerEnable_Set();` and the matching Clear pair. The MCC comment in the function ("name it to 'VBUS_AH'") acknowledges the single-pin assumption.

  6. **`initialization.c` `SYS_Initialize` — peripheral init order.** **(Hybrid board: the peripheral is now `FLEXCOM8_TWI_Initialize` (not FLEXCOM6) and there's no USB-host regen, but the MMU/AIC-before-peripherals concern still stands — verified intact on 2026-06-08: MMU/AIC run before TC0/FLEXCOM2/FLEXCOM8/XLCDC.)** The USB-host MCC regen (2026-05-20) reordered SYS_Initialize so that `TC0_CH0_TimerInitialize`, `FLEXCOM6_TWI_Initialize`, and `XLCDC_Initialize` run *before* `MMU_Initialize` and `AIC_INT_Initialize`. Symptom on hardware: TC358743 probe wedges on its first I²C write — `OSAL_SEM_Pend(transferDone, WAIT_FOREVER)` never returns because the FLEXCOM6 ISR never fires. (Display + capture totally dead; FBL heartbeat keeps printing because it doesn't depend on a peripheral interrupt.) Fix: move the `MMU_Initialize → AIC_INT_Initialize → WDT-disable` block back to *before* the TC0/FLEXCOM6/XLCDC inits, matching the pre-USB-regen order. After this revert, video came back immediately. Resolved on 2026-05-20.

  7. **`usb_host_cdc.c` — accept `bInterfaceProtocol == 0` in CDC ACM Communications-interface match.** **(Obsolete on the SAM9X75 Curiosity Hybrid — the USB host stack was removed in `598d370`; this patch only matters on a host-capable board.)** Two checks (single-interface path ~line 677, IAD path ~line 798) currently require `bInterfaceProtocol == USB_CDC_PROTOCOL_AT_V250` (`0x01`). EDBG-style USB-to-UART bridges (the on-board PIC/AVR debugger CDC, common across Microchip dev boards — including the fretboard board this project uses) declare the comm interface with `bInterfaceProtocol = USB_CDC_PROTOCOL_NO_CLASS_SPECIFIC` (`0x00`). Without this patch the host CDC driver accepts the IAD via the TPL match (TPL ignores subclass/protocol) but rejects the comm interface inside `F_USB_HOST_CDC_InterfaceAssign`, the interrupt pipe never opens, the CDC instance flips to `STATE_ERROR`, and the app-level attach handler never fires. Patch: extend each check to `(... == AT_V250 || ... == NO_CLASS_SPECIFIC)`. Two one-line OR additions. Re-apply after every USB-host MCC regen.

  8. **`FreeRTOSConfig.h` — `portCONFIGURE_TIMER_FOR_RUN_TIME_STATS` / `portGET_RUN_TIME_COUNTER_VALUE` macros + 64-bit counter type.** With `configGENERATE_RUN_TIME_STATS = 1` (set via `FREERTOS_GENERATE_RUN_TIME_STATS` MCC symbol), FreeRTOS expects the application to provide a free-running counter. MCC's FreeRTOS Harmony component does not expose a knob for an app-specific counter source. Append at the end of `FreeRTOSConfig.h` (just before `#endif`):
     ```c
     extern uint64_t SYS_TIME_Counter64Get(void);
     #define configRUN_TIME_COUNTER_TYPE             uint64_t
     #define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
     #define portGET_RUN_TIME_COUNTER_VALUE()        SYS_TIME_Counter64Get()
     ```
     `SYS_TIME` is already initialised by Harmony before the scheduler starts, so the configure macro is a no-op. `SYS_TIME_Counter64Get` returns the full 64-bit TC0 CH0 counter; the matching `configRUN_TIME_COUNTER_TYPE = uint64_t` overrides FreeRTOS's `uint32_t` default so cumulative `ulRunTimeCounter` values don't wrap (uint32_t at 266 MHz wraps after ~16 s of accumulated CPU time across all tasks, which made percentages garbage almost immediately). Bare `extern` (rather than including a Harmony header) keeps `FreeRTOSConfig.h` consumable by low-level kernel sources that don't pull in `definitions.h`.

  9. **`drv_usb_udphs_device.c` — multi-IRP DMA arming in completion ISRs.** Stock UDPHS device driver only programs the DMA channel in `IRPSubmit`'s queue-empty branch (line 1604+); IRPs appended to a non-empty queue link in (line 2273-2284 of patched file via `iterator->next = irp_t`) but never get armed for transmission. `Tasks_ISR_DMA` advances `irpQueue = irp->next` after firing the callback but does not re-program DMA hardware. Result: only one IRP per "queue idle" sequence ever transmits — the rest sit `STATUS_PENDING` forever.

      Fix is additive (no refactor of working code): add a static helper `F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp` just before `DRV_USB_UDPHS_DEVICE_IRPSubmit`, mirroring the existing inline DMA-program block (~80 lines covering both DEVICE_TO_HOST and HOST_TO_DEVICE directions). Then add a call after the queue advance in **two** places:

      - `Tasks_ISR_DMA` immediately after `endpointObj->irpQueue = irp->next; irp->callback(...)` — the normal DMA-completion path.
      - The ZLP-completion path inside `Tasks_ISR` (under "endpoint interrupt on a DMA capable endpoint, so it should be a ZLP"), same place: after queue advance.

      Both call sites guarded by `if (endpointObj->irpQueue != NULL)`. Patch markers `/* PATCH: */` make the change findable after MCC regen. Helper carries the same cache-clean and DMA-program shape as the inline original, including the `__DSB(); __ISB();` barriers and the `SYS_CACHE_CleanDCache_by_Addr` for the IRP data buffer.

      Without this patch, the lever-1 multi-IRP work (`queueSizeWrite=3`, `USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED=5`) accepts writes but only ever transmits the first of any batch. With it, multi-IRP pipelining works as designed at the driver level. Patch is specific to Harmony USB v3.16.0 device driver; check whether future versions wire the post-completion arm and obviate the patch.

  Previously listed but now resolved or moot:
  - ~~`plib_xlcdc.c` LVDSPLL multiplier~~ — MCC now emits the chosen `MUL/FRACR/DIVPMC` for our 50 Hz target once the XLCDC driver MCC config was set correctly. Manual override no longer needed.
  - ~~`plib_lvdsc.c` `LVDSC_CFGR.DEN_POL`~~ — Latest Harmony gfx library intentionally omits the DEN_POL field. File reverted to MCC default; not load-bearing.

  Recovery plan: re-apply all nine (small, self-contained diffs). Long-term options are (a) file MCC bugs, (b) shim into our own files, (c) live with periodic re-application.

- **`log_csi_status` was removed** (Phase 5 restructure — tc358743 no longer auto-enables stream). If we ever want to re-query TC358743 CSI_STATUS/CSI_ERR bits, re-add the helper. The register addresses and masks are still defined in the file.

- **ISC_Capture framebuffer sized for max 1920×1080 × 2 (~16 MB)** — wastes DDR at our current 720×480 use, but trivial at 256 MB total DDR and gives headroom for different sources. Reconsider if DDR becomes tight later.

- **Retry semantics on source loss.** `app_coordinate_capture` uses a `capture_attempted` flag to prevent retry spam. On unlock it resets, so the next lock triggers a fresh Configure+Start. Untested for fast lock/unlock cycles — if the Wii toggles power, watch for state-machine glitches.

- **Frame counter wraps at UINT32_MAX.** `g_frame_count` is 32-bit; at 60 fps it wraps after ~2.3 years continuous run. Not an immediate issue.

- **`TP_STRUM_DELAY_MS` likely needs to vary by difficulty.** 220 ms tuned well on Expert (notes are dense, the delay lines up against fast-moving notes near the strike line). Easy/Medium/Hard place notes higher up the highway with longer travel time, so the same 220 ms may strike too early. Open: per-difficulty preset, runtime-tunable from the manual-control surface, or auto-tuned from observed note-velocity. Defer until the game-state controller (spec §4.8) lands and difficulty is known to marvin — until then, expert-tuned 220 ms is the working default.

---

## Session log

### 2026-06-08 — FT4232H EEPROM: identity + driver flags (macOS ignores VCP bit)

Follow-on to the JTAG bring-up below. Tried to stop the JTAG channel (A) and the two unconnected channels (B/D) from showing up as `/dev/cu` serial ports by setting their EEPROM driver to D2XX, keeping only the DBGU (C) as VCP.

- **Found the EEPROM blank.** `ftdi_eeprom --read-eeprom` errored with a checksum error; a raw `ftdi_read_eeprom_location` loop showed all 256 words = `0xFFFF`. So the 93LC46B (confirmed present on sch sheet 14, U9) was unprogrammed → FT4232H ran on defaults ("Quad RS232-HS", all 4 channels VCP).
- **Confirmed the channel map from the schematic** (sheet 14, `SAM9X75 cHybrid-REV1_SCH.PDF`): A=JTAG, C=DBGU (R149/R151 populated), B+D unconnected (B's `DBGU_*_FTDI` bridge resistors are DNP). I initially misread the `pdftotext` extraction and tied DBGU to B; Greg corrected from the schematic — it's C.
- **Authored + flashed** [`ft4232h-chybrid.conf`](../ftdi/ft4232h-chybrid.conf) (A/B/D=D2XX, C=VCP; `Microchip`/`SAM9X75 cHybrid`/`W16-2026-359`). Validated with `--build-eeprom` dry-runs + a `cha_vcp` true/false byte-diff (bit 3 of EEPROM byte `0x00`) before writing. Write succeeded (`FTDI write eeprom: 0`), read-back confirmed the image on-chip, and the device re-enumerated with the new strings/serial without a replug.
- **Result:** macOS 26 (Tahoe) still shows **4** ports (`/dev/cu.usbserial-W16_2026_3590..3`) — `AppleUSBFTDI` does not consult the per-channel VCP flag. Port-hiding is not achievable on macOS this way; kept the flash for the identity/serial benefit (see decision log). JTAG re-verified working post-flash (halt at pc≈0x0030dc40).

- **Flashed all three boards** with unique serials and JTAG-verified each: `W16-2026-359`, `W16-2026-430`, `W16-2026-413` (all halt cleanly, TAP `0x0792603f`).
- **Tooling, split by concern:** EEPROM programming lives in `firmware/marvin/ftdi/` (`ft4232h-chybrid.conf` base image + `flash-eeprom.sh <SERIAL>`, which overrides the serial and flashes the connected board — run one board at a time since `ftdi_eeprom` selects by VID/PID); JTAG debug lives in `firmware/marvin/openocd/` (`sam9x75-chybrid.cfg`, supports `-c "set FTDI_SERIAL <serial>"` to pick a specific board via `adapter serial`). Each folder has its own README. Scratch `ft4232h-read.conf` + built `.bin` were removed; host-side libusb/libftdi probes (`/tmp/usblist.c`, `/tmp/eepread.c`) were one-offs.

**macOS sandbox gotcha (carried over):** all `ftdi_eeprom`/`openocd`/libusb calls must run outside the Claude command sandbox — it blocks USB enumeration.

### 2026-06-08 — JTAG debug bring-up on the Curiosity Hybrid (OpenOCD + onboard FT4232H)

Goal: program/debug the SAM9X75 over the board's onboard FT4232H JTAG (channel A) instead of treating it as four serial ports. The board enumerates as FTDI "Quad RS232-HS" (`0403:6011`) with all four channels grabbed by Apple's in-kernel FTDI driver as `/dev/cu.usbserial-100..103`; the worry was that a driver swap would be needed on macOS (26.5.1, Apple Silicon).

- **pyOCD ruled out**, OpenOCD chosen — SAM9X75 is ARM926EJ-S, and pyOCD is Cortex-M/CMSIS-DAP only and can't drive raw FTDI MPSSE. Homebrew `openocd` 0.12.0 + `libusb`/`libftdi` already installed.
- **Red herring diagnosed:** first `openocd`/`ftdi_eeprom` runs reported "device not found", and a libusb probe saw **0 USB devices total** even though `ioreg`/`system_profiler` listed the FTDI. Cause = the Claude Code **command sandbox blocks libusb USB enumeration**; `ioreg` works because it reads the IORegistry. Re-running the libusb probe with the sandbox disabled: device visible, `libusb_open` + `libusb_claim_interface(0)` both succeed. So Apple's driver does **not** block channel A — no driver swap, no EEPROM reflash.
- **Wrote [`firmware/marvin/openocd/sam9x75-chybrid.cfg`](../openocd/sam9x75-chybrid.cfg)** (FTDI channel 0, MPSSE JTAG pinout, `arm926ejs` target, `reset_config none`). Probe result: TAP IDCODE `0x0792603f` (ARM926EJ-S), EmbeddedICE v6, 2 HW breakpoints, gdb server on :3333, **halt/resume verified** — CPU halted from internal SRAM (pc≈0x0030d8ec, Supervisor/Thumb, MMU off). OpenOCD logs a harmless `libusb_detach_kernel_driver ... LIBUSB_ERROR_ACCESS` warning, then proceeds.

**Caveat for future sessions:** to run OpenOCD from a Claude `Bash` call you must disable the command sandbox (it blocks USB); from Greg's own terminal there's no sandbox, so plain `openocd -f firmware/marvin/openocd/sam9x75-chybrid.cfg` works.

**Open follow-ups:** (1) nTRST/nSRST aren't in the MPSSE byte layout — fine for halt/resume, but reset-halt / flashing may need them wired (check the Hybrid schematic for the FT4232H ADBUS4-7 → reset mapping). (2) No SAM9X7-specific board cfg in mainline OpenOCD; MPLAB X bundles an OpenOCD with SAM9X7 support if richer target/flash support is wanted. (3) DDR/flash-load init scripts not written — current config only does core debug.

### 2026-06-08 — fretboard link: USB CDC host → FLEXCOM2 USART (Curiosity Hybrid port)

Part of the ongoing port to the **SAM9X75 Curiosity Hybrid** board. The Hybrid has no host-capable USB port, so the fretboard link can't ride the EDBG CDC-ACM bridge anymore. Greg's prior MCC work on this branch (`598d370` removed USB Host + moved display/MIPI I²C to FLEXCOM8; `3af09bc` added FLEXCOM2 for the guitar serial link, pins `PA13 GUITAR_TX` / `PA14 GUITAR_RX`) left the firmware mid-migration: FLEXCOM2 was initialized but unused while `app.c` still called `USB_HOST_BusEnable` and `fretboard_link.c` still drove the (no-longer-initialized) USB host stack. This session finished the cleanup.

- **Regenerated FLEXCOM2 USART in ring-buffer mode** (RX ring 512 B ≈125 ms headroom at 4080 B/s, TX ring 64 B; baud 500 000 8N1 — `BRGR CD=66 FP=5`, 8× oversampling ≈ 500 312 Bd, 0.06 % error). Ring-buffer over the basic single-transfer plib so the continuous 240 Hz RX stream has no re-arm gap to overrun on.
- **Rewrote [`actuator/fretboard_link.c`](../default/src/actuator/fretboard_link.c)**: dropped all `usb/*` includes, the CDC handle/attach/detach machinery, `open_cdc`/`close_cdc`, line-coding + DTR control-line-state, and the `s_rx_stream` stream buffer. TX = `FLEXCOM2_USART_Write(&byte,1)` (synchronous enqueue, no ack semaphore). RX = persistent read-threshold (`DS_FRAME_LEN`=17) notification → `rx_event_handler` (ISR) gives `s_rx_notify` → `fretboard_rx_task` drains the ring via `FLEXCOM2_USART_Read`/`ReadCountGet` and runs the unchanged `0x03 … 0xFC` resync + `PerfLog_EmitFretboardRaw`. `s_connected` → `s_link_up` (a wire is always "up"). Public API and all perf-log emits preserved.
- **Did NOT emit `PERF_STAGE_FBL_READ_COMPLETE`** — first draft did, but [`perf_log_records.h:52`](../default/src/perf_log/perf_log_records.h#L52) shows that stage was deliberately removed (per-Read emit at 240 Hz doubled timeline markers and froze the live viewer; FRETBOARD_RAW carries its own `ts_counter`). Left it out.
- **[`app.c`](../default/src/app.c)**: removed `#include "usb/usb_host.h"`, `app_usb_host_event_handler`, the `VBUS_AH_PC27/PC31` enables, `USB_HOST_EventHandlerSet`, and `USB_HOST_BusEnable`. `FretboardLink_Initialize` stays in the same spot.
- **[`fretboard_link.h`](../default/src/actuator/fretboard_link.h)**: header comment rewritten (USB CDC host → FLEXCOM2 ring-buffer UART).

**Verification:** both changed TUs compile clean with the project's XC32 v4.60 invocation (via `compile_commands.json`, `-fsyntax-only`); no `USB_HOST`/`VBUS_AH` references remain in `app.c` or `actuator/`. Full link not run here (no `ninja`/`cmake` in this environment) — **needs a full MPLAB build + on-hardware bring-up to validate** (frame rate at the `fretboard_rx_task` debug line should read `parsed=240/s skipped=0`, and gameplay should actuate as before).

**Re-apply patch list cleanup** (see "MCC-file modifications maintenance risk" below): patches **#5** (per-pin VBUS) and **#7** (`usb_host_cdc.c` `bInterfaceProtocol==0`) are **obsolete on the Hybrid board** — both are USB-host-only and there's no USB host anymore. Patch **#9** (UDPHS *device* multi-IRP) still applies — it's the perf-log device path, unaffected. Annotated inline below.

**Also reconciled FLEXCOM6 → FLEXCOM8 doc drift this session.** The broader Hybrid port moved TC358743 control I²C (and display MIPI I²C) off FLEXCOM6/PA24-PA25 to FLEXCOM8 TWI/PB4-PB5 (`LCD_MIPI_SDA/SCL`, `DRV_I2C_INDEX_0` → `FLEXCOM8_TWI_*` in `initialization.c`; no source-code references FLEXCOM6 since the TC358743 driver uses the `DRV_I2C` handle). Updated `spec.md` §3.1 (bridge row) and §3.2 (peripheral table: FLEXCOM6 row → FLEXCOM8). The dated 2026-05-01/05-20 entries above and `journal-archive.md` are left as historical record (FLEXCOM6 was correct on the original Curiosity board). Patch #6 in the re-apply list annotated with the Hybrid peripheral name.

### 2026-06-03 — cv_marvin_v1: wire PerfLog_EmitDetector

First end-to-end export-ml run on hardware produced a 248-second CSV with **0 DETECTOR records** despite the firmware-side enabled-mask correctly carrying bit 3 (`PerfLog: mask=0x0000084a` = SESSION+DETECTOR+DROP+FRETBOARD_RAW). Root cause: `PerfLog_EmitDetector` was defined in [`perf_log.c:453`](../default/src/perf_log/perf_log.c#L453) and declared in [`perf_log.h:31`](../default/src/perf_log/perf_log.h#L31) but **never actually called from anywhere**. cv_marvin_v1's `detect_frame` was building a `detector_state_t`, posting it to the detector bus (which is why the actuator/timing pipeline runs and the game plays correctly), but skipping the perf-log mirror of the same per-frame decision.

Fix: add the call at the bottom of [`cv_marvin_v1.c:detect_frame`](../default/src/detector/cv_marvin_v1.c) right after the bus push. Build `hold_dist[]` / `edge_dist[]` / `pressed_mask` / `edge_active_mask` from the same struct fields the bus message carried (`state.fret[i].raw_value`, `state.fret[i].confidence`, `s_pressed[i]`, `s_edge_active[i]`). The perf-log struct's hold/edge values already share the 0..65535 scaling with `detector_state_t.{raw_value, confidence}` per the schema comment, so no rescaling needed.

Also confirmed via grep that all other `PerfLog_Emit*` declarations have at least one caller — this was a singleton miss.

### 2026-06-03 — marvin-perf: prepend cached SESSION + DETECTOR_CONFIG to recordings

First real export-ml run on hardware tripped over a missing `SESSION` record in the bin: the firmware emits SESSION exactly once per sink-attach edge, but the web-mode recorder doesn't start mirroring framed bytes to disk until the user clicks Record — usually well after attach. By then the SESSION has been received and consumed by the reader thread, but it's never written to the recording's bin, so the offline exporter can't recover `timer_freq_hz`.

Fix in [`tools/marvin-perf/marvin_perf/web/live.py`](../../../tools/marvin-perf/marvin_perf/web/live.py): cache the framed bytes of each "prepend-worthy" record type into a `prepend_framed: dict[str, bytes]`. On `record_start`, write the cached bytes to the new bin in declared order before letting the live mirror take over. The two types covered today:

- **SESSION** — strictly one-shot per attach; without it the bin has no `timer_freq_hz` anchor and downstream tools can't convert `ts_counter` to seconds.
- **DETECTOR_CONFIG** — re-emits at ~1 Hz from cv_marvin_v1 ([cv_marvin_v1.c:374](../default/src/detector/cv_marvin_v1.c#L374)) but a sub-second capture might end before the next heartbeat. Carries cv sample coords + thresholds + colour weights — needed by the offline review UI to render STRIP overlays from frame 0 and useful for auditability of the detector configuration that produced the labels.

Other 1 Hz heartbeats (`DROP`, `TASK_HIGHWATER`, `TASK_RUNTIME`) are cumulative counters; the second sample (1 second in) gives the same info, no prepend needed. Per-event records (`STAMP`, `DETECTOR`, `TIMING`, `STRIP`, `ACTUATOR`, `FRETBOARD_RAW`) are high-rate and self-contained. New types added in the future just need an entry in `_PREPEND_TYPES`.

Also tightened the exporter so the first emitted CSV row's timestamp is **0.0** instead of `(first_row.ts_counter - session.ts_counter) / freq` — closer to SensiML's "elapsed since logging started" convention and friendlier when sessions span minutes between attach and record-start.

The headless `marvin-perf record` path was never affected: opening the serial port toggles DTR, which fires the firmware-side SESSION emit, which lands in the very first frames of the bin.

### 2026-06-02 — marvin-perf export-ml: SensiML CSV from a capture

Step 2 of the Edge-AI training-dataset workstream. Capture pipeline was already wiring `PERF_REC_FRETBOARD_RAW` (240 Hz) and `PERF_REC_DETECTOR` (60 Hz) through marvin-perf's recording sink; this session adds the offline export that turns one of those captures into a labelled CSV ready for [MPLAB Machine Learning Development Suite](https://www.microchip.com/en-us/tools-resources/develop/mplab-machine-learning-development-suite) / SensiML Data Capture Lab.

**Phototransistor placement note (clarified this session).** The fretboard sensors sit *upstream* of the strike line, at roughly the same vertical position as cv_marvin_v1's hold sense line. So the cv detector's `pressed_mask` aligns instantaneously with each fretboard ADC sample at the same moment in time — no need to time-shift labels backward. The downstream actuator (timing pipeline) owns sensor→strike-line propagation as a fixed deterministic delay. Edge-AI model's job is therefore just per-fret presence detection: "is a note at my sensor right now?" — five independent binary classifiers.

**v0 export shape:**
- One CSV per capture, one row per `FRETBOARD_RAW` record (~240 Hz).
- Columns: `timestamp, ph_green, ph_red, ph_yellow, ph_blue, ph_orange, label_green, label_red, label_yellow, label_blue, label_orange`.
- `timestamp` is decimal seconds since the SESSION record's `ts_counter`, matching the [MPLAB Data Visualizer SensiML CSV preset](https://onlinedocs.microchip.com/oxy/GUID-4FF3C687-0C30-4D21-82D0-5AE401E8BE9D-en-US-8/GUID-6D2B490A-A171-4C9F-8F48-68438016B07D.html).
- `ph_*` are raw 12-bit ints (no normalisation; SensiML normalises during training).
- `label_*` are five independent binaries from `pressed_mask`, broadcast forward by `frame_epoch` (one cv frame ~ four fretboard rows).

**Implementation:**
- New package [`tools/marvin-perf/marvin_perf/exporters/`](../../../tools/marvin-perf/marvin_perf/exporters/), single module `sensiml_csv.py` with `export_sensiml_csv(capture_path, out_path, *, strict=False) -> ExportStats`. Stream-decode (no all-in-memory load) walking SESSION → DETECTOR-cursor → emit-row-per-FRETBOARD_RAW. Stdlib `csv.writer`; no pandas dep added.
- New CLI subcommand `marvin-perf export-ml <capture> --out <csv>` (`cli.py`). Mirrors the shape of the existing `record` / `set-mask` / `serve` subcommands; intentionally does NOT pull in the `viewer` dep group (no fastapi import) so it works in headless contexts.
- `ExportError` raised if the capture has no SESSION record (no `timer_freq_hz` → no way to compute timestamps); partial output file is unlinked on failure.
- New `build_fretboard_raw_payload` test helper in `tests/conftest.py`.

**Edge cases handled:**
- `FretboardRaw` records arriving before any `Detector` record — emitted with all labels = 0 by default; `--strict` drops them so every row carries a real label.
- `frame_epoch == 0` records — included; the cursor's most-recent label still applies.

**Tests:** 8 new in `test_export_sensiml_csv.py` covering header schema, row count, ADC-column passthrough, label step-function correctness, timestamp arithmetic, both pre-detector strict/non-strict paths, and the missing-SESSION error. 82/82 host pytests pass.

**Out of scope (deliberate):**
- `.dcli` segment-label sidecar — defer until segmentation policy is decided after first SensiML import attempt.
- Per-fret CSV split — single combined CSV is simpler; user creates 5 SensiML projects each focused on one `label_*` column.
- Edge prediction / windowed samples — Data Capture Lab does its own segmenting after import.
- `_LoadedCapture` lift from `web/api.py` to a non-web home — kept as-is to keep this diff small; the exporter does its own streaming decode and avoids the cross-import.

### 2026-06-02 — fretboard ADC stream into perf-log

Step 1 of the Edge-AI training-dataset workstream. Goal: route fretboard 5-channel ADC samples through marvin's perf-log so an offline tool can join them with `cv_marvin_v1` detector output for labelled training data. (Edge-AI MCU = future device that has only the fretboard sensors, no HDMI; `cv_marvin_v1` is ground truth.)

- New record `PERF_REC_FRETBOARD_RAW = 0x0B` (28 B framed): `perf_hdr_t` + `uint16_t adc[FRET_COUNT]` + `uint16_t reserved`. Schema purely additive — no `PERF_LOG_SCHEMA_VERSION` bump. Producer is `PerfLog_EmitFretboardRaw(adc, frame_epoch)` in [`perf_log.c`](../default/src/perf_log/perf_log.c); slot union and `record_size()` dispatch updated.
- [`fretboard_link.c`](../default/src/actuator/fretboard_link.c) gains the RX path it never had: `s_rx_stream` (512 B static stream buffer), `s_rx_buf[64]` for the in-flight USB Read, a `READ_COMPLETE` handler that pushes bytes via `xStreamBufferSendFromISR` and re-arms via `arm_rx_read`, plus first-arm at the end of `open_cdc()` after `ControlLineStateSet` settles. `cdc_event_handler` was previously write-only.
- New sibling task `fretboard_rx_task` (priority 5, 512-word stack, `PERF_TASK_FRETBOARD_RX = 17`). Pops bytes off the stream buffer, runs the same `0x03 … 0xFC` resync logic as [`tools/ds_monitor.py`](../../../firmware/fretboard/tools/ds_monitor.py), then on each valid 12-byte frame parses 5×u16 LE, reads `Video_GetFrameInfo()` for the current `frame_count`, and calls the new perf-log producer. Sibling rather than folded into `fretboard_link_task` because that task's blocking `xQueueReceive` shape doesn't compose with the stream buffer without bigger surgery, and the RX path is independent of the TX state machine.
- Default-disabled at boot: this is a 240 Hz × 28 B ≈ 6.7 KB/s record type, follows the existing `STAMP/DETECTOR/TIMING/STRIP` posture. Host turns it on with `marvin-perf set-mask` when capturing.
- Host decoder updated in lockstep: [`tools/marvin-perf/marvin_perf/records.py`](../../../tools/marvin-perf/marvin_perf/records.py) gets `FretboardRaw` dataclass + `RecordType.FRETBOARD_RAW` + `TaskId.FRETBOARD_RX`; [`decode.py`](../../../tools/marvin-perf/marvin_perf/decode.py) adds `_decode_fretboard_raw` and dispatch entry. 74 existing pytests still pass; manual roundtrip-decode of a synthetic frame returns the expected 5-tuple.

**Out of scope this session** — `adc_fretboard` detector that publishes onto the detector-state bus (§4.2, M2; bus slot `DETECTOR_ADC_FRETBOARD = 1` already reserved); offline join/export tool that turns a perf-log capture into a training-friendly format (natural next step now that the records exist).

**End-to-end verified on hardware later the same day.** With FRETBOARD_RAW + STAMP enabled, marvin parsed 240 frames per second exactly (one full second's worth between adjacent serial-log lines), `skipped=0` framing-error count, sensible ADC values (~3800–3970/4095, expected for "no note present" since the spec says lower = brighter sensor). Two real bugs surfaced during bring-up:

1. **`xStreamBufferCreateStatic` size off-by-one.** First-cut code declared `s_rx_stream_storage[FBL_RX_STREAM_BYTES + 1u]` (the FreeRTOS `+1` byte) but then passed `sizeof(storage)` as `xBufferSizeBytes`, asking the impl for one byte more than it was given. Fixed to pass `FBL_RX_STREAM_BYTES` directly. Latent — would have shown up as a one-byte-end overrun under sustained pressure.
2. **Host UI didn't auto-render a checkbox for the new type.** The `RECORD_TYPES` array in `tools/marvin-perf/marvin_perf/web/static/app.js` is hand-maintained; adding a new `RecordType` enum entry isn't enough. Added `FRETBOARD_RAW` (bit 11, default-off matching the STRIP convention).

Also restored IDLE/OTHER to the per-task RTOS table — they were filtered out in the 2026-06-01 RTOS-CPU-snapshot work on the assumption that the new totals header above the table (busy / idle / other) was sufficient. The user wants both: keep the totals header but include IDLE/OTHER in the workload table for direct comparison.

A new `PERF_STAGE_FBL_READ_COMPLETE = 0x42` perf-log STAMP fires once per USB Read completion (gated by the host type-mask) so the host can see Read-side timing as a peer of the existing `CDC_WRITE_COMPLETE` stamp. `aux` packs `(result << 24) | length`. Useful for tuning USB latency end-to-end now that we're capturing both transport directions. Periodic `LOG_DEBUG` line in the RX task prints `parsed`/`skipped` counts plus current ADC values; off by default (level INFO), enable with `log_set_level(LOG_LEVEL_DEBUG)`.

### 2026-06-01 — marvin-perf RTOS view: CPU% snapshot

RTOS tab in [`tools/marvin-perf`](../../../tools/marvin-perf) now shows per-task CPU% so the user can see what's actually consuming the SAM9X75 in real time. Closes the [`web/api.py`](../../../tools/marvin-perf/marvin_perf/web/api.py) `cpu_pct: None  # Phase 2` placeholder and unsticks the "RTOS tab is empty in live mode" symptom.

- New `compute_cpu_snapshot(records)` in [`analyze.py`](../../../tools/marvin-perf/marvin_perf/analyze.py): groups TASK_RUNTIME records by task_id, takes the last two per task, computes Δrun_time_counter (uint32-wrap-safe) and normalizes against the sum of deltas across every emitting task. Σ across all tasks (incl IDLE and OTHER) = 100% by construction since the firmware producer at [`perf_log.c:225`](../default/src/perf_log/perf_log.c#L225) emits OTHER as `Σ all − Σ registered`.
- `/api/capture/{id}/rtos` populates `tasks[*].cpu_pct` and `totals.cpu_pct_idle` / `cpu_pct_busy`. Tasks with TaskHighwater records but no TaskRuntime samples (or fewer than 2) still appear in the table with `cpu_pct: null`.
- **Live mode was the bigger gap** — `state.rtos` was only ever populated by `loadCapture()` (offline path), so the live WS streamed TaskRuntime/TaskHighwater into `state.records` but never re-rendered the panel. New `recomputeLiveRtos()` in `app.js` mirrors the host snapshot logic in JS, runs from `scheduleLiveRedraw` on every WS-debounce tick (250 ms), and produces the same `{tasks, totals}` shape the offline endpoint returns so `renderRtosPanel()` reads both sources uniformly.
- **Firmware bug fixed:** IDLE was registering as NULL because `PerfLog_Start()` runs from `APP_Initialize()` ([app.c:186](../default/src/app.c#L186)) — pre-scheduler. `xTaskGetIdleTaskHandle()` returns NULL until `vTaskStartScheduler()` creates the idle task, so slot 6 stayed unset and idle's run_time_counter fell into OTHER (which presented as ~98% on hardware — what we *thought* was idle). Moved the idle lookup into `register_post_scheduler_tasks()` (renamed from `register_mcc_tasks`), which the drain task calls on first iteration after the scheduler is up. Same one-shot pattern the MCC task lookups already use.
- UI: added a CPU totals block at the top of the RTOS tab — `busy / idle / other` %. IDLE is the FreeRTOS idle task (its own slot, registered via `xTaskGetIdleTaskHandle()` at [perf_log.c:386](../default/src/perf_log/perf_log.c#L386)); OTHER is `Σ all − Σ registered` ([perf_log.c:237](../default/src/perf_log/perf_log.c#L237)) and should idle near zero — surfacing it in the header makes a nonzero value (= an unnamed task is running) immediately visible. Both pulled out of the per-task table so the table is the workload view (sorted by CPU% desc, hottest first). HWM column rules unchanged (red <32 words, yellow <64).
- 104/104 pytests pass. New tests: `compute_cpu_snapshot` simple three-task, last-two-only windowing, uint32 wrap-around, single-sample empty, no-runtime-records empty; plus `/rtos` returns CPU% when TaskRuntime records are present.

Snapshot rather than time-series for v0 — answers "what's hot right now" in one glance and avoids a second Plotly chart on the panel. Time-series can plug in later if a transient (Legato spike during UI redraw) needs root-causing — same `compute_cpu_snapshot` shape applied over a sliding window of records.

### 2026-06-01 — perf-log v3 plan: pivot to tuning visibility

The low-level perf-log channel work is done (v1 framing/integrity, v2 STRIP + TASK_RUNTIME, multi-IRP throughput at 2.77 MB/s with zero drops). The wire's job pivots: from "prove bytes survived and the pipeline isn't dropping" to "show the host what the detection algorithm and actuator are thinking." Integrity records (SESSION, DROP, framing layer) keep their existing semantics — the trust they buy is load-bearing for everything else on the wire. Tuning records expand.

One schema bump (v2 → v3) bundles four additions to avoid multiple rounds of firmware/host sync.

**`DETECTOR_CONFIG` (new, low-rate / on-change).** Carries cv_marvin_v1 per-fret configuration the host needs to interpret the per-frame DETECTOR record:
- sample coords `(hx, hy, ex, ey)` per fret
- `hold_thresh`, `hold_release_frac`, `edge_thresh`
- color filter `target[3]`, `reject[3]` per fret

Static today (compile-time tables in [`cv_marvin_v1.c`](../default/src/detector/cv_marvin_v1.c)); will become tunable when M6 calibration UI lands. Strip-relative pixel coords are computed host-side: `sample_x_in_strip = hx − strip.x`, since STRIP records already carry `(x, y, w, h)` in frame space.

**`ACTUATOR` (new, per-publish or 1 Hz heartbeat).** Replaces the partial picture from `FBL_SEND` STAMP + `TIMING.publish_mask`:
- `intended_mask` — what the active producer wants
- `asserted_mask` — what's currently on the wire
- `strum_dir` — next direction (toggles each strum)
- `producer_id` — 1 byte tag (`timing_pipeline`, `manual_control`, future `game_state_controller`). The arbitration signal that's invisible today: when manual_control takes the wire, nothing on the wire indicates the handoff.
- ack timing fields — relate ACTUATOR ts to last `CDC_WRITE_COMPLETE` for transport-side latency.

**`TIMING` (extended, per-frame snapshot).** Today's record carries counts only (`chord_window_fill`, `fifo_depth`). v3 carries the snapshot needed to answer "why did it publish *that* mask":
- `now_ms` (pipeline clock, anchors all `*_at_ms` deltas)
- `chord_open`, `chord_age_ms`, `chord_mask` — open window's accumulating mask
- `note_q_count`, `note_head_at_ms`, `note_head_mask`, `note_tail_mask` — front of queue + union of remainder
- `strum_q_count`, `strum_head_at_ms`, `strum_head_mask`, `strum_dir_next`
- `frets_active`, `strum_active`, `strum_release_at_ms`
- `release_pending_mask`, `release_min_at_ms`
- `publish_mask` (kept)

~36 B body, per-detector-frame rate (60 Hz) ≈ 2 KB/s. Snapshot, not causal trace; if push/pop trace becomes necessary later, a sibling `TIMING_EVENT` record plugs in cleanly without revisiting this design.

**CPU accounting completion.** TASK_RUNTIME already supports per-window CPU% (host subtracts adjacent `run_time_counter` records — wrap-safe modular subtraction; uint32 wire field is fine since 1-second window delta at 266 MHz is ~266M counts, well under 2³²). Three additions:
- `PERF_TASK_IDLE` — register `xTaskGetIdleTaskHandle()` so absolute CPU% = `1 − idle_delta / Σ_all_delta` is computable.
- **MCC per-task slots** (`LEGATO`, `XLCDC`, `MAXTOUCH`, `SYS_INPUT`, `USB_DEVICE`, `USB_HOST`, `DRV_USB_UDPHS`, `DRV_USB_HOST`, `APP`) — registered by string name via `xTaskGetHandle` from the perf-drain task at startup (pcName matches MCC's emitted `tasks.c` literals). MCC tasks are created in `SYS_Tasks()` just before `vTaskStartScheduler`, so handle lookup must happen *after* scheduler start — drain task does it on first iteration. Missing names log a warning and fall through into OTHER. Legato is the primary tuning target (Composer + Legato render path is the biggest single MCC CPU consumer); the rest come along essentially for free.
- `PERF_TASK_OTHER` — pseudo-slot that emits `Σ all-task runtime − Σ registered-task runtime` from `uxTaskGetSystemState`. With every notable named task registered, OTHER should normally idle near zero — a non-trivial OTHER means a task is active that we aren't naming yet (e.g. an unanticipated Harmony service).

**Host-side pickups (marvin-perf).** [`web/api.py`](../../../tools/marvin-perf/marvin_perf/web/api.py)'s `/capture/{id}/rtos` endpoint currently returns `cpu_pct: None  # Phase 2` and `cpu_pct_idle: None  # Phase 2`. With idle + OTHER on the wire, the per-window math fills those in. Viewer also gets:
- Strip overlays — sample dots in fret colors at `(hx,hy)` / `(ex,ey)` from DETECTOR_CONFIG, per-frame value labels from DETECTOR's `hold_dist` / `edge_dist`, threshold reference lines.
- Actuator panel — intended vs asserted mask, producer name, strum_dir history.
- Timing detail panel — chord window mask, note/strum queue head with deadlines, release-pending visualization.

**What stays.** Framing layer (SOF + LEN + Fletcher-16), `SESSION`, `DROP`, `STAMP`, `STRIP`, `DETECTOR`, `TASK_HIGHWATER` all unchanged.

**Out of scope this round.** Causal-trace `TIMING_EVENT` records, per-task MCC breakdown, ISR latency histograms, Cortex-A5 PMU instrumentation (cycles / cache misses / instructions retired), free-heap reporting (heap_1 is static post-init; Legato pool needs separate APIs).

### 2026-06-01 — doc accuracy sweep

Swept all marvin docs for accuracy relative to the current implementation. Changes made:

- **`spec.md`** — §1.3/1.4/2.1/2.2/2.3/3.2: BGRX32 → BGR888 packed throughout. §1.4 status table: updated CV detection (M1 done), fretboard link (✅ USB CDC host), timing pipeline (✅ working), operator UI, and system services to reflect current state. §3.1: fretboard connection note updated from "UART" to USB CDC host over EDBG. §3.2: USB device now ✅ in use (perf-log), USB host row added ✅ in use (fretboard link). §4.3: changed from "UART" to USB CDC host description.  §4.6: `.bgrx` → `.bgr` keyframe extension, 1.38 MB → 1.04 MB keyframe size, `"format": "BGRX32"` → `"BGR888"` in manifest sample.
- **`capture_pipeline.md`** — Title, header, §1 pipeline diagram, §2.4 (RMS=0 → RMS=1 — documents current path, includes patch #3 code), §2.5 COLMAX formula updated for RMS=1, §2.7 DMA IMODE description, §2.8 framebuffer layout (4 B/px → 3 B/px, 4-deep ring, `.region_nocache`).
- **`display_path.md`** — Title, header, §1 overview diagram (OVR1 → HEO, ARGB_8888 → RGB_888_PACKED, `.region_cache_aligned` → `.region_nocache`), §2.1 (OVR1 → HEO with rationale), §2.2 (ARGB_8888 → RGB_888_PACKED), §2.5 (single-buffer limitation → per-frame pointer swap done), §3 cache coherency simplified (nocache = no maintenance needed), §4.2 call sequence updated, §5 limitations updated.
- **`README.md`** — Added one-sentence project description and pointers to spec.md and journal.md.

### 2026-05-29 — perf-log USB throughput: multi-IRP, drain refactor, Fletcher-16, single MAX_BYTES

Day-long push to unblock perf-log throughput beyond the ~1.88 MB/s ceiling that the journal's [2026-05-22 USB CDC bandwidth headroom](#2026-05-22--usb-cdc-bandwidth-headroom--state-queue-starvation-diagnosis) entry diagnosed. Outcome on hardware (185×32 sensing + 290×32 strike strips, 60 fps, 20 s capture): **2.77 MB/s sustained, zero drops anywhere, every record type flowing at 100% of its design rate, ts_counter inversions bounded to 5 ms (queue-interleave noise only), drain CPU recovered from ~26% (soft CRC) to ~2% (Fletcher), UI responsive again**. Several discrete landings, documented in causal order along with the diagnostic that drove each.

| Metric (185×32 + 290×32 strips, 20 s) | Before this session | After |
|---|---|---|
| Sustained wire | 1.88 MB/s ceiling, ~700 state drops/s | **2.77 MB/s, no ceiling reached, 0 state drops** |
| Sink drops | 0 at 1.88, climbing above | **0**, headroom remaining |
| Strip drops | growing at higher dims | **0** |
| Frame-side stamps | 71% delivery (CV_END worst at 8%) | **100%** (60.0/s exactly) |
| FBL_SEND / CDC_WRITE_COMPLETE | 71% | **100%** (240/s exactly) |
| HWM/RUNTIME records | absent (lost at state queue) | **5/s each, flowing** |
| ts_counter inversion p99 | 7,345 ms (queue lag) | **5.2 ms** (drain-cycle interleave only) |
| Drain CPU on CRC | ~26% (soft, bit-by-bit) | **~2%** (Fletcher) |

**Multi-IRP pipelining via UDPHS driver patch.** The journal's "lever 1" hypothesis (pipeline CDC writes for 2-3× win) had a hidden gotcha. The CDC layer accepts multiple in-flight IRPs (`queueSizeWrite` and `USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED` are both knobs), but the underlying UDPHS device driver only programs the DMA channel in `IRPSubmit`'s queue-empty branch. Subsequent IRPs are linked into the endpoint's queue but *never armed for transmission* — `Tasks_ISR_DMA` advances `irpQueue = irp->next` after a callback fires but doesn't re-program the DMA hardware. Net effect: even with all three layers configured for N=3 in-flight, only one IRP ever transmitted; the other two sat as `STATUS_PENDING` in the linked list forever.

Walked the IRPSubmit and Tasks_ISR_DMA paths line by line in [drv_usb_udphs_device.c](../default/src/config/default/driver/usb/udphs/src/drv_usb_udphs_device.c) before convincing myself this was a real driver-side limitation. Fix: extracted the existing inline DMA-arm code (~80 lines mirroring lines 1880-2008 of the IRPSubmit body) into a static helper `F_DRV_USB_UDPHS_DEVICE_ArmDmaForIrp` and added a call after the queue advance in two places — `Tasks_ISR_DMA` (DMA-completion ISR) and the ZLP-completion path inside `Tasks_ISR`. Patch markers (`/* PATCH: */`) make the change findable after MCC regen. Logged as **patch #9** in the re-apply list. False starts before getting here:

- N=3 ring + counting semaphore + sink reject path (correct in principle, blocked by `queueSizeWrite=1` cap in MCC's emitted CDC init).
- Bumped combined queue depth to 5 + per-instance `queueSizeWrite` to 3 (correct, but blocked by missing DMA arm in driver).
- Patched the driver (real fix).

The MCC-side prerequisites for the patch to do anything useful:
- `usb_device_cdc_0.yml` adds `CONFIG_USB_DEVICE_FUNCTION_WRITE_Q_SIZE = 3` (per-instance write queue).
- `usb_device_cdc.yml` adds `CONFIG_USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED = 5` (RX prime + write queue + serialState — exact fit, no slack).
- `usb_device_init_data.c` shows `.queueSizeWrite = 3` after regen.
- `configuration.h` shows `USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED = 5U`.
All three are now in MCC yml — regen preserves them, no re-apply patch needed for the config side. Just the driver change is patch #9.

**Sink rewrite to N=3 staging ring + counting semaphore.** With multi-IRP working at the driver, [perf_log_sink_cdc.c](../default/src/perf_log/perf_log_sink_cdc.c) swapped the single staging buffer + binary `s_write_done` semaphore for a 3-deep `s_tx_ring[3][SINK_FRAME_BYTES_MAX]` + counting semaphore (init=3) `s_tx_credits`. Producer takes a credit, fills `s_tx_ring[s_tx_head]`, submits non-blocking, advances head; ISR `WRITE_COMPLETE` returns one credit. Single producer (drain task) + FIFO completion order on the bulk endpoint = no race. `SINK_FRAME_BYTES_MAX` derived from `PERF_STRIP_MAX_BYTES + record/framing overhead, rounded to cache-line` so the two constants can't drift again — the hand-tuned `23104u` literal had already drifted once when the strip max bumped.

**`PL_STATE_QUEUE_DEPTH` 128 → 1024.** Phase A. State queue was overflowing under STAMP burst traffic — 60 Hz × multiple frame stages plus 240/s FBL_SEND/CDC_WRITE_COMPLETE = ~780/s producer rate. 1024 covers ~1.3 s at full producer rate with drain stalled. Kept after analysis even though current load uses <1% of that — BSS cost is 35 KB on 240 MB cached DDR (rounding error), and the headroom matters if a future strip-size bump pushes toward wire saturation (drain blocks on credit timeouts, state queue accumulates during the block).

**Drain-loop fix: drain ALL state records per outer iteration.** First symptom after the multi-IRP work landed: at 185×32 + 290×20 strips (2.14 MB/s), state queue was clean. Bumped strike to 290×32 (2.65 MB/s) and state queue started dropping at 240/s while sink and strip queues stayed clean. Root cause: drain-task loop had `if (xQueueReceive(state_q,…)) WriteFramed(…)` — *one* state record per outer iteration. Strips dominate per-iteration time (~5 ms each on the wire), so state drained at ~50 records/s while produced at ~700/s. Fixed by adding an inner `while` loop that drains every currently-queued state record before moving to the strip drain. After: state delta = 0 across 20 s captures.

**Pointer-pool refactor for the strip queue.** FreeRTOS queues are pass-by-value: every `xQueueSend`/`xQueueReceive` does `memcpy(slot, src, item_size)` inside a critical section. With `sizeof(perf_rec_strip_t)` at 23-61 KB depending on `PERF_STRIP_MAX_H`, that's 38-100 µs of *interrupts-off* time per queue op × 240 ops/s = 9-24 ms/s of ISR-latency stretching. Discovered when bumping `PERF_STRIP_MAX_H` to 64 caused the firmware to exhibit erratic behaviour even before any cv_marvin dimension change — the bigger queue items alone were the trigger, via critical-section duration.

Refactored: [perf_log.c](../default/src/perf_log/perf_log.c) `s_strip_q` now carries 1-byte slot indices into a static `s_strip_pool[6]` (queue depth + 2 to cover "1 slot in producer's hand, 1 in drain's hand" while preserving drop-on-full semantics). A second small queue, `s_strip_free_q` (depth 6, 1-byte items), holds the free-list. Producer claims a free index, fills the pool slot directly via pointer, queues just the index. Drain dequeues the index, processes via pointer, returns the index to the free list. Removed the producer's `static perf_rec_strip_t r;` and the drain's `static perf_rec_strip_t s_strip_drain;` — same total BSS, no extra copies. Critical sections drop from ~38-100 µs to ~1 µs per queue op.

**Fletcher-16 replaces CRC-16/CCITT-FALSE in the framing layer.** Soft CRC bit-by-bit was burning ~26% CPU at 2.65 MB/s — the biggest remaining CPU sink and the cause of the UI sluggishness symptom under load. SAM9X75 has **no CRCCU peripheral** (the journal lever-2 entry was wrong about that — verified by the device pack header list at `packs/SAM9X75D2G_DFP/component/`: AES, SHA, TDES, TRNG, PMECC, but no CRCCU). Hardware-DMA CRC isn't an option on this part.

Considered alternatives:

| Option | Cycles/byte | CPU @ 2.65 MB/s | Detection |
|---|---|---|---|
| CRC-16 bit-by-bit (current) | ~25 | ~26% | full CRC strength |
| CRC-16 table-driven (256 × u16 LUT) | ~6 | ~6% | full CRC strength |
| **Fletcher-16** | **~2** | **~2%** | single-byte changes, adjacent swaps, most non-adjacent swaps; ~1/65536 false-positive rate against random byte sequences |
| Sum-16 / XOR-16 | ~1 | ~1% | far weaker (misses swaps and most reorders) |
| No checksum | 0 | 0% | rely on USB hardware + record magic + length bounds |

USB hardware already CRCs every bulk packet on the wire, so our framing-layer checksum's job is *firmware-side framing-bug detection* + *resync alignment* (false SOF in the middle of a corrupt stream). Both jobs are well-served by Fletcher-16. Picked it.

Wire format unchanged (still SOF | LEN | PAYLOAD | 16-bit checksum). Old `.bin` captures from before this change can't be decoded with new code (one-shot break, fine for dev). Updated both ends:

- Firmware [perf_log_sink_cdc.c](../default/src/perf_log/perf_log_sink_cdc.c) TX path — block-mod Fletcher pattern from RFC 1146 (deferred mod once per ~5800 iterations to keep uint32 from overflowing).
- Firmware [perf_log_rx.c](../default/src/perf_log/perf_log_rx.c) RX state machine — streaming Fletcher with byte-at-a-time mod 255 (single-byte pace, no overflow concern).
- Host [framing.py](../../../tools/marvin-perf/marvin_perf/framing.py) — `fletcher16()` replaces `crc16_ccitt_false`. `FrameStats.fcs_mismatches` (was `crc_mismatches`).
- All `crc_*` field names renamed to `fcs_*` across host (decode.py, records.py, web/api.py, web/live.py, cli.py, web/static/app.js).
- Test vector `fletcher16(b"123456789") == 0x1EDE` (hand-computed, verified end-to-end).

Net CPU win at 2.77 MB/s wire: drain task drops from ~26% (CRC) to ~2% (Fletcher). UI responsiveness recovered immediately on flash.

**`PERF_STRIP_MAX_W`/`MAX_H` collapsed to single `PERF_STRIP_MAX_BYTES`.** The split-into-W*H pair was always arbitrary — producer code only ever checks total bytes (`w * h * BPP <= PERF_STRIP_MAX_BYTES`), not the per-axis dimensions. Discovered the *actual* binding constraint isn't the UDPHS DMA cap (128 KB at current `DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE = 2`) but the wire LEN field, which is `uint16_t` → max 65535-byte payload → max ~65,507 byte pixel data per strip. Replaced both constants on both ends with `PERF_STRIP_MAX_BYTES = 65000u` (just under the LEN cap with a small margin). Producer accepts any (w, h) shape under that. Practical envelope: 720×30, 480×45, 320×64, 290×72. Going beyond requires bumping LEN to u32 — wire-format break, not worth doing casually since the wire-bandwidth ceiling kicks in around the same point at 60 fps.

**Default-disabled strips at boot.** Firmware now initializes the type mask to `DROP | TASK_HIGHWATER | TASK_RUNTIME` only — the cheap diagnostic types (~300 B/s combined). Higher-rate types (STAMP, DETECTOR, TIMING, STRIP) start disabled; host viewer enables them via `PERF_CMD_SET_TYPE_MASK` once it's ready to consume them. Avoids the unattended-firmware case where boot generates ~3 MB/s of strip records that all get dropped at the sink (no DTR), wasting producer-side CPU. SESSION is still always emitted regardless of mask (host needs `timer_freq_hz` on attach).

**Host-side manifest improvements.** Working through diagnosis, hit two real gaps:

1. `frame_epoch_first` was reporting 0 because `FBL_SEND` and `CDC_WRITE_COMPLETE` STAMPs use `frame_epoch=0` as a "no frame association" sentinel — the chord queue between `timing_pipeline` and `fretboard_link` strips the epoch (decision logged 2026-05-20). The earlier filter enumerated record types (SESSION/DROP/TASK_HIGHWATER/TASK_RUNTIME); now it just skips `frame_epoch == 0` records, type-agnostic.
2. Manifest gained `recording_started_at`, `recording_stopped_at`, `recording_duration_s` — the live recorder ([web/live.py](../../../tools/marvin-perf/marvin_perf/web/live.py)) already tracked `_Recording.started_at` but didn't write it to disk. Now it does. Also added `timer_freq_hz` fallback from the cached SESSION dict (`_state.last_session_dict`), so captures starting mid-stream still get the freq for ts_counter conversion.

**RTOS stack/CPU snapshot at 2.77 MB/s** (from RUNTIME records, percentages relative to recorded tasks — idle excluded):

| Task | % of busy time | Stack used |
|---|---|---|
| `CV_MARVIN_V1` | 71.97 | 153 / 1024 words |
| `PERF_DRAIN` | 26.99 | 138 / 512 words |
| `FRETBOARD_LINK` | 0.71 | 109 / 768 |
| `TIMING` | 0.26 | 127 / 768 |
| `VIDEO` | 0.08 | 187 / 1024 |

System overall is mostly idle — non-idle CPU is single-digit % absolute. CV_MARVIN's 72% relative is detect_frame + the per-strip producer fill (memcpy from frame buffer to pool slot). PERF_DRAIN's 27% is Fletcher + the pool→ring memcpy + USB submit per write.

**Out of scope today, sequenced for later:**

- **Lever 5 (zero-copy DMA scatter-gather from frame buffer)** — would eliminate the producer-side memcpy and the drain's pool→ring memcpy. Requires programming UDPHS DMA descriptors with one entry per pixel-row plus header/trailer. Modest CPU win (~2%); only worth it if a CRCCU equivalent ever shows up. The frame buffer is already in `.region_nocache` so cache coherence is free.
- **Lever 4 (move bulk-IN from EP3 → EP2 for 3-bank FIFO depth)** — would matter if we approach wire saturation; not needed at current load. Single-knob change in `usb_device_cdc_0.yml`.
- **Boot-time SESSION re-emit on host command** — would close the `timer_freq_hz: null` case for live captures starting mid-stream when DTR didn't toggle. Either (a) host sends a "request session" command on WS attach, or (b) firmware emits SESSION at 0.1 Hz alongside DROP. Latter is simpler.
- **Stack-overflow hook is still silent infinite-loop** — `vApplicationStackOverflowHook` would benefit from a UART log line naming the offending task. Carried forward separately.

### 2026-05-22 — marvin-perf web viewer: live-mode recording (Phase 3 of 3)

Closes the iteration loop. While a live session is up, the user picks an out-dir, hits Record, and the reader thread mirrors validated framed bytes (`FrameBytes.framed`) to `<dir>/perf.bin` directly — no second decode, no resync garbage. Stop / Live-Stop / serial-error all funnel through the same `record_stop()` finalize path that `cli.cmd_record` uses (`finalize_capture_dir(..., source=CaptureSource(kind="serial", port=…))`), so the resulting capture opens cleanly in Offline mode without any extra handling.

**Reader-thread tap.** Per-frame, under the session lock: if `_rec` is set, `rec.fh.write(frame.framed)` then bump byte/frame counters. Lock is held briefly (microseconds) — reader is single-producer, control endpoints are the only other contender. Write failures (disk full, ENOSPC) disable the recording in place and post a `record-write` error over the WS rather than tearing down the whole session.

**Auto-finalize.** `_LiveSession.stop()` calls `record_stop()` first, while the reader is still alive, so in-flight frames land in the bin instead of being lost to the close race. The reader's own `finally` block also calls `record_stop()` to cover the serial-disconnect path. Both calls hit the same idempotent path (lock-take fh, null `_rec`, close, `finalize_capture_dir`).

**REST + WS shape.** `POST /api/live/record/{start,stop}`. Start body `{out_dir}`; 409 on already-recording or session-inactive, 409 on `init_capture_dir(exist_ok=False)` collision so the user gets explicit feedback rather than silently overwriting. Stop is idempotent; returns `{capture_dir, bytes_written, n_frames, manifest}` for scripting use. WS broadcasts `{type:"recording", state:"started"|"stopped", …}` so a passively-attached browser sees state changes from any source — including auto-finalize.

**Frontend.** New record-controls toolbar group: out-dir input (suggested `captures/web-YYYYMMDD-HHMMSS` on live-start), Record/Stop buttons, and a pill that pulses red while recording. UI flips on the WS broadcast, not the REST response, so a re-attached browser mid-recording reflects reality. Reload during a live recording: `probeLiveSession()` reads `/api/live/status`, sees `recording != null`, re-attaches the WS, and the pill picks up the in-flight session.

**Out of scope.** Resumable / appendable recordings (each Record click is a fresh dir), client-side progress meter (REST manifest summary in the stop banner is enough), and dual-mode "open the just-finalized capture in offline tab" auto-handoff.

### 2026-05-22 — marvin-perf web viewer: live-mode frontend (Phase 2 of 3)

Frontend half of "browser is the iteration surface." Single-page app gains a Mode toggle (Offline/Live), port `<select>`, Start/Stop buttons, mask display, and an 8-checkbox types panel with ALL/MIN presets. Records stream over the WS into a sliding-window buffer (last 30 s, hard-capped at 10 k). STRIP records render client-side from the `bgr_b64` payload — no server PNG round-trip. The existing offline path is unchanged: every offline route + UI element still works as before; the mode toggle is the boundary.

**State machine.** Added `state.fsm = "live"` and `state.mode = "live" | "offline"`. Live disables transport (prev/next/play-pause/speed) — playhead is always "now". Crossing the mode boundary calls `resetCaptureState()` to clear the previous mode's records, panels, and Plotly charts so leftover state can't bleed across.

**Sliding window.** Each WS record append calls `pruneLiveBuffer()` which drops anything older than the newest record's `ts_counter` minus `LIVE_BUFFER_SECONDS * timer_freq_hz`, then enforces `LIVE_MAX_RECORDS`. `state.ts0` re-pins to the oldest survivor so timeline x-axis stays sensible as the window slides. Plotly redraw is debounced 250 ms; the playhead text + strip canvas update inline so per-frame visuals stay smooth.

**Mask checkboxes.** SESSION is pinned-on (firmware always emits it; checkbox disabled). Any change debounces 200 ms then `POST /api/live/set-mask` with the integer mask; server reply updates the displayed `0x…` value. ALL / MIN preset buttons map to `0xFFFFFFFF` and `(SESSION|DROP)` respectively. The very first start fires an immediate set-mask so server and UI start synchronized.

**Strip render.** `paintBgrIntoCanvas(canvas, b64, w, h)` decodes base64 → BGR-byte string → `ImageData` (RGBA via per-pixel byte swap) → `putImageData`. First STRIP record of a new kind triggers `indexByKind() + renderStripSlots()` so the canvas exists before paint; subsequent strips paint directly. Offline strip path still uses the PNG endpoint — `updateStripsAtPlayhead` prefers `rec.bgr_b64` when present, falls through to PNG otherwise.

**WS lifecycle.** Open on Live-Start (after `/api/live/start` returns OK), close on Live-Stop. Auto-reconnect on unexpected close: 1 s × attempt-count, capped at 5 s. Server's `session_replay` on reconnect re-binds `timer_freq_hz`. Close code 4409 ("already in use") surfaces as a banner without retry. On page reload mid-session, `probeLiveSession()` sees the active session via `/api/live/status` and re-attaches the WS — no manual restart.

**Out of scope this commit:** recording start/stop UI + backend (Phase 3), debounced timeline patching via `Plotly.extendTraces` (current full-replot is fine at the 250 ms cadence), and a "drops since session" live counter.

### 2026-05-22 — marvin-perf web viewer: live-mode backend (Phase 1 of 3)

Backend half of "make the browser the iteration surface." Single-tenant live session: one serial reader thread, one open `SerialSource`, one WebSocket peer at a time (a second WS gets close code 4409 with reason `"live already in use"`). REST for control (`/api/live/{start,stop,set-mask,status}`, `/api/serial/ports`), WS for the data stream (`/api/live/ws`); browser sends nothing over WS. No frontend changes yet — frontend live mode + record-types panel is Phase 2; recording start/stop is Phase 3.

**Module shape.** New [`web/live.py`](../../../tools/marvin-perf/marvin_perf/web/live.py) owns a mutex-guarded `_LiveSession` singleton. Reader thread runs `iter_frames(SerialSource, stats) → decode_record(frame.payload) → _record_to_dict(rec, include_bgr=True)` and posts onto a per-WS-attach `asyncio.Queue` via `loop.call_soon_threadsafe`. Disconnects in either direction flow through the same shutdown path that joins the thread and closes the port. `_record_to_dict` is late-bound from `api.py` to avoid the import cycle.

**Recording groundwork.** Added `framed: bytes` field to `FrameBytes`, populated from the same `buf[:total]` slice `iter_frames` already constructs — Phase 3's record path becomes a one-line write of validated frame bytes with no extra CRC compute. The existing `transport.TeeSource` would tee resync garbage too, so it's not the right shape for live recording.

**Strip pixels in WS messages.** Offline mode keeps strip pixels on the dedicated PNG endpoint, but live mode has no `capture_id` to hang a URL off. `_record_to_dict` grew an `include_bgr` flag — when set, Strip records carry `bgr_b64` (base64) so the browser can render client-side onto the existing strip canvas without a server round-trip.

**Tests.** 92/92 pass. New `test_iter_frames_yields_framed_bytes`, plus 8 in `test_live.py` (lifecycle, mask round-trip via fake `SerialSource`, error paths, status). Test injection point on `_LiveSession.start(port, *, ser_factory=...)` lets unit tests substitute a context-managed fake that yields nothing until `__exit__` fires — exercises the thread-join and stop-event paths without real serial.

**Out of scope this commit:** all frontend changes (mode toggle, port `<select>`, record-types checkboxes, sliding-window timeline append, client-side strip render, WS reconnect) and the recording REST routes. Verification beyond unit tests waits on the frontend landing — `curl POST /api/live/start` + `websocat ws://…/api/live/ws` is the Phase-1 hardware smoke check.

### 2026-05-22 — perf-log host→device record-type masking

Bidirectional control on the perf-log channel. Host can now narrow which record types the device emits per-capture without rebuilding firmware. Default is all-on (parity with prior behavior); host sends a `PERF_CMD_SET_TYPE_MASK` over the same SOF/LEN/CRC framer that's used for records, with a distinct command magic (`0x4D43` 'MC' vs records' `0x4D56` 'MV') so misrouted bytes can't be parsed in the wrong direction.

**Device side.** New [`perf_log_rx.{h,c}`](../default/src/perf_log/perf_log_rx.c) holds a small SOF-resync state machine fed from the CDC sink's `USB_DEVICE_CDC_EVENT_READ_COMPLETE` handler; valid `SET_TYPE_MASK` frames call `PerfLog_SetEnabledMask`. The CDC sink primes a 64-byte read on `EVENT_DEVICE_CONFIGURED` and re-primes after each completion. Filter is one `s_enabled_mask` (volatile u32, lock-free single-load on Cortex-A) checked at three early-return points: `send_state` (covers all state records), `PerfLog_EmitStripFromFrame`, and `PerfLog_EmitStampFromISR`. SESSION is always emitted regardless of mask — the host needs `timer_freq_hz` on attach.

**Host side.** New `frame_encode` in [`framing.py`](../../../tools/marvin-perf/marvin_perf/framing.py) (mirror of the device framer); `encode_set_mask_payload` + `RECORD_TYPE_BY_NAME` in [`records.py`](../../../tools/marvin-perf/marvin_perf/records.py); `SerialSource.send_command` in [`transport.py`](../../../tools/marvin-perf/marvin_perf/transport.py). CLI `live` and `record` get `--types STAMP,SESSION,DROP,…` (special tokens `ALL`, `MIN`); new `set-mask` subcommand pokes a running capture from another terminal. 9 new tests; 83/83 passing.

**Why now.** The 1.88 MB/s STRIP-dominated load was masking HWM/RUNTIME records (state-queue starvation, ~707 dropped/s — see prior entry). Cutting record types at the source is a cleaner fix than enlarging queues we don't actually want full. With STRIP off, expected sink load drops to ~25 KB/s, and the state queue should drain freely.

**Out of scope.** No schema bump (purely additive at the host→device edge — v2 stays v2). No flash persistence of the mask. No mask-state echo back (host knows what it sent). No viewer UI checkbox panel — deferred to when live-WS lands.

**Verification pending hardware.** Bench check: `marvin-perf record --types STAMP,SESSION,DROP,HIGHWATER,RUNTIME --port … --out-dir …` should produce a capture with zero STRIP/DETECTOR/TIMING records and `dropped_state == 0`. Sanity: firmware UART log shows `PerfLog: mask=0x...` when the host attaches.

### 2026-05-22 — USB CDC bandwidth headroom + state-queue starvation diagnosis

End-to-end strip viewer is up and rendering after the user adjusted strip dimensions in [`cv_marvin_v1.c:341-347`](../default/src/detector/cv_marvin_v1.c#L341-L347) to sensing 185×16 @ (265, 310) and strike 290×20 @ (212, 395). At those dimensions current device→host load is **1.88 MB/s** (≈8.9 KB sensing + ≈17.4 KB strike per frame × 60 Hz + ~25 KB/s state pipeline + ~80 B/frame record overhead) — the link badge is green and `dropped_strip`/`dropped_sink` are flat. But we were dropping at slightly higher dimensions just before this, so our practical CDC-ACM ceiling is < 5 MB/s on this firmware build today. Two concrete things came out of investigating that:

**(1) Where the bandwidth is actually going.** The USB-HS bulk-IN theoretical ceiling on UDPHS is ~40-45 MB/s real-world; we're leaving ~85 % of it on the floor. The conservative 5 MB/s working number isn't a USB-HS limit — it's a software-pipeline limit specific to how the sink writes today. Five candidate levers, ranked by expected payoff:

  1. **Pipeline writes** ([`perf_log_sink_cdc.c:251-267`](../default/src/perf_log/perf_log_sink_cdc.c#L251-L267)) — biggest single win, expect 2-3×. Today the sink owns a single staging buffer and pends a `WRITE_COMPLETE` semaphore between writes, so the USB DMA sits idle for the round-trip. The CDC write-queue depth is already 3 (`USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED = 3U` in [`configuration.h:243`](../default/src/config/default/configuration.h#L243)) so multi-buffer queueing is supported by the layers below — the sink just doesn't use it. Move to a 2- or 3-deep ring of staging buffers; submit the next write as soon as the previous returns to the queue, not when it completes.
  2. **Hardware CRC via CRCCU** — soft CRC at [`perf_log_sink_cdc.c:58-70`](../default/src/perf_log/perf_log_sink_cdc.c#L58-L70) burns ~14 % of CPU at the current rate (byte-at-a-time over every framed payload). CRCCU peripheral does CCITT-FALSE in DMA. Frees CPU for whatever the next bottleneck turns out to be; not a bandwidth knob in itself, but it's the cheapest path to free headroom.
  3. **Combine sensing+strike into one wire frame** — saves ~80 B framing overhead per ISC frame (×60 Hz = 4.8 KB/s, ~5-15 % of state-pipeline payload). Small but free if the schema accommodates it; revisit only if (1) and (2) aren't enough.
  4. **Move bulk-IN from EP3 → EP2** — UDPHS only exposes 3-bank FIFO depth on EP1-2 ([`drv_usb_udphs_local.h:159`](../default/src/config/default/driver/usb/udphs/src/drv_usb_udphs_local.h#L159) `DRV_USB_UDPHS_EPT_BK = 0x00060000`); EP3-7 are 2-bank only. The current bulk-IN is on EP3 ([`usb_device_init_data.c:213-220`](../default/src/config/default/usb_device_init_data.c#L213-L220)). 3-bank lets the controller pre-load three packets ahead of the host IN tokens; smooths out scheduler micro-stalls. Marginal win unless we're already pipelining at the software layer (so do this *after* lever 1).
  5. **Eliminate one memcpy** (architectural) — strip producer copies pixel rows out of the ISC frame into a queue slot, drain task copies that slot bytes onto the wire. Could be reduced to one copy if the strip producer wrote into a sink-owned ring directly, at the cost of tighter coupling between perf_log and the sink. Defer; the ratio of CPU spent in memcpy vs CRC favors lever 2 first.

  **What's *not* the bottleneck:** the `DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE = 2` knob in [`configuration.h:255`](../default/src/config/default/configuration.h#L255) (multiples of 64 KB, so 128 KB cap). Our largest single record is the 23 KB strip — 5× under the cap. Bumping this to 4 or 8 changes nothing about today's behavior. Documented here because it was the first knob the user noticed and it's tempting to try; save the time.

**(2) HWM + RUNTIME records are absent from captures because the state queue is full.** Decoded a 24 s capture — zero `PERF_REC_TASK_HIGHWATER` and zero `PERF_REC_TASK_RUNTIME` records, but `dropped_state since_session = 17188` (~707 dropped/s). Producers are wired and the drain task is calling them every second; the records hit a full queue and get counted as drops instead. Root cause: all state-pipeline records share `s_state_q` ([`perf_log.c:50-52`](../default/src/perf_log/perf_log.c#L50-L52), `PL_STATE_QUEUE_DEPTH = 128`). The 60 Hz × multiple-stage STAMP traffic — especially fretboard_link's `CDC_WRITE_COMPLETE` and `FBL_SEND` pair, which fire on every actuator wire byte — bursts faster than the drain can pull, the queue stays near full, and the once-per-second HWM/RUNTIME emits land on a full queue. Three candidate fixes:

  1. **Bump `PL_STATE_QUEUE_DEPTH` from 128 to 1024.** Cheapest. 128 × 32 B/slot = 4 KB today → 32 KB after; static BSS, no malloc. Probably enough on its own — the drain isn't *behind*, it's just unable to absorb burst peaks at 128 slots. Try first.
  2. **Give HWM/RUNTIME a dedicated low-pressure queue** drained on the same 1 Hz tick as DROP. Decouples the slow analytics path from the fast stamp path entirely; HWM/RUNTIME can never lose to a stamp burst. More plumbing (second queue + second drain branch) but it's the right shape long-term — and natural alongside lever 1 above (pipelining the sink) since the drain task changes anyway.
  3. **Throttle the high-rate stamp producers** — e.g. drop `CDC_WRITE_COMPLETE` to 1-in-N, or roll FBL_SEND/CDC_WRITE_COMPLETE into a single record. Loses information from the timeline; do this only if (1) and (2) both fail.

  Going to start with (1) since it's a single `#define` change and HWM is the most useful "is something about to overflow its stack" signal — we want it back before any of the bandwidth-lever work begins, since stack pressure is exactly the kind of thing that surfaces while we're refactoring the sink.

Neither finding is being acted on in this session — captured here so the analysis survives context.

### 2026-05-22 — Phase 2 perf-log: schema v2 (STRIP + TASK_RUNTIME) wired

Schema bump landed end-to-end. Firmware emits the two new record types; host decoder, analyzer, and viewer read them.

**Schema v2.** [`perf_log_records.h`](../default/src/perf_log/perf_log_records.h) bumps `PERF_LOG_SCHEMA_VERSION` to 2. `PERF_REC_PATCH (0x05)` is replaced by `PERF_REC_STRIP (0x05)` — variable-length BGR888 region tagged by `perf_strip_kind_t` (SENSING=0, STRIP=1; 256 codes available). `PERF_REC_TASK_RUNTIME (0x08)` adds per-task state/priority/`ulRunTimeCounter` snapshots. Both mirrored on the host in [`records.py`](../../../tools/marvin-perf/marvin_perf/records.py); `EXPECTED_SCHEMA_VERSION = 2`.

**STRIP producer in [`cv_marvin_v1.c`](../default/src/detector/cv_marvin_v1.c).** Two strips per ISC frame, emitted between `detect_frame` and `draw_overlay` so the captured pixels are clean detector input. Sensing line at (240, 295) 240×32, strike line at (240, 400) 240×32 — same coordinates `cv_marvin_v1` already samples for fret detection. Cost: 240×32×3 × 2 × 60 Hz ≈ 2.76 MB/s, well under the 5 MB/s CDC ceiling. The strip queue holds max-sized slots (`PERF_STRIP_MAX_BYTES = 23040`); drain task computes the on-wire length per record from `(w, h)` so future kinds at smaller dimensions cost only what they emit.

**TASK_RUNTIME producer in `perf_log.c`.** 1 Hz alongside the existing HWM emitter — `sample_and_emit_runtimes` fills a static `TaskStatus_t buf[16]` via `uxTaskGetSystemState`, maps each handle to its `perf_task_id_t` via `s_task_handles[]`, and emits `state` (mapped from `eTaskState`), `uxCurrentPriority`, and `ulRunTimeCounter`. Static buffer + bounded loop (no malloc, no recursion). Cost: 24 B × 6 tasks × 1 Hz = 144 B/s.

**FreeRTOSConfig.** `INCLUDE_eTaskGetState` flipped 0 → 1 — required for the `eCurrentState` field that `uxTaskGetSystemState` populates. `configRUN_TIME_COUNTER_TYPE = uint64_t` from patch #8 already in place.

**Host.** `decode.py` validates `len(bgr) == w*h*3` against on-wire dims (not a compile-time constant) so future kinds at other rectangles decode cleanly. `analyze.py` field rename `dropped_patch → dropped_strip`. Viewer's `STRIP_KIND_REGISTRY` already kind-driven from Phase 1 — strips render in their pre-allocated slots without additional work. 60 → 74 passing pytests.

End-to-end verification still pending: user builds in MPLAB X, captures a session, confirms strips render in the viewer and the RTOS-tab CPU% chart populates.

### 2026-05-22 — STAMP producers wired across pipeline; 64-bit ts_counter; diag UART dump retired

Follow-on session to Phase 1 — wired the seven `perf_stage_t` producers that had been declared since the schema landed but had no emitters, fixed a 32-bit timer truncation that was poisoning the timeline, and retired the 10 s diag UART dump now that the perf-log channel covers RTOS analytics.

**STAMP producers (commit `f4e8aa8`).** One emitter per stage:
- [`video.c`](../default/src/video/video.c) `on_frame_done` ISR → `ISC_IRQ` at top, `VIDEO_PUBLISH` after the subscriber fan-out (subscriber bitmap in aux).
- [`cv_marvin_v1.c`](../default/src/detector/cv_marvin_v1.c) task → `CV_START` / `CV_END` bracketing `detect_frame`.
- [`timing_pipeline.c`](../default/src/actuator/timing_pipeline.c) `process_frame` → `TP_TICK` at end with `s_output_mask` in aux.
- [`fretboard_link.c`](../default/src/actuator/fretboard_link.c) → `FBL_SEND` after a successful `USB_HOST_CDC_Write` (tx byte in aux), `CDC_WRITE_COMPLETE` from the CDC event ISR (USB result code in aux).

Five frame-side stages share `frame_epoch` from the ISC frame counter; `FBL_SEND`/`CDC_WRITE_COMPLETE` use `frame_epoch=0` since the cmd queue between TimingPipeline and FretboardLink carries only the fret mask byte. Host pairs that two-stage segment by `ts_counter` adjacency rather than threading an epoch through the queue — the plan's design choice.

Hardware-verified with a 53.4 s capture: 3199 frames at 59.9 Hz, 5 stages × 3199 records each, `dropped_state = 0`, `dropped_patch = 0` over the full run. `dropped_sink ≈ 1 MB` is fixed first→last drop record (pre-attach accumulation, no in-capture loss).

**64-bit `ts_counter` (commit `2cad94a`).** First post-Phase-1 capture showed the timeline collapsing to ~16 s with negative deltas — `hdr_fill` was casting a 32-bit `SYS_TIME_CounterGet()` to `uint64_t` *after* truncation, so the counter wrapped every `UINT32_MAX / 266 MHz ≈ 16.1 s`. Switched to `SYS_TIME_Counter64Get()` (already declared in `sys_time.h` and used by FreeRTOSConfig patch #8). One-line change at [`perf_log.c:78`](../default/src/perf_log/perf_log.c#L78). Captures now hold their full duration end-to-end.

**Diag UART dump retired (commit `895eece`).** The 10 s `vTaskListTasks` + run-time-stats UART dump in `diag/diag.c` (added 2026-05-21) was the temporary observation channel pending perf-log RTOS records. With `PERF_REC_TASK_HIGHWATER` shipping at 1 Hz and `PERF_REC_TASK_RUNTIME` queued for the v2 schema bump, the UART path no longer earns its keep — the perf-log channel reaches the visual viewer; the UART output was invisible to it and lost to history. Deleted `diag.c`/`diag.h` and the `app.c` init call. `vTaskListTasks` / `vTaskGetRunTimeStatistics` / `uxTaskGetStackHighWaterMark` stay enabled in `FreeRTOSConfig.h` since the perf-log producers are their consumers now.

Phase 2 (live WS + STRIP + TASK_RUNTIME, schema bump to v2) is the next chunk.

### 2026-05-22 — Visual review viewer (Phase 1) + HWM producer wired

Phase 1 of the marvin perf-log visual review tool landed. Plan in `~/.claude/plans/start-planning-on-host-iterative-floyd.md`. Two halves shipped together:

**Host (`tools/marvin-perf/marvin_perf/web/`):**
- Capture-as-directory container — `manifest.json` + `perf.bin`. `record --out-dir DIR` writes both; legacy `--out FILE.bin` still works. Viewer accepts a directory or a bare `.bin` (synthesizes manifest by scanning the bin once).
- FastAPI server gated behind a `viewer` dep group (`uv sync --group viewer`). Routes: `/api/capture/open`, `/manifest`, `/summary`, `/records?from=&to=&types=`, `/strip/{epoch}/{kind}.png`, `/health`, `/rtos`, plus `/api/preloaded` so `marvin-perf serve --capture PATH` auto-loads in the browser.
- Pure-fn layers (`render.py` for BGR888 → PNG via Pillow, `capture.py` for dir round-trip) tested without uvicorn.
- Vanilla-JS frontend — three-pane layout (strips / inspector / Plotly timeline), playback FSM (`idle | loaded | playing | paused`), `requestAnimationFrame` advancing the playhead in `ts_counter` space, click-to-seek on timeline, kbd shortcuts (space, ←/→, Shift+←/→, [/]). Strip panel is **kind-driven** via a `STRIP_KIND_REGISTRY` so future kinds (score, minimap) are one entry each. RTOS tab renders HWM trend per task.
- 42 → 60 passing pytests; new: `test_capture.py`, `test_render.py`, `test_api.py`. End-to-end demo with a synthetic 369-record capture verified the `/api/preloaded` auto-load path works.
- Frontend commit: `b83b938`.

**Firmware (`firmware/marvin/default/src/perf_log/`):**
- `PerfLog_EmitTaskHighwater(task_id, words)` and `PerfLog_RegisterTaskForHighwater(task_id, handle)` added to `perf_log.h`/`.c`. Drain task at 1 Hz now iterates a `s_task_handles[PL_TASK_SLOT_COUNT]` array, samples `uxTaskGetStackHighWaterMark`, and emits one `PERF_REC_TASK_HIGHWATER` per registered task. Unregistered slots are skipped (so `PERF_TASK_DETECTOR_DRAIN`, whose task was deleted earlier, is silently absent — wire-format slot stays in the enum for stability).
- Per-task self-registration: each marvin task module captures the `TaskHandle_t` returned by `xTaskCreateStatic` and calls `PerfLog_RegisterTaskForHighwater` immediately after. Wired in [`video.c`](../default/src/video/video.c), [`cv_marvin_v1.c`](../default/src/detector/cv_marvin_v1.c), [`timing_pipeline.c`](../default/src/actuator/timing_pipeline.c), [`fretboard_link.c`](../default/src/actuator/fretboard_link.c), and the drain task self-registers as `PERF_TASK_PERF_DRAIN` inside `PerfLog_Start`. Coupling stays minimal — modules call the registrar; `perf_log` doesn't reach into them.
- Cost: 24 B per record × 5 active tasks × 1 Hz = 120 B/s, dwarfed by the strip budget that lands in Phase 2.

This closes the host side of the carry-forward "FreeRTOS analytics — dump path still TODO" item: HWM is now visible in the viewer's RTOS panel as soon as a capture is opened. Phase 2 (live WS + STRIP + TASK_RUNTIME, schema bump to v2) is the next chunk.

### 2026-05-22 — Host-side perf-log decoder landed (`tools/marvin-perf/`)

Built v0 of the host decoder for the perf-log USB CDC stream. Runs under **uv** — `uv run marvin-perf ...` is the canonical invocation, `pyproject.toml` is the dep source-of-truth, `uv.lock` committed for reproducibility, no `requirements.txt`. Python 3.12 pinned via `.python-version`.

Layout (`tools/marvin-perf/`):

- `marvin_perf/records.py` — schema mirror of [`perf_log_records.h`](../default/src/perf_log/perf_log_records.h) (`EXPECTED_SCHEMA_VERSION = 1`). Hand-mirror, not codegen — small + version-gated, and a hand-mirror is the diff a reviewer reads when the schema bumps. `Patch` keeps the 5×150 B BGR payload as raw bytes; v0 doesn't render pixels.
- `marvin_perf/framing.py` — table-based CRC-16/CCITT-FALSE (verified against the canonical `0x29B1` test vector) + a streaming SOF-resync state machine that handles arbitrary chunk boundaries, oversized/undersized LEN, mid-payload SOF false positives, and CRC drops. `FrameStats` exposes counters (`frames_ok`, `bytes_resync_dropped`, `crc_mismatches`, `bad_lengths`) for diagnostics.
- `marvin_perf/decode.py` — `Header.unpack` + per-type decoders dispatched by `RecordType`. Unknown types yield `UnknownRecord` rather than aborting, so a forward-schema firmware doesn't crash an older host.
- `marvin_perf/transport.py` — `FileSource(path)`, `SerialSource(port)` (lazy `import serial`, asserts DTR on open to trip the firmware sink's re-emit-SESSION path), `TeeSource(upstream, out_path)` (live + record).
- `marvin_perf/analyze.py` — adjacent stage-pair latency histograms (p50/p95/p99/max), drop deltas, per-task HWM trend. Sanity checks: schema-version match (hard fail), `frame_epoch` monotonicity, VIDEO_PUBLISH cadence (16.67 ms ± 2 ms at 60 Hz).
- `marvin_perf/cli.py` — argparse subcommands `live` / `record` / `decode` / `summarize`.
- `tests/` — 42 passing pytest cases covering CRC vectors, frame round-trips per record type, SOF resync, partial-chunk reassembly, length sanity, decode-error paths, and analysis math (drops, HWM, schema mismatch, cadence).

Updated `perf_log_records.h:8-9` to point the mirror reference at `tools/marvin-perf` (was the placeholder `tools/perf-log-decoder`).

Producer wiring is still incomplete (per the 2026-05-21 perf-log entry — only `SESSION` on DTR + 1 Hz `DROP` heartbeat fire today), but the decoder is built and tested against the wire format that's settled, so it stays correct as STAMP/DETECTOR/TIMING/PATCH/TASK_HIGHWATER come online. `summarize` will produce real latency tables and HWM trends as soon as those producers land — closes the carry-forward "FreeRTOS analytics dump path" on the host side once `PERF_REC_TASK_HIGHWATER` emission lands.

### 2026-05-21 — FreeRTOS resource inventory + priority/analytics review

Stood up the work to enable FreeRTOS analytics (`vTaskListTasks`, `vTaskGetRunTimeStatistics`, `uxTaskGetStackHighWaterMark`) and audit task priority assignments across marvin. Three findings worth logging — one outright bug, one priority inversion, one structural problem with the priority budget.

**Resource inventory (16 tasks, 8 sync primitives).**

Hand-written (under `default/src/`, all `xTaskCreateStatic` per the static-allocation rule):

| Task | Prio | Stack (words) | File:Line | Cadence | Notes |
|---|---:|---:|---|---|---|
| `VideoTask` | 1 | 1024 | [video.c:323](../default/src/video/video.c#L323) | 20 ms poll | Capture-pipeline state machine; arms ISC on TC358743 lock; rebinds HEO per frame in ISR |
| `CvMarvinV1` | 2 | 1024 | [cv_marvin_v1.c:357](../default/src/detector/cv_marvin_v1.c#L357) | event (frame queue, portMAX_DELAY) | 5×5 patch sample + threshold + bus publish |
| `Timing` | 2 | 768 | [timing_pipeline.c:358](../default/src/actuator/timing_pipeline.c#L358) | event (bus queue, 5 ms timeout) | Chord window + strum scheduler |
| `FretLink` | 2 | 768 | [fretboard_link.c:248](../default/src/actuator/fretboard_link.c#L248) | event (cmd queue, 50 ms timeout) | USB-host CDC writer + 50 ms heartbeat republish |
| `PerfDrain` | 2 | 512 | [perf_log.c:176](../default/src/perf_log/perf_log.c#L176) | event (state queue, 20 ms timeout) | Frames + writes to USB-device CDC sink |
| `DetectorDrain` | 1 | 512 | [detector.c:69](../default/src/detector/detector.c#L69) | event (bus queue, portMAX_DELAY) | **M1 stub consumer; should have been deleted when M3 wired Timing** |

(`ManualControl` has no task — it's a synchronous API that calls `FretboardLink_Send` directly.)

MCC-generated (under `default/src/config/default/`, all `xTaskCreate` dynamic, all priority 1, all 1024-word stacks, all 10 ms `vTaskDelay` polling loops):

| Task | File:Line |
|---|---|
| `XLCDC_Tasks` | [tasks.c:178](../default/src/config/default/tasks.c#L178) |
| `DRV_MAXTOUCH_Tasks` | [tasks.c:187](../default/src/config/default/tasks.c#L187) |
| `USB_DEVICE_TASKS` | [tasks.c:199](../default/src/config/default/tasks.c#L199) |
| `USB_HOST_TASKS` | [tasks.c:208](../default/src/config/default/tasks.c#L208) |
| `DRV_USB_UDPHS_TASKS` | [tasks.c:217](../default/src/config/default/tasks.c#L217) |
| `LEGATO_Tasks` | [tasks.c:226](../default/src/config/default/tasks.c#L226) |
| `DRV_USB_HOST_TASKS` | [tasks.c:235](../default/src/config/default/tasks.c#L235) — wraps EHCI + OHCI |
| `SYS_INPUT_Tasks` | [tasks.c:244](../default/src/config/default/tasks.c#L244) |
| `APP_Tasks` | [tasks.c:257](../default/src/config/default/tasks.c#L257) — self-deletes after one tick (see [app.c:198-205](../default/src/app.c#L198-L205)) |

Sync primitives (all hand-written, all static):

| Primitive | Type | Depth × item | Producers → Consumers |
|---|---|---|---|
| `s_bus_queue` ([detector.c:61](../default/src/detector/detector.c#L61)) | Queue | 8 × `detector_state_t` | CvMarvinV1 → **Timing + DetectorDrain (bug)** |
| `frames` ([cv_marvin_v1.c:309](../default/src/detector/cv_marvin_v1.c#L309)) | Queue | 1 × `Video_FrameInfo` | Video ISR → CvMarvinV1 |
| `s_cmd_queue` ([fretboard_link.c:230](../default/src/actuator/fretboard_link.c#L230)) | Queue | 1 × `uint8_t` (overwrite) | Timing + ManualControl → FretLink |
| `s_state_q` ([perf_log.c:161](../default/src/perf_log/perf_log.c#L161)) | Queue | 128 × 40 B union slot | All perf producers → PerfDrain |
| `s_patch_q` ([perf_log.c:167](../default/src/perf_log/perf_log.c#L167)) | Queue | 8 × `perf_rec_patch_t` | CvMarvinV1 (future) → PerfDrain |
| `s_write_done` ([fretboard_link.c:236](../default/src/actuator/fretboard_link.c#L236)) | BinarySemaphore | — | Host CDC ISR → FretLink |
| `s_ctrl_done` ([fretboard_link.c:239](../default/src/actuator/fretboard_link.c#L239)) | BinarySemaphore | — | Host CDC ISR → FretLink |
| `s_write_done` ([perf_log_sink_cdc.c:50](../default/src/perf_log/perf_log_sink_cdc.c#L50)) | BinarySemaphore | — | Device CDC ISR → PerfDrain |
| `s_mutex` ([log.c:20](../default/src/log.c#L20)) | Mutex | — | log_vprintf() — **`portMAX_DELAY` ⇒ ISR-illegal**, see 2026-05-21 freeze entry |

No event groups, no stream/message buffers, no task notifications anywhere in the codebase today.

**Finding 1 (bug, fix before next test): `DetectorDrain` is double-consuming the bus.** Both `DetectorDrain` (priority 1, [detector.c:30-57](../default/src/detector/detector.c#L30-L57)) and `Timing` (priority 2, [timing_pipeline.c:330,338](../default/src/actuator/timing_pipeline.c#L330)) call `xQueueReceive` on `s_bus_queue`. FreeRTOS queues are single-consumer-per-record by design, so each task sees roughly half the records — the half DetectorDrain gets is silently dropped. Plays nicely with what we observe (gameplay still works, but timing decisions are derived from every other detector publish at best). The 2026-05-20 plan-of-record explicitly said `DetectorDrain` would be deleted when M3 wired the real consumer; that step was missed. Fix is one-line: delete the `xTaskCreateStatic(drain_task, …)` call in `Detector_Initialize` and the supporting `drain_task` function. Throughput logging it provided is moot now that the perf-log path exists.

**Finding 2 (priority inversion): `VideoTask` runs below its consumers.** `VideoTask` is the source of frames `CvMarvinV1` blocks on, but it's at priority 1 while `CvMarvinV1`/`Timing`/`FretLink`/`PerfDrain` all sit at 2. A late-frame condition (TC358743 lock churn, ISC retry) needs `VideoTask` to push the capture-pipeline state machine forward, but any priority-2 work currently runnable will preempt it. The detector then blocks on an empty frame queue while the video task can't run. Empirically not biting because `CvMarvinV1` only runs on frame arrival (event-driven, blocks immediately after consuming), but it's a latent class of stall whenever a priority-2 task ends up briefly busy. `VideoTask` belongs at priority 2 (or above) — the producer of the real-time pipeline shouldn't be in the same priority band as MCC's polling tasks.

**Finding 3 (structural): the priority budget is too narrow.** `configMAX_PRIORITIES = 5` (priorities 0..4). Idle is 0. With time-slicing on (`configUSE_TIME_SLICING = 1`, `configIDLE_SHOULD_YIELD = 1`) and round-robin between equal-priority Ready tasks, current bunching is:

- Priority 4: (unused)
- Priority 3: (unused)
- Priority 2: CvMarvinV1, Timing, FretLink, PerfDrain (4 tasks — real-time path, all event-driven, mostly mutually exclusive)
- Priority 1: 9 MCC polling tasks + VideoTask + DetectorDrain (11 tasks)
- Priority 0: idle

With 11 tasks ready at priority 1 and the tick rate at 1 kHz, each priority-1 task gets ≈ 1 ms of every ≈ 11 ms — tolerable but means each MCC stack does its 10 ms `vTaskDelay` poll on roughly the cadence it asks for plus or minus a tick of jitter. The bigger problem is that we have *zero* headroom for further structure: there's no place to put a faster-than-detector watchdog, a strict-priority audio path, or anything that should clearly outrank `Timing` without sharing. If we ever hit a priority-inversion class problem the kernel can't help via priority inheritance because there's nowhere to invert *to*. Preferred fix is to bump `configMAX_PRIORITIES` to 8 — costs 3 × `sizeof(List_t)` ≈ 60 bytes of BSS per extra band on this port, trivial — and reorganize as:

- 4: real-time strum-critical (Timing, FretLink)
- 3: real-time vision (VideoTask, CvMarvinV1)
- 2: PerfDrain + MCC USB host/device + DRV_USB_UDPHS (anything where stalling means a missed enumeration window)
- 1: MCC display/touch/Legato/SYS_INPUT (UI tasks; visible jitter only, not failure)
- 0: idle

The MCC tasks are dynamic-created but their priority arg comes from a single MCC config knob per task; we can override via the existing `user.cmake` patch list rather than editing `tasks.c` directly. Worth doing alongside enabling analytics — once HWM + run-time-stats are live we'll have the data to confirm the split is correct.

**Analytics enable plan.** Three flags + one macro pair in `FreeRTOSConfig.h`:

```c
#define configGENERATE_RUN_TIME_STATS         1
#define configUSE_TRACE_FACILITY              1
#define configUSE_STATS_FORMATTING_FUNCTIONS  1
#define INCLUDE_uxTaskGetStackHighWaterMark   1
```

`configGENERATE_RUN_TIME_STATS` requires a counter source via two macros:

```c
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  /* SYS_TIME init runs in SYS_Initialize already, no-op */
#define portGET_RUN_TIME_COUNTER_VALUE()          ((uint32_t)SYS_TIME_CounterGet())
```

Same TC0-CH0 source as the perf-log timestamps, so the units are consistent across both surfaces. Cost: `configUSE_TRACE_FACILITY` adds two pointers + a `UBaseType_t` per TCB; on 16 tasks that's a couple hundred bytes. Run-time stats arithmetic on every context switch is one 32-bit subtract + add. Acceptable.

Once enabled, dump on an SBC-style trigger — initially a 10-second-cadence `PERF_REC_TASK_HIGHWATER` from the perf-log drain task is the cheapest path (record format already exists in `perf_log_records.h`), and host-side decoder work makes it visible in the same Gantt view. Stretch: a console UART command to dump `vTaskListTasks` + `vTaskGetRunTimeStatistics` on demand, decoupled from the perf-log path so it works without USB.

Carry-forward "FreeRTOS analytics not yet enabled" note in §Open questions can come out once this lands.

**What landed (findings 1–3 resolved).**

Finding 1 fixed in its own commit: `DetectorDrain` task and its supporting storage deleted from `detector.c`; bus queue create stays. Smoke-tested on hardware before continuing.

Findings 2+3 landed together as a single re-tiering pass after deciding it was cleaner to size the priority budget once with the full layout in mind than to bump VideoTask now and re-shuffle later. `configMAX_PRIORITIES` 5 → 8 (room for 0..7; 60 B BSS for the extra ready-list bands; CLZ-based selection stays constant-time up to 32). Per-task priorities ended up:

| Band | Tasks | Role |
|---:|---|---|
| 7 | (reserved) | Future emergency-stop / hard watchdog |
| 6 | (reserved) | Future soft watchdog / fault recovery |
| 5 | `Timing`, `FretLink`, `USB_HOST_TASKS`, `DRV_USB_HOST_TASKS` | Strum-critical output (and the wire it travels) |
| 4 | `VideoTask`, `CvMarvinV1` | Vision real-time |
| 3 | `PerfDrain`, `USB_DEVICE_TASKS`, `DRV_USB_UDPHS_TASKS` | Diagnostic + perf-log wire |
| 2 | `LEGATO_Tasks`, `DRV_MAXTOUCH_Tasks`, `XLCDC_Tasks`, `SYS_INPUT_Tasks` | UI |
| 1 | (reserved) | Background housekeeping |
| 0 | idle, `APP_Tasks` (self-deletes) | — |

The MCC priorities are set in the per-component yml files (`gfx_legato.yml`, `usb_host.yml`, `drv_usbhs_v1.yml`, `drv_usb_udphs.yml`, `usb_device.yml`, `gfx_maxtouch_controller.yml`, `le_gfx_driver_xlcdc.yml`, `sys_input.yml`) plus `FreeRTOS.yml` for `FREERTOS_MAX_PRIORITIES = 8`, so MCC regen will produce the same `tasks.c` priority arguments — **no MCC re-apply patch needed for this layout**.

Hand-written tasks already had `*_TASK_PRIORITY` constants at file top; updates are one-line each in [video.c](../default/src/video/video.c), [cv_marvin_v1.c](../default/src/detector/cv_marvin_v1.c), [timing_pipeline.c](../default/src/actuator/timing_pipeline.c), [fretboard_link.c](../default/src/actuator/fretboard_link.c), and [perf_log.c](../default/src/perf_log/perf_log.c). Normalized `perf_log.c`'s priority constant from `(tskIDLE_PRIORITY + N)` to a bare integer literal to match the rest of the marvin code; `tskIDLE_PRIORITY` is `0` so this is a stylistic change only.

Hardware behavior post-change: gameplay path unaffected (Easy/Expert smoke-tested). One observation: framebuffer paint is visibly slower to come up during boot, which is expected — Legato is now in band 2, below USB (3/5), VideoTask + CvMarvinV1 (4), and Timing + FretLink (5), so the early-boot window where USB is enumerating, capture is locking, and the detector is spinning up gives Legato less CPU. Steady-state UI responsiveness is unchanged because higher-priority tasks all spend most time blocked.

**Analytics enabled + UART diag dump live.** Flipped `configGENERATE_RUN_TIME_STATS`, `configUSE_TRACE_FACILITY`, `configUSE_STATS_FORMATTING_FUNCTIONS`, and `INCLUDE_uxTaskGetStackHighWaterMark` via the FreeRTOS Harmony component (per-component yml updated). Wired the run-time counter to `SYS_TIME_Counter64Get` with `configRUN_TIME_COUNTER_TYPE = uint64_t` directly in [`FreeRTOSConfig.h`](../default/src/config/default/FreeRTOSConfig.h) — added as patch #8 on the MCC re-apply list. The `uint64_t` choice was forced by hardware: SYS_TIME runs at ~266 MHz, so a `uint32_t` cumulative counter wraps after ~16 s of accumulated CPU time across all tasks and percentages immediately go to nonsense — caught the wrap on the first multi-minute test.
- New [`diag/diag.{h,c}`](../default/src/diag/) module: priority-1 task at 10 s cadence dumps `uxTaskGetSystemState` results line-by-line over the existing `LOG_INFO` channel — task name, state, priority, stack high-water, accumulated CPU (ms), and percentage. Uses a static `TaskStatus_t[24]` array; **does not** call `vTaskListTasks` / `vTaskGetRunTimeStatistics` because both pvPortMalloc internally on every call, and our `heap_1` linker doesn't free — the convenience wrappers freeze the system after ~2 dumps when the 40 KB heap is exhausted (also a static-allocation-rule violation, would have been a footgun even with heap_4).
- Steady-state baseline measured during a multi-minute Easy-mode gameplay session: **97.9% idle**. CvMarvinV1 0.72% (≈ 120 µs/frame at 60 Hz for 5 patches × 5×5 BGR sample + threshold), FretLink 0.34%, Timing 0.10%, LEGATO 0.04%, VideoTask 0.02% (zero-copy DMA confirmed — VideoTask just rotates buffers and notifies subscribers, no per-frame memcpy). Stack high-waters all stable; `PerfDrain` is the tightest at 173 free of 512 (66% used) and worth bumping when producers wire up.
- Still pending: 10 s `PERF_REC_TASK_HIGHWATER` cadence on the perf-log channel (record type already in [perf_log_records.h](../default/src/perf_log/perf_log_records.h)) for off-device replay alongside the rest of the perf-log stream. The on-device UART dump is enough for live tuning today; the perf-log version becomes the durable record once the host decoder lands.

### 2026-05-21 — Perf-log producer-side module landed

Built the perf-log subsystem end-to-end on the producer side, including the real USB-device CDC ACM sink. Producers are not yet wired (module is dark beyond a SESSION + 1 Hz DROP heartbeat), but the wire path is live: a `PERF_REC_SESSION` is re-emitted on every host DTR-rising edge, so reconnecting a terminal mid-session always sees the schema record.

**What landed under [`firmware/marvin/default/src/perf_log/`](../default/src/perf_log/):**
- `perf_log_records.h` — wire format, schema version 1. 16 B common header (`magic` 0x4D56 'MV', `type`, `flags`, `frame_epoch`, `ts_counter`). Record types: SESSION, STAMP, DETECTOR, TIMING, PATCH, DROP, TASK_HIGHWATER. Stage IDs cover ISC_IRQ, VIDEO_PUBLISH, CV_START, CV_END, TP_TICK, FBL_SEND, CDC_WRITE_COMPLETE.
- `perf_log.{h,c}` — façade with separate task / `*FromISR` entry points (mirrors `xQueueSend` / `xQueueSendFromISR`). Two queues sized for the 30× small/large record-size ratio: state queue (slot = 40 B largest small record, depth 128) and patch queue (slot = `sizeof(perf_rec_patch_t)` = 814 B, depth 8). Drop-on-full, never block. Per-queue + sink drop counters RMW under `taskENTER_CRITICAL` / `taskENTER_CRITICAL_FROM_ISR` (the XC32 ARM926 port doesn't link libatomic; GCC's `__atomic_add_fetch` falls through to a libcall and won't link). Drain task at `tskIDLE_PRIORITY + 2` (above idle, below detector/timing) with 2 KB stack; emits one `PERF_REC_DROP` per second.
- `perf_log_sink.h` + `perf_log_sink_cdc.c` — sink interface and USB-device CDC ACM implementation. CRC-16/CCITT-FALSE over `LEN || PAYLOAD`; framing is `0x55 0x4D 0x52 0x56 || u16 LEN || PAYLOAD || u16 CRC`. MCC now ships USB-device + USB-device-CDC class config (UDPHS); the sink owns the application side: opens `USB_DEVICE_INDEX_0`, registers the device-layer event handler (Attach on POWER_DETECTED, register CDC handler on CONFIGURED), and exposes a blocking `WriteFramed`. CDC class events handle GET/SET line coding, control-line state (DTR latched into `s_cls`), and signal `WRITE_COMPLETE` via `xSemaphoreGiveFromISR` on a binary semaphore. Single producer (the drain task); writes are serialized one at a time on a single cache-aligned 832 B staging buffer (header + max patch payload + CRC). DTR-gated: if the host hasn't asserted DTR, frames are dropped into `s_drop_sink`. Write timeout (100 ms) drops + accumulates and the next state-machine pass observes DECONFIGURED if the host went away.
+ Cold-boot-with-cable required an explicit `Detach → 100 ms → Attach` edge after handler registration; relying on the driver's natural VBUS-edge `POWER_DETECTED` event to fire after-the-fact didn't enumerate (suspected: host gave up retrying after observing transient pull-up state during early boot, or the UDPHS driver's `vbusLevel` tracking races with handler-registration timing). The forced edge gives the host an unambiguous device-arrival regardless.
- μs timestamps via `SYS_TIME_CounterGet()` (TC0 CH0 raw counter) — host divides by `timer_freq_hz` from the SESSION record. Explicitly *not* the `xTaskGetTickCount() * (1000000 / configTICK_RATE_HZ)` pattern at [cv_marvin_v1.c:156](../default/src/detector/cv_marvin_v1.c#L156); that's ms-resolution masquerading as μs and is useless for sub-frame attribution.

**Wired into [`app.c`](../default/src/app.c):** `PerfLog_Initialize()` after `Video_Initialize()` (queues exist before any producer can post); `PerfLog_Start()` at the end of `APP_Initialize` (creates the drain task; Harmony brings the scheduler up after `APP_Initialize` returns, so this is the standard "create static tasks before `vTaskStartScheduler`" pattern). `cmake/marvin/default/user.cmake` updated.

**Producers not yet wired** — module is dark today. Next steps land them in this order: (1) ISR producers (`video.c` ISC_IRQ + VIDEO_PUBLISH; `fretboard_link.c` CDC_WRITE_COMPLETE — *no* `LOG_INFO` from this ISR per the 2026-05-20/21 freeze pattern); (2) task producers in `cv_marvin_v1`, `timing_pipeline`, `fretboard_link.FretboardLink_Send`. Replaces the commented-out 2 Hz dump at [`cv_marvin_v1.c:336-351`](../default/src/detector/cv_marvin_v1.c#L336-L351). `PERF_REC_PATCH` emission slots in last (Tier 2 — reuses already-sampled 5×5 patches, no new I/O).

### 2026-05-21 — Perf-logging bandwidth/capacity scoping

Crunched numbers ahead of designing a frame-by-frame perf log (video + detector + actuator state) so we know which resolutions and which transports are actually in scope. Detail below; takeaway is that the spec's already-chosen "sparse keyframes + dense state" point (~1 MB/s) is the only real sweet spot, and the "full-rate raw video" wish is dead on arrival across every transport SAM9X75 has.

**Baseline — 720×480 RGB888 (BGR888-packed in our pipeline) @ 60 Hz**

- Per frame: 720 × 480 × 3 = **1.04 MB**.
- Per second: **62.2 MB/s ≈ 498 Mb/s**.
- All state payload (detector_state_t × 2 detectors + actuator + timing snapshot + per-frame metadata header) is ~150 B/frame ≈ **9 KB/s** — rounding error against pixels. This is a video-bandwidth problem, not a state-volume problem.

**Scaled-down variants (per-second bandwidth)**

| Variant | B/s |
|---|---|
| Full 720×480 RGB888 @ 60 Hz | 62.2 MB/s |
| Same @ 30 Hz | 31.1 MB/s |
| 720×480 RGB565 @ 60 Hz | 41.5 MB/s |
| 720×480 grayscale @ 60 Hz | 20.7 MB/s |
| 360×240 RGB888 @ 60 Hz | 15.6 MB/s |
| 360×240 RGB888 @ 30 Hz | 7.78 MB/s |
| 180×120 RGB888 @ 30 Hz | 1.94 MB/s |
| Strike-line strip 720×80 @ 60 Hz | 10.4 MB/s |
| 5 ROI patches 32×32 RGB888 @ 60 Hz | 922 KB/s |
| State-only (no pixels) @ 60 Hz | 9.1 KB/s |

**Capacity over a session length**

| Stream | 1 min | 5 min (song) | 10 min |
|---|---|---|---|
| Full @ 60 Hz | 3.73 GB | 18.7 GB | 37.3 GB |
| 360×240 @ 30 Hz | 467 MB | 2.33 GB | 4.67 GB |
| 1 keyframe/s + state (≈ spec §4.6) | 62 MB | 311 MB | 622 MB |
| ROI patches 60 Hz | 55 MB | 277 MB | 553 MB |
| State only | 547 KB | 2.7 MB | 5.5 MB |

**Transport ceilings on this hardware** (sustained, realistic)

| Channel | Sustained | Peripheral status |
|---|---|---|
| UART 115.2 kBd (console) | ~11 KB/s | in use (FLEXCOM4) |
| UART 921.6 kBd | ~92 KB/s | EDBG bridge cap |
| UART 3 Mbd (FLEXCOM max) | ~300 KB/s | unlikely through bridges |
| USB CDC ACM (HS device, class-stack overhead) | ~5–15 MB/s | device peripheral unused |
| USB Bulk (HS device, custom class) | ~30–40 MB/s | needs custom host-side reader |
| SDMMC Class-10 via FAT32 | ~5–10 MB/s | already planned for §4.6 keyframes |
| SDMMC UHS-I | ~25–50 MB/s | bus capable, FS overhead bites |
| GMAC Ethernet UDP | ~30–60 MB/s | GMAC unused |
| GMAC Ethernet TCP | ~10–20 MB/s | GMAC unused |

**What fits where**

- **State-only (9 KB/s)** fits everywhere including the console UART — no new transport needed if the goal is just "trace detector + timing + actuator decisions per frame."
- **ROI patches 60 Hz (~1 MB/s)** preserves everything cv_marvin_v1 actually samples; streams comfortably over USB CDC, fits an entire song on SD with room to spare.
- **1 keyframe/s + dense state (~1 MB/s)** is what spec §4.6 already commits to, fits any modern SD card, and survives over USB CDC or Ethernet.
- **Full 60 Hz (62 MB/s)** doesn't fit any transport sustained, *and* a 5-minute song at that rate is 18.7 GB — DOA.

**Implication for what to build next.** The scoping confirms the spec's existing reference-data architecture is the right place to start; perf-logging is a *consumer* of the same infrastructure, not a separate path. Next step is to figure out which questions the perf log actually has to answer (latency attribution? frame-rate stability? detector confidence over time? actuator jitter?) and pick the minimum stream that answers them. State-only might already be enough for the latency/jitter questions; ROI patches are the cheapest way to get "did the detector see what I think it saw" replayability.

### 2026-05-21 — cv_marvin_v1 threshold tuning + timing-pipeline tweaks

First end-to-end gameplay test on hardware. Detector was firing roughly random presses at first; resolved by reading actual signal values rather than guessing.

**Visibility added:**
- State-reactive overlay in `cv_marvin_v1.c` — position rings always; filled center dot when `s_pressed[i]` (white in the hold ring) or `s_edge_active[i]` (fret-color in the edge ring). Lets the user watch chatter live: stable note → steady dot for hold duration; strum window → flash on the edge dot.
- Periodic ~2 Hz log dump (`frame_count % 30 == 0`) showing per-fret `hold_dist / edge_dist / P E` flags.

**Diagnosis (Easy mode, no real B/O presses):**
- Idle hold floor sits at 22–48 across all five frets, **not** the limited-range 16 floor `display_path.md` would suggest. Playfield-glow at the sample sites is well above the limited-range black point.
- Real-press hold spikes to 67–186, leaving a clean gap above the noise ceiling.
- Hold threshold 50 with release-frac 0.6 (release=30) was *below* the idle floor for Y and B → those frets latched permanently pressed and never released.
- Edge threshold 50 was at the peak of real-edge signal (40–53), almost never tripping. timing_pipeline only uses `pressed` today, so dead edges aren't the chatter source — but lowering edge threshold lets `s_edge_active` overlay flicker meaningfully again.

**Settings landed (in `cv_marvin_v1.c`):**
- `CV_HOLD_THRESH`: 50 → **100** (after iterating 65 → 80 → 100; 100 cleanly above noise across all frets, real presses still spike to 150–186).
- `CV_HOLD_RELEASE_FRAC`: 0.60 → **0.78** (release ≈ 78 — above the noise ceiling, comfortably below real-press floor).
- `CV_EDGE_THRESH`: 50 → **25** (gives the overlay an honest edge indicator without affecting pipeline behavior).
- Sample coords nudged 1 px on G/R/Y/B/O edges + B hold for slightly cleaner alignment.

**Performance:** Expert-mode play is now quite good — clean note recognition, fret hold-through across consecutive same-fret notes confirmed by trace through `process_releases` (release suppressed when the next chord-commit's `note_q` entry already needs the bit, within `TP_STRUM_DELAY_MS − TP_CHORD_WINDOW_MS = 190 ms` of detector-release-to-detector-press).

**Timing-pipeline tweaks** (Greg's hand-edits): `TP_STRUM_PULSE_MS` 50→25, `TP_CHORD_WINDOW_MS` 20→30, `TP_FIFO_CAP` 16→32.

Open question added above: `TP_STRUM_DELAY_MS = 220 ms` is Expert-tuned; Easy/Medium/Hard may need different values because note travel time differs.

### 2026-05-21 — Manual fretboard-control producer + timing-pipeline gate

Stood up the actuator-side half of a basic on-device manual-control surface — primarily for game-menu navigation (start a song, advance menus, hit pause) where single-finger input is the natural shape. UI half waits on a Microchip Graphics Composer regen.

What landed:

- `actuator/manual_control.{h,c}` — second producer of `FretboardLink_Send`, alongside `timing_pipeline`. Internal state: `s_fret_mask` (bits 0..4) and `s_strum_mask` (bits 5..6). Public API: `Initialize / SetEnabled / IsEnabled / SetFret / SetStrum`. Both fret and strum are momentary — held while the UI button is down, cleared on release. `recompute_and_send` only calls `FretboardLink_Send` while enabled.
- `TimingPipeline_SetEnabled(bool)` — output gate on `publish_mask`. Pipeline still advances internal state when gated, so the next `advance()` (≤ `TP_TICK_MS` = 5 ms) after re-enable republishes the right mask without a stale frame.
- `ManualControl_SetEnabled` ordering: enter manual mode = gate timing pipeline first, zero local state, send `0`, set enabled flag (so a stale pipeline frame can't race a manual zero on the wire); exit = clear enabled flag, zero state, send `0`, ungate pipeline.
- App init wires `ManualControl_Initialize()` after `TimingPipeline_Initialize()`. No Legato dependency at this stage.
- `cmake/marvin/default/user.cmake` adds `manual_control.c` and `ui/manual_input.c`.

UI half landed same session after Greg ran the Composer regen. Composer added 8 buttons to `Screen0` — `Screen0_Button_Manual_{Green,Red,Yellow,Blue,Orange,StrumDown,StrumUp,Enable}` — plus 15 extern `event_Screen0_Button_Manual_*` handler declarations the application must define. The binding shape came out simpler than the plan called for: `screenShow_Screen0` itself calls `setPressedEventCallback` / `setReleasedEventCallback` with those extern symbols, so the linker resolves them and **no `ManualInput_Bind()` walk is needed**. `ui/manual_input.c` is just the 15 function bodies forwarding to `ManualControl_SetFret / Strum / SetEnabled`. The originally planned `APP_Tasks` poll-loop for null widget pointers is moot — `APP_Tasks` keeps the existing `vTaskDelete(NULL)`.

Strum buttons are momentary, not one-shot: holding the strum button keeps the fretboard's strum line asserted, which Guitar Hero / Rock Band menus interpret as auto-repeat scrolling — the primary use case for this surface. Gameplay-path strums still pulse correctly because they go through `timing_pipeline`, which has its own `TP_STRUM_PULSE_MS` one-shot. The Enable button is `setToggleable(LE_TRUE)` with only `OnReleased`; both the widget and `ManualControl` default to off, so a simple `SetEnabled(!IsEnabled())` flip stays in sync without querying the widget's toggle state.

Plan agent flagged two real blockers worth recording: (1) Legato's input pipeline routes each touch to one focus widget — multi-finger momentary chords aren't possible without a custom dispatcher, so the model is single-finger only; (2) the lazy widget-pointer init means any code that walks the `Screen0_*` pointers has to run post-`screenShow_Screen0` — moot here because Composer does the wiring itself, but still true for any future shim that needs to read or modify widget state.

Decision-log entries above cover the producer-as-peer pattern, the Composer-driven UI choice, and the single-finger input model.

**Bring-up over the wire.** Marvin enumerates the EDBG CDC and `FretboardLink_Send` round-trips cleanly through the queue, but the fretboard MCU's SERCOM1 wasn't receiving anything. Two real fixes:

1. **EDBG bridge needs DTR raised.** The bridge holds its UART TX idle until the host asserts DTR. Added `USB_HOST_CDC_ACM_ControlLineStateSet` with `dtr=1, carrier=1` to `open_cdc()` after the existing `LineCodingSet(115200)`. Baud matters here — fretboard runs a real UART at 115200, not USB-CDC-ignored.
2. **Control-pipe completion handlers run from ISR context.** First instrumented attempt logged `LOG_INFO` directly from the new `SET_LINE_CODING_COMPLETE` / `SET_CONTROL_LINE_STATE_COMPLETE` event cases; system froze mid-print. Microchip's CDC stack dispatches *all* class events from ISR (the existing `WRITE_COMPLETE` case already uses `*FromISR` primitives — that was the precedent). `log_vprintf` takes a mutex with `portMAX_DELAY`, illegal from ISR. Stripped both the diagnostic logging and the latch infrastructure once the link was confirmed functional. Captured for the journal because it's a recurring shape: any new event-handler case in this stack must stay strictly ISR-safe.

### 2026-05-20 — USB CDC host attach: EDBG composite enumerates, CDC class driver now binds

Picked up where the prior session left off — VBUS not asserting, no device detected. Closed both halves.

**VBUS.** MCC's `DRV_USB_VBUSPowerEnable` callback is wired in the host driver but the host stack never invokes it on this build. Asserted the two VBUS GPIOs (`VBUS_AH_PC27_PowerEnable_Set` / `VBUS_AH_PC31_PowerEnable_Set`) directly from `APP_Initialize` ahead of consumer init and `USB_HOST_BusEnable`. EDBG immediately enumerates.

**Class-driver binding.** Enumeration succeeded but the CDC attach handler never fired. Added per-interface descriptor logging inside `F_USB_HOST_UpdateDeviceTask` to see what was actually being matched. Output revealed VID `0x03EB` PID `0x2175` — an EDBG composite with five interfaces: HID/CMSIS-DAP, CDC ACM IAD pair (comm + data), vendor, MSC. The IAD passed TPL match and got assigned to the CDC driver, but the CDC instance still flipped to `STATE_ERROR` without our app-level handler ever seeing the attach.

Root cause was inside Microchip's CDC class driver: `F_USB_HOST_CDC_InterfaceAssign` (both single-interface and IAD paths) requires the comm interface to declare `bInterfaceProtocol == USB_CDC_PROTOCOL_AT_V250` (`0x01`). EDBG declares `0x00` (none) — functionally identical CDC ACM in practice; Linux/macOS accept either. The TPL/IAD pre-check passes (TPL wildcards subclass/protocol), the CDC driver gets the interface group, then immediately rejects it on the AT-V.250 check. The interrupt pipe never opens, the instance errors out, and the app-level listener stays silent.

Fix: relaxed both checks to `(AT_V250 || NO_CLASS_SPECIFIC)`. After the patch the FBL log shows `CDC device attached, handle opened` and `c=Y` heartbeat as expected. Logged as patch #7 in the MCC re-apply list.

Also tested a hypothesis from the prior session that `USB_HOST_Initialize` had to run *after* the EHCI/OHCI driver inits. With the diagnostic logging in place, MCC's emitted order (`USB_HOST_Initialize` → `Legato_Initialize` → driver inits) enumerates and binds the CDC IAD just as well — the host layer evidently looks up HCD interfaces lazily at `USB_HOST_BusEnable` time. Backed out the hand-edit; one fewer re-apply patch to maintain. Decision-log entry above.

Diagnostic instrumentation stripped at the end of the session: the per-interface descriptor dump and enumeration state-change logger in `usb_host.c`, and the periodic OHCI/EHCI register heartbeat in `fretboard_link.c`. What remains on disk: the two `usb_host_cdc.c` protocol-relaxation lines (patch #7), the app-level VBUS asserts, and a minimal `USB_HOST_EVENT` printf so future host-stack drama still surfaces in the log.

Next session: revisit timing-pipeline behavior end-to-end now that the actuator wire is live.

### 2026-05-20 — Hardware bring-up after USB CDC host regen: TC358743 wedge resolved

First on-target test after the M2 actuator path landed. Symptoms: video stream not starting, USB ports unpowered, FBL heartbeat firing with `connected=N`. Boot stopped after `TC358743: probe starting`.

Root cause: the USB MCC regen reordered SYS_Initialize so `MMU_Initialize` and `AIC_INT_Initialize` ran *after* `TC0_CH0_TimerInitialize`, `FLEXCOM6_TWI_Initialize`, and `XLCDC_Initialize`. With AIC initialized late, the FLEXCOM6 interrupt vector wasn't owned by the AIC when the first I²C transfer ran; `OSAL_SEM_Pend(transferDone, WAIT_FOREVER)` blocked forever waiting for an ISR that never fired. FBL stayed alive because it's pure FreeRTOS — no peripheral interrupt dependency.

Fix: reverted SYS_Initialize order so MMU/AIC/WDT-disable run before TC0/FLEXCOM6/XLCDC. Display + capture came back on first boot. Logged as patch #6 in the MCC re-apply list above.

USB enumeration is still not working (VBUS not asserting, no device detected). That's a separate investigation — next session.

### 2026-05-20 — Static-allocation conversion, cv_marvin_v1 port, USB CDC host bring-up

Long session covering three independent threads:

1. **Static-allocation conversion.** Adopted project rule: marvin code never uses dynamic FreeRTOS APIs. MCC regenerated with `configSUPPORT_STATIC_ALLOCATION=1` and `vApplicationGetIdleTaskMemory`. Converted `log.c`, `video.c`, `detector.c`, `cv_marvin_v1.c` to `xTaskCreateStatic` / `xQueueCreateStatic` / `xSemaphoreCreateMutexStatic` with per-module `Static{Task,Queue,Semaphore}_t` plus storage arrays at file scope. FreeRTOS heap stays at heap_1 only because MCC tasks still need it. Also fixed two latent video.c issues surfaced by the build: forward-declared `s_capture_armed/s_display_bound` above the ISR that reads them, added missing `#include <stdbool.h>` to `video.h`.

2. **cv_marvin_v1 reference port.** Ported `tools/fret-tuner/detect_video.py` to C — 5×5 patch BGR sampling, brightness-based hold detect with hysteresis (`HOLD_THRESH=50`, release at 60%), color-filtered edge detect with rising-edge `press_count`, per-fret BGR target/reject filter table. Coords scaled from Python's 1920×1080 anchor to 720×480 (Wii 480p60) at port time — same Elgato direct-pixel topology, just different resolution. Bus record stays canonical (option A): only `pressed / confidence / raw_value` cross the bus; `press_count` and raw color distances stay private to the module. Recording-format choice (custom packed binary vs nanopb vs CBOR) deferred to M5. ISC framebuffer pool moved from `.region_cache_aligned` to `.region_nocache` so CPU readers see DMA writes coherently; cap dropped 1080p→720p to fit the 16 MB nocache region.

3. **USB CDC host MCC regen.** Decided on USB CDC (marvin host, fretboard device) for the actuator/detector link instead of FLEXCOM UART — single cable, auto-enumeration, host-PC-debuggable. MCC regenerated with EHCI+OHCI host drivers and CDC host class. VBUS power-enable pins on PC27+PC31 (one per port). Hand-edit needed in `initialization.c` `DRV_USB_VBUSPowerEnable` because MCC assumes a single VBUS pin named `VBUS_AH` and emits one Set/Clear call; with two pins we need both per-pin macros explicitly. Logged as patch #5 in the MCC re-apply list.

Spec update: added §4.8 game-state awareness & control as a marvin subsystem (peer of detection but separate bus, video-frame consumer not a detector). Defers recognizer algorithm + arbitration boundary as Q10/Q11.

Next session: USB device CDC on fretboard side, then wire-protocol design (framing, command set, baud, ack semantics) before either host or device code is written.

### 2026-05-20 — Video fan-out + per-frame buffer routing

Two-commit sequence opening up video capture for parallel CV consumers:

1. **Per-frame buffer routing + multi-subscriber.** Found that `ISC_Capture_GetBufferAddress()` always returned the base, so HEO and `cv_marvin_v1` were both reading slot 0 only — slot 1 written and silently lost. Bumped the ISC frame callback signature to deliver the just-completed buffer address (computed from `iscObj->frameIndex`, which the driver pre-increments). `video.c` now re-points HEO in the ISR every frame and fans out the frame info to a static array of 4 subscribers.
2. **Ring depth from 2 to 4.** Cheap DDR cost; gives slow consumers ~50 ms read window before lapping.

Deferred: per-task XDMAC sub-region copies for slow / sub-region consumers (e.g., menu/score readers). Will add when the first such consumer arrives.

### 2026-05-20 — M1 scaffolding landed

Stood up the detector subsystem skeleton. New module at `default/src/detector/`:

- `detector.h` — `detector_state_t` (canonical layout from spec §4.2.3), `fret_t` enum, `detector_id_t` enum, `Detector_Initialize`, `Detector_BusQueue`.
- `detector.c` — owns `xDetectorStateQueue` (depth 8, holds `detector_state_t`) and a temporary `DetectorDrain` task that consumes the bus and logs records/sec at INFO. Drain task is M1-only scaffolding; the timing pipeline (M3) will replace it as sole consumer.
- `cv_marvin_v1.{h,c}` — owns its FreeRTOS task. Creates a depth-1 frame queue, calls `Video_SubscribeFrames`, blocks on `xQueueReceive`, publishes one `detector_state_t` per frame with `frame_epoch = frame.frame_count`, `timestamp_us = xTaskGetTickCount() × CV_US_PER_TICK`, all-zero `fret[]`.

Wired from `app.c`'s `APP_Initialize` after `Video_Initialize`. Added both `.c` files to `cmake/marvin/default/user.cmake`. No code change to video or capture; `Video_FrameInfo.bytes_per_pixel` already reports 3 (RGB888 packed).

Spec touch-up: §4.2.2 "BGRX32 frames" → "RGB888-packed (3 B/pixel) frames". Surfaced the broader spec drift on capture format as a carried-forward open question — deserves a doc-only sweep before M5 (recording schema depends on it).

Added per-detector enable/disable + single-active selector to the bus API (`Detector_Enable`/`Disable`/`IsEnabled` + `Detector_SetActive`/`GetActive`). Defaults all-disabled; app explicitly enables `DETECTOR_CV_MARVIN_V1` and selects it as active. cv_marvin_v1 still drains its frame queue when disabled but skips publishing. Future video CV tasks (menu, score) go in as video-frame consumers, not detectors — needs a multi-subscriber upgrade to `Video_SubscribeFrames` before the second one lands.

Next session: pick Q8 (initial CV algorithm — leaning pixel-mean threshold per ROI as the simplest end-to-end bring-up) and start populating the `fret[]` field in cv_marvin_v1. ROI geometry calibration also needs a home — spec §4.7 (system config) is the planned destination.

### 2026-05-20 — System spec drafted

Stepped back from per-feature work and scoped marvin's role across the whole guitar-playing-robot system. Output is [`spec.md`](spec.md), a durable system-level description (versus this journal as running diary). Drafted in document order: §1 purpose & scope, §2 system context (three-tier diagram, ownership table, latency budget), §3 hardware platform inventory, §4 all seven subsystems (4.2 CV detection + detector-state bus and 4.6 reference-data recording fully drafted; 4.1 video capture summarized referencing existing docs; 4.3–4.5 + 4.7 sketched). §5–9 stubbed.

Settled architectural decisions (all in the decision log above):
- Marvin replaces fret-tuner PC as runtime brain.
- Marvin is the reference detector; recording is first-class.
- Multiple coexisting detectors on one canonical bus.
- Timing pipeline centralized on marvin with a fretboard-takeover fallback mode.
- Reference data = SD card + detector-state + sparse keyframes (60-frame stride, raw BGRX32).
- `frame_epoch` is the master sync token.
- Operating modes are independent toggles, not a state machine.

Open questions moved into spec §9: UI framework choice (Q5), initial CV algorithm (Q8), config persistence location (Q9), Ethernet ref-data live stream (Q2; deferred post-M8).

Next session resumes at **M1 — Reference detector v0** (one CV detector running on captured frames, publishing `detector_state_t` records). No code changed today.

_(Earlier session entries — 2026-05-15 back through 2026-05-01 — are in [`journal-archive.md`](journal-archive.md).)_
