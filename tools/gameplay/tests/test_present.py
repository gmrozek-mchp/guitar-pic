"""Gameplay-screen detection by scoreboard-chrome presence (gameplay/present.py).

Proves the probe offline before it ports to firmware (gameplay_present.c): it must
classify every corpus frame's gameplay/not-gameplay correctly, keep "present" and
"absent" separated across the analog value-slop envelope, and leave a comfortable
margin around TAU. Positional slop (translate/scale) is excluded: marvin's capture
is pixel-locked (capture_pipeline.md), so it is a one-time registration concern,
not a per-frame effect — see scan_scoreboard_presence.py for that analysis.
"""

from __future__ import annotations

import numpy as np

from gameplay import perturb
from gameplay.present import TAU, build_probes, classify_present, probe_sad

import pytest

from gameplay import amp2p as _amp2p

# The 2p amp probes register through the hand-painted masks, which are origin-bound;
# they are stale until re-painted at the new AMP2P_BLOCK (see amp2p.MASK_PAINTED_AT).
pytestmark = pytest.mark.skipif(
    _amp2p.mask_origin_mismatch() is not None,
    reason=_amp2p.mask_origin_mismatch() or "",
)

_GAMEPLAY = {"in_song", "in_song_2p"}
_VALUE_AXES = ("gain", "offset", "noise")  # per-frame realistic on a pixel-locked capture


def _expected(screen_id: str) -> str | None:
    return screen_id if screen_id in _GAMEPLAY else None


def test_present_classifies_corpus(corpus):
    """Every frame's gameplay/1p/2p/none decision is correct with the committed refs."""
    probes = build_probes()
    for s in corpus:
        r = classify_present(s.image, probes)
        assert r.screen_id == _expected(s.screen_id), (
            f"{s.path.name}: got {r.screen_id} "
            f"(1p={r.sad_1p:.1f} 2pL={r.sad_2pL:.1f} 2pR={r.sad_2pR:.1f}, TAU={TAU})"
        )


def test_present_value_slop_separation(corpus):
    """Under the value-slop envelope, present stays < TAU and absent stays > TAU."""
    probes = build_probes()
    rng = np.random.default_rng(0)
    worst_present, best_absent = 0.0, float("inf")
    for s in corpus:
        variants = [s.image] + [
            img for cat, _n, img in perturb.envelope(s.image, rng) if cat in _VALUE_AXES
        ]
        for img in variants:
            s1 = probe_sad(img, probes["1p"])
            two = max(probe_sad(img, probes["2pL"]), probe_sad(img, probes["2pR"]))
            if s.screen_id in _GAMEPLAY:
                worst_present = max(worst_present, s1 if s.screen_id == "in_song" else two)
            else:
                best_absent = min(best_absent, min(s1, two))
    assert worst_present < TAU < best_absent, (
        f"no separation: present {worst_present:.1f} / TAU {TAU} / absent {best_absent:.1f}"
    )
    # Keep a real margin, not a hairline pass (proof measured ~2.9x).
    assert best_absent / worst_present > 2.0


def test_present_1p_2p_mutually_exclusive(corpus):
    """1p and 2p probes never both fire on the same frame."""
    probes = build_probes()
    for s in corpus:
        s1 = probe_sad(s.image, probes["1p"])
        two = max(probe_sad(s.image, probes["2pL"]), probe_sad(s.image, probes["2pR"]))
        assert not (s1 <= TAU and two <= TAU), f"{s.path.name}: both fired"
