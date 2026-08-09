# marvin ↔ fauxmote command link

> Protocol spec for the control link between **marvin** (SAM9X75 runtime brain) and
> **fauxmote** (ESP32 Wiimote/guitar emulator). marvin sends controller input;
> fauxmote reflects it to the real Wii. This is an alternative to the T1S
> `guitar` actuator node — fauxmote *becomes* the controller instead of pressing a
> real one.
>
> Companion docs: [`firmware/fauxmote/SPEC.md`](../firmware/fauxmote/SPEC.md),
> [`firmware/fauxmote/docs/journal.md`](../firmware/fauxmote/docs/journal.md), and the
> T1S bus link [`docs/t1s-podl-link.md`](t1s-podl-link.md) (the eventual second transport).

**Protocol version:** 1. Both ends are compiled to a matching version for now
(no runtime negotiation); the version is reported in `STATUS` so a mismatch is
visible.

---

## 1. Design goals

1. **Low-latency gameplay control.** The hot path — frets, strum, whammy — is a
   single tiny fixed message fauxmote acts on immediately. No handshake, no ack,
   no per-message state machine.
2. **Absolute state, latest-wins.** Every input message carries the *complete
   current state* of its group, not deltas or events. Idempotent: a dropped or
   corrupt frame self-corrects on the next one. This matches both ends — marvin's
   existing 1-byte guitar bitmask is latest-wins, and fauxmote already streams the
   current state to the Wii every ~15 ms regardless of the command source.
3. **Transport-independent message layer.** The message bytes (§4) are identical
   over any physical layer. UART is the first transport (§3); 10BASE-T1S is the
   planned second (§6). Only the framing/binding changes.
4. **Fail safe.** If the command link goes quiet, fauxmote releases all inputs —
   a dropped link must never leave a note held.

## 2. Layering

```
  ┌─────────────────────────────────────────────┐
  │  L3  Message layer   TYPE + fixed payload     │  ← identical on every transport
  ├─────────────────────────────────────────────┤
  │  L2  Framing layer   delimit + integrity      │  ← UART: SOF/LEN/CRC (§3.2)
  │                                               │     T1S: none (Ethernet FCS) (§6)
  ├─────────────────────────────────────────────┤
  │  L1  Physical        UART now / T1S later      │
  └─────────────────────────────────────────────┘
```

Both firmwares implement the message layer against a thin transport seam
(`send(type, payload, len)` / `on_message(type, payload, len)`); UART and T1S are
two implementations of that seam (§7).

## 3. UART transport (transport #1)

### 3.1 Physical

| Parameter | Value |
|---|---|
| Baud | **1 000 000** (1 Mbaud), 8-N-1, no flow control |
| Wiring | 3 wires: marvin TX → fauxmote RX, fauxmote TX → marvin RX, common GND |
| marvin port | **FLEXCOM5 USART** — `PA16` (`FAUXMOTE_TX`) / `PA15` (`FAUXMOTE_RX`); a dedicated peripheral, independent of the guitar transport |
| fauxmote port | **UART1** on the Feather V2's broken-out RX=`GPIO7` / TX=`GPIO8` (the board's second hardware UART, `Serial1`) — **not** UART0, which is the USB-CDC console/CLI |

Baud is generous on purpose: a 7-byte hot frame is ~70 µs on the wire, negligible
against the Wii's ~15 ms report period, so the link adds no meaningful latency.
Baud is a compile-time constant on both ends and may be lowered if wiring
reliability requires it.

### 3.2 Framing

Each message is wrapped in a frame:

```
  ┌──────┬──────┬──────┬───────────────┬──────┐
  │ SOF  │ TYPE │ LEN  │  payload[LEN]  │ CRC8 │
  │ 0x7E │      │      │                │      │
  └──────┴──────┴──────┴───────────────┴──────┘
```

