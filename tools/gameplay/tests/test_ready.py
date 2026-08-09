"""P1 READY!-badge probe (guitar_select_2p)."""

from __future__ import annotations

import numpy as np
import pytest

from gameplay import perturb
from gameplay.corpus import corpus_dir, load_bgr
from gameplay.present import TAU
from gameplay.ready import build_ready_probe, read_ready

# Ground truth for the four guitar_select_2p corpus frames: P1 confirmed or not.
FRAMES = {
    "p1_unready": False,
    "p1_ready": True,
    "p2_unassigned": True,
    "p2_assigned": True,
}


def _frame(name: str) -> np.ndarray:
    return load_bgr(corpus_dir() / f"guitar_select_2p__{name}.png")


@pytest.fixture(scope="module")
def probe():
    return build_ready_probe()


@pytest.mark.parametrize("name,want", FRAMES.items())
def test_clean(probe, name, want):
    ready, sad = read_ready(_frame(name), probe)
    assert ready is want, f"{name}: sad={sad:.2f} (TAU {TAU})"


def test_separation_is_wide(probe):
    """Present should sit near 0 and absent far above TAU, not merely either side."""
    present = [read_ready(_frame(n), probe)[1] for n, v in FRAMES.items() if v]
    absent = [read_ready(_frame(n), probe)[1] for n, v in FRAMES.items() if not v]
    assert max(present) < 2.0
    # Measured: absent 51.7 against TAU 18, i.e. 2.87x. Guard a little below that so
    # the test flags a real narrowing rather than tracking noise.
    assert min(absent) > 2.5 * TAU


def test_roi_is_static_across_present_frames(probe):
    """The flame animation must not reach inside the ROI — the premise for one
    reference frame standing in for all badge-present frames."""
    present = [read_ready(_frame(n), probe)[1] for n, v in FRAMES.items() if v]
    assert max(present) - min(present) < 1.0


def test_value_slop(probe):
    """Gain/offset/noise must not flip any frame's decision (positional slop is a
    known, shared limitation of this mechanism — see the module docstring)."""
    rng = np.random.default_rng(7)
    for name, want in FRAMES.items():
        img = _frame(name)
        for axis, label, pert in perturb.envelope(img, rng):
            if axis not in ("gain", "offset", "noise"):
                continue
            ready, sad = read_ready(pert, probe)
            assert ready is want, f"{name} {axis}:{label}: sad={sad:.2f}"
