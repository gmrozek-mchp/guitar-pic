# fauxmote — Working Journal

Running log of planning, decisions, open questions, and work-in-progress for fauxmote. Newest entries at the top. For *what fauxmote is* (purpose, hardware, interfaces, milestones), read [`../SPEC.md`](../SPEC.md) — this journal does not duplicate it.

---

**2026-08-09 — `reconnect` is now a no-op when the link is already up.** `Fauxmote_Reconnect()` (`main/bt_hid_device.c`) bails out if `Wiimote_IsConnected()` (HID interrupt channel open) or if `s_reconnecting` is still set from an attempt in flight; the bonded-address guard is unchanged. Reason: a client connect on top of a live session risks dropping it and leaks an L2CAP slot (servers are armed per pairing window, so slots don't come back), and `MF_CMD_RECONNECT` arriving repeatedly from marvin could stack up connects. `s_reconnecting` is cleared in the `L2CAP_OPEN` handler before the interrupt-channel connect, so the in-progress window is short and a genuinely stalled attempt can be retried. Guard sits in the one entry point, so both callers (CLI `reconnect`, marvin-link `MF_CMD_RECONNECT`) get it. **Pending build + on-hardware check.**

---

**2026-08-08 — T1S SPI dropped 15 → 4 MHz. TEMPORARY, test hardware only — do not carry into the real system.** `T1S_SPI_HZ` in `main/mf_t1s.c`. The T1S Click board on the current test rig is unreliable at the higher rate; 4 MHz is a workaround for that board, not a change to the link design. Revert to 15 (or pick a rung deliberately) when running against good hardware.

Worth knowing for whoever reverts: the ESP32 SPI2 divider is an integer off the 80 MHz APB, so only 80/N is reachable and IDF snaps to the *nearest* rung, rounding up if that's closer. **The old 15 MHz request was actually clocking 16 MHz** (80/5 = 16 beats 80/6 = 13.33), which is what Greg measured. 4 MHz is exactly 80/20, so it lands dead-on. Rungs: 40 · 26.67 · 20 · 16 · 13.33 · 11.43 · 10 · 8.89 · 8 · … Two ceilings if raising it: LAN8651 SCLK maxes at **25 MHz** (`docs/t1s-podl-link.md` §67), and the configured pins (SCK 5 / MO 19 / MI 21) are **not** SPI2's IOMUX pins on the ESP32 (14/13/12), so signals route through the GPIO matrix — the added MISO input delay in full-duplex is the practical limit well below 40 MHz. `spi_device_get_actual_freq()` reports what the divider really produced.

Rate is per-node and buys nothing by matching marvin — each node's SPI is a private link to its own LAN8651, and the 10 Mbit/s T1S wire is the shared bottleneck. A 68-byte TC6 chunk goes ~34 µs → ~136 µs, still well inside the 2 ms `T1S_POLL_MS` service cadence.

---

**2026-08-04 — extended heartbeat to v2: report per-node telemetry for marvin's bus-stats UI.** Same contract as the PIC followers (`mf_t1s.c`, ESP-IDF side). `0x88B6` payload 8→20 bytes (`T1S_HB_VERSION` 1→2, `T1S_HB_LEN` 8→20): append LE `tx_count_u32, rx_count_u32, crc_err_u16, sym_err_u16`. `s_tx_count` already counted all sends (HB + mf) and is reported as-is; `s_rx_count` moved up to count **all** received frames (was mf-branch only); new `s_crc_err`/`s_sym_err` in `TC6Regs_CB_OnEvent`. Wire contract: `docs/t1s-podl-link.md` §7.2; marvin parses gated on length → standalone reflash. App logic only. **Pending build + on-hardware check.** Completes Phase 2 — all six follower nodes now emit the v2 heartbeat.

---

**2026-07-28 — fixed T1S command RX: min-frame padding was rejected by exact-length checks.** With the marvin link on the `0x88B7` T1S controller channel, `rx` climbed steadily (marvin's GUITAR floor-refresh + commands were arriving and passing the MAC filter) but nothing actuated. The T1S MAC-PHY pads short frames to the 60-byte Ethernet minimum, and `mf_t1s.c` clamps the received payload to `MF_MAX_PAYLOAD` (8), not the per-type length — so `MfLink_HandleMessage` saw len 8 for a 3-byte GUITAR / 1-byte LINK_CMD and its `!=` checks dropped everything. Changed those per-type checks to `>=` (`len < MF_LEN_*` rejects), matching the design note that the message layer ignores trailing pad. UART path unaffected (LEN framing gives exact lengths). Mirror of the same fix on marvin's `latch_status` (STATUS uplink). File: `main/mf_link.c`. **Pending Greg's build.**

---

## Current focus

**Phase 1 DONE — the Wii pairs with fauxmote and opens both HID channels.** Custom
Wiimote SDP record (built via Bluedroid's internal `SDP_*` API in `wiimote_sdp.c`,
with SDP buffers raised to 512/1024) + raw `esp_bt_l2cap` servers on PSM 0x11/0x13.
On a red-SYNC press the Wii: accepts our SDP, completes legacy PIN pairing
(`auth OK`), and opens both HID L2CAP channels (control CID 0x40 / interrupt CID 0x41,
both `mtu 640`), then starts sending us data. Wii BD_ADDR `00:17:ab:07:2c:21`.

**Phase 2 DONE — the Wii fully initializes fauxmote as an assigned Wiimote.**
`wiimote.c` answers the Wii's output reports (`0x17` read→`0x21`, `0x15`→`0x20`
status, `0x16` write→`0x22` ack, `0x11` LEDs, `0x12` reporting mode, `0x13`/`0x1a`
IR-enable acks) and streams `0x30` core-button reports (~15 ms) for keep-alive.
On hardware: clean full handshake, the Wii assigns a player slot (`0x11`), and the
GPIO13 status LED (no real player LEDs on a Feather) goes **solid = assigned**
(heartbeat=waiting, fast-blink=connected). IR is acked but not implemented (only
needed for the pointer, not guitar gameplay).

**Core-Wiimote + test-CLI workstream (before guitar).**

- **Step A DONE — manual test CLI + connection management.** `esp_console` REPL
  (`console_cli.c`): `pair`/`stop`/`reconnect`/`unlink`/`status`/`btn`/`tap` — all
  thin pass-throughs over the module API (logic lives in `wiimote.c`/`bt_hid_device.c`,
  so Marvin can drive the same API later). Boots idle; `pair` arms the HID listeners
  for that sync window and `stop` tears them down; servers are **armed per pairing
  window**, not at boot or auto-re-armed (re-arming leaked L2CAP slots, since
  `reconnect` uses client connects). Honors the Wii's requested reporting mode
  (`0x12` → builds that report ID; buttons populated, accel/IR/ext zeroed). Disconnect
  fully resets state (`Wiimote_NotifyDisconnected`) and readers exit by L2CAP handle
  (robust to fd reuse). Bond persists in NVS (recalled at boot via
  `esp_bt_gap_get_bond_device_list`); `unlink` removes it. Status LED on GPIO13:
  blip/~3 s = idle, fast blink = pairing/connecting, N flashes = assigned player N.
- **Step B DONE — IR pointer.** Pointer `(x,y)` is a state variable in `wiimote.c`
  (`Wiimote_SetPointer`/`ClearPointer`), CLI `point x y` / `point off`. `build_ir_extended`
  synthesizes two sensor-bar dots into mode `0x33`'s 12-byte extended-IR field + a
  level accel. **Verified on hardware** — navigated the Wii menu and launched GH3 with
  it. Calibration constants (`IR_X/Y_CENTER/HALF`, from on-hardware edge measurement)
  map pointer 0..1 to the full screen, (0,0)=top-left. Basic-IR modes `0x36`/`0x37`
  are now filled too (`build_ir_basic` + shared `ir_dots`); the byte packing is
  verified against Dolphin's `IRBasic` struct and the Linux `hid-wiimote` decoder.
  Pointer tracks correctly on the main Wii screen and its Home overlay (basic `0x37`),
  so one calibration covers both modes (see the GH3-pointer note under Phase 3).

**Phase 3 DONE — guitar extension plays GH3 (detection + frets + strum + whammy).**
On hardware: GH3 detects the guitar and frets/strum/whammy register in-game.

- **Module split.** The guitar lives in its own `guitar.c` behind a small interface
  (`wiimote_ext.h`: `wiimote_extension_t` = register bank + `build_report`/`set_button`/
  `reset`; `Wiimote_RegisterExtension`). `wiimote.c` is now extension-agnostic (serves
  the registered bank for `0xa4xxxx` reg I/O, calls `build_report` for the ext field,
  delegates unknown button names to `set_button`). Buttons drive by name through
  `Wiimote_SetButton`/`TapButton` (green/red/yellow/blue/orange/strumup/strumdown/
  gplus/gminus/pedal); CLI `btn`/`tap`/`whammy`/`ext` are thin pass-throughs. Adding a
  nunchuk/classic later is just another self-registering `*.c`.
- **Report bit layout** (buttons **active-low**, rest = 1; verified against wiibrew +
  Linux `hid-wiimote`): byte0/1 = stick X/Y (bits5-0); **byte0/1 bits7-6 = 1 to
  identify a GH3 Les Paul** (0 = GHWT — with them clear GH3 treats us as GHWT and
  ignores the strum bar, which cost real debugging time); byte2 touchbar; byte3
  whammy; byte4 bit6 BD / bit4 B− / bit2 B+; byte5 bit7 BO / bit6 BR / bit5 BB /
  bit4 BG / bit3 BY / bit2 pedal / bit0 BU. GH3 streams mode **`0x37`** (not `0x34`);
  the ext field is filled for every ext-bearing mode (`0x32/34/35/36/37/3d`).
- **ENCRYPTION is required and now implemented** (`ext_crypto.c/.h`). GH3's actual
  handshake (from a register trace), *not* the old `0→0x40` init the plan guessed:
  `0x55`→`0xf0` (disable) → read ID at `0xfa` in the clear (decide GH3 vs GHWT) →
  `0xAA`→`0xf0` (re-enable) → 16-byte key to `0x40`-`0x4f`. Everything the game then
  reads (streamed ext bytes + the `0x20` calibration) is decrypted with that key, so
  plaintext reads as garbage. The base Wiimote captures the key, derives the ft/sb
  tables, and encrypts outgoing ext data (streamed bytes at reg offset `0x08`; reg
  reads at `addr & 7`); the guitar module stays plaintext. Cipher = the marcan Wii
  extension cipher; **rnconrad's `wm_crypto.c` tables are corrupted** (sbox[3]@0x60,
  [4]@0xd8, [6]@0x88/0xec — `0xD0`/`0xF0` & `0xD2`/`0xE2` flips, not valid
  permutations), so the tables were taken from **Dolphin's 1st-party** set and the
  algorithm verified term-for-term against Dolphin; round-trip validated against GH3's
  real key (idx 0 matched, encrypt→decrypt identity). Earlier "green/red work without
  encryption" was a decryption fluke.
