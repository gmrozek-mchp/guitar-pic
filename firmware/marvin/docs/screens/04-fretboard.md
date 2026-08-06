# Screen: Node Detail — fretboard

> Content spec for Figma Make. Detail screen reached by tapping the "fretboard" card on the
> System Overview screen. Product-showcase tone — lead with the chip; keep this to what it does
> and why the part fits, not the internal messaging between boards.

## Identity header

- **Name:** fretboard — the eyes
- **Microchip part:** **PIC32CM6408**
- **Accent color:** `#FB923C` (orange)
- **Status:** In development

## What it does

fretboard watches the game screen where the notes actually appear and senses them the instant they
arrive — then decides, right there on the board, which button that note calls for. It's a small
board doing real-time sensing *and* on-device decision-making, without waiting on anything else to
tell it what to do.

## Why the PIC32CM6408

Running a lightweight inference model directly on a small, low-power microcontroller — right next
to the sensor — means split-second decisions happen locally instead of round-tripping to a bigger
processor. It's a nice showcase of edge intelligence on a genuinely small chip.

## Built with

- **MPLAB X IDE** + **MCC** for peripheral and sensor setup
- Microchip **LAN8651** for its network connection

## Visual style notes

- Orange (`#FB923C`) accent.
- A simple eye or sensor glyph — or five small colored dots echoing the fret colors — fits well.

## Navigation

- Back → System Overview (`00-system-overview.md`).
