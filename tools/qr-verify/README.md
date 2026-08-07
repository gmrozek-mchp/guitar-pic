# qr-verify

Decode-checks the QR codes on marvin's **System Info** detail screen.

The QRs are not stored anywhere — they are encoded on the panel at screen setup from the
URLs in the `NODE` table
(`firmware/marvin/default/src/ui/screens/system/screen_system.c`). So there is no asset
to inspect: this tool compiles the firmware's own rasterizer for the host, renders every
URL in that table, and decodes the result with an independent decoder.

```bash
cd tools/qr-verify
uv run verify.py                  # verify every URL in the NODE table
uv run verify.py --png data/tiles # also dump the tiles as PNGs to look at
```

Expected output:

```
8 QR URLs in the NODE table (5 unique)

ok   marvin       50B  164px  https://www.microchip.com/en-us/product/SAM9X75D2G
...
all 8 QR tiles decode back to their URL
```

## What it actually links

`qr_dump.c` (host-only) plus two unmodified firmware sources:

- `firmware/marvin/default/src/ui/gfx/qr_raster.c` — the rasterizer, deliberately free
  of Legato and Harmony dependencies so it compiles on the host
- `firmware/marvin/default/src/third_party/qrcodegen/qrcodegen.c` — vendored encoder

`ui/qr_art.c` is *not* linked: it is the Legato/DDR cache around the rasterizer, and has
no pixel logic of its own to get wrong.

## What it checks

- the tile decodes, and the decoded text equals the URL character for character
- the tile is exactly 164x164 — the size the screen's layout constants are derived from
- every pixel is pure black or pure white (a QR is not antialiased)
- the 4-module quiet zone is genuinely blank
- the URL still fits the pinned QR version, cross-checked against an independent encoder

That covers the ways a rasterizer goes wrong — stride, transposition, inversion, a quiet
zone eaten by an off-by-one — none of which look wrong to a human glancing at a QR.

The URL list is scraped from `screen_system.c`, not duplicated here, so a URL added to
the table is verified without touching this tool. It hard-fails if the number of
`.qr_url` initializers does not match the number of `.name` entries, since that means
the scrape has silently lost track of which URL belongs to which card.
