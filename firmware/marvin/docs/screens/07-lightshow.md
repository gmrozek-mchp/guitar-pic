# Screen: Node Detail — lightshow

> Content spec for Figma Make. Detail screen reached by tapping the "lightshow" card on the
> System Overview screen. Product-showcase tone — lead with the chip.

## Identity header

- **Name:** lightshow — the lights
- **Microchip part:** **PIC32CM6408**
- **Accent color:** `#F87171` (red)
- **Status:** In development

## What it does

lightshow turns the same beat that moves lemmy's head into a stage light show — driving colorful
LED strips that pulse and animate along with the music.

## Why the PIC32CM6408

This board drives its LED strips directly from the microcontroller's own pins at full brightness
voltage — no extra driver chip needed in between. It's a nice showcase of how much a single small
MCU can do on its own: sensing the network, running the show logic, and driving the lights, all in
one chip.

## Built with

- **MPLAB X IDE** + **MCC** for peripheral setup
- Microchip **LAN8651** for its network connection

## Visual style notes

- Red (`#F87171`) accent — a coincidence of the palette, not a warning; keep the status pill
  neutral so this card doesn't read as an error state.
- A small strip of colored LED dots or a sparkle glyph fits well.

## Navigation

- Back → System Overview (`00-system-overview.md`).
