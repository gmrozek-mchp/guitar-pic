# marvin UI assets — original source material

The MGS design database (`default/src/config/default/default_design.zip`) embeds a copy of
every image and font it uses, but those copies are **derived**: icons are rasterized to the
pixel size the UI needs, artwork is cropped and scaled, and the design keeps one TTF per size
label. This directory holds the **originals** those were made from, so the design can be
rebuilt, an icon re-rendered at a different size, or a logo swapped, without reverse-engineering
a 24×24 PNG.

**Source assets from the design zip only as a last resort.** If an asset has no upstream
original recorded here, the zip's `sourceData` is the only copy — say so explicitly rather than
quietly treating an extracted raster as the original. (`export_assets.py` in the
`mgs-legato-design` skill exists for that recovery case.)

```
assets/
  font/          TTFs the design imports
  icon/lucide/   vector icon originals
  image/         raster assets as imported into the design
  image/source/  upstream artwork the image/ files were derived from
```

## icon/lucide/

From [lucide](https://lucide.dev) — the icon set the Figma mockup used (`lucide-react@0.487.0`).
Filenames are lucide's own, so a fresh download diffs cleanly. Each renders to its design asset
at the size and colour below; verified by rasterizing with
`rsvg-convert -w N -h N` after substituting `currentColor`.

**The eight nav icons ship as pairs**, because a Legato scheme changes a button's fill but
cannot recolour an image: rest is zinc-300, selected is white, and `navigation_highlight()` in
`ui/screens/navigation/screen_navigation.c` swaps the image alongside the scheme. Both members of
a pair are rendered from the *same* SVG so only the colour differs (verified: alpha channels are
pixel-identical).

Only three pairs are on screen today — the drawer carries one row per screen that exists
(Dashboard, Wiimotes, Bus Statistics; `NAV_ENTRY` in `screen_navigation.c`). The other five are
kept for the screens they name, so `prune_unused_images.py` will report them as unreferenced;
that is deliberate, not a leak.

| design asset | lucide file | size | stroke |
|---|---|---|---|
| `NAV_ICON_DASHBOARD` + `_SELECTED` | `house.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_WIIMOTES` + `_SELECTED` | `gamepad-2.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_LOGS` + `_SELECTED` | `file-text.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_PERFORMANCE` + `_SELECTED` | `gauge.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_SYSTEM_INFO` + `_SELECTED` | `info.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_DIAGNOSTICS` + `_SELECTED` | `activity.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `NAV_ICON_SETTINGS` + `_SELECTED` | `settings.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `BUTTON_ICON_HAMBURGER` | `menu.svg` | 24×24 | `#FFFFFF` |
| `BUTTON_ICON_CLOSE` | `x.svg` | 20×20 | `#9F9FA9` |
| `BUTTON_ICON_CHECK` | `check.svg` | 20×20 | `#000000` (dark, on a light button) |
| `BUTTON_FACE_SELECT_SONG` | `list-music.svg` | 14×14 | `#D4D4D8` |
| `BUTTON_FACE_START` | `play.svg` | 14×14 | `#FFFFFF` |
| `NAV_ICON_BUS` + `_SELECTED` | `network.svg` | 24×24 | `#D4D4D8` / `#FFFFFF` |
| `BUTTON_ICON_TROPHY` | `trophy.svg` | 20×20 | `#492F03` (see below) |

To regenerate a nav pair:

```bash
sed 's/currentColor/#D4D4D8/' icon/lucide/<icon>.svg | rsvg-convert -w 24 -h 24 -o rest.png
sed 's/currentColor/#FFFFFF/' icon/lucide/<icon>.svg | rsvg-convert -w 24 -h 24 -o sel.png
```

then `set_image_source.py` (replace in place) or `add_image.py` (new asset) from the
`mgs-legato-design` skill. `BUTTON_ICON_HAMBURGER` is white because it lives in the titlebar and
inherits that chrome's colour — not a nav state.

`BUTTON_ICON_TROPHY`'s stroke is a **composite, not a colour the mockup names**: the mockup
draws it `text-black/70` on the SHOWDOWN button's `bg-amber-500`, and a Legato image carries no
alpha-over-scheme relationship, so the 70% black is pre-composited against `#F59E0B` →
`#492F03`. Re-derive it if that button's fill changes.

Two traps:

- **`play.svg` ships as an open outline** (`fill="none"`). The shipped asset is a **solid**
  triangle, because the mockup rendered it as `<Play className="… fill-current" />`. To
  reproduce it, set `fill` as well as `stroke`. It is the only icon here that needs this —
  every other one is stroke-only.
- **lucide renames and aliases icons between versions.** `NAV_ICON_DASHBOARD` is `house.svg`
  today; the mockup imported the component as `Home`, which is now a legacy alias. If a filename
  above 404s, search lucide for the shape rather than assuming the name is stable.

## image/

As imported into the design — already at final pixel size, so these are *not* upstream masters
where `image/source/` has one.

| file | design asset | upstream |
|---|---|---|
| `Logo-GUITAR.png` (225×45) | `LOGO_GUITAR` | — project branding, authored at this size |
| `Logo-PIC.png` (121×43) | `LOGO_PIC` | — as above |
| `Logo-MICROCHIP.png` (194×45) | `LOGO_MICROCHIP` | `source/Microchip-Logo-Horizontal-White-Red.png` (361×84) |
| `HumanPlayer_gradient.png` (254×208) | `PLAYER_HUMAN_ART` | `source/VitruvianGuitarist.png` (1024×1024) |
| `LemmyOnStagePlayerImage_gradient.png` (254×208) | `PLAYER_ROBOT_ART` | `source/LemmyOnStage.png` (816×1276) |
| `HumanPlayer.png`, `LemmyOnStagePlayerImage.png` | — | the same crops *before* the gradient overlay |
| `StandBy.png` (718×478) | — | video "please stand by" card |
| `Logo-MCC.png`, `Logo-MGS.png` | — | were in the design, pruned 2026-08-05 |
| `*_LED_*.png` (8×8 / 12×12) | — | status dots; never referenced by a widget or by C |

Everything in the second group is kept deliberately: unused by the current design, but original
material worth not losing.

## image/source/

Upstream artwork, higher resolution than anything the design contains.

| file | note |
|---|---|
| `VitruvianGuitarist.png` | AI render; was `Gemini_Generated_Image_squxexsquxexsqux.png` |
| `LemmyOnStage.png` | AI render; was `Gemini_Generated_Image_an80s3an80s3an80.png` |
| `LemmyPuppet.jpg` | the puppet on a plain backdrop — different framing, not the source of a design asset |
| `Microchip-Logo-Horizontal-White-Red.png` | 361×84, official horizontal lockup |
| `mchpLogo.png` | 288×67 variant |

## font/

| file | status |
|---|---|
| `DejaVuSansMono.ttf` | **live** — backs all 7 `DejaVuSansMono_*` design fonts |
| `DejaVuSansMono-Bold.ttf` | **live** — backs all 7 `DejaVuSansMonoBold_*` |
| `NotoMono-Regular.ttf`, `NotoSans-Regular.ttf` | superseded — the pre-2026-08-05 design used Noto |

The design carries 14 font assets but only **two** distinct TTFs; the rest are size variants of
the same file. Verify a TTF's real identity from its `name` table rather than the design's label,
which MGS preserves even when it substituted a different face.