- `SOF` = `0x7E` — start-of-frame marker.
- `TYPE` — message type (§4).
- `LEN` — payload length in bytes (0..255). Fixed per type in v1; carried anyway
  for extensibility and to bound the frame during resync.
- `payload` — `LEN` bytes (§5).
- `CRC8` — over `TYPE`, `LEN`, and the payload bytes (not `SOF`).
  Polynomial `0x07` (CRC-8/CCITT), init `0x00`, no reflection.

Framing overhead is 4 bytes; a 3-byte `GUITAR` payload is 7 bytes on the wire.

**Receiver rules:**
- Scan for `SOF`, then read `TYPE`/`LEN`, then `LEN` payload bytes, then `CRC8`.
- If the CRC fails, or `LEN` disagrees with the known length for `TYPE`, **discard
  and rescan** from the next `0x7E`. (A `0x7E` inside a payload that causes a false
  frame is rejected by the CRC and the receiver re-syncs on the real boundary.)
- `TYPE = 0x00` is never valid — treat as noise and rescan.
- No ack on any message; corruption self-heals on the next state frame.

## 4. Message types

| TYPE | Name | Dir | LEN | Status | Purpose |
|---|---|---|---|---|---|
| `0x01` | `GUITAR` | m→f | 3 | v1 | Hot gameplay input (frets/strum/whammy/aux). |
| `0x02` | `WIIMOTE` | m→f | 4 | v1 | Menu-nav input (core buttons, D-pad, analog stick). |
| `0x03` | `LINK_CMD` | m→f | 1 | v1 | Bluetooth link management (pair/stop/reconnect/disconnect/btreset/reboot/unlink/ext). |
| `0x04` | `ACCEL` | m→f | 3 | v1 | Wiimote accelerometer state (X/Y/Z acceleration in g). |
| `0x05` | `POINTER` | m→f | 3 | v1 | IR pointer position (bare-Wiimote menu nav). |
| `0x81` | `STATUS` | f→m | 4 | v1 | Link/connection/extension state + last-command result. |

`0x00` reserved (invalid). `0x06`–`0x7F` reserved for future m→f; `0x82`–`0xFF`
for future f→m (telemetry). Each message updates one **independent slice** of the
latched controller state (§4.1) — new slices are added as new types without
touching existing ones.

### 4.1 State model — independent slices, latched with timeout

fauxmote holds a **single controller state** (buttons, whammy, stick, accelerometer,
IR pointer, extension bytes). Its report sender already assembles the Wii Bluetooth
report from that state every ~15 ms, independent of the command source.

Each message type updates **one independent slice** of that state and nothing else —
`GUITAR` never touches the accel slice, `POINTER` never touches the frets, etc. So
the messages are fully independent: marvin sends whichever slices it needs, at
whatever rate each needs, in any order. fauxmote latches each slice and keeps
sending it to the Wii until a newer message for that slice replaces it.

A **single link watchdog** (§6) guards the whole state: any control message
(`GUITAR`/`WIIMOTE`/`ACCEL`/`POINTER`) resets it. So long as fauxmote receives *any*
control message within the timeout, it holds all latched slices; if the link goes
fully quiet past the timeout, it reverts *all* slices to their safe defaults. This
is the core of the design — the wire protocol is a set of independent state-slice
updaters over one live link, not a single monolithic frame.

## 5. Message payloads

All fields are single bytes, so byte order is irrelevant. Bits are numbered
`bit0` = LSB. Reserved bits/bytes are sent `0` and ignored on receive.

### 5.1 `GUITAR` (m→f, 3 bytes) — the hot path

| Byte | Field | Encoding |
|---|---|---|
| 0 | fret/strum mask | `bit0` Green, `bit1` Red, `bit2` Yellow, `bit3` Blue, `bit4` Orange, `bit5` Strum-Down, `bit6` Strum-Up, `bit7` reserved |
| 1 | whammy | raw 5-bit extension value `0..31`: rest ≈ `0x10`, fully pressed ≈ `0x1F` (upper bits 0) |
| 2 | aux | `bit0` Start (`+`), `bit1` Select (`−`), `bit2` Pedal, `bit3` reserved*, `bit4`–`7` reserved |

