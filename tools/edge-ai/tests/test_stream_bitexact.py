"""Bit-exactness gate for the STREAMING on-device inference
(firmware/fretboard/model_infer_stream.c).

Streaming computes a continuous causal conv, which is bit-exact with a model
trained at window >= receptive field (85). So we generate a window-85 model,
stream the capture through model_infer_stream.c, and assert it matches the host
int8 reference (int8_sim at window 85) + the strum monostable, from the first
fully-warmed row onward.

Needs numpy, torch, and a C compiler.
"""

import shutil
import subprocess
from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
torch = pytest.importorskip("torch")

from edge_ai.metrics import monostable
from edge_ai.model import StrumNet
from edge_ai.quantize import emit_c_header, int8_sim_windows, quantize, _raw_windows
from tests.test_quantize import _synthetic_capture

CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
FW_DIR = Path(__file__).resolve().parents[3] / "firmware" / "fretboard"
HARNESS = Path(__file__).resolve().parent / "model_infer_stream_host.c"

WINDOW = 85           # = receptive field for dilations (1,4,16), kernel 5
STRUM_THRESH, HOLD, REFRACTORY = 0.7, 8, 8


@pytest.mark.skipif(CC is None, reason="no C compiler available")
def test_stream_matches_int8_sim(tmp_path):
    torch.manual_seed(7)
    model = StrumNet(channels=8, kernel=5, dilations=(1, 4, 16))
    cap = _synthetic_capture(n_rows=400, seed=11)
    arr = np.asarray(cap.adc, dtype=np.float32)
    ckpt = tmp_path / "ckpt.pt"
    torch.save({
        "state_dict": model.state_dict(), "window": WINDOW,
        "channels": 8, "kernel": 5, "dilations": [1, 4, 16],
        "norm_mean": arr.mean(0).tolist(), "norm_std": arr.std(0).tolist(),
    }, ckpt)

    qp = quantize(str(ckpt), [cap], strum_thresh=STRUM_THRESH,
                  strum_hold=HOLD, strum_refractory=REFRACTORY)

    # compile the streaming module from tmp so its quoted #include "model_weights.h"
    # resolves to the generated (window-85) header
    for f in ("model_infer.h", "model_infer_stream.h", "model_infer_stream.c"):
        shutil.copy(FW_DIR / f, tmp_path / f)
    (tmp_path / "model_weights.h").write_text(emit_c_header(qp))
    exe = tmp_path / "stream_host"
    subprocess.run(
        [CC, "-O2", "-std=c11", "-I", str(tmp_path),
         str(HARNESS), str(tmp_path / "model_infer_stream.c"), "-o", str(exe)],
        check=True,
    )

    # stream the capture's raw ADC rows
    trace = "\n".join(" ".join(str(v) for v in row) for row in cap.adc)
    out = subprocess.run([str(exe)], input=trace, capture_output=True, text=True, check=True)
    c_masks = [int(x) for x in out.stdout.split()]
    assert len(c_masks) == len(cap)

    # host reference: window-85 int8 sim + streaming monostable, per row from row 84
    raw, _ = _raw_windows(cap, WINDOW)
    bits = int8_sim_windows(qp, raw)              # one row per window: rows WINDOW-1..N-1
    mono = monostable([int(b[5]) for b in bits], hold=HOLD, refractory=REFRACTORY)
    ref = []
    for j, b in enumerate(bits):
        m = 0
        for k in range(5):
            if b[k]:
                m |= (1 << k)
        if mono[j]:
            m |= (1 << 5)
        ref.append(m)

    # compare from the first warmed row (WINDOW-1) onward
    c_warm = c_masks[WINDOW - 1:]
    assert len(c_warm) == len(ref)
    assert c_warm == ref
