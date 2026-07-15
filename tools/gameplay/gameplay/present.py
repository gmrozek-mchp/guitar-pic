"""Gameplay-screen detection by static scoreboard-chrome presence.

A gameplay screen is identified by its fixed scoring chrome, not by a whole-frame
centroid: most of a gameplay frame is dynamic (scrolling note highways, animated
crowd, changing digits), so a whole-frame centroid built from a few frames matches
that one capture and drifts on any other -> intermittent UNKNOWN. The scoring
chrome is the one thing that is static, and its layout *is* the 1p/2p signature:

  1-player  -> one bottom-left score block   (SCORE_BLOCK_ROI, score.py chrome)
  2-player  -> two top amp panels            (AMP2P_BLOCK left+right, amp2p.py)

Each probe is a masked, per-frame-normalized SAD between the frame block at fixed
nominal coordinates and a committed reference crop, divided by the masked-pixel
count so the probes are comparable and the threshold is scale-free (~0 present,
~1 unrelated). The per-frame normalize (zero-mean/unit-std over the masked pixels)
cancels the analog-component value slop (gain/offset). No offset search: marvin's
capture is pixel-locked native BGR888 (capture_pipeline.md), so the chrome sits at
nominal coords; any static per-rig offset is a one-time registration concern,
deferred with the 2p digit reader (amp2p.calibrate is host-only).

Decision:
  1p present   iff  sad1p           <= TAU
  2p present   iff  max(sadL, sadR) <= TAU     (both amps must register)
  else -> not a gameplay screen (fall through to the whole-frame classifier).

TAU proven offline (fixed nominal offset, perturb.envelope value axes): worst
present 0.22, best absent 0.62 -> geo-mean 0.37. See scan_scoreboard_presence.py.
Everything is integer-luma + a single masked SAD, shaped to port 1:1 to C
(gameplay_present.c) and cross-check against it.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from . import amp2p, score
from .corpus import load_bgr, score_corpus_dir
from .metadata import AMP2P_BLOCK, SCORE_BLOCK_ROI

# Accept threshold: integer L1-per-masked-pixel in the uint8-normalized space below
# (same mean128/std48 quantization gp_fingerprint uses). The unit-std proof (worst
# present 0.22, best absent 0.62 -> geo-mean 0.37) scales by GP_NORM_STD: present
# ~11, absent ~30, TAU ~18. See scan_scoreboard_presence.py + tests/test_present.py.
TAU = 18

GP_NORM_MEAN = 128.0   # must match gameplay_classify.c / gp_fingerprint
GP_NORM_STD = 48.0

_LUMA = np.array([29, 150, 77], dtype=np.int64)  # BGR weights (== score._LUMA_WI)


def _luma(bgr: np.ndarray) -> np.ndarray:
    """Integer luma of a BGR patch (no /256 — a per-frame normalize follows)."""
    return bgr.astype(np.int64) @ _LUMA


def _norm_u8(luma: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Standardize a block's luma to mean128/std48 over the masked pixels, uint8.

    Mirrors gp_fingerprint's normalize+quantize so the firmware port reproduces it.
    Stats are taken over the masked (static-chrome) pixels only; the whole block is
    quantized but only masked pixels are ever compared.
    """
    m = luma[mask]
    std = float(m.std())
    if std < 1e-6:
        v = np.full(luma.shape, GP_NORM_MEAN, dtype=np.float64)
    else:
        v = (luma.astype(np.float64) - float(m.mean())) / std * GP_NORM_STD + GP_NORM_MEAN
    return np.clip(np.round(v), 0, 255).astype(np.uint8)


@dataclass(frozen=True)
class Probe:
    name: str                              # "1p" | "2pL" | "2pR"
    block: tuple[int, int, int, int]       # (x0, y0, x1, y1) canonical space
    mask: np.ndarray                       # (bh, bw) bool: static-chrome pixels
    ref_u8: np.ndarray                     # (bh, bw) uint8: mean128/std48-normalized ref luma
    npix: int


def _make_probe(name: str, block, ref_png: str, mask: np.ndarray) -> Probe:
    ref = _luma(load_bgr(score_corpus_dir() / ref_png))
    if ref.shape != mask.shape:
        raise ValueError(f"{name}: ref {ref.shape} != mask {mask.shape}")
    return Probe(name, tuple(block), mask, _norm_u8(ref, mask), int(mask.sum()))


def build_probes() -> dict[str, Probe]:
    """The three committed chrome probes (1p block, 2p left/right amp panels)."""
    return {
        "1p":  _make_probe("1p",  SCORE_BLOCK_ROI,      "score_block_ref.png", score.load_chrome_mask()),
        "2pL": _make_probe("2pL", AMP2P_BLOCK["left"],  "amp2p_left_ref.png",  amp2p.load_amp2p_mask("left")),
        "2pR": _make_probe("2pR", AMP2P_BLOCK["right"], "amp2p_right_ref.png", amp2p.load_amp2p_mask("right")),
    }


def probe_sad(image: np.ndarray, p: Probe) -> float:
    """Integer L1-per-masked-pixel at the probe's fixed nominal block (uint8 space)."""
    x0, y0, x1, y1 = p.block
    block = _norm_u8(_luma(image[y0:y1, x0:x1]), p.mask)
    l1 = int(np.abs(block.astype(np.int32) - p.ref_u8.astype(np.int32))[p.mask].sum())
    return l1 / p.npix


@dataclass(frozen=True)
class Presence:
    screen_id: str | None       # "in_song" | "in_song_2p" | None (not gameplay)
    sad_1p: float
    sad_2pL: float
    sad_2pR: float


def classify_present(image: np.ndarray, probes: dict[str, Probe] | None = None,
                     tau: float = TAU) -> Presence:
    """Classify a BGR frame as 1p / 2p gameplay by scoreboard-chrome presence."""
    if probes is None:
        probes = build_probes()
    s1 = probe_sad(image, probes["1p"])
    sl = probe_sad(image, probes["2pL"])
    sr = probe_sad(image, probes["2pR"])
    two = max(sl, sr)  # 2p requires BOTH amp panels
    if s1 <= tau and s1 <= two:
        sid = "in_song"
    elif two <= tau:
        sid = "in_song_2p"
    else:
        sid = None
    return Presence(screen_id=sid, sad_1p=s1, sad_2pL=sl, sad_2pR=sr)
