# Screen: Node Detail — beatbox

> Content spec for Figma Make. Detail screen reached by tapping the "beatbox" card on the
> System Overview screen. Product-showcase tone — lead with the chip.

## Identity header

- **Name:** beatbox — the ears
- **Microchip part:** **dsPIC33AK512MPS512** (Digital Signal Controller)
- **Accent color:** `#F472B6` (pink)
- **Status:** In development

## What it does

beatbox listens to the music itself and figures out where the beat falls, in real time — then
shares that rhythm with the rest of the system so the puppet can nod its head and the lights can
pulse in time with the song.

## Why the dsPIC33AK512MPS512

Finding a beat in a live audio signal means crunching real-time signal-processing math fast — this
is exactly what a Digital Signal Controller is built for: microcontroller-style ease of use with
the horsepower of a DSP. It's a great showcase of Microchip silicon built specifically for
real-time audio analysis.

## Built with

- **MPLAB X IDE** with the **XC-DSC** compiler
- The Curiosity Platform Development Board

## Visual style notes

- Pink (`#F472B6`) accent.
- A waveform or audio-pulse glyph fits well — this is the only board listening to sound rather
  than watching light or receiving a command.

## Navigation

- Back → System Overview (`00-system-overview.md`).