Byte 0 is **bit-identical to marvin's existing guitar-node command bitmask** (the
[`T1SLink_SendToGuitar`](../firmware/marvin/default/src/net/t1s/t1s_link.h) mask),
so marvin feeds the same computed mask to either actuation sink with no
translation.

\* `bit3` was a "Star-Power" semantic bit. **fauxmote ignores it** — star power is a
*tilt*, not a device button, and fauxmote emulates only the raw device (a Wiimote +
guitar extension). marvin expresses star power by driving the accelerometer through the
`ACCEL` slice (§5.5). The bit is left reserved pending a decision to retire it.

### 5.2 `WIIMOTE` (m→f, 4 bytes) — menu navigation

| Byte | Field | Encoding |
|---|---|---|
| 0 | core buttons | `bit0` A, `bit1` B, `bit2` One, `bit3` Two, `bit4` Plus, `bit5` Minus, `bit6` Home, `bit7` reserved |
| 1 | D-pad | `bit0` Up, `bit1` Down, `bit2` Left, `bit3` Right, `bit4`–`7` reserved |
| 2 | stick X | `0..63`, center `32` (the guitar extension's 6-bit analog stick) |
| 3 | stick Y | `0..63`, center `32` |

The analog stick is the guitar-mode Home-menu control (the IR pointer is
physically blocked when the Wiimote is seated in the guitar — see the fauxmote
journal). Bare-Wiimote menu nav uses the separate `POINTER` slice (§5.6).

### 5.5 `ACCEL` (m→f, 3 bytes)

Wiimote accelerometer state — the raw device axis, in physical units. marvin sends
acceleration; fauxmote renders it into the Wiimote report. It carries **no** tilt or
star-power semantics: those belong to marvin (e.g. to activate GH3 star power, marvin
tilts the accelerometer via this slice). Independent of `GUITAR`.

Each byte is a **signed 8-bit (2's-complement) acceleration in g**, `1 LSB = 1/32 g`
(so `+1 g = +32`), giving an int8 range of `−4.0 g … +3.97 g`.

| Byte | Field | Encoding |
|---|---|---|
| 0 | accel X | signed g, `+32` = +1 g |
| 1 | accel Y | signed g, `+32` = +1 g |
| 2 | accel Z | signed g, `+32` = +1 g (gravity → +1 g at rest) |

fauxmote translates each axis to a raw Wiimote report byte using its own advertised
accelerometer calibration (per-axis zero-g `0x85`, +1 g `0xA0`, i.e. 27 raw counts/g):
`raw = clamp(0x85 + g·27/32, 0, 255)`. Level therefore maps to raw `{0x85, 0x85, 0xA0}`.

Default when the slice is stale/absent: **level** — `{X=0, Y=0, Z=+32}` (held flat, Z at
+1 g). A finer 10-bit encoding can be added later without changing the type.

Console: `accel <gx> <gy> <gz>` / `accel level`.

### 5.6 `POINTER` (m→f, 3 bytes)

IR pointer position, for bare-Wiimote menu navigation. marvin sends it via
`Fauxmote_SendPointer(x, y, visible)` (latched, on-change); fauxmote applies it
through `Wiimote_SetPointer`/`ClearPointer`. Independent of `WIIMOTE`. Moot while
the Wiimote is seated in the guitar (camera blocked) — use the analog stick there.

| Byte | Field | Encoding |
|---|---|---|
| 0 | x | `0..255` → `0..1`, `0` = left edge |
| 1 | y | `0..255` → `0..1`, `0` = top edge |
| 2 | flags | `bit0` `MF_PTR_VISIBLE` (`0` = pointer off / off-screen), `bit1`–`7` reserved |

Console: `fauxmote pointer <x> <y>` / `fauxmote pointer off`.

Default when the slice is stale/absent: **off** (pointer off-screen).

### 5.3 `LINK_CMD` (m→f, 1 byte) — Bluetooth link management

Mirrors the fauxmote CLI so marvin owns the whole link lifecycle. Fire-and-forget;
the result is reflected in the next `STATUS`.

| Byte | Field | Values |
|---|---|---|
| 0 | opcode | `0x01` PAIR (enter sync/pairing) · `0x02` STOP (leave pairing) · `0x03` RECONNECT · `0x04` UNLINK (erase bond) · `0x05` EXT_ATTACH · `0x06` EXT_DETACH · `0x07` STATUS_REQ (send a `STATUS` now) · `0x08` DISCONNECT · `0x09` REBOOT · `0x0A` BT_RESET |

`RECONNECT` is a no-op when the link is already up or an attempt is in flight.
`DISCONNECT` closes both HID channels, staying bonded and reconnectable. `BT_RESET` does
that plus an explicit L2CAP deinit/re-init. `REBOOT` restarts fauxmote (the bond lives in
NVS and survives) — last resort; it costs a heartbeat gap of about a second, and the T1S
link re-establishes on its own.

**`RECONNECT` alone is the normal recovery from the GH3 game-launch drop.** fauxmote
restarts its L2CAP layer as part of every link teardown, because a torn-down session
leaves state that the *next* connect inherits — on hardware the channels reopen and look
healthy while the Wii sees nothing on them and never answers. With the layer restarted,
a reconnect gets a full re-init from the Wii (`0x30` → `0x33` → `0x37`) and a usable
in-game session. `BT_RESET` and `REBOOT` remain as escalation if that ever stops holding;
watch `STATUS` bit6 (host-silent) to decide.

### 5.4 `STATUS` (f→m, 4 bytes) — the uplink

Sent on any state change **and** as a heartbeat (~2 Hz) so marvin can tell the link
is alive and gate/annotate commands.

| Byte | Field | Encoding |
|---|---|---|
| 0 | flags | `bit0` discoverable, `bit1` connected (HID data channel), `bit2` assigned (Wii gave a player slot), `bit3` ext-attached, `bit4` pairing-active, `bit5` bonded (bond in NVS), `bit6` host-silent, `bit7` reserved. fauxmote sets `bit0` and `bit4` **together** — discoverable and pairing-active are the same state in the current code |
| 1 | player_slot | `0` = none, else `1..4` |
| 2 | report_mode | the Wii's last-requested report ID (e.g. `0x37`); defaults to `0x30` (core buttons) before the Wii sets a mode, never `0` |
| 3 | last_result | result of the most recent `LINK_CMD`: `0` = ok/idle, nonzero = error code |

`bit6` (host-silent) means both HID channels are open but the Wii has sent nothing for
3 s — the session exists and is being ignored. `bit1` alone is therefore not proof the
Wii is listening; `bit1 && !bit6` is.

## 6. Semantics & timing

- **Absolute / latest-wins, per slice.** Every input message carries the full
  current state of *its slice* (§4.1). fauxmote latches the most recent message per
  slice and reflects all latched slices in its next Wii report. No history, no
  ordering requirement beyond last-writer-wins within a slice.
- **Send policy (marvin).** Send a slice on any change; additionally resend it at a
  floor rate (~10 Hz) so a dropped frame self-corrects. Bursting faster than the Wii
  report cadence (~15 ms) is wasted. marvin sends only the slices it needs —
  `GUITAR` continuously during gameplay, `WIIMOTE`/`POINTER` only while navigating.
- **Fail-safe (fauxmote), one watchdog.** A single **link timeout** (200 ms
  default) is reset by *any* control message. While it holds, all latched slices
  stay in effect. On expiry (the link has gone fully quiet) fauxmote reverts *all*
  slices to their safe defaults at once — `GUITAR` frets/strum released + whammy
  rest (≈ `0x10`), `WIIMOTE` buttons released / stick centered, `ACCEL` level, `POINTER`
  off. A dead link never holds a note. (marvin's ~10 Hz refresh of at least one
  slice keeps the watchdog alive during normal play.)
- **`STATUS` cadence (fauxmote).** On every state change, plus a ~500 ms heartbeat,
  plus immediately on `STATUS_REQ`.
- **`LINK_CMD`** is fire-and-forget; marvin reads the outcome in the following
  `STATUS`.

## 7. Implementation seam (both ends)

Keep the message layer transport-agnostic so the T1S move (§8) is a transport swap,
not a rewrite.

**fauxmote** — `main/marvin_link.c` (implemented 2026-07-02):
- Owns the second UART, the §3.2 framing/CRC, and message parse/dispatch (FreeRTOS
  task); started at boot via `MarvinLink_Start()`.
- On `GUITAR`/`WIIMOTE`/`LINK_CMD`, calls the **same module APIs the CLI uses**
  (`Wiimote_SetButton`, `Guitar_SetWhammy`, `Wiimote_SetPointer`,
  `Fauxmote_EnterPairing`/`StopPairing`/`Reconnect`/`Unlink`, `Wiimote_SetExtension`),
  so the link is a second front-end over existing logic — no behavior duplicated.
- Emits `STATUS` from the existing `Wiimote_Is*` / `Fauxmote_*` state.
- The CLI stays for manual bring-up/debug.

**marvin** — `net/fauxmote/fauxmote_link.c` (implemented 2026-07-02; FLEXCOM5 2026-07-23;
transport made selectable 2026-07-28):
- The producer/latching layer is transport-independent. Exposes
  `Fauxmote_SendGuitar(mask, whammy, aux)` / `SendGuitarMask(mask)`,
  `Fauxmote_SendNav(...)`, `Fauxmote_SendCmd(op)`, and `Fauxmote_GetStatus(...)`. A TX task
  re-sends the latched `GUITAR` slice on change (low latency) and at a 50 ms floor; the
  `STATUS` uplink is latched into the same state on either transport.
- **Transport seam** behind `MARVIN_FAUXMOTE_TRANSPORT` (default **T1S**, §8): T1S rides
  the shared `net/t1s` controller channel (`0x88B7`, no dedicated peripheral, no RX task —
  a registered handler latches `STATUS`); `=0` owns the **FLEXCOM5 USART (PA16/PA15)** and
  the §3.2 framing with a dedicated RX task parsing the uplink.
- **Mirror-to-both** (not a switched sink): `FretboardLink_Send()` — the one choke
  point both producers (timing pipeline + `manual_control`) call — also calls
  `Fauxmote_SendGuitarMask()`, so the guitar node and fauxmote move in lock-step.
  `GUITAR` byte 0 == the 7-bit guitar mask. Runs on every build, regardless of
  `MARVIN_FRETBOARD_TRANSPORT`.

## 8. T1S transport (transport #2)

The fauxmote side of the T1S transport is **implemented** (`main/mf_t1s.c`, a LAN8651
MAC-PHY on SPI driven by the vendored OPEN Alliance TC6 library) and is the **default**
build; UART is retained as a Kconfig-selectable fallback (`FAUXMOTE_LINK_TRANSPORT_UART`).
The **message layer (§4–§5) travels unchanged as the payload of a T1S Ethernet frame**:

- The UART framing layer (§3.2) is dropped — the Ethernet frame provides delimiting
  and an FCS; `TYPE` + payload become the frame payload.
- **Frame layout:** `[dst MAC][src MAC][ethertype 0x88B7][TYPE][payload…]`. dst is the
  coordinator MAC `02:00:00:00:00:00`; src is this node's MAC `02:00:00:00:00:<id>`.
  The MAC-PHY appends the FCS and pads short frames to the 46-byte minimum, so no manual
  padding — the message layer's per-`TYPE` length check ignores the trailing pad.
- **`STATUS` is an uplink frame** on the same `0x88B7` ethertype, directed at the
  coordinator MAC, sent on change / on `STATUS_REQ` / as the 500 ms heartbeat — identical
  cadence to the UART transport, just carried in a frame instead of a `SOF/LEN/CRC8`
  packet. A separate `0x88B6` presence heartbeat (node_type 3 = controller) rides
  alongside it; see [`docs/t1s-podl-link.md`](t1s-podl-link.md) §7.1/§7.2.
- Only the transport seam (§7) changes; message handling and semantics are identical.

**marvin side (implemented 2026-07-28, default T1S).** marvin has a *single* LAN8651, so
the controller channel is not a second interface — `fauxmote_link` rides the shared
`net/t1s` MAC-PHY. Behind `MARVIN_FAUXMOTE_TRANSPORT={UART,T1S}` (default T1S):
- **TX:** `send_frame` → `T1SLink_SendToController(type, payload, len)`, which stages the
  message into a small static FIFO. The T1S service task frames it
  `[dst=controller][src=coord][0x88B7][TYPE][payload]` and puts it on the bus, interleaved
  with the `0x88B5` guitar command (single in-flight TC6 TX). The MAC-PHY appends the FCS
  and pads to the 46-byte minimum.
- **RX (`STATUS` uplink):** `net/t1s` demuxes `0x88B7` frames from the controller node and
  calls a registered `T1SLink_ControllerHandler`; `fauxmote_link`'s handler latches
  `STATUS` exactly as the UART parser did (shared `latch_status`).
- **Node table:** the controller (id 1) is a gated row in marvin's `s_nodes[]`, so its
  `0x88B6` presence heartbeat lights the `nodes` CLI for free.
- `T1SLink_Initialize` is idempotent, so the fretboard-T1S and fauxmote-T1S paths can both
  call it. `MARVIN_FAUXMOTE_TRANSPORT=0` restores the dedicated FLEXCOM5 UART path (§3).

**Status:** both ends now speak T1S in source; the fauxmote side still awaits its LAN8651
being wired to the Feather (Feather V2 pin defaults SCK=5/MO=19/MI=21/CS=33/RST=27/IRQ=32,
all Kconfig-overridable), so on-bus verification is a later session.

## 9. Open questions

- ~~Exact marvin FLEXCOM instance + pins~~ **settled 2026-07-23:** marvin =
  **FLEXCOM5 / PA16 (`FAUXMOTE_TX`) / PA15 (`FAUXMOTE_RX`)** — a dedicated peripheral,
  independent of the guitar transport; fauxmote = UART1 on Feather RX=`GPIO7` /
  TX=`GPIO8`. Wiring: marvin PA16 → ESP `GPIO7`, ESP `GPIO8` → marvin PA15, common GND.
  (Originally FLEXCOM1/PA28/PA29 on 2026-07-02; moved to a dedicated FLEXCOM5 so the
  link no longer depends on the guitar being on T1S.)
- ~~When to make the planned `ACCEL` slice live in fauxmote~~ **done (fauxmote side)
  2026-07-24:** fauxmote drives the accel field from the `ACCEL` slice; encoding is
  signed g (`1 LSB = 1/32 g`), fauxmote translates to raw. Slices carry no tilt/SP
  semantics — the `GUITAR` aux star-power bit is ignored; marvin drives star power via
  `ACCEL`. Marvin-side `Fauxmote_SendAccel()` producer still to do.
- Whether to retire the now-unused `MF_AUX_STARPOWER` aux bit (star power moved to the
  `ACCEL` slice); a shared-`mf_proto.h` decision to make with the marvin side.
- Whether `ACCEL` needs finer than 8-bit-per-axis for smooth tilt; 10-bit can be
  added under the same type.
- Runtime version negotiation (a `HELLO` exchange) if the two ends ever ship
  independently; unnecessary while both are built from this repo.
</content>
</invoke>
