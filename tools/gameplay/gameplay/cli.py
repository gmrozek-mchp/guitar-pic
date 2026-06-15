"""Command-line entry point: classify a single frame, or evaluate the corpus.

    gameplay classify <png> [--samples N|--dense] [--normalize]
    gameplay eval [--cols C --rows R --samples N|--dense] [--normalize] [--no-sweep]
"""

from __future__ import annotations

import argparse
import sys

from . import evaluate
from .classifier import build_templates, classify_image
from .corpus import load_bgr, load_corpus
from .fingerprint import FingerprintConfig
from .highlight import _cell_bounds, build_selection_calibration, read_selection
from .metadata import MENU_LAYOUTS, selected_item_from_filename
from .screens import screen_id_for_filename
from .songselect import build_song_catalog, read_song


def _config_from_args(args: argparse.Namespace) -> FingerprintConfig:
    spr = None if getattr(args, "dense", False) else args.samples
    return FingerprintConfig(
        cols=args.cols,
        rows=args.rows,
        samples_per_region=spr,
        normalize=args.normalize,
    )


def cmd_classify(args: argparse.Namespace) -> int:
    config = _config_from_args(args)
    samples = load_corpus()
    templates = build_templates(evaluate.labelled_fps(samples, config), config)
    rec = evaluate.recommend_thresholds(samples, config)
    t_abs, t_margin = rec.rec_t_abs, rec.rec_t_margin

    image = load_bgr(args.image)
    result = classify_image(image, templates, t_abs=t_abs, t_margin=t_margin)
    print(
        f"{result.screen_id}\t(best={result.best_id} dist={result.best_dist} "
        f"margin={result.margin}; thresholds t_abs={t_abs} t_margin={t_margin})"
    )
    layout = MENU_LAYOUTS.get(result.screen_id)
    if layout is not None:
        calibration = build_selection_calibration(samples)
        sel = read_selection(image, layout, calibration)
        print(f"  selection: [{sel.index}] {sel.item}\t(margin={sel.margin:.2f})")
    elif result.screen_id == "song_select":
        catalog = build_song_catalog(samples)
        song = read_song(image, catalog)
        print(
            f"  song: [{song.setlist} #{song.index}] {song.song_id}\t"
            f"(dist={song.dist:.0f} margin={song.margin:.1f})"
        )
    return 0


def cmd_rows(args: argparse.Namespace) -> int:
    """Debug the static-list reader: per-cell scores for one screen image."""
    image = load_bgr(args.image)
    screen_id = args.screen or screen_id_for_filename(args.image)
    layout = MENU_LAYOUTS.get(screen_id)
    if layout is None:
        print(f"rows: {screen_id!r} is not a modelled static-list screen", file=sys.stderr)
        return 2
    sel = read_selection(image, layout, build_selection_calibration(load_corpus()))
    truth = selected_item_from_filename(args.image)
    print(f"{screen_id}  axis={layout.axis} band={layout.band}")
    for i, (item, (x0, y0, x1, y1)) in enumerate(zip(layout.items, _cell_bounds(layout))):
        mark = " <== selected" if i == sel.index else ""
        star = " (TRUE)" if truth == item else ""
        print(f"  [{i}] {item:<16} dev={sel.scores[i]:>5.2f}  cell=({x0},{y0},{x1},{y1}){mark}{star}")
    if truth is not None:
        print(f"predicted={sel.item}  true={truth}  {'OK' if sel.item == truth else 'WRONG'}")
    return 0


def cmd_eval(args: argparse.Namespace) -> int:
    config = _config_from_args(args)
    print(evaluate.run_report(config=config, sweep=not args.no_sweep))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gameplay",
        description="GH3 game-state engine prototype: screen classifier.",
    )
    sub = p.add_subparsers(dest="command")

    def add_fp_args(sp: argparse.ArgumentParser) -> None:
        d = FingerprintConfig()  # chosen defaults
        sp.add_argument("--cols", type=int, default=d.cols, help=f"grid columns (default {d.cols})")
        sp.add_argument("--rows", type=int, default=d.rows, help=f"grid rows (default {d.rows})")
        sp.add_argument(
            "--samples", type=int, default=d.samples_per_region,
            help=f"N x N samples per region (default {d.samples_per_region}); ignored with --dense",
        )
        sp.add_argument("--dense", action="store_true", help="dense per-region mean (every pixel)")
        sp.add_argument(
            "--normalize", action=argparse.BooleanOptionalAction, default=d.normalize,
            help="per-frame value normalization (on by default; --no-normalize to disable)",
        )

    p_classify = sub.add_parser("classify", help="Classify one image against the corpus templates.")
    p_classify.add_argument("image", help="path to a PNG/BGR frame")
    add_fp_args(p_classify)
    p_classify.set_defaults(func=cmd_classify)

    p_eval = sub.add_parser("eval", help="Evaluate the classifier against the corpus.")
    add_fp_args(p_eval)
    p_eval.add_argument("--no-sweep", action="store_true", help="skip the parameter sweep")
    p_eval.set_defaults(func=cmd_eval)

    p_rows = sub.add_parser("rows", help="Debug the static-list selection reader on one image.")
    p_rows.add_argument("image", help="path to a PNG/BGR frame")
    p_rows.add_argument("--screen", help="screen id (default: inferred from filename)")
    p_rows.set_defaults(func=cmd_rows)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command is None:
        args = parser.parse_args(["eval"])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
