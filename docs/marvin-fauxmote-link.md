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
| `0x03` | `LINK_CMD` | m→f | 1 | v1 | Bluetooth link management (pair/stop/reconnect/unlink/ext). |
| `0x04` | `ACCEL` | m→f | 3 | planned | Wiimote accelerometer state (tilt → star power, motion). |
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
| 2 | aux | `bit0` Start (`+`), `bit1` Select (`−`), `bit2` Pedal, `bit3` Star-Power*, `bit4`–`7` reserved |

Byte 0 is **bit-identical to marvin's existing guitar-node command bitmask** (the
[`T1SLink_SendToGuitar`](../firmware/marvin/default/src/net/t1s/t1s_link.h) mask),
so marvin feeds the same computed mask to either actuation sink with no
translation.

\* Star-Power is a semantic bit: fauxmote maps it to whatever mechanism activates
SP in-game (GH3 uses a Wiimote tilt, not a button — driven via the `ACCEL` slice,
§5.5). **Not implemented in fauxmote yet** (the emulator streams a fixed level
accel); a no-op until the `ACCEL` slice goes live.

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

### 5.5 `ACCEL` (m→f, 3 bytes) — *planned*

Wiimote accelerometer state, the same slice used to synthesize star-power tilt in
GH3 and general motion input. Independent of `GUITAR`.

| Byte | Field | Encoding |
|---|---|---|
| 0 | accel X | `0..255`, center `128` (mapped to the Wiimote's accel range) |
| 1 | accel Y | `0..255`, center `128` |
| 2 | accel Z | `0..255`, center `128` (gravity ~+1 g at rest) |

Default when the slice is stale/absent: **level** (all `128`, i.e. Z at +1 g). Not
consumed by fauxmote yet — the emulator currently streams a fixed level accel;
`ACCEL` goes live when fauxmote drives the accel field from this slice (which also
makes the `GUITAR` aux star-power bit meaningful). A finer 10-bit encoding can be
added later without changing the type.

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
| 0 | opcode | `0x01` PAIR (enter sync/pairing) · `0x02` STOP (leave pairing) · `0x03` RECONNECT · `0x04` UNLINK (erase bond) · `0x05` EXT_ATTACH · `0x06` EXT_DETACH · `0x07` STATUS_REQ (send a `STATUS` now) |

### 5.4 `STATUS` (f→m, 4 bytes) — the uplink

Sent on any state change **and** as a heartbeat (~2 Hz) so marvin can tell the link
is alive and gate/annotate commands.

| Byte | Field | Encoding |
|---|---|---|
| 0 | flags | `bit0` discoverable, `bit1` connected (HID data channel), `bit2` assigned (Wii gave a player slot), `bit3` ext-attached, `bit4` pairing-active, `bit5` bonded (bond in NVS), `bit6`–`7` reserved. fauxmote sets `bit0` and `bit4` **together** — discoverable and pairing-active are the same state in the current code |
| 1 | player_slot | `0` = none, else `1..4` |
| 2 | report_mode | the Wii's last-requested report ID (e.g. `0x37`); defaults to `0x30` (core buttons) before the Wii sets a mode, never `0` |
| 3 | last_result | result of the most recent `LINK_CMD`: `0` = ok/idle, nonzero = error code |

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

**marvin** — `net/fauxmote/fauxmote_link.c` (implemented 2026-07-02; moved to FLEXCOM5 2026-07-23):
- Owns the **FLEXCOM5 USART (PA16/PA15)** — a dedicated peripheral, independent of
  the guitar transport — and the framing. Exposes
  `Fauxmote_SendGuitar(mask, whammy, aux)` / `SendGuitarMask(mask)`,
  `Fauxmote_SendNav(...)`, `Fauxmote_SendCmd(op)`, and `Fauxmote_GetStatus(...)`.
- A TX task re-sends the latched `GUITAR` slice on change (low latency) and at a
  50 ms floor; an RX task parses the `STATUS` uplink.
- **Mirror-to-both** (not a switched sink): `FretboardLink_Send()` — the one choke
  point both producers (timing pipeline + `manual_control`) call — also calls
  `Fauxmote_SendGuitarMask()`, so the guitar node and fauxmote move in lock-step.
  `GUITAR` byte 0 == the 7-bit guitar mask. Runs on every build (FLEXCOM5 is
  dedicated), regardless of `MARVIN_FRETBOARD_TRANSPORT`.

## 8. T1S transport (transport #2, future)

When fauxmote (or a successor node) joins the T1S bus, the **message layer (§4–§5)
travels unchanged as the payload of a T1S Ethernet frame**:

- The UART framing layer (§3.2) is dropped — the Ethernet frame provides delimiting
  and an FCS; `TYPE` + payload become the frame payload.
- Addressed marvin ↔ fauxmote-node like the other T1S nodes (see
  [`docs/t1s-podl-link.md`](t1s-podl-link.md) §7 for the addressing/ethertype scheme).
- Only the transport seam (§7) changes; message handling and semantics are identical.

**Caveat:** the current ESP32 Feather has no 10BASE-T1S MAC-PHY, so this transport
is hardware-dependent and genuinely future work; UART is the working link.

## 9. Open questions

- ~~Exact marvin FLEXCOM instance + pins~~ **settled 2026-07-23:** marvin =
  **FLEXCOM5 / PA16 (`FAUXMOTE_TX`) / PA15 (`FAUXMOTE_RX`)** — a dedicated peripheral,
  independent of the guitar transport; fauxmote = UART1 on Feather RX=`GPIO7` /
  TX=`GPIO8`. Wiring: marvin PA16 → ESP `GPIO7`, ESP `GPIO8` → marvin PA15, common GND.
  (Originally FLEXCOM1/PA28/PA29 on 2026-07-02; moved to a dedicated FLEXCOM5 so the
  link no longer depends on the guitar being on T1S.)
- When to make the planned `ACCEL` slice live in fauxmote (drive the Wiimote accel
  field from it) — this is what makes GH3 star power (`GUITAR` aux `bit3`) work.
- Whether `ACCEL` needs finer than 8-bit-per-axis for smooth tilt; 10-bit can be
  added under the same type.
- Runtime version negotiation (a `HELLO` exchange) if the two ends ever ship
  independently; unnecessary while both are built from this repo.
</content>
</invoke>
