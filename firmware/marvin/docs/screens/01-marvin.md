# Screen: Node Detail — marvin

> Content spec for Figma Make. Detail screen reached by tapping the "marvin" card on the
> System Overview screen. Product-showcase tone — lead with the chip, describe the job in plain
> language, no wiring/protocol detail.

## Identity header

- **Name:** marvin — the brain
- **Microchip part:** **SAM9X75** (Cortex-A5 microprocessor)
- **Accent color:** `#22D3EE` (cyan)
- **Status:** Live

## What it does

marvin is the one running this screen right now, and it's the brains of the whole operation. It
watches the game over HDMI, recognizes notes in real time, decides exactly when to press each
button, and coordinates every other board in the system. It's also driving the 10.1″ touchscreen
display you're looking at.

## Why the SAM9X75

One chip doing the job of several: a capable applications processor with the horsepower for
real-time computer vision, plus the display and multimedia interfaces to drive a full touchscreen
UI — all without needing a separate graphics chip or a second board. It's the kind of "do it all"
processor that lets a project like this stay a one-board brain instead of a rack of them.

## Built with

- **MPLAB X IDE** and the **Curiosity** development platform for bring-up
- A 10.1″ touchscreen display, driven directly by the SAM9X75

## Visual style notes

- Cyan (`#22D3EE`) accent.
- Since this is the device the viewer is holding, a small "this device" cue is a nice touch —
  everything else in the lineup is a separate physical board.

## Navigation

- Back → System Overview (`00-system-overview.md`).