- **Guitar-mode pointer (informational, not pursued).** Basic-IR pointer is correct on
  the main Wii screen + its Home overlay. *Inside GH3*, the Home-overlay cursor scales
  wrong — X over-sensitive, Y under, **opposite per axis** — which is GH3 applying its
  own pointer transform, not our IR (packing is verified identical to extended). Moot
  in practice: **with the Wiimote seated in the guitar the IR camera is physically
  blocked**, so guitar-mode Home nav uses the **analog stick (SX/SY)** instead. Kept a
  single IR calibration. Deferred idea: drive `SX/SY` (guitar module already owns them,
  fixed at center `0x20`) for guitar-mode Home navigation if Marvin ever needs it.

**Phase 4 IN PROGRESS — marvin command link (fauxmote side done; now T1S-capable).**
The link is a second front-end beside the CLI that drives the existing
`Wiimote_*`/`Guitar_*`/`Fauxmote_*` APIs — `GUITAR` (fret/strum/whammy/aux), `WIIMOTE`
(core/D-pad/stick, via new `Guitar_SetStick`), `LINK_CMD`, `ACCEL`/`POINTER` slices,
and a `STATUS` uplink (on-change + 500 ms heartbeat). One 200 ms link watchdog reverts
all inputs to safe defaults when the link goes quiet.

The transport-neutral message layer now lives in `mf_link.c` (`MfLink_Init`/
`HandleMessage`/`Service`); a Kconfig `choice` selects the backend that provides
`MarvinLink_Start()` + the `mf_send_fn`:
- **T1S (default)** — `mf_t1s.c`: LAN8651 MAC-PHY over SPI (SPI2_HOST, mode 0, 4 MHz — temporarily lowered for the test rig, see the 2026-08-08 entry)
  driven by the vendored OPEN Alliance TC6 library. PLCA follower id `CONFIG_FAUXMOTE_T1S_NODE_ID`
  (default 3), MAC `02:00:00:00:00:<id>`; each mf_proto message = one Ethernet frame on
  ethertype **`0x88B7`** to the coordinator, plus a 500 ms `0x88B6` heartbeat (node_type 3).
  Feather V2 pin defaults SCK=5/MO=19/MI=21/CS=33/RST=27/IRQ=32 (all Kconfig-overridable).
- **UART (fallback)** — `marvin_link.c`: the original UART1 task (Feather RX=`GPIO7`/
  TX=`GPIO8`, 1 Mbaud) with `SOF/TYPE/LEN/CRC8` framing.

**UART path verified on hardware: builds, boots, the Wii connects with the link task
running.** T1S path is written ahead of hardware (LAN8651 not yet wired); not yet driven
by marvin over either transport (coordinator side + bring-up are later sessions). Wire
protocol: `docs/marvin-fauxmote-link.md`; T1S framing: `docs/t1s-podl-link.md`.

