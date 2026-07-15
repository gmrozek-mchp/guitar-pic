"""Offline proof of scoreboard-chrome presence as the gameplay-screen test.

Idea under test: a gameplay screen is identified not by a whole-frame centroid
(flaky — most of the frame is dynamic) but by the presence of its static scoring
chrome. 1-player shows one bottom-left score block (score.py / SCORE_CHROME_BOX);
2-player shows two top amp panels (amp2p.py / AMP2P_BLOCK). Three masked,
per-frame-normalized SAD probes run over the whole labelled corpus.

A frame reads:
  1p present   iff  p1              <= TAU        (bottom score block)
  2p present   iff  max(pL, pR)     <= TAU        (both top amp panels)
  not gameplay iff  neither fires

Each probe = min masked normalized SAD over a small integer offset search, divided
by the masked-pixel count so the three probes are comparable (per-frame zero-mean/
unit-std normalize => ~0.0 identical shape, ~1.0 unrelated).

Passes: (1) clean per-class distribution; (2) A2D-slop robustness over perturb.py's
envelope — present must stay present, absent must stay absent; (3) a recommended
TAU with its margin.

Run: uv run python scan_scoreboard_presence.py
"""

from __future__ import annotations

import numpy as np

from gameplay import amp2p, perturb, score
from gameplay.corpus import load_corpus, load_score_corpus

import os

SEARCH = int(os.environ.get("SB_SEARCH", "0"))  # offset search radius (±SEARCH). 0 = fixed
                    # nominal offset, no search — the corpus is the game's own framebuffer
                    # captured pixel-exact, so no rig offset to absorb (registration, the
                    # ±8 amp2p.calibrate, is host-only + deferred with the 2p digit reader).
GAMEPLAY = ("in_song", "in_song_2p")


def _min_sad_per_px(block_luma_fn, ref_n, mask) -> float:
    npix = int(mask.sum())
    best = None
    for dy in range(-SEARCH, SEARCH + 1):
        for dx in range(-SEARCH, SEARCH + 1):
            tn = score._norm_masked(block_luma_fn(dx, dy), mask)
            sad = float(np.abs((tn - ref_n)[mask]).sum())
            if best is None or sad < best:
                best = sad
    return best / npix


def build_probe():
    score_imgs = [s.image for s in load_score_corpus()]
    if not score_imgs:
        raise SystemExit("no 1p score corpus frames (data/scores/score__*.png)")
    chrome = score.build_chrome_reference(score_imgs)
    ref1p_n = score._norm_masked(chrome.ref, chrome.mask)

    refL, refR = amp2p.build_reference("left"), amp2p.build_reference("right")
    refL_n = amp2p._norm_masked(refL.ref, refL.mask)
    refR_n = amp2p._norm_masked(refR.ref, refR.mask)

    def probe(img):
        p1 = _min_sad_per_px(lambda dx, dy: score._block_luma(img, dx, dy), ref1p_n, chrome.mask)
        pl = _min_sad_per_px(lambda dx, dy: amp2p._block_luma(img, "left", dx, dy), refL_n, refL.mask)
        pr = _min_sad_per_px(lambda dx, dy: amp2p._block_luma(img, "right", dx, dy), refR_n, refR.mask)
        return p1, pl, pr

    return probe


def match_score(sid: str, p) -> float:
    """The SAD that must be LOW for this frame to read as its (gameplay) class."""
    p1, pl, pr = p
    return p1 if sid == "in_song" else max(pl, pr)  # 2p requires both amps


def fire_proximity(p) -> float:
    """Lowest SAD any gameplay rule sees — how close a frame is to reading gameplay."""
    p1, pl, pr = p
    return min(p1, max(pl, pr))


