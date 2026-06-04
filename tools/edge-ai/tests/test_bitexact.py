"""Bit-exactness gate: the on-device C (firmware/fretboard/model_infer.c) must
produce the same command stream as the host int8 reference (edge_ai.quantize).

Generates a tiny checkpoint, emits a model_weights.h, compiles model_infer.c +
a host harness, feeds a synthetic ADC trace through both paths, and asserts the
per-row command bytes match. Needs numpy, torch, and a C compiler.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
torch = pytest.importorskip("torch")

from edge_ai.metrics import monostable
from edge_ai.model import StrumNet
from edge_ai.quantize import emit_c_header, int8_sim_windows, quantize
from tests.test_quantize import _synthetic_capture, WINDOW

CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
FW_DIR = Path(__file__).resolve().parents[3] / "firmware" / "fretboard"
HARNESS = Path(__file__).resolve().parent / "model_infer_host.c"

STRUM_THRESH, HOLD, REFRACTORY = 0.55, 8, 4


def _streaming_reference(qp, cap):
    """Per-row command bytes the C must reproduce: 0 during warm-up, then the
    int8 window prediction with the streaming strum monostable applied."""
    raw, _ = _raw_for(cap)
    bits = int8_sim_windows(qp, raw)                 # (n_windows, 6), window i -> row WINDOW-1+i
    raw_strum = [int(b[5]) for b in bits]
    mono = monostable(raw_strum, hold=HOLD, refractory=REFRACTORY)
    masks = [0] * (WINDOW - 1)                        # warm-up rows
    for j, b in enumerate(bits):
        m = 0
        for k in range(5):
            if b[k]:
                m |= (1 << k)
        if mono[j]:
            m |= (1 << 5)
        masks.append(m)
    return masks


def _raw_for(cap):
    from edge_ai.quantize import _raw_windows
    return _raw_windows(cap, WINDOW)


@pytest.mark.skipif(CC is None, reason="no C compiler available")
def test_c_matches_int8_sim(tmp_path):
    # tiny checkpoint
    torch.manual_seed(3)
    model = StrumNet(channels=8, kernel=5, dilations=(1, 4, 16))
    cap = _synthetic_capture(n_rows=300, seed=5)
    arr = np.asarray(cap.adc, dtype=np.float32)
    ckpt = tmp_path / "ckpt.pt"
    torch.save({
        "state_dict": model.state_dict(), "window": WINDOW,
        "channels": 8, "kernel": 5, "dilations": [1, 4, 16],
        "norm_mean": arr.mean(0).tolist(), "norm_std": arr.std(0).tolist(),
    }, ckpt)

    qp = quantize(str(ckpt), [cap], strum_thresh=STRUM_THRESH,
                  strum_hold=HOLD, strum_refractory=REFRACTORY)

    # Compile the real firmware module, but from tmp so its quoted
    # #include "model_weights.h" resolves to the generated header (a quoted
    # include checks the including file's own dir first, which would otherwise
    # shadow it with the committed firmware header).
    shutil.copy(FW_DIR / "model_infer.c", tmp_path / "model_infer.c")
    shutil.copy(FW_DIR / "model_infer.h", tmp_path / "model_infer.h")
    (tmp_path / "model_weights.h").write_text(emit_c_header(qp))

    exe = tmp_path / "infer_host"
    subprocess.run(
        [CC, "-O2", "-std=c11", "-I", str(tmp_path),
         str(HARNESS), str(tmp_path / "model_infer.c"), "-o", str(exe)],
        check=True,
    )

    # feed the capture's raw ADC rows
    trace = "\n".join(" ".join(str(v) for v in row) for row in cap.adc)
    out = subprocess.run([str(exe)], input=trace, capture_output=True, text=True, check=True)
    c_masks = [int(x) for x in out.stdout.split()]

    ref = _streaming_reference(qp, cap)
    assert len(c_masks) == len(cap)
    assert c_masks == ref
