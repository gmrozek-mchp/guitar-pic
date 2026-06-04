"""edge-ai CLI.

  edge-ai lag CAPTURE.csv [--max-lag-ms 600]
        Measure the photo-dip → strum lag (stdlib only; no torch needed).

  edge-ai train --holdout HELD.csv DATA.csv [DATA.csv ...] [--window 60] [--out model.pt]
        Train the baseline StrumNet (needs the `train` dep group).

  edge-ai eval --model model.pt CAPTURE.csv [CAPTURE.csv ...]
        Evaluate a checkpoint and print per-bit accuracy + strum timing.
"""

from __future__ import annotations

import argparse
import sys


def _parse_dilations(s: str) -> tuple[int, ...]:
    """Parse a comma-separated dilation list, e.g. '1,4,16' -> (1, 4, 16)."""
    try:
        vals = tuple(int(x) for x in s.split(","))
    except ValueError:
        raise argparse.ArgumentTypeError(f"dilations must be comma-separated ints, got {s!r}")
    if not vals or any(v < 1 for v in vals):
        raise argparse.ArgumentTypeError("dilations must be one or more positive ints")
    return vals


def cmd_lag(args) -> int:
    from .data import load_capture
    from .lag import measure_lag

    cap = load_capture(args.capture)
    res = measure_lag(cap, max_lag_ms=args.max_lag_ms)
    print(f"{cap.name}: {len(cap)} rows, strum {cap.strum_fraction*100:.1f}%, "
          f"{cap.n_strum_events} events")
    print(res.summary())
    if args.curve:
        step = max(1, len(res.curve) // 40)
        peak = res.best_lag_samples
        for L, s in res.curve[::step]:
            bar = "#" * int(max(0.0, s) / (max(x for _, x in res.curve) or 1) * 40)
            mark = " <-- peak" if abs(L - peak) < step else ""
            print(f"  {L:4d} ({L/240*1000:6.1f} ms) {bar}{mark}")
    return 0


def cmd_train(args) -> int:
    if not args.overfit:
        if not args.holdout:
            print("train: need --holdout HELD.csv (or --overfit FILE.csv)", file=sys.stderr)
            return 2
        if not args.data:
            print("train: need training CSV(s) (or --overfit FILE.csv)", file=sys.stderr)
            return 2
    from .train import train
    train(args)
    return 0


def cmd_eval(args) -> int:
    import torch
    from .data import load_capture
    from .model import StrumNet
    from .train import evaluate
    from .metrics import LABEL_NAMES

    import numpy as np

    ckpt = torch.load(args.model, map_location="cpu")
    model = StrumNet(channels=ckpt["channels"], kernel=ckpt["kernel"],
                     dilations=tuple(ckpt.get("dilations", (1, 4))))
    model.load_state_dict(ckpt["state_dict"])
    window = ckpt["window"]
    stats = (np.asarray(ckpt["norm_mean"], dtype=np.float32),
             np.asarray(ckpt["norm_std"], dtype=np.float32))
    caps = [load_capture(p) for p in args.captures]
    rep = evaluate(model, caps, window, stats, tol_samples=args.tol,
                   hold=args.strum_hold, refractory=args.strum_refractory,
                   strum_thresh=args.strum_thresh)
    print(" ".join(f"{n}={a:.3f}" for n, a in zip(LABEL_NAMES, rep.per_bit_acc)))
    print("f1: " + " ".join(f"{n}={a:.3f}" for n, a in zip(LABEL_NAMES, rep.per_bit_f1)))
    print(f"raw  {rep.strum.summary()} | {rep.pulse.summary()}")
    if rep.strum_post is not None:
        print(f"mono {rep.strum_post.summary()} | {rep.pulse_post.summary()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="edge-ai", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    pl = sub.add_parser("lag", help="Measure photo→strum lag (stdlib only).")
    pl.add_argument("capture", help="actuator-labelled CSV")
    pl.add_argument("--max-lag-ms", type=float, default=600.0)
    pl.add_argument("--curve", action="store_true", help="print the correlation curve")
    pl.set_defaults(func=cmd_lag)

    pt = sub.add_parser("train", help="Train the baseline StrumNet.")
    pt.add_argument("data", nargs="*", help="training CSVs (holdout excluded if listed)")
    pt.add_argument("--holdout", help="held-out CSV for validation")
    pt.add_argument("--overfit", help="diagnostic: train AND eval on this single CSV")
    pt.add_argument("--window", type=int, default=60)
    pt.add_argument("--channels", type=int, default=8)
    pt.add_argument("--kernel", type=int, default=5)
    pt.add_argument("--dilations", type=_parse_dilations, default=(1, 4, 16),
                    help="comma-separated causal-conv dilation factors, one conv layer "
                         "each; default '1,4,16' (RF 85) covers the photo->strum lag. "
                         "'1,4' (RF 21) is too short for actuator-fb labels")
    pt.add_argument("--epochs", type=int, default=30)
    pt.add_argument("--batch", type=int, default=256)
    pt.add_argument("--lr", type=float, default=1e-3)
    pt.add_argument("--strum-dilate", type=int, default=0,
                    help="widen strum training labels by ±N samples (eval unaffected)")
    pt.add_argument("--strum-weight", type=float, default=0.0,
                    help="override strum BCE pos_weight (0 = auto inverse-frequency)")
    pt.add_argument("--tol", type=int, default=5, help="strum match tolerance (samples)")
    pt.add_argument("--strum-hold", type=int, default=0,
                    help="monostable hold ticks for the strum post-processor (0=off)")
    pt.add_argument("--strum-refractory", type=int, default=4,
                    help="monostable refractory ticks after a strum")
    pt.add_argument("--strum-thresh", type=float, default=0.5,
                    help="decision threshold for the strum bit (raise to trade recall for precision)")
    pt.add_argument("--seed", type=int, default=0)
    pt.add_argument("--out", help="checkpoint path (.pt)")
    pt.set_defaults(func=cmd_train)

    pe = sub.add_parser("eval", help="Evaluate a checkpoint.")
    pe.add_argument("captures", nargs="+", help="CSV(s) to evaluate")
    pe.add_argument("--model", required=True, help="checkpoint .pt")
    pe.add_argument("--tol", type=int, default=5)
    pe.add_argument("--strum-hold", type=int, default=0,
                    help="monostable hold ticks for the strum post-processor (0=off)")
    pe.add_argument("--strum-refractory", type=int, default=4,
                    help="monostable refractory ticks after a strum")
    pe.add_argument("--strum-thresh", type=float, default=0.5,
                    help="decision threshold for the strum bit (raise to trade recall for precision)")
    pe.set_defaults(func=cmd_eval)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