def main():
    probe = build_probe()
    samples = load_corpus()  # full 720x480 frames only (score/streak are block crops)
    by_class: dict[str, list] = {}
    for s in samples:
        by_class.setdefault(s.screen_id, []).append((s, probe(s.image)))

    # ── Pass 1: clean per-class distribution ─────────────────────────────────
    print(f"SAD-per-masked-pixel, clean (min over ±{SEARCH} search). Low = chrome present.\n")
    print(f"{'screen':<20} {'n':>3}   {'1p  min/med/max':>21}   {'2pL min/med/max':>21}   {'2pR min/med/max':>21}")
    print("-" * 96)

    def fmt(v):
        a = np.array(v)
        return f"{a.min():6.2f} {np.median(a):6.2f} {a.max():6.2f}"

    for sid in sorted(by_class):
        rows = [p for _, p in by_class[sid]]
        print(f"{sid:<20} {len(rows):>3}   "
              f"{fmt([r[0] for r in rows]):>21}   "
              f"{fmt([r[1] for r in rows]):>21}   {fmt([r[2] for r in rows]):>21}")

    # ── Pass 2: A2D-slop robustness over the envelope battery ────────────────
    # marvin captures native BGR888 at the Wii's 720x480, pixel-locked (no CPU
    # post-processing; capture_pipeline.md). The component->HDMI converter re-samples
    # to a fixed digital raster, so positional slop (translate/scale) is not a
    # per-frame effect — the real 2p snapshots read ~0.04 at fixed offset 0. Any
    # residual is a static offset (one-time, not needed here). So the operative
    # threshold comes from the per-frame value axes; positional axes reported apart.
    REALISTIC = ("clean", "gain", "offset", "noise")

    rng = np.random.default_rng(0)
    worst_present = 0.0                 # highest match_score a gameplay frame reaches (realistic)
    best_absent = float("inf")          # lowest fire_proximity a non-gameplay frame reaches (realistic)
    worst_present_where = best_absent_where = ""
    present_by_cat: dict[str, float] = {}
    absent_by_cat: dict[str, float] = {}

    for sid, rows in by_class.items():
        gameplay = sid in GAMEPLAY
        for s, _ in rows:
            variants = [("clean", "clean", s.image)] + list(perturb.envelope(s.image, rng))
            for cat, _name, img in variants:
                p = probe(img)
                if gameplay:
                    ms = match_score(sid, p)
                    present_by_cat[cat] = max(present_by_cat.get(cat, 0.0), ms)
                    if cat in REALISTIC and ms > worst_present:
                        worst_present, worst_present_where = ms, f"{sid}/{cat}"
                else:
                    fp = fire_proximity(p)
                    absent_by_cat[cat] = min(absent_by_cat.get(cat, float("inf")), fp)
                    if cat in REALISTIC and fp < best_absent:
                        best_absent, best_absent_where = fp, f"{sid}/{cat}"

    print("\nA2D-slop robustness (worst case over frames + perturb.envelope; realistic categories):")
    print(f"  worst PRESENT match_score (must stay low):  {worst_present:.2f}  @ {worst_present_where}")
    print(f"  best  ABSENT fire_proximity (must stay high): {best_absent:.2f}  @ {best_absent_where}")

    print("\n  per-category  worst-present / best-absent:")
    for cat in ["clean", "gain", "offset", "noise", "translate", "scale"]:
        wp = present_by_cat.get(cat)
        ba = absent_by_cat.get(cat)
        print(f"    {cat:<10} {('%.2f'%wp) if wp is not None else '  - ':>6}  /  {('%.2f'%ba) if ba is not None else '  - ':>6}")

    # ── Pass 3: recommended threshold ────────────────────────────────────────
    if worst_present < best_absent:
        tau = float(np.sqrt(worst_present * best_absent))  # geometric midpoint
        print(f"\nRecommended TAU = {tau:.2f}  (geo-mean of {worst_present:.2f} and {best_absent:.2f})")
        print(f"  margin: present ≤ {worst_present:.2f}  <  TAU {tau:.2f}  <  absent ≥ {best_absent:.2f}"
              f"   (ratio {best_absent / worst_present:.1f}x)")
    else:
        print(f"\n*** NO SEPARATION: worst present {worst_present:.2f} >= best absent {best_absent:.2f} ***")


if __name__ == "__main__":
    main()
