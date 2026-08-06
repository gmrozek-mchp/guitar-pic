# Screen: System Overview — Powered by Microchip

> Content spec for Figma Make. Top-level informational screen on marvin's on-device UI, reached
> from the nav drawer. This is a **product showcase**, not an engineering diagram: the goal is to
> make a viewer go "wait, all of that runs on Microchip parts?" Keep language plain and confident;
> save wiring/protocol detail for engineering docs, not this screen.

## Purpose

This whole robot — the vision system that watches the game, the hands that press the buttons, the
ears that hear the beat, the puppet that dances to it — is built from a family of Microchip chips,
all talking to each other over **one shared cable** using Microchip's 10BASE-T1S single-pair
Ethernet technology. This screen is the "meet the team" page: one card per board, tap any card to
see that board's story.

## Headline message

**"Seven boards. One cable. Every one Microchip inside."**

The single-pair Ethernet link that ties all seven nodes together runs through Microchip's
**LAN8651** — a chip that turns one ordinary twisted-pair cable into a shared network for up to
eight devices, no switch or hub required. That's the hero fact for this screen: a whole
multi-board robot, wired together as simply as possible, on Microchip's networking silicon.

## Layout

Draw the seven nodes as cards tapped off a single shared rail (this is a real shared-cable network,
not a hub-and-spoke wiring diagram — showing it as one line with taps is both accurate and a nice
visual metaphor for "one cable does it all"). Each card shows:

- Node name
- **Microchip part number**, prominently — this is the marketing payload, treat it like a spec sheet
  callout, not fine print
- A one-line, plain-English description of what the board does
- A status pill (see below)
- Its accent color

| Node | Microchip part | What it does | Status | Accent color |
|---|---|---|---|---|
| **marvin** | SAM9X75 | The brain — watches the game and calls the shots | Live | `#22D3EE` (cyan) |
| **fauxmote** | *(none shown — see note)* | An alternate way to talk to the game console | Concept demo | `#A78BFA` (violet) |
| **guitar** | PIC32CM PL10 | The hands — presses the buttons in perfect time | Live | `#34D399` (emerald) |
| **fretboard** | PIC32CM6408 | The eyes — watches the fretboard and senses every note | In development | `#FB923C` (orange) |
| **beatbox** | dsPIC33AK512MPS512 | The ears — listens to the music and finds the beat | In development | `#F472B6` (pink) |
| **lemmy** | PIC32CM6408 | The body — head-bangs a puppet in time with the music | In development | `#FACC15` (yellow) |
| **lightshow** | PIC32CM6408 | The lights — brings the stage to life | In development | `#F87171` (red) |

**Note on fauxmote:** it's the one board in this lineup that isn't Microchip — a wireless
alternative to physically pressing buttons. Leave its part-number cell blank (no badge, no chip
name) rather than naming the non-Microchip hardware; it's fine to include the card for
completeness, but it shouldn't carry any "Microchip inside" styling like the other six.

## Status pills

Keep this to three plain states — no engineering jargon (no "PLCA follower," "bring-up," etc.):

- **Live** — running on real hardware today
- **In development** — hardware exists, still being brought fully online
- **Concept demo** — proof-of-concept, not the primary path

## Visual style

- Confident, product-page energy: bold part numbers, clean card layout, generous spacing —
  think "spec sheet meets team roster," not a wiring schematic.
- Keep the per-node accent colors — they're used consistently across this app's UI, so reusing
  them here keeps the whole product feeling coherent.
- A one-line caption under the headline works well: *"Six Microchip microcontrollers, one
  Microchip networking chip, wired together with a single cable."*

## Navigation

- Tap a node card → its detail screen (`01-marvin.md` … `07-lightshow.md`).
- Back button returns to the nav drawer / previous screen.
