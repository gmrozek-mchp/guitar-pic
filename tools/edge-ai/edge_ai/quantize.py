"""Post-training int8 quantisation of StrumNet for the fretboard MCU.

Produces an integer-only inference path (no FPU) that is bit-exact between this
host reference (`int8_sim_*`) and the on-device C in `firmware/fretboard/
model_infer.c`. The same arrays are emitted as `model_weights.h`.

Scheme (per-tensor symmetric int8 weights, int32 accumulators, int64
fixed-point requant):

  input affine   q_in_c = clamp_i8( (adc_c*M_in[c] - B_in[c] + 2^(S_in-1)) >> S_in )
                 folds per-channel standardisation (adc-mean)/std AND the input
                 quant scale into integer (M_in, B_in) + a shared shift S_in.
  conv layer l   acc_i32 = qbias[l][o] + Σ_i Σ_k qW[l][o,i,k] * q_in[i, p+k*d]
                 (causal, left zero-pad (K-1)*d — replicates the 60-sample
                 training window with zeros beyond it)
                 relu:    acc = max(acc, 0)
                 requant: q = clamp_i8_pos( (acc*MULT[l] + 2^(S[l]-1)) >> S[l] )
  head           acc_i32[o] = qbias_h[o] + Σ_i qW_h[o,i] * act_last[i]
                 bit o = (acc_i32[o] >= TQ[o])      (no sigmoid: sigmoid(x)>=t
                 ⇔ x>=logit(t) ⇔ acc>=TQ; frets TQ=0, strum TQ=logit(thresh)/scale)

All right shifts are arithmetic (round-half-up via the +2^(S-1) bias before the
shift). The requant multiply uses int64 to avoid overflow.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from . import FRET_COUNT, N_LABELS
from .data import Capture, causal_window_indices, contiguous_runs

# Fixed shifts. Chosen generously: int8 quant error dominates, the fixed-point
# rounding is far below it. The requant multiply is int64 so MULT magnitude is
# unconstrained; S_IN is bounded so adc*M_in stays well inside int32.
S_IN = 15
S_REQUANT = 15
_QMAX = 127  # symmetric int8 range [-127, 127]


@dataclass
class QuantParams:
    # architecture
    channels: int
    kernel: int
    dilations: tuple[int, ...]
    window: int
    # input affine (per channel), shared shift S_IN
    m_in: list[int]      # int32
    b_in: list[int]      # int32
    s_in_shift: int
    # conv layers: weights int8 [Cout][Cin][K], bias int32 [Cout], requant
    conv_w: list[list[list[list[int]]]]
    conv_b: list[list[int]]
    conv_mult: list[int]   # int32 per layer
    conv_shift: list[int]  # per layer
    # head: weights int8 [N_LABELS][channels], bias int32, integer thresholds
    head_w: list[list[int]]
    head_b: list[int]
    head_tq: list[int]     # int32 per output
    # strum monostable post-processor (deploy-time)
    strum_hold: int
    strum_refractory: int
    # bookkeeping (not emitted to C)
    strum_thresh: float
    scales: dict


def _round_half_up_shift(x: int, shift: int) -> int:
    """(x + 2^(shift-1)) >> shift with arithmetic (floor) shift — matches C."""
    return (x + (1 << (shift - 1))) >> shift


def _quantize_weight_tensor(w):
    """Per-tensor symmetric int8. Returns (q_int_list, scale)."""
    import numpy as np

    amax = float(np.max(np.abs(w))) if w.size else 0.0
    scale = amax / _QMAX if amax > 0 else 1.0
    q = np.clip(np.round(w / scale), -_QMAX, _QMAX).astype(np.int64)
    return q, scale


def _conv_float(x, w, b, d):
    """Causal float conv. x:(N,Cin,L) w:(Cout,Cin,K) b:(Cout,) -> (N,Cout,L)."""
    import numpy as np

    n, cin, length = x.shape
    cout, _, k = w.shape
    pad = (k - 1) * d
    xp = np.concatenate([np.zeros((n, cin, pad), x.dtype), x], axis=2)
    out = np.zeros((n, cout, length), dtype=np.float64)
    for j in range(k):
        out += np.einsum("oi,nil->nol", w[:, :, j], xp[:, :, j * d : j * d + length])
    return out + b[None, :, None]


def _raw_windows(cap: Capture, window: int):
    """Raw int ADC windows (N,5,window) + the true label rows (N,6), in the same
    run/window order build_arrays uses, so int8 metrics line up with float eval."""
    import numpy as np

    adc = np.asarray(cap.adc, dtype=np.int64)      # (rows, 5)
    lab = np.asarray(cap.labels, dtype=np.int64)   # (rows, 6)
    xs, ys = [], []
    for run_start, run_end in contiguous_runs(cap.fb_seq):
        run_len = run_end - run_start
        if run_len < window:
            continue
        for start, end, label_row in causal_window_indices(run_len, window):
            xs.append(adc[run_start + start : run_start + end].T)  # (5, window)
            ys.append(lab[run_start + label_row])
    if not xs:
        return np.zeros((0, FRET_COUNT, window), np.int64), np.zeros((0, N_LABELS), np.int64)
    return np.stack(xs), np.stack(ys)


def quantize(ckpt_path: str, calib_caps: list[Capture], *, strum_thresh: float = 0.5,
             strum_hold: int = 0, strum_refractory: int = 4) -> QuantParams:
    """Calibrate activation ranges on `calib_caps` and produce int8 params."""
    import numpy as np
    import torch

    ck = torch.load(ckpt_path, map_location="cpu")
    sd = ck["state_dict"]
    channels, kernel = ck["channels"], ck["kernel"]
    dilations = tuple(ck.get("dilations", (1, 4)))
    window = ck["window"]
    mean = np.asarray(ck["norm_mean"], dtype=np.float64)
    std = np.asarray(ck["norm_std"], dtype=np.float64)

    conv_keys = [k for k in sd if k.startswith("features.") and k.endswith(".conv.weight")]
    conv_keys.sort(key=lambda s: int(s.split(".")[1]))
    w_convs = [sd[k].numpy().astype(np.float64) for k in conv_keys]
    b_convs = [sd[k.replace("weight", "bias")].numpy().astype(np.float64) for k in conv_keys]
    w_head = sd["head.weight"].numpy().astype(np.float64)
    b_head = sd["head.bias"].numpy().astype(np.float64)

    # --- calibration: float forward over all calib windows, record activation maxes
    Xall = []
    for cap in calib_caps:
        xw, _ = _raw_windows(cap, window)
        if xw.shape[0]:
            Xall.append(xw)
    if not Xall:
        raise ValueError("no calibration windows produced")
    raw = np.concatenate(Xall, axis=0).astype(np.float64)          # (N,5,window)
    xstd = (raw - mean[None, :, None]) / std[None, :, None]        # standardised
    in_amax = float(np.max(np.abs(xstd)))
    s_in = in_amax / _QMAX

    act = xstd
    act_scales = []
    for w, b, d in zip(w_convs, b_convs, dilations):
        act = np.maximum(_conv_float(act, w, b, d), 0.0)           # post-ReLU
        amax = float(np.max(act)) if act.size else 1.0
        act_scales.append(amax / _QMAX if amax > 0 else 1.0)
    s_act_last = act_scales[-1]

    # --- input affine: q = round((adc-mean)/(std*s_in)) folded to integer
    m_in, b_in = [], []
    for c in range(FRET_COUNT):
        kc = 1.0 / (std[c] * s_in)
        m_in.append(int(round(kc * (1 << S_IN))))
        b_in.append(int(round(mean[c] * kc * (1 << S_IN))))

    # --- conv weights, biases, requant
    conv_w, conv_b, conv_mult, conv_shift, w_scales = [], [], [], [], []
    s_in_l = s_in
    for li, (w, b, d) in enumerate(zip(w_convs, b_convs, dilations)):
        qw, sw = _quantize_weight_tensor(w)
        w_scales.append(sw)
        acc_scale = sw * s_in_l
        qb = [int(round(v / acc_scale)) for v in b]
        mreq = acc_scale / act_scales[li]                         # accum -> next int8
        conv_mult.append(int(round(mreq * (1 << S_REQUANT))))
        conv_shift.append(S_REQUANT)
        conv_w.append(qw.tolist())
        conv_b.append(qb)
        s_in_l = act_scales[li]

    # --- head: int thresholds in accumulator units
    qhw, shw = _quantize_weight_tensor(w_head)
    head_acc_scale = shw * s_act_last
    qhb = [int(round(v / head_acc_scale)) for v in b_head]
    tq = [0] * N_LABELS                                           # frets: logit>=0
    logit_t = math.log(strum_thresh / (1.0 - strum_thresh))       # strum threshold
    tq[5] = int(round(logit_t / head_acc_scale))

    return QuantParams(
        channels=channels, kernel=kernel, dilations=dilations, window=window,
        m_in=m_in, b_in=b_in, s_in_shift=S_IN,
        conv_w=conv_w, conv_b=conv_b, conv_mult=conv_mult, conv_shift=conv_shift,
        head_w=qhw.tolist(), head_b=qhb, head_tq=tq,
        strum_hold=strum_hold, strum_refractory=strum_refractory,
        strum_thresh=strum_thresh,
        scales={"s_in": s_in, "w": w_scales, "act": act_scales, "head_w": shw},
    )


def _conv_int(xq, qw, qb, d, mult, shift):
    """Causal int8 conv + ReLU + requant. xq:(N,Cin,L) int -> (N,Cout,L) int8."""
    import numpy as np

    n, cin, length = xq.shape
    qw = np.asarray(qw, dtype=np.int64)        # (Cout,Cin,K)
    cout, _, k = qw.shape
    pad = (k - 1) * d
    xp = np.concatenate([np.zeros((n, cin, pad), np.int64), xq.astype(np.int64)], axis=2)
    acc = np.zeros((n, cout, length), dtype=np.int64)
    for j in range(k):
        acc += np.einsum("oi,nil->nol", qw[:, :, j], xp[:, :, j * d : j * d + length])
    acc += np.asarray(qb, dtype=np.int64)[None, :, None]
    acc = np.maximum(acc, 0)
    q = (acc * int(mult) + (1 << (shift - 1))) >> shift
    return np.minimum(q, _QMAX).astype(np.int64)


def int8_sim_windows(qp: QuantParams, raw_windows):
    """Run integer inference over raw ADC windows (N,5,window) -> bits (N,6)."""
    import numpy as np

    if raw_windows.shape[0] == 0:
        return np.zeros((0, N_LABELS), np.int64)
    raw = raw_windows.astype(np.int64)
    m = np.asarray(qp.m_in, np.int64)[None, :, None]
    b = np.asarray(qp.b_in, np.int64)[None, :, None]
    qin = (raw * m - b + (1 << (qp.s_in_shift - 1))) >> qp.s_in_shift
    act = np.clip(qin, -_QMAX, _QMAX)
    for w, qb, d, mult, shift in zip(qp.conv_w, qp.conv_b, qp.dilations, qp.conv_mult, qp.conv_shift):
        act = _conv_int(act, w, qb, d, mult, shift)
    last = act[:, :, -1]                                    # (N, channels)
    hw = np.asarray(qp.head_w, np.int64)                    # (6, channels)
    hb = np.asarray(qp.head_b, np.int64)
    acc = last @ hw.T + hb[None, :]                         # (N, 6) int
    tq = np.asarray(qp.head_tq, np.int64)[None, :]
    return (acc >= tq).astype(np.int64)


def int8_sim_capture(qp: QuantParams, cap: Capture):
    """(pred_bits, true_bits) lists for one capture, matching float-eval order."""
    raw, true = _raw_windows(cap, qp.window)
    bits = int8_sim_windows(qp, raw)
    return bits.tolist(), true.tolist()


def evaluate_int8(qp: QuantParams, captures: list[Capture], *, tol_samples=5,
                  hold=0, refractory=0):
    """int8 counterpart of train.evaluate — same EvalReport, so float vs int8
    print identically."""
    from .metrics import (per_bit_accuracy, per_bit_f1, strum_event_timing,
                          strum_pulse_stats, monostable, PulseStats)
    from .train import EvalReport, _agg_timing

    all_pred, all_true = [], []
    raw = {"err": [], "nt": 0, "np": 0, "m": 0, "pulses": 0, "glitch": 0, "short": 0, "dur": []}
    post = {k: ([] if isinstance(v, list) else 0) for k, v in raw.items()}
    do_post = hold > 0

    def _accum(acc, st, ps):
        acc["err"].extend(st.errors_ms); acc["nt"] += st.n_true
        acc["np"] += st.n_pred; acc["m"] += st.matched
        acc["pulses"] += ps.n_pulses; acc["glitch"] += ps.n_glitches
        acc["short"] += ps.n_too_short; acc["dur"].extend(ps.durations_ticks)

    for cap in captures:
        if len(cap) < qp.window:
            continue
        pred_bits, true_bits = int8_sim_capture(qp, cap)
        all_pred.extend(pred_bits); all_true.extend(true_bits)
        pred_strum = [r[5] for r in pred_bits]
        true_strum = [r[5] for r in true_bits]
        _accum(raw, strum_event_timing(pred_strum, true_strum, tol_samples=tol_samples),
               strum_pulse_stats(pred_strum))
        if do_post:
            clean = monostable(pred_strum, hold=hold, refractory=refractory)
            _accum(post, strum_event_timing(clean, true_strum, tol_samples=tol_samples),
                   strum_pulse_stats(clean))

    def _pulse(acc):
        return PulseStats(acc["pulses"], acc["glitch"], acc["short"], acc["dur"])

    return EvalReport(
        per_bit_acc=per_bit_accuracy(all_pred, all_true),
        per_bit_f1=per_bit_f1(all_pred, all_true),
        strum=_agg_timing(raw["err"], raw["nt"], raw["np"], raw["m"]),
        pulse=_pulse(raw),
        strum_post=_agg_timing(post["err"], post["nt"], post["np"], post["m"]) if do_post else None,
        pulse_post=_pulse(post) if do_post else None,
    )


def _fmt_arr(vals, per_line=12):
    """Comma-joined ints with line wrapping for readable C initialisers."""
    out, line = [], []
    for v in vals:
        line.append(str(int(v)))
        if len(line) >= per_line:
            out.append(", ".join(line)); line = []
    if line:
        out.append(", ".join(line))
    return ",\n    ".join(out)


def _sanitize_name(name: str) -> str:
    s = "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in name).strip("_")
    if not s or s[0].isdigit():
        s = "m_" + s
    return s.lower()


def emit_c_header(qp: QuantParams, name: str = "model") -> str:
    """Render a model_weights.h defining one named `model_def_t model_<name>`.

    Per-model arrays are prefixed so several models (e.g. one per difficulty)
    can be linked together and chosen at runtime via model_infer_set_model().
    The architecture dims are shared #defines (first included model defines
    them); a _Static_assert makes a mismatched-architecture model fail the build.
    `MODEL_DEFAULT` points at the first model included.
    """
    nm = _sanitize_name(name)
    P = f"M_{nm.upper()}_"          # array symbol prefix
    K = qp.kernel
    C = qp.channels
    n_layers = len(qp.dilations)

    L = [f"#ifndef MODEL_WEIGHTS_{nm.upper()}_H",
         f"#define MODEL_WEIGHTS_{nm.upper()}_H", "",
         "/* Generated by edge_ai.quantize — do not edit by hand. */",
         "#include <stdint.h>", '#include "model_infer.h"', "",
         "/* Shared architecture dims — first model included sets them; all "
         "linked models must match. */",
         "#ifndef MODEL_ARCH_DIMS",
         "#define MODEL_ARCH_DIMS",
         f"#define MODEL_CHANNELS {C}",
         f"#define MODEL_KERNEL   {K}",
         f"#define MODEL_WINDOW   {qp.window}",
         f"#define MODEL_N_IN     {FRET_COUNT}",
         f"#define MODEL_N_OUT    {N_LABELS}",
         f"#define MODEL_N_LAYERS {n_layers}",
         "#endif",
         f"_Static_assert(MODEL_CHANNELS == {C} && MODEL_KERNEL == {K} && "
         f"MODEL_WINDOW == {qp.window} && MODEL_N_IN == {FRET_COUNT} && "
         f"MODEL_N_OUT == {N_LABELS} && MODEL_N_LAYERS == {n_layers},",
         f'               "model {nm}: architecture must match the other linked models");', ""]

    L += [f"static const int32_t {P}M_IN[{FRET_COUNT}] = {{ {_fmt_arr(qp.m_in)} }};",
          f"static const int32_t {P}B_IN[{FRET_COUNT}] = {{ {_fmt_arr(qp.b_in)} }};",
          f"static const uint8_t {P}DILATION[{n_layers}] = "
          f"{{ {', '.join(str(d) for d in qp.dilations)} }};",
          f"static const int32_t {P}REQ_MULT[{n_layers}] = "
          f"{{ {', '.join(str(m) for m in qp.conv_mult)} }};",
          f"static const uint8_t {P}REQ_SHIFT[{n_layers}] = "
          f"{{ {', '.join(str(s) for s in qp.conv_shift)} }};", ""]
    for li, (w, b) in enumerate(zip(qp.conv_w, qp.conv_b)):
        cout = len(w); cin = len(w[0])
        flat = [w[o][i][k] for o in range(cout) for i in range(cin) for k in range(K)]
        L += [f"/* layer {li}: Conv1d({cin}->{cout}, k={K}, d={qp.dilations[li]}) */",
              f"static const int8_t {P}W{li}[{cout * cin * K}] = {{\n    {_fmt_arr(flat)}\n}};",
              f"static const int32_t {P}B{li}[{cout}] = {{ {_fmt_arr(b)} }};", ""]
    L += [f"static const int8_t *const {P}W[{n_layers}] = "
          f"{{ {', '.join('%sW%d' % (P, i) for i in range(n_layers))} }};",
          f"static const int32_t *const {P}B[{n_layers}] = "
          f"{{ {', '.join('%sB%d' % (P, i) for i in range(n_layers))} }};",
          f"static const uint8_t {P}CIN[{n_layers}] = "
          f"{{ {', '.join(str(len(w[0])) for w in qp.conv_w)} }};", ""]
    hw_flat = [qp.head_w[o][i] for o in range(N_LABELS) for i in range(C)]
    L += [f"static const int8_t {P}HEAD_W[{N_LABELS * C}] = {{\n    {_fmt_arr(hw_flat)}\n}};",
          f"static const int32_t {P}HEAD_B[{N_LABELS}] = {{ {_fmt_arr(qp.head_b)} }};",
          f"static const int32_t {P}HEAD_TQ[{N_LABELS}] = {{ {_fmt_arr(qp.head_tq)} }};", ""]

    L += [f"static const model_def_t model_{nm} = {{",
          f"    .m_in = {P}M_IN, .b_in = {P}B_IN, .s_in_shift = {qp.s_in_shift},",
          f"    .w = {P}W, .b = {P}B, .cin = {P}CIN, .dilation = {P}DILATION,",
          f"    .req_mult = {P}REQ_MULT, .req_shift = {P}REQ_SHIFT,",
          f"    .head_w = {P}HEAD_W, .head_b = {P}HEAD_B, .head_tq = {P}HEAD_TQ,",
          f"    .strum_hold = {qp.strum_hold}, .strum_refractory = {qp.strum_refractory},",
          "};", "",
          "#ifndef MODEL_DEFAULT",
          f"#define MODEL_DEFAULT (&model_{nm})",
          "#endif", "",
          f"#endif /* MODEL_WEIGHTS_{nm.upper()}_H */", ""]
    return "\n".join(L)
