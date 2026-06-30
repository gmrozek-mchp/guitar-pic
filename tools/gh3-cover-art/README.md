# GH3 Cover Art Tools

Tools for fetching and processing Guitar Hero III cover art for the marvin game UI.

## Structure

- `fetch_gh3_cover_art.py` — Fetch cover art from MusicBrainz Cover Art Archive
- `process_gh3_cover_art.py` — Resize + install covers straight into the marvin art tree
- `data/` — Original JPG files (fetched by `fetch_gh3_cover_art.py`, tracked in git as the
  stable re-process inputs; the fuzzy MusicBrainz fetch isn't reproducible)

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

### Process + install covers (one step)

After fetching, generate both tiers straight into the marvin art tree:

```bash
uv run process_gh3_cover_art.py
```

This writes (clearing any stale cover files first), keyed by the recognizer's
`<setlist>-<NN>` and pulling each song's difficulty from `songs.csv`:
- `../../firmware/marvin/data/games/gh3-wii/art/large/<setlist>-<NN>.png` (508×208)
- `../../firmware/marvin/data/games/gh3-wii/art/small/<setlist>-<NN>.jpg` (144×144)

Point `--out` elsewhere to stage into a different tree.

## Asset Format

- **large (`art/large/<setlist>-<NN>.png`)** — 508×208 song-select detail strip: the
  cover scaled to 508 wide, center-cropped to the middle 208 px band, with a top/bottom
  **difficulty-colored fade** baked in (main tiers `"1".."8"` → green→red, `"bonus"`/unknown
  → gray, read from `songs.csv`). PNG so the gradient stays lossless. Fade depth/brightness
  are the `FADE_PX` / `FADE_VALUE` tunables at the top of the script.
- **small (`art/small/<setlist>-<NN>.jpg`)** — 144×144 now-playing thumbnail: scaled to fit
  with black pillarbox/letterbox, no fade. JPEG quality 92.

Both are offline-processed once; marvin decodes them into static RGB888 caches at boot
(`firmware/marvin/default/src/game/art.c`) and loads with no runtime scaling.