**Link stability (done):** idle disconnects are fixed — Bluedroid's JV idle→sniff
delay (default 5 s) is overridden to 65 s via a `-D BTA_FTC_OPS_IDLE_TO_SNIFF_DELAY_MS`
in the top-level CMake (`#ifndef`'d `#define`; the timeout field is UINT16 + a 197 ms
offset, so stay < ~65338). With sniff effectively off the link stays active (Wii keeps
polling), so no supervision timeouts and no BTA PM-slot leak (the leak that previously
forced a power-cycle). **Auto-reconnect was removed** (2026-06-13): it didn't survive
the GH3 launch and is the wrong tool for keep-awake. Recovery from an unexpected drop
is now the explicit `reconnect` command; keep-awake is better served by occasional
input/state changes (which Marvin's command stream provides during use).

**OPEN — GH3 game-launch handoff (needs a BT sniffer):** when a game disc boots, the
Wii goes HID-silent toward fauxmote for many seconds, then drops the link (`rsn 0x08`).
A manual `reconnect` re-establishes the link, but the session isn't reliably usable in-game.
A real Wiimote rides the handoff with a quick ~2 s LED off/on. fauxmote's own logs
don't show *why* the Wii goes silent on us vs a real Wiimote — there's no command we
visibly mishandle (zero RX reports during the gap). Diagnosing needs a Bluetooth
sniffer to diff a real Wiimote vs fauxmote through the GH3 launch. **Workaround for
now: restart fauxmote after the game has started** (a fresh boot reconnects cleanly).
The recurring `mode 3 … BASIC mode` reconnect warning is likely a red herring (real
Wiimotes use Basic-mode L2CAP for HID). Revisit after the guitar extension — GH3 may
engage the controller differently once it sees a guitar.

**Other notes:**
- **Keep-awake:** the Wii idle-disconnects on lack of *input*; streaming reports +
  occasional button activity keeps it alive (free during gameplay).
- **Caveat:** a Wiimote cannot power a Wii on from standby (BT radio off); "wake" only
  means reconnecting while the console is already running.

Phase progression and success criteria are in [`../SPEC.md`](../SPEC.md) §6.

---

## Decision log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-07-28 | **The marvin link moves onto the shared 10BASE-T1S PLCA bus (LAN8651 MAC-PHY over SPI); UART is retained as a build-flag fallback.** Transport is selected by a Kconfig `choice` (`FAUXMOTE_LINK_TRANSPORT_T1S` default / `_UART`), mirroring marvin's `MARVIN_FRETBOARD_TRANSPORT`. The message layer (`mf_proto.h` TYPE + fixed payload, latest-wins slices, 200 ms watchdog, STATUS uplink) is unchanged and now lives in a transport-neutral `mf_link.c`; each transport backend (`marvin_link.c` UART / `mf_t1s.c` T1S) provides `MarvinLink_Start()` and an `mf_send_fn`. | Puts fauxmote on the same wire as the guitar (id 2) and detector (id 1) followers instead of a dedicated UART, so the coordinator drives every actuator over one bus. Splitting the message layer out keeps the wire semantics byte-for-byte identical across transports — only the framing binding changes (Ethernet FCS replaces `SOF`/`LEN`/`CRC8`). Keeping UART as a one-flag fallback preserves the proven bring-up path. |
| 2026-07-28 | **Bus renumber: fauxmotes take PLCA ids 1–2 (at most two), and the target full-bus table becomes guitar 3, fretboard 4, beatbox 5, lemmy 6, lightshow 7 (coordinator stays 0).** `CONFIG_FAUXMOTE_T1S_NODE_ID` now defaults to 1, range 1–2. This pass changes **fauxmote + docs only**: the already-flashed guitar (id 2) and fretboard (id 1) firmware and marvin's coordinator node table still run the old ids — renumbering those three to 3/4 is a coordinated follow-up (all re-flash together). Until then a default-id-1 fauxmote collides with the fretboard's current id 1, so don't co-bus them yet (use id 2, or renumber the fretboard first). Full table: [`docs/t1s-podl-link.md`](../../../docs/t1s-podl-link.md) §7.1. | Controllers grouped low (1–2) keeps the actuator/detector/future nodes in a tidy contiguous block; capping fauxmotes at two matches the real build. Deferring the guitar/fretboard renumber avoids re-flashing working nodes in a session scoped to fauxmote. |
| 2026-07-28 | **fauxmote's T1S data channel uses a new dedicated ethertype `0x88B7`; heartbeat node_type = `3` (controller).** Each mf_proto message = one Ethernet frame `[dst][src][0x88B7][TYPE][payload…]` to the coordinator MAC `02:00:00:00:00:00`; this node's MAC is `02:00:00:00:00:<id>`. Heartbeat stays on `0x88B6` with node_type code 3 beside 1=detector, 2=guitar. (Node-id assignment superseded by the bus-renumber entry above.) | A distinct ethertype keeps the fauxmote channel cleanly demultiplexable from the guitar/detector traffic at the coordinator, and per-node ids/MACs let several fauxmotes share the bus later (one PLCA node each). Reuses the existing heartbeat frame format so the coordinator's presence table just gains a controller type. |
| 2026-07-24 | **The ACCEL slice carries the device's acceleration in g, not tilt/star-power semantics.** Wire = 3 B, signed int8 2's-complement g per axis (X/Y/Z), `1 LSB = 1/32 g` (`+1 g = +32`, range −4.0..+3.97 g), level `{0,0,+32}`. fauxmote translates g → raw report bytes with its own advertised calibration (`raw = clamp(0x85 + g·27/32, 0, 255)`); `MF_AUX_STARPOWER` is ignored on fauxmote. | fauxmote emulates a Wiimote + guitar extension, so it exposes what the *device* has (an accelerometer) as clean physical units and leaves the tilt→star-power interpretation to marvin. Signed g keeps the wire transport-clean and device-agnostic; 1/32-g scale makes gravity a round `+32` and matches raw resolution (~0.84 raw counts/LSB) with no clipping. Resolves the "Star-Power mechanism in fauxmote" open question — it was the wrong framing (fauxmote shouldn't know SP). |
| 2026-07-02 | **marvin↔fauxmote link = UART first, layered message protocol (spec: [`docs/marvin-fauxmote-link.md`](../../../docs/marvin-fauxmote-link.md)).** Three layers: transport-agnostic message layer (`TYPE`+fixed payload), a UART framing layer (`SOF 0x7E`/`TYPE`/`LEN`/`CRC8`), UART PHY (1 Mbaud 8-N-1 on a **second** UART, not the CDC console). Messages are **independent state-slice updaters**: `GUITAR` (3 B hot: fret/strum mask + whammy + aux), `WIIMOTE` (4 B nav: core buttons/D-pad/stick), plus planned `ACCEL` (3 B, tilt→star power) and `POINTER` (3 B, IR) slices, `LINK_CMD` (1 B: pair/stop/reconnect/unlink/ext), and `STATUS` (4 B f→m). fauxmote latches each slice and assembles the Wii report from all of them; each slice is absolute/latest-wins. A **single** 200 ms link watchdog (reset by any control message) reverts *all* slices to safe defaults when the link goes fully quiet. `GUITAR` byte 0 is bit-identical to marvin's T1S guitar mask. The link is a second front-end over the existing `Wiimote_*`/`Guitar_*`/`Fauxmote_*` APIs (a `marvin_link.c` beside the CLI). | Primary purpose is low-latency GH gameplay, so the hot path is one tiny fixed message with no handshake/ack. Absolute state matches both ends (marvin's bitmask + fauxmote's ~15 ms streaming) and self-heals dropped frames. Layering keeps the message bytes identical when the transport later moves to T1S — only the framing binding changes (Ethernet FCS replaces `SOF`/`CRC`). Reusing the CLI's module APIs avoids duplicating any behavior. Resolves Q5. |
| 2026-06-13 | **Removed the persistent auto-reconnect task; recovery is the manual `reconnect` command only.** Drops just reset state (`Wiimote_NotifyDisconnected`); no automatic re-initiation. | Auto-reconnect neither survived the GH3 game-launch handoff nor served as a keep-awake mechanism (reconnecting ≠ staying awake). Keeping the Wii awake is better done with occasional input/state changes, which Marvin's command stream provides during use. Removing it also drops the slot-leak/backoff complexity. The sniff-delay override (idle→sniff = 65 s) stays — that genuinely prevents idle supervision-timeout drops. |
| 2026-06-13 | **Extension encryption is mandatory for GH3 and implemented in the base Wiimote (`ext_crypto.c`); the guitar module stays plaintext.** GH3's real key handshake (register trace): `0x55`→`0xf0` (disable) → read ID `0xfa` in clear → `0xAA`→`0xf0` (enable) → 16-byte key to `0x40`-`0x4f`. The base captures the key, derives ft/sb, and encrypts outgoing ext data (streamed bytes @ offset `0x08`; reg reads @ `addr & 7`). | Everything GH3 reads from the extension is decrypted with that key, so plaintext is garbage (the "green/red worked unencrypted" observation was a decryption coincidence). Encryption is a generic Wiimote feature (nunchuk/classic encrypt too), so it belongs in the base, not the guitar. Corrects the plan's guess that GH3 used the old `0→0x40` init. |
| 2026-06-13 | **Cipher tables come from Dolphin's 1st-party set, not `rnconrad/WiimoteEmulator`'s `wm_crypto.c`.** rnconrad's S-boxes are corrupted (sbox[3]@0x60, [4]@0xd8, [6]@0x88/0xec are `0xD0`/`0xF0` & `0xD2`/`0xE2` transcription flips → not valid permutations). Used Dolphin's `keygen_sbox_1st_party` + `sboxes_1st_party[8]`; verified the key-schedule/encrypt algorithm term-for-term against Dolphin and round-tripped GH3's real key (idx 0, encrypt→decrypt identity). | Wrong table bytes silently corrupt the keystream → undebuggable "notes are garbage." Dolphin is GPL and validated against real games; the marcan tables are reverse-engineered hardware constants (facts), reproduced with our own code. |
| 2026-06-13 | **Guitar is a self-registering extension module behind `wiimote_ext.h`; `wiimote.c` is extension-agnostic.** `wiimote_extension_t` = register bank + `build_report`/`set_button`/`reset`; `Wiimote_RegisterExtension`. Buttons drive by name via `Wiimote_SetButton`/`TapButton`. | Plans for other extensions (nunchuk/classic) as drop-in `*.c` files, keeps the base generic, and keeps the CLI as thin pass-throughs over the same API Marvin will use. |
| 2026-06-13 | **Byte0/1 bits 7-6 must be set (GH3 Les Paul ident); single IR calibration kept.** Clear ident bits make GH3 treat us as a GHWT guitar and ignore the strum bar. Basic-IR pointer is correct on the main Wii screen with the same calibration as extended; the GH3-internal Home-overlay mis-scaling is GH3's own transform and is moot (IR is physically blocked when the Wiimote is in the guitar — guitar-mode Home nav uses the analog stick). | Found by hardware testing. No second/basic-mode calibration: the format packing is provably identical to extended (Dolphin + Linux), so the only real difference (GH3) isn't ours to fix and doesn't matter on real hardware. |
| 2026-06-12 | **`esp_hidd` is a dead end for Wii emulation; the only working ESP-IDF path is a custom SDP record (internal Bluedroid `SDP_*` API) + raw L2CAP HID on PSM 0x11/0x13 (`esp_bt_l2cap`), with the hardcoded SDP buffer raised.** Two independent walls: (1) the Wii rejects esp_hidd's record even with a small descriptor (wrong HID attribute values — subclass/reconnect-initiate/etc., which esp_hidd hardcodes and doesn't expose); (2) the real 217-byte descriptor can't even be registered — `SDP_MAX_PAD_LEN` (the per-record attribute pad) is **hardcoded 300** (`sdp_db.c:495`, `bluedroid_user_config.h`; no menuconfig), and the full Wiimote record is ~463 B → `SDP_AddAttribute fail … ID 518`. Confirmed feasible building blocks in IDF v6.0.1: `SDP_CreateRecord`/`SDP_AddAttribute`/`SDP_AddSequence` (private `stack/sdp_api.h`) and `esp_bt_l2cap_start_srv(psm)`. | Bluedroid is the only Classic-BT stack on ESP32, and its public APIs can't host an arbitrary SDP record (`esp_sdp` RAW is search-only). So the path requires *unsupported internals*: raise `SDP_MAX_PAD_LEN` via an injected `CONFIG_BT_SDP_PAD_LEN` compile define, build the exact Wiimote record with the internal SDP DB API, skip esp_hidd, and serve HID over raw L2CAP + hand-rolled HIDP. Heavy + fragile across IDF updates, but it's the real route. The exact 463-byte record + 217-byte descriptor are saved at [`wiimote-sdp.md`](wiimote-sdp.md). |
| 2026-06-12 | **Q1 resolved: ESP-IDF `esp_hidd`'s auto-generated SDP record is NOT accepted by the Wii → pivot to raw L2CAP (listen on PSM 0x11 control / 0x13 data) + a hand-built SDP record matching a real Wiimote.** Verbose Bluedroid logs: the Wii opens an ACL link, connects to our SDP server (PSM 1), reads our records (we send 14/250/37-byte SDP responses), then disconnects the SDP channel and terminates the ACL (`rsn 0x13` = remote user terminated) **without ever opening the HID PSMs**. SSP was confirmed off this round (legacy pairing) and discovery works (limited-discoverable + COD `0x002504`), so SDP content is the only remaining blocker. Wii BD_ADDR observed: `00:17:ab:07:2c:21`. | The Wii validates the SDP record against a real Wiimote's; esp_hidd's generic HID record (right VID/PID, wrong attributes + HID descriptor) fails the check, so it never proceeds to HID. Matches why `rnconrad/WiimoteEmulator` replaces the host BT stack to serve the exact SDP. Open: whether ESP-IDF lets us serve a fully custom SDP record (esp_sdp API vs internal Bluedroid `SDP_*` API) and listen on the fixed HID PSMs via `esp_bt_l2cap`. |
| 2026-06-12 | **Phase 1 first attempt uses ESP-IDF's unified `esp_hidd` BT-classic HID device wearing the Wiimote identity, not a hand-rolled raw-L2CAP/SDP stack.** `bt_hid_device.c`: `esp_hidd_dev_init(ESP_HID_TRANSPORT_BT)` with VID `0x057e`/PID `0x0306`/version `0x0100`/name `Nintendo RVL-CNT-01`, COD set to `0x002504` (peripheral/joystick), a minimal vendor-defined report map (just report IDs 0x30 in / 0x12 out), **SSP disabled** (`CONFIG_BT_SSP_ENABLED=n`) for legacy PIN pairing, and a GAP `PIN_REQ` handler that replies with the requesting host's BD_ADDR reversed. | The stack already implements HIDP + L2CAP (PSM 0x11/0x13) + auth, so this is the cheapest probe of Q1: if a real Wii connects + authenticates against the auto-generated SDP, we avoid hand-building raw SDP entirely. If it rejects it, fall back to raw L2CAP + the exact SDP record from `rnconrad/WiimoteEmulator` (cloned locally for byte-exact bytes; WebFetch can't reproduce the ~463-byte blob). |
| 2026-06-12 | **Target the console-SYNC *bonding* flow, not the 1+2 temporary flow.** PIN handler returns the connecting host's (Wii's) BD_ADDR reversed (`param->pin_req.bda` byte-reversed). | Bonding (red SYNC button under the Wii's SD cover + controller in sync mode) is how a controller gets *persistently* registered, which is what a robot wants. The 1+2 flow is one-time/temporary and uses the controller's own address instead. Resolves Q2. |
| 2026-06-12 | **Build fauxmote as a parallel, independent proof-of-concept subproject; the fretboard GPIO-press path stays authoritative.** New top-level firmware subproject `firmware/fauxmote/`. | Device-side Wiimote *emulation* to a real Wii is rare/novel (host-side use is common), so de-risk it in isolation before touching marvin/fretboard/edge-ai. Nothing in the existing actuation path changes until fauxmote is proven. |
| 2026-06-12 | **Framework = ESP-IDF (v6.x; v6.0.1 installed), Bluedroid in Bluetooth-Classic-only mode (BLE disabled).** | Wiimote uses Bluetooth Classic HID; ESP-IDF exposes the BT-Classic HID-device API (`esp_hidd_api.h`), custom SDP records, BD_ADDR control, and raw L2CAP — all of which emulation needs. Arduino-ESP32's BT-Classic HID-device support is too thin for the custom SDP/descriptor the Wiimote requires. |
| 2026-06-12 | **Board = Adafruit ESP32 Feather V2 (product 5400); its *original* ESP32 is the load-bearing choice.** | The original ESP32 has a Bluetooth Classic (BR/EDR) radio. ESP32-S3/C3 are BLE-only and cannot emulate a Wiimote. Confirmed the product 5400 is the Feather V2 (not a QT Py S3). |
| 2026-06-12 | **marvin↔fauxmote command link is deferred; early phases use a local test command source.** | Lets the hard part (ESP32↔Wii Bluetooth emulation) be developed and validated standalone. The likely eventual link mirrors the fretboard 1-byte fret/strum bitmask over UART, but that's a later-session decision. |

---

## Open questions

- Exact marvin FLEXCOM instance + pins for the command link (integration-time). *fauxmote side settled: UART1 on Feather RX=`GPIO7`/TX=`GPIO8`.*
- Whether to retire the `MF_AUX_STARPOWER` bit from the shared `mf_proto.h` now that star power is expressed via the ACCEL slice. Deferred — a marvin-side decision, and the two `mf_proto.h` copies must stay byte-for-byte in sync (fauxmote's copy currently leads on the ACCEL constants until the marvin side lands).

(Q5 resolved 2026-07-02: marvin↔fauxmote link = UART first, layered message protocol; spec at [`../../../docs/marvin-fauxmote-link.md`](../../../docs/marvin-fauxmote-link.md) — see decision log.)

(Q1 resolved 2026-06-12: ESP-IDF `esp_hidd`'s auto-generated SDP record is NOT accepted by the Wii → pivoted to raw L2CAP + hand-built SDP record — see decision log.)

(Q4 resolved 2026-06-13: GH3 streams mode `0x37`; the guitar report is the first 6 ext bytes and must be **encrypted** — see decision log.)

(Star-Power mechanism resolved 2026-07-24: fauxmote exposes acceleration in g via the ACCEL slice; tilt→SP is marvin's semantics — see decision log.)

---

## Session log

### 2026-07-28 — marvin link onto 10BASE-T1S (fauxmote side, ahead of hardware)

Moved the marvin command link off its dedicated UART and onto the shared 10BASE-T1S
PLCA bus via a LAN8651 MAC-PHY over SPI — written ahead of hardware, exactly like the
guitar/detector followers were. ESP32/fauxmote side only; the marvin-coordinator routing
(0x88B7 demux + controller node-table entry) and physical bring-up are later sessions.

**Transport seam.** Split the old `marvin_link.c` into a transport-neutral message layer
and two swappable backends:
- `mf_link.c`/`.h` (new) — the message layer, moved verbatim: `apply_guitar/wiimote/
  pointer`, ACCEL apply, `neutralize`, `handle_link_cmd`, `build_status`, the 200 ms
  watchdog and STATUS cadence. API `MfLink_Init(mf_send_fn)` / `MfLink_HandleMessage(type,
  payload,len)` / `MfLink_Service(now_ms)`. STATUS_REQ now sets an internal flag instead
  of the plan's out-param.
- `marvin_link.c` (trimmed) — UART backend: keeps the `SOF/LEN/CRC8` parser + `mf_send`,
  its post-CRC switch calls `MfLink_HandleMessage`, the task calls `MfLink_Service`.
- `mf_t1s.c`/`.h` (new) — T1S backend (ESP-IDF port of guitar's `t1s_follower.c`). Owns
  the SPI/GPIO/IRQ setup, the TC6 integrator callbacks, a static service task, RX demux
  on ethertype `0x88B7`, the `mf_t1s_send` uplink, and a 500 ms `0x88B6` heartbeat
  (node_type 3). Provides `MarvinLink_Start()` so `main.c` is unchanged.

**ESP-IDF specifics.** SPI completion is **synchronous**: `TC6_CB_OnSpiTransaction` runs
a blocking `spi_device_polling_transmit` (manual CS) and calls `TC6_SpiBufferDone` in-line
before returning — which tc6.h explicitly permits. This is required, not just convenient:
`TC6Regs_Init` is **not** background/async — it runs the whole register sequence inline as
a series of `while (initialized && …) TC6_Service(…)` busy-loops that only advance when a
transaction's completion callback fires. A first cut deferred completion to `service_pump`
(in the yet-to-exist service task), so during `TC6Regs_Init` nothing ever called
`TC6_SpiBufferDone`, the loops spun forever, and the task watchdog tripped (~6 s, IDLE0
starved) before `MarvinLink_Start` returned. Completing in-line fixes both the wired and
the unwired case: with the LAN8651 present init completes; with it absent every control
read fails its echo/parity check (`read_rx_ctrl_buffer` → `success=false`), which clears
`initialized` and lets every loop fall through, so `TC6Regs_Init` returns cleanly with
`GetInitDone` false instead of hanging.

The runtime service loop needed a second fix. `service_pump` does exactly **one**
`TC6_Service` per pass (like guitar's follower) — an earlier bounded 8× loop on
`s_need_service` still starved IDLE0, because synchronous `TC6_SpiBufferDone` re-arms
`OnNeedService` every pass and the task then spun on IRQ-notify without blocking (task
watchdog on `mft1s`, not `main`). IRQ_N handling is now **deferred**: the ISR masks IRQ_N
(`gpio_intr_disable`) and notifies the task, which re-enables it after the service pass —
so a level-latched or chattering IRQ_N can't re-notify faster than the task consumes it.
The task blocks on `ulTaskNotifyTake` — the wait clamped to **≥1 tick**, because
`pdMS_TO_TICKS(2)` truncates to **0** at the default 100 Hz tick, turning the wait into a
non-blocking poll (this was the third and final watchdog: link came up, then the task
spun with a zero wait and starved IDLE0 exactly 5 s later). Real RX wakes the task
immediately via the IRQ notification; the tick timeout is only the fallback poll that
re-services a level-latched IRQ and drives the heartbeat + STATUS cadence. Blocking there
is what yields to the idle task. `max_transfer_sz` = 4096
(multi-chunk transactions concatenate). CS driven manually (`spics_io_num = -1`).

**Build wiring.** `Kconfig.projbuild` (new) — `choice FAUXMOTE_LINK_TRANSPORT` (T1S
default / UART), `FAUXMOTE_T1S_NODE_ID` (default 3, range 3–7), and six pin ints
(Feather V2 defaults SCK=5/MO=19/MI=21/CS=33/RST=27/IRQ=32). `CMakeLists.txt` compiles
`mf_link.c` always and, per the choice, either `mf_t1s.c` + vendored `tc6.c`/`tc6-regs.c`
(REQUIRES `esp_driver_spi esp_timer`) or `marvin_link.c` (REQUIRES `esp_driver_uart`).
`tc6-conf.h` (new) is fauxmote's copy of marvin's, one instance. `sdkconfig.defaults`
sets `CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S=y`. `console_cli.c` gained a guarded `t1s`
diagnostics command (link/synced/node id/chipRev + rx/tx/err/hb_seq).

Pin choices dodge the console UART (7/8), status LED (13), NeoPixel (0/2), strapping
(12/15), and input-only (34–39) pins. SCK/MO/MI route through the GPIO matrix (fine past
the LAN8651's 25 MHz ceiling). Docs updated: `docs/marvin-fauxmote-link.md` §8,
`docs/t1s-podl-link.md` §7.1/§7.2.

**On-hardware (LAN8651 wired, not yet on the bus).** Boots stable, no watchdog: `LAN8651
up - chipRev=2, MAC=02:00:00:00:00:03, PLCA follower id=3/8`, ethertype 0x88B7; the CLI
and BT/Wiimote paths are unaffected. **Remaining:** connect to the T1S bus and confirm
against the marvin coordinator — heartbeat (0x88B6, node_type 3) seen, 0x88B7 command
frames actuate the emulated controller, STATUS uplink, 200 ms neutralize on link loss.
The marvin-coordinator side (0x88B7 demux + controller node-table entry) is still a
separate session.

**UART fallback build compiles** (`FAUXMOTE_LINK_TRANSPORT_UART`) — `mf_link.c` +
`marvin_link.c` with no TC6/SPI deps, `MarvinLink_Start` resolves. Not yet re-run on
hardware over the UART link.

### 2026-07-24 — ACCEL slice: expose the Wiimote accelerometer over the marvin link (fauxmote side)

Wired the previously-stubbed `MF_MSG_ACCEL` slice end to end on fauxmote so marvin can
move the emulated accelerometer (how GH3 activates star power — the player tilts the
guitar and the console reads the Wiimote accel). fauxmote holds **no** tilt/star-power
semantics: it emulates a Wiimote + guitar extension and exposes the *device's
acceleration* as clean physical units; the tilt/SP interpretation is marvin's.

**Wire encoding (settled this session):** ACCEL is 3 B, one **signed int8 (2's-complement)
acceleration in g per axis** (X/Y/Z), `1 LSB = 1/32 g` ⇒ `+1 g = +32`, range −4.0..+3.97 g.
Level/rest = `{0, 0, +32}`. fauxmote translates g → raw Wiimote report bytes with its own
advertised calibration (`accel_g_to_raw`: `raw = clamp(0x85 + g·27/32, 0, 255)`; per-axis
zero-g `0x85`, +1 g `0xA0` = 27 raw counts/g). Chosen so gravity is a round number and 1
wire LSB ≈ 0.84 raw counts (no wasted precision, no clipping over the raw envelope).

**Changes:** `mf_proto.h` — added `MF_ACCEL_LSB_PER_G`/`MF_ACCEL_LEVEL_{X,Y,Z}` and
reworded `MF_AUX_STARPOWER` (fauxmote ignores it; SP is expressed via the ACCEL slice).
`wiimote.c` — `s_accel[3]` state, `accel_g_to_raw()`, `build_report` copies `s_accel` in
all four accel modes (0x31/0x33/0x35/0x37), reset to level on disconnect.
`wiimote.h`/`marvin_link.c` — `Wiimote_SetAccel`/`Wiimote_ClearAccel`; `MF_MSG_ACCEL` now
an independent slice; `neutralize()` clears accel to level with the other slices.
`console_cli.c` — `accel <gx> <gy> <gz>` / `accel level` for bring-up. Protocol doc §4/§5.1/§5.5
updated. **Builds clean** (ESP-IDF v6.0.1). Hardware verification with GH3 (dial in the
tilt axis/threshold that trips star power) remains.

**Sync caveat:** this diverges fauxmote's `mf_proto.h` from marvin's copy (the ACCEL
constants) until the marvin side lands. The two are meant to be byte-for-byte identical.

### 2026-07-14 — FIXED: two-controller input lag = L2CAP tx-FIFO stuffed with stale duplicates

**Root cause (not role — that was ruled out, see entry below; role is SLAVE).** `sender_task`
streamed a full input report **every 15 ms unconditionally** while streaming. Bluedroid's L2CAP tx
path is a **10-deep FIFO** (`SLOT_TX_QUEUE_SIZE = 10`, `btc_l2cap.c`) with a *blocking* writer
(`l2cap_vfs_write` waits on `tx_event_group` when full). fauxmote is the ACL **slave**, so it only
transmits when the Wii (master) polls it. With one controller the Wii polls fast → the FIFO drains
and stays empty. With a second **real** Wiimote, the Wii's poll rate *to fauxmote* drops (it
sniff-schedules the real one), the FIFO fills with **10 identical, stale snapshots**, and a fresh
button-press report enqueues *behind* them → multi-hundred-ms lag **on fauxmote only** (the real
Wiimote, on its own reserved schedule, stayed crisp).

**Fix (`wiimote.c`, `sender_task`): send on change + keepalive.** Send a report only when its bytes
differ from the last one sent; otherwise resend once per `SENDER_KEEPALIVE_MS = 250` ms (well under
the ~1 s supervision timeout — `supv_to 1600` observed). The keepalive timer resets on *every* send,
so a steady input stream never triggers a keepalive. On-change keeps the tx FIFO empty while idle, so
a press ships on the **next** poll regardless of contention. Reset the last-sent tracker in
`Wiimote_NotifyDisconnected` so the first report after a reconnect always ships. Correct across
report modes / encryption: the dedup compares the final report bytes, and the extension cipher is
position-keyed (identical state → identical ciphertext). Side benefit: idle BT airtime drops ~67 Hz
→ 4 Hz, friendlier to the shared piconet.

**Confirmed on hardware:** significant improvement to two-controller input latency.

### 2026-07-14 — Two-controller input lag: ACL-role instrumentation (`role` cmd + open-time log)

**Symptom (hardware, marvin 2p bring-up):** with fauxmote *and* a real Wiimote both connected
to the Wii, **fauxmote's** inputs lag noticeably (seconds-scale, visible on the Wiimote-settings
screen) while the **real Wiimote stays crisp**. Single fauxmote is fine; a real Wii runs 4 real
Wiimotes with no contention. Player slot is stable (fauxmote=1, real=2) — not slot reassignment.

**Leading hypothesis — Bluetooth role / scatternet.** A real Wiimote is always the piconet
**slave**; the Wii is master and schedules all its Wiimotes on one clock (why 4 coexist cleanly).
If fauxmote is instead **master** of its link (Wii = slave to it), then when a real Wiimote joins
(Wii = master of *that* piconet) the Wii becomes a scatternet node — one radio, two piconets, two
clocks — and must time-hop between them. The foreign piconet (fauxmote's) gets serviced only
intermittently → lag on fauxmote's link only. Matches every observation. Fauxmote can end up master
because it never forces slave, and role isn't fixed by who paged: either side can role-switch, and
Bluedroid/ESP32 may request master on connect (so even a fresh Wii-paged pair can leave us master).

**Instrumentation added (no automatic behavior change yet):**
- `bt_role.{c,h}` — `BtRole_Get()` / `BtRole_Switch(to_slave)` over Bluedroid internal
  `BTM_GetRole` / `BTM_SwitchRole` (`stack/btm_api.h`). Isolated TU like `wiimote_sdp.c` because the
  internal `BTM_*`/`stack` headers clash with the public `esp_*` BT headers in one file.
- `bt_hid_device.c` `L2CAP_OPEN_EVT` now logs `ACL role to Wii = MASTER/SLAVE/UNKNOWN`.
- Console `role [slave|master]` — `role` prints the current ACL role; `role slave` requests the
  switch (real-Wiimote behavior). Manual lever kept deliberately (not auto) so we confirm the role
  *and* test the fix before committing to always-slave.

**Next (hardware):** connect fauxmote, read the open-time role log; connect the 2nd Wiimote; if
fauxmote is `master`, run `role slave` and check whether the lag clears. If it does → make the
switch automatic at `L2CAP_OPEN` (and/or set link policy to allow role switch / force slave). If the
role is already `slave` → scatternet is ruled out; pivot to link parameters (the active-mode /
sniff-off choice from the 2026-06-13 idle-disconnect fix, `Tpoll`, or an ESP32 2-ACL scheduling
limit — `CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN=2`).

### 2026-07-03 — doc-vs-code audit fixes

Part of a repo-wide doc audit ([`../../../docs/doc-audit-2026-07.md`](../../../docs/doc-audit-2026-07.md)). `SPEC.md` Phase 4 marked done (`marvin_link.c` is built + on hardware, per the 2026-07-02 entry below) and added to the §5 Software list. `docs/wiimote-sdp.md` HID-descriptor location corrected (`main/wiimote_sdp.c`, not `bt_hid_device.c`). Cross-cutting link docs also touched: `docs/marvin-fauxmote-link.md` STATUS-byte details (report_mode default `0x30`; discoverable+pairing bits set together) and `docs/t1s-podl-link.md` (marvin coordinator SPI is 15 MHz; the ≤12 MHz note is follower-side).

### 2026-07-02 — Phase 4: fauxmote side of the marvin link implemented + on hardware

- Implemented the fauxmote end of the command link. New `mf_proto.h` (transport-neutral
  wire constants + CRC-8/CCITT, meant to be shared verbatim with marvin), `marvin_link.c`/`.h`
  (UART1 task on Feather RX=`GPIO7`/TX=`GPIO8` @ 1 Mbaud; `SOF/TYPE/LEN/CRC8` parser with
  resync; dispatch to `Wiimote_*`/`Guitar_*`/`Fauxmote_*`; one 200 ms watchdog → neutralize;
  `STATUS` uplink on-change + 500 ms heartbeat).
- Small API additions to reuse existing logic: `Guitar_SetStick(x,y)` (nav stick had no
  setter) and `Wiimote_ExtAttached()` (for the `STATUS` ext bit). Wired `MarvinLink_Start()`
  into `main.c`; added `marvin_link.c` + `esp_driver_uart` to `main/CMakeLists.txt`.
- Confirmed Feather V2 (PICO-MINI-02) breaks out GPIO7/8 as the second hardware UART
  (`Serial1`), independent of the USB console — so the CLI keeps UART0. Resolved the
  fauxmote half of the pin open-question.
- Reconciled the spec's whammy encoding to the extension's real rest value (~`0x10`).
- **On hardware: builds clean, boots, and the Wii connects with the link task running.**
  Not yet exercised by marvin. Next: the marvin (SAM9X75) transmit side over a FLEXCOM USART.

### 2026-07-02 — marvin↔fauxmote link protocol drafted (Phase 4 seam)

- Worked up the command-link protocol spec: [`docs/marvin-fauxmote-link.md`](../../../docs/marvin-fauxmote-link.md)
  (top-level, mirroring `docs/t1s-podl-link.md` since it spans marvin + fauxmote).
- Layered: transport-agnostic message layer, UART framing (`SOF/TYPE/LEN/CRC8`),
  UART PHY (1 Mbaud). Messages `GUITAR`/`WIIMOTE`/`LINK_CMD` (m→f) + `STATUS` (f→m).
  Absolute/latest-wins hot path; 200 ms link-timeout fail-safe.
- Confirmed scope with Greg: split hot `GUITAR` vs nav `WIIMOTE` (not one unified
  message), include nav now, and put full BT link management (pair/unlink/reconnect/
  stop/ext) + a status uplink on the link. Resolved Q5.
- No firmware written yet — spec first. Next: implement `marvin_link.c` (second
  UART, framing, dispatch to the existing CLI-backing APIs) and the marvin side.

### 2026-06-13 — Phase 3 done: GH3 guitar plays (module + encryption + basic IR)

- **Guitar module split.** New `wiimote_ext.h` (`wiimote_extension_t` interface +
  `Wiimote_RegisterExtension`) and `guitar.c`; `wiimote.c` made extension-agnostic.
  CLI `btn`/`tap`/`whammy` unchanged (thin pass-throughs).
- **Detection + report.** ID `00 00 A4 20 01 03` at `0xfa`; GH3 streams mode `0x37`.
  Fixed strum being ignored — root cause was **byte0/1 bits7-6 cleared** (we looked
  like a GHWT guitar to GH3). Bit layout re-verified against wiibrew *and* the Linux
  `hid-wiimote` guitar parser (strum bits were correct all along).
- **Encryption (`ext_crypto.c`).** Captured GH3's real key handshake via a temporary
  register trace (`0x55`→`0xf0` → read ID → `0xAA`→`0xf0` → 16-byte key). Implemented
  the marcan cipher in the base Wiimote (encrypt streamed ext bytes + reg reads; guitar
  stays plaintext). Found rnconrad's `wm_crypto.c` S-boxes corrupted → used Dolphin's
  1st-party tables; verified the algorithm against Dolphin and round-tripped GH3's
  actual key. On hardware: **frets, strum, and whammy register in GH3.**
- **Basic IR.** Filled `0x36`/`0x37` (`build_ir_basic`, packing cross-checked vs
  Dolphin `IRBasic` + Linux decode). Pointer correct on the main Wii screen + Home
  overlay. GH3's in-game Home overlay mis-scales the cursor (GH3's own transform) but
  it's moot — IR is blocked when the Wiimote is in the guitar (analog-stick nav there).
- **Console cleanup.** Removed the temporary `WR`/`RD` register-trace logs; demoted the
  per-report RX log to `ESP_LOGD` (it was garbling the `esp_console` line editor).
- Committed as `939eb5c`.

### 2026-06-13 — Link stability (sniff + auto-reconnect); GH3 handoff open

- Idle disconnects root-caused to Bluedroid sniff: link idled → sniff → supervision
  timeout (`rsn 0x08`), and abnormal closes leaked BTA PM-profile slots (pool of 5 →
  reconnect fails → power-cycle). No clean public/repo override (PM spec is `const`),
  but the idle→sniff delay is a `#ifndef`'d `#define` → overrode it to 65 s via a
  global `-D` in the top-level CMake (`idf_build_set_property` before `project()`).
  Link now stays active; no timeouts, no PM leak.
- Added a persistent auto-reconnect task (device-initiated, retries with backoff,
  suppressed on `stop`/`unlink`) so unexpected drops self-heal like a real Wiimote.
- GH3 game-launch handoff still drops us and isn't reliably usable after reconnect;
  the Wii goes HID-silent then drops, and fauxmote's logs don't reveal why (no
  mishandled command). Documented as OPEN (needs a BT sniffer). Workaround: restart
  fauxmote after the game starts. Moving on to the guitar extension.

### 2026-06-12 — Step A: test CLI + connection management

- Added `esp_console` REPL (`console_cli.c`) — thin commands over the module API.
  Boot-idle; `pair` arms HID listeners + discoverable, `stop` tears down. Honors the
  Wii's reporting mode. Bond recall at boot + `unlink`. New LED scheme (idle blip /
  fast blink / N-flash player slot). `tap` logic moved into `Wiimote_TapButton`
  (non-blocking, sender auto-releases).
- Bugs found + fixed on hardware during cycle testing:
  - Disconnect left `connected=1` → `Wiimote_NotifyDisconnected` now clears
    `s_data_fd` immediately (not just on reader exit).
  - Reader tasks leaked their link slot when a re-armed server recycled their fd →
    readers now exit by **L2CAP handle** signalled from the CLOSE event, not by
    `read()` return.
  - **L2CAP slot exhaustion after ~4-5 reconnect cycles**: auto-re-arming servers on
    every disconnect leaked server slots (reconnect uses *client* connects, so the
    re-armed servers were never consumed). Fix: arm servers only per `pair` window,
    tear down on `stop`; no auto-re-arm.
- Open: device-initiated `reconnect` uses Basic L2CAP mode vs the Wii's ERTM (may
  misbehave) — reliable path is `pair`+SYNC.

### 2026-06-12 — Phase 2b done: assigned Wiimote + status LED

- `wiimote.c` report state machine (ported from rnconrad's `wm_reports`/handlers,
  explicit bytes, no bitfields): EEPROM read (`0x21`, calibration at 0x16/0x20,
  error for >0x16FF), status (`0x20`), ack (`0x22`), reporting-mode/LED/IR acks,
  and a static sender task streaming `0x30` every 15 ms. Register reads currently
  return an error (no extension yet → Phase 3).
- Wired into the L2CAP reader (`Wiimote_HandleRx` per frame, `Wiimote_NotifyFdClosed`
  on close). Added `status_led.c` driving GPIO13 (heartbeat/fast-blink/solid) since
  the Feather has no player LEDs; reflects `Wiimote_IsConnected/IsAssigned`.
- On hardware: full clean handshake (`0x17`×n, `0x11`, `0x15`, `0x12`, `0x13`/`0x1a`,
  `0x16` IR-register writes), Wii assigns a slot, **LED solid = assigned**. Turned
  off the verbose Bluedroid trace block in `sdkconfig.defaults` (needs `set-target`
  to re-apply) — monitor is now readable.

### 2026-06-12 — Phase 2a: HID read path working

- Added per-channel reader tasks (static `xTaskCreateStatic`) that `read()` the
  L2CAP fds from `ESP_BT_L2CAP_OPEN_EVT`. Gotcha: `esp_bt_l2cap`'s VFS `read()` is
  **non-blocking** — empty rx queue returns `0` (not EOF), peer-close returns `-1`
  (errno `EPIPE`). So poll on `0`, exit only on `<0` (first cut wrongly treated `0`
  as EOF and the reader exited instantly). No `select` support on this VFS.
- First output report from the Wii (data channel, PSM 0x13): `a2 17 00 00 17 70 00
  01` = report `0x17` Read Memory, EEPROM offset `0x001770`, size 1. The Wii then
  **blocks waiting for our `0x21` (Read Data) reply** — confirms the report
  responder is the gate to a live/assigned controller. → Phase 2b.

### 2026-06-12 — Phase 1 complete: Wii pairs + opens HID channels

- Custom SDP path works. Replaced esp_hidd with: `wiimote_sdp.c` building the exact
  Wiimote record via internal `SDP_CreateRecord`/`SDP_AddAttribute`/… (modeled on
  Bluedroid's own `HID_DevAddRecord`, Wiimote-exact values), + `esp_bt_l2cap`
  servers on PSM 0x11/0x13. Raised `CONFIG_BT_SDP_ATTR_LEN=512` /
  `CONFIG_BT_SDP_PAD_LEN=1024` (defaults exist; depend only on `BT_CLASSIC_ENABLED`).
- On red-SYNC: SDP accepted → legacy PIN pairing `auth OK` → both HID L2CAP
  channels open (CID 0x40 control / 0x41 interrupt, mtu 640) → Wii starts sending us
  HID data (`BTA_JvL2capRead`). Phase 1 success criterion met on hardware.
- Build/integration lessons (durable):
  - To call the internal SDP API, add bt internal include roots in CMake:
    `<bt>/common/include`, `<bt>/host/bluedroid/{stack,common,osi}/include`,
    `<bt>/host/bluedroid/api/include/api`. `bdroid_buildcfg.h` is behind
    `#ifdef HAS_BDROID_BUILDCFG` (not defined externally) so it's skipped.
  - `esp_bt_l2cap` data path is fd/VFS-based: must `esp_bt_l2cap_vfs_register()`
    (after INIT_EVT) BEFORE `esp_bt_l2cap_start_srv`, else `l2cap_malloc_slot
    unable to register fd` / NO_RESOURCE.

### 2026-06-12 — Phase 1 debugging: discovery solved, SDP rejected

- esp_hidd HID-device first attempt — Wii initially saw nothing. Two fixes got it
  to connect: (a) the Wii's SYNC scan uses a *limited* inquiry → switched to
  `ESP_BT_LIMITED_DISCOVERABLE`; (b) `esp_hidd_dev_init` overrides the Class of
  Device → now set `0x002504` in the HIDD START handler (after init).
- **sdkconfig.defaults gotcha:** edits to `sdkconfig.defaults` do NOT apply once
  `sdkconfig` exists. `CONFIG_BT_SSP_ENABLED=n` + 8 MB flash silently didn't take
  effect until `idf.py set-target esp32` regenerated `sdkconfig`. Always regenerate
  (or menuconfig) after editing defaults.
- With legacy pairing active + verbose Bluedroid logs: Wii connects ACL → SDP
  (PSM 1) → reads our records → disconnects → ACL term `rsn 0x13`, never opening the
  HID PSMs. → Q1 resolved (see decision log). Pivoting to raw L2CAP + exact Wiimote
  SDP. Added a temporary verbose-BT-log block to `sdkconfig.defaults` (trim later).

### 2026-06-12 — Phase 0 verified on hardware

- Built + flashed on the Adafruit ESP32 Feather V2 (ESP-IDF **v6.0.1**, Apple
  Silicon macOS). Clean boot, no `ESP_ERROR_CHECK` abort: `BTDM_INIT` controller up,
  `Bluetooth MAC c0:cd:d6:37:fd:0a`, reached the "discoverable" log line.
- Discoverability confirmed Mac-only via `blueutil --inquiry 15`:
  `address: c0-cd-d6-37-fd-0a … name: "Nintendo RVL-CNT-01"`. (macOS System Settings
  doesn't expose a Classic inquiry; `blueutil` does. No Linux/Android needed.)
- Added a boot-time BD_ADDR log to `main/main.c` (`esp_bt_dev_get_address`) — also the
  basis for the Phase-1 pairing PIN (BD_ADDR reversed = `0a fd 37 d6 cd c0`).
- Bumped doc/version references v5.x → v6.0.1 across SPEC/journal/README/sdkconfig.
- Noted (not yet fixed): image header defaults to 2 MB flash on an 8 MB board
  (`CONFIG_ESPTOOLPY_FLASHSIZE="8MB"` to set later; harmless for now).

### 2026-06-12 — subproject created

- First mention of Wiimote emulation in the repo; confirmed no prior discussion.
- Verified the board (Feather V2 = original ESP32, has BT Classic) and the Wiimote
  protocol facts (Classic HID, PSM 0x11/0x13, SDP `Nintendo RVL-CNT-01` /
  `0x057e`/`0x0306`, PIN = reversed BD_ADDR, guitar extension ID `00 00 A4 20 01 03`
  + 6-byte report). Captured in [`../SPEC.md`](../SPEC.md) §3.
- Decisions above logged. Created the ESP-IDF scaffold: top-level + `main/`
  `CMakeLists.txt`, `sdkconfig.defaults` (Classic BT + Bluedroid HID device, BLE off),
  and a Phase-0 `main/main.c` (radio up + discoverable by name). Untested pending
  toolchain + hardware.
- Registered the subproject: journal added to `CLAUDE.md`, pointer row + repo-map
  entry added to root `SPEC.md`.
