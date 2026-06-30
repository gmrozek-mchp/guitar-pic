# GH3 Cover Art Tools

Tools for fetching and processing Guitar Hero III cover art for the marvin game UI.

## Structure

- `fetch_gh3_cover_art.py` — Fetch cover art from MusicBrainz Cover Art Archive
- `process_gh3_cover_art.py` — Resize covers to marvin-ready sizes (offline processing)
- `data/` — Cover art data directory
  - Original JPG files (fetched by `fetch_gh3_cover_art.py`)
  - `508w/` — Song-select screen size (508px wide, aspect ratio preserved)
  - `144x144/` — Now-playing section size (144×144 with pillarboxing if needed)

## Setup

```bash
cd tools/gh3-cover-art
uv sync
```

## Usage

### Fetch Original Cover Art

Requires MusicBrainz API contact info (your email or project URL).

```bash
uv run fetch_gh3_cover_art.py --contact your@email.com
```

Optional: Generate the song catalog CSV alongside with year/genre metadata:

```bash
uv run fetch_gh3_cover_art.py --contact your@email.com \
    --catalog ../../firmware/marvin/data/games/gh3-wii/songs.csv
```

### Process Covers to Marvin Sizes

After fetching, generate the two sized versions for offline use on marvin:

```bash
uv run process_gh3_cover_art.py
```

This creates:
- `data/508w/` — Song-select assets (scale-to-width, high quality)
- `data/144x144/` — Now-playing assets (fit-to-square with black padding, high quality)

All images are JPEG at quality 92 to balance size and fidelity.

### Install to Marvin Data

Once processed, install the covers to the marvin SD-card data tree:

```bash
uv run install_art.py
```

This copies covers to the firmware's expected locations with the standard naming:
- `../../firmware/marvin/data/games/gh3-wii/art/small/<setlist>-<NN>.jpg` (144×144)
- `../../firmware/marvin/data/games/gh3-wii/art/large/<setlist>-<NN>.jpg` (508px wide)

## Asset Format

- **508px wide (`art/large/`)** — For the song-select detail screen. Covers are scaled to 508px wide with aspect ratio preserved. Heights vary (~500–520px for square album art).
- **144×144 (`art/small/`)** — For the now-playing info panel. Covers are scaled to fit within 144×144 with black pillarboxing/letterboxing if needed to maintain aspect ratio.

Both are offline-processed once; marvin loads pre-scaled assets without runtime scaling. Naming follows firmware convention: `<setlist>-<NN>.jpg` (e.g., `main-04.jpg`, `bonus-12.jpg`).
