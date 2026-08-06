# Screen: Node Detail — guitar

> Content spec for Figma Make. Detail screen reached by tapping the "guitar" card on the
> System Overview screen. Product-showcase tone — lead with the chip.

## Identity header

- **Name:** guitar — the hands
- **Microchip part:** **PIC32CM PL10**
- **Accent color:** `#34D399` (emerald)
- **Status:** Live

## What it does

This board is the one that actually presses the buttons — receiving marvin's call and instantly
lighting up the right frets and strum, in perfect time with the music. It's a small, dedicated
board with one job, and it does it fast and reliably.

## Why the PIC32CM PL10

A tiny, efficient Cortex-M0+ microcontroller is all it takes to react instantly and drive outputs
precisely — no need for a big, power-hungry processor just to press a button on cue. Paired with
Microchip's **LAN8651** single-pair Ethernet chip, this little board talks to the rest of the
system over the same simple shared cable as everyone else.

## Built with

- **MPLAB X IDE** + **MCC (MPLAB Code Configurator)** for fast peripheral setup
- Microchip **LAN8651** for its network connection

## Visual style notes

- Emerald (`#34D399`) accent.
- A simple guitar-fret-button glyph (5 colored buttons + a strum bar) suits this card well.

## Navigation

- Back → System Overview (`00-system-overview.md`).
