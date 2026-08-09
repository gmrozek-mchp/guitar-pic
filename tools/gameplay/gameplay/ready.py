"""P1's READY! badge on guitar_select_2p — did marvin's own confirm register?

The Select Guitar screen advances only once *both* sides confirm, so the controller
cannot use "the screen changed" to tell whether its own GREEN landed: a screen that
hasn't moved means either "my confirm failed" or "the human hasn't confirmed yet",
and those want different handling. The red READY! banner over P1's shield answers
it directly.

Mechanism is deliberately the same as the scoreboard-chrome probes in present.py —
a masked, per-frame-normalized SAD against a reference at fixed nominal coordinates,
divided by the masked-pixel count so the threshold is scale-free and TAU is shared.
Differences from those probes, both of which fall out of what this region is:

- **No hand-painted mask.** The chrome probes carve out the parts of their block
  that change (digit strips, medallion interiors). Here the whole ROI is the
  fiducial: badge-present it is the banner, badge-absent it is the guitar body on
  the shield, and the two share almost nothing. So the mask is the full rectangle.
- **The reference is a corpus frame, not a curated crop.** `guitar_select_2p__p1_ready`
  *is* the reference appearance, so it is cropped at load time rather than committed
  as a second asset that could drift from it.

Positional slop: like its siblings this probe assumes the nominal offset and does no
offset search. That assumption is load-bearing — measured, all three probes (both
shipped chrome ones and this) exceed TAU at a **1 px** shift, so there is no shift
headroom anywhere in this mechanism, only value-slop headroom. If a per-rig offset
ever appears, they all need the offset search `amp2p.calibrate` prototypes; this is
not the first one that would break.

Value slop is where this probe is strong: clean 0.0-0.5 against TAU 18 (the chrome
probes sit at ~10-11), and across the gain/offset/noise envelope worst-present stays
0.5 while best-absent is 51.2 — a 2.8x margin on the absent side.

Data caveat: the corpus has **one** badge-absent frame, so the absent side of the
threshold rests on a single exemplar plus its value-slop variants. The inference that
other absent frames land near 51 rather than near 18 is supported by the ROI being
effectively static (~0.5 spread across the present frames, i.e. the flame animation
does not reach inside this box) — but more unready captures would retire it. See
docs/journal.md.
"""

from __future__ import annotations

import numpy as np

from .corpus import corpus_dir, load_bgr
from .metadata import READY_BADGE_ROI
from .present import TAU, Probe, _luma, _norm_u8, probe_sad

# The corpus frame whose badge appearance is the reference.
READY_REF_FRAME = "guitar_select_2p__p1_ready.png"


def build_ready_probe() -> Probe:
    """P1's READY!-badge probe, referenced against the corpus' badge-present frame."""
    x0, y0, x1, y1 = READY_BADGE_ROI
    ref = _luma(load_bgr(corpus_dir() / READY_REF_FRAME)[y0:y1, x0:x1])
    mask = np.ones(ref.shape, dtype=bool)  # whole ROI is the fiducial — see module docstring
    return Probe("readyP1", READY_BADGE_ROI, mask, _norm_u8(ref, mask), int(mask.size))


def read_ready(image: np.ndarray, probe: Probe | None = None,
               tau: float = TAU) -> tuple[bool, float]:
    """Is P1's READY! badge showing? Returns (ready, sad) — sad for diagnostics."""
    if probe is None:
        probe = build_ready_probe()
    sad = probe_sad(image, probe)
    return sad <= tau, sad
