"""Train and evaluate the baseline StrumNet on actuator-labelled captures.

Imports torch/numpy — invoked via the `train` / `eval` CLI subcommands, which
require the `train` dependency group installed.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from . import N_LABELS
from .data import Capture, build_arrays, compute_norm_stats, load_capture, strum_pos_weight
from .metrics import (
    StrumTiming,
    per_bit_accuracy,
    per_bit_f1,
    strum_event_timing,
)


@dataclass
class EvalReport:
    per_bit_acc: list[float]
    per_bit_f1: list[float]
    strum: StrumTiming


def _predict_capture(model, cap: Capture, window: int, stats):
    """Return (pred_bits, true_bits, pred_strum_seq, true_strum_seq) for one capture.

    Eval never dilates the strum label — `strum_dilate=0` — so timing is scored
    against the true single-tick events.
    """
    import torch

    X, Y, _ = build_arrays([cap], window, stats=stats, strum_dilate=0)
    model.eval()
    with torch.no_grad():
        bits = model.predict_bits(torch.from_numpy(X)).cpu().numpy()
    pred_bits = bits.tolist()
    true_bits = Y.astype(int).tolist()
    pred_strum = [int(r[5]) for r in pred_bits]
    true_strum = [int(r[5]) for r in true_bits]
    return pred_bits, true_bits, pred_strum, true_strum


def evaluate(model, captures: list[Capture], window: int, stats, *, tol_samples: int = 5) -> EvalReport:
    all_pred: list[list[int]] = []
    all_true: list[list[int]] = []
    merged_errors: list[float] = []
    n_true = n_pred = matched = 0
    for cap in captures:
        if len(cap) < window:
            continue
        pred_bits, true_bits, pred_strum, true_strum = _predict_capture(model, cap, window, stats)
        all_pred.extend(pred_bits)
        all_true.extend(true_bits)
        st = strum_event_timing(pred_strum, true_strum, tol_samples=tol_samples)
        merged_errors.extend(st.errors_ms)
        n_true += st.n_true
        n_pred += st.n_pred
        matched += st.matched
    strum = StrumTiming(
        n_true=n_true,
        n_pred=n_pred,
        matched=matched,
        precision=(matched / n_pred) if n_pred else 0.0,
        recall=(matched / n_true) if n_true else 0.0,
        errors_ms=merged_errors,
    )
    return EvalReport(
        per_bit_acc=per_bit_accuracy(all_pred, all_true),
        per_bit_f1=per_bit_f1(all_pred, all_true),
        strum=strum,
    )


def train(args) -> None:
    import numpy as np
    import torch
    from torch.utils.data import DataLoader, TensorDataset

    from .model import StrumNet, count_macs
    from .metrics import LABEL_NAMES

    torch.manual_seed(args.seed)

    if args.overfit:
        # Diagnostic: train and evaluate on the same single capture. If frets
        # can't reach ~98% here, the problem is model/optimisation/formulation,
        # not data variety.
        cap = load_capture(args.overfit)
        train_caps = [cap]
        eval_caps = [cap]
        print(f"OVERFIT mode: train==eval on {cap.name} "
              f"({len(cap)} rows, strum {cap.strum_fraction*100:.1f}%)")
    else:
        holdout_path = Path(args.holdout)
        train_caps = [load_capture(p) for p in args.data if Path(p) != holdout_path]
        eval_caps = [load_capture(holdout_path)]
        print(f"train: {len(train_caps)} captures, holdout: {eval_caps[0].name}")
        for c in train_caps:
            print(f"  {c.name}: {len(c)} rows, strum {c.strum_fraction*100:.1f}%, "
                  f"{c.n_strum_events} events")

    stats = compute_norm_stats(train_caps)
    X, Y, _ = build_arrays(train_caps, args.window, stats=stats, strum_dilate=args.strum_dilate)
    pos_w = args.strum_weight if args.strum_weight else strum_pos_weight(train_caps)
    print(f"windows: {X.shape[0]}, input {X.shape[1:]} | strum pos_weight={pos_w:.1f}"
          f"{' | strum_dilate=' + str(args.strum_dilate) if args.strum_dilate else ''}")

    model = StrumNet(channels=args.channels, kernel=args.kernel)
    print(f"~{count_macs(model, args.window)} MACs/inference (budget ≈100k @ 24MHz)")

    pos_weight = torch.tensor([1.0] * (N_LABELS - 1) + [pos_w], dtype=torch.float32)
    loss_fn = torch.nn.BCEWithLogitsLoss(pos_weight=pos_weight)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    ds = TensorDataset(torch.from_numpy(X), torch.from_numpy(Y))
    dl = DataLoader(ds, batch_size=args.batch, shuffle=True)

    for epoch in range(1, args.epochs + 1):
        model.train()
        total = 0.0
        for xb, yb in dl:
            opt.zero_grad()
            loss = loss_fn(model(xb), yb)
            loss.backward()
            opt.step()
            total += loss.item() * xb.shape[0]
        rep = evaluate(model, eval_caps, args.window, stats, tol_samples=args.tol)
        acc_str = " ".join(f"{n}={a:.3f}" for n, a in zip(LABEL_NAMES, rep.per_bit_acc))
        print(f"epoch {epoch:3d} | loss {total/len(ds):.4f} | acc {acc_str}")
        print(f"            | {rep.strum.summary()}")

    if args.out:
        torch.save({"state_dict": model.state_dict(),
                    "window": args.window,
                    "channels": args.channels,
                    "kernel": args.kernel,
                    "norm_mean": stats[0].tolist(),
                    "norm_std": stats[1].tolist()}, args.out)
        print(f"saved {args.out}")
