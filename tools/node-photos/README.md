# node-photos

Prepares the per-node board photos shown on marvin's **System Info** detail screen.

Drop one photo per node in `data/`, named for the node, then:

```bash
cd tools/node-photos
uv run process_node_photos.py
```

That writes `288x620` PNGs into `firmware/marvin/data/system/nodes/`, which is the
card image tree — copy it to the SD card (or point `--out` straight at the card).

Node names must match the `PHOTO` table in
`firmware/marvin/default/src/game/node_art.c`:

```
marvin  fauxmote  guitar  fretboard  beatbox  lemmy  lightshow
```

## Why this step isn't optional

The firmware reads the PNG header and **skips any file that isn't exactly
288x620** (`NODE_ART_W` x `NODE_ART_H` in `game/node_art.h`), leaving that node's
photo slot empty and the detail screen showing a blank frame. It also has to be PNG:
Legato's JPEG decoder gates its block writes on the renderer clip rect, which is
stale during marvin's offscreen boot decode, so a runtime JPEG decodes to noise.

A missing photo is fine — the loader never fails boot, and the card just reads blank.

## Fit modes

The photo column is tall and narrow (~1:2.15), which almost no camera photo is.

- `--fit contain` (default) — scale the whole photo to fit and letterbox the rest in
  the card's own zinc-900, so the board stays recognizable. Right for landscape shots.
- `--fit cover` — centre-crop to fill the column. Right for a photo already framed
  portrait.
