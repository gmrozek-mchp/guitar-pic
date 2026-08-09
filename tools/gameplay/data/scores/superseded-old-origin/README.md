# Superseded: 2-player amp score crops cut at the old block origin

These 10 crops are 68x78 amp blocks cut at the **pre-2026-08-10** `AMP2P_BLOCK`
origins — left `(128,164)`, right `(515,164)`. Greg re-registered both blocks on
2026-08-10 to left `(131,172)` / right `(513,172)`, which moved where the digit
strip sits *inside* a block crop, so these can no longer build the digit bank:
`AMP2P_BAND_Y0` / `AMP2P_RIGHT_EDGE` are block-local and moved with the origin.

They are kept only as a record of the labelled values. **Do not re-point the loader
at this directory** — re-cut the corpus from a fresh capture taken with the new
`marvin-perf` region rects (`score-2p-left` / `score-2p-right`, now
`(131,172,68,78)` / `(513,172,68,78)`), which is the capture that supersedes them.

Filenames carry the ground truth: `score2p__<side>__<value>__<frame>.png`.
