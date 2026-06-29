# marvin SD-card data (seed)

Files here mirror the FAT32 layout marvin expects on the SD card. Copy the tree
to the card root. Paths are relative to the mount point (spec §4.6.2, §4.8).

```
games/gh3-wii/
  songs.csv          song catalog — labels keyed by (setlist, index); spec §4.8.3
  art/               album art, keyed by the same (setlist, index); spec §4.8.7
    small/  <setlist>-<NN>.{jpg,png}     e.g. main-04.jpg   (one DDR cache tier)
    large/  <setlist>-<NN>.{jpg,png}     e.g. bonus-12.png  (one DDR cache tier)
players/
  results.csv        per-player high scores (written by marvin); spec §4.8.6
```

`songs.csv` columns: `setlist,index,title,artist,album,bpm,length_s,year,genre,difficulty`.
`setlist` is `main`/`bonus`; `title`/`artist`/`album`/`genre`/`difficulty` are
CSV-quoted (and may be empty); `bpm`/`length_s`/`year` are integers, `0` when
unknown. Rows may be sparse or out of order — only `(setlist, index)` is the key.

`title`/`artist`/`album` are sourced from the `SONGS` table in
[`tools/fetch_gh3_cover_art.py`](../../../../tools/fetch_gh3_cover_art.py); that
script's `--catalog` mode also fills `year` and `genre` from MusicBrainz. `bpm`,
`length_s`, and `difficulty` are reserved-but-blank for now (GH3 exposes no
per-song difficulty rating).

The catalog is **labels only**: recognition is compile-time in flash, so a
missing or partial `songs.csv` just shows "Unknown song" and never affects play.
Album art is derived directly from `(setlist, index)` and is **not** referenced
from `songs.csv` — the two are independent.

> **The checked-in `songs.csv` is a placeholder seed** generated from the
> recognizer's song slugs (titles auto-humanized; `artist`/`bpm`/`length_s`
> blank). Replace the values with real metadata before relying on the display.

## Boot splash (`ui/splash.raw`)

The boot splash is **not** read from the SD card — it lives in QSPI NOR and is
provisioned over JTAG. `ui/splash.jpg` is the editable source; `ui/splash.raw` is
the pre-decoded framebuffer the firmware actually loads.

Regenerate the raw from the source with [`ui/make-splash.sh`](ui/make-splash.sh):

```
ui/make-splash.sh [input] [output]   # defaults: ui/splash.jpg -> ui/splash.raw
```

It produces headerless raw RGBA8888, 1280×800, in the XLCDC layer's native byte
order (pixel word `0xRRGGBBAA`, memory bytes `[A,B,G,R]`) — exactly 4,096,000
bytes, the only size `splash.c` accepts. Uses `uv` + Pillow (no ImageMagick/ffmpeg).

Then flash it: `../../openocd/program-qspi.sh splash` (writes `ui/splash.raw` to
QSPI `0x000000`).
