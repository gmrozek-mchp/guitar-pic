"""Tests for the int8 quantiser (edge_ai.quantize).

Needs numpy + torch (the `train` dep group); skipped otherwise.
"""

import math

import pytest

np = pytest.importorskip("numpy")
torch = pytest.importorskip("torch")

from edge_ai import FRET_COUNT, N_LABELS
from edge_ai.data import Capture
from edge_ai.model import StrumNet
from edge_ai.quantize import (
    QuantParams,
    emit_c_header,
    int8_sim_capture,
    int8_sim_windows,
    quantize,
)

WINDOW = 60


def _synthetic_capture(n_rows=200, seed=0):
    """Deterministic capture: per-channel sinusoidal ADC dips + periodic labels."""
    rng = np.random.RandomState(seed)
    adc, labels = [], []
    for i in range(n_rows):
        row = []
        for c in range(FRET_COUNT):
            base = 3500 - int(1500 * (0.5 + 0.5 * math.sin(i / 7.0 + c)))
            row.append(max(0, min(4095, base + rng.randint(-20, 20))))
        adc.append(tuple(row))
        frets = [1 if (i // (5 + b)) % 3 == 0 else 0 for b in range(FRET_COUNT)]
        strum = 1 if i % 17 == 0 else 0
        labels.append(tuple(frets + [strum]))
    return Capture(
        name="synth", timestamps=[i / 240 for i in range(n_rows)],
        fb_seq=list(range(n_rows)), adc=adc, labels=labels,
    )


def _make_checkpoint(tmp_path, seed=0):
    torch.manual_seed(seed)
    model = StrumNet(channels=8, kernel=5, dilations=(1, 4, 16))
    cap = _synthetic_capture()
    arr = np.asarray(cap.adc, dtype=np.float32)
    path = tmp_path / "ckpt.pt"
    torch.save({
        "state_dict": model.state_dict(),
        "window": WINDOW, "channels": 8, "kernel": 5, "dilations": [1, 4, 16],
        "norm_mean": arr.mean(axis=0).tolist(),
        "norm_std": arr.std(axis=0).tolist(),
    }, path)
    return path, cap


def test_int8_sim_deterministic(tmp_path):
    path, cap = _make_checkpoint(tmp_path)
    qp = quantize(str(path), [cap], strum_thresh=0.55)
    b1, _ = int8_sim_capture(qp, cap)
    b2, _ = int8_sim_capture(qp, cap)
    assert b1 == b2


def test_output_bits_are_binary_and_right_shape(tmp_path):
    path, cap = _make_checkpoint(tmp_path)
    qp = quantize(str(path), [cap], strum_thresh=0.5)
    bits, true = int8_sim_capture(qp, cap)
    # one prediction per row with full history
    assert len(bits) == len(cap) - WINDOW + 1
    assert len(true) == len(bits)
    for row in bits:
        assert len(row) == N_LABELS
        assert all(v in (0, 1) for v in row)


def test_threshold_folding(tmp_path):
    """Fret thresholds are logit>=0 (TQ=0); strum TQ tracks logit(strum_thresh)."""
    path, cap = _make_checkpoint(tmp_path)
    qp_half = quantize(str(path), [cap], strum_thresh=0.5)
    assert qp_half.head_tq[:5] == [0] * 5
    assert qp_half.head_tq[5] == 0  # logit(0.5) == 0

    qp_high = quantize(str(path), [cap], strum_thresh=0.8)
    # logit(0.8) > 0 -> a positive integer threshold (harder to fire strum)
    assert qp_high.head_tq[5] > 0
    assert qp_high.head_tq[:5] == [0] * 5


def test_int8_close_to_float(tmp_path):
    """PTQ on a real (if untrained) net should not wreck per-bit accuracy vs float."""
    from edge_ai.quantize import evaluate_int8
    from edge_ai.train import evaluate

    path, cap = _make_checkpoint(tmp_path)
    qp = quantize(str(path), [cap], strum_thresh=0.5)
    ck = torch.load(str(path), map_location="cpu")
    model = StrumNet(channels=8, kernel=5, dilations=(1, 4, 16))
    model.load_state_dict(ck["state_dict"])
    stats = (np.asarray(ck["norm_mean"], np.float32), np.asarray(ck["norm_std"], np.float32))
    flt = evaluate(model, [cap], WINDOW, stats)
    q8 = evaluate_int8(qp, [cap])
    for f, q in zip(flt.per_bit_acc, q8.per_bit_acc):
        assert abs(f - q) < 0.05  # int8 within 5 pts of float per bit


def test_emit_header_structure(tmp_path):
    path, cap = _make_checkpoint(tmp_path)
    qp = quantize(str(path), [cap], strum_thresh=0.55, strum_hold=8, strum_refractory=4)
    h = emit_c_header(qp, name="hard")
    assert '#include "model_infer.h"' in h
    assert "#define MODEL_WINDOW   60" in h
    assert "#define MODEL_N_LAYERS 3" in h
    assert "_Static_assert(" in h
    assert "M_HARD_W0[200]" in h         # 8*5*5, name-prefixed
    assert "M_HARD_W1[320]" in h         # 8*8*5
    assert f"M_HARD_HEAD_W[{N_LABELS * 8}]" in h
    assert "static const model_def_t model_hard = {" in h
    assert ".strum_hold = 8" in h
    assert "#define MODEL_DEFAULT (&model_hard)" in h
    assert h.count("M_HARD_M_IN[5]") == 1


def test_requant_shift_matches_floor_semantics():
    """The +half-then-arithmetic-shift round must match C's signed >> (floor)."""
    from edge_ai.quantize import _round_half_up_shift

    # round-half-up via floor((x + 2^(s-1)) / 2^s)
    for x, s in [(10, 2), (-10, 2), (7, 1), (-7, 1), (0, 4), (-1, 1)]:
        assert _round_half_up_shift(x, s) == (x + (1 << (s - 1))) >> s
