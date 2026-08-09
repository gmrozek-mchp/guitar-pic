"""Command-line entry point: classify a single frame, or evaluate the corpus.

    gameplay classify <png> [--samples N|--dense] [--normalize]
    gameplay eval [--cols C --rows R --samples N|--dense] [--normalize] [--no-sweep]
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import sys

from . import amp2p, evaluate
from .classifier import build_templates, classify_image
from .corpus import (
    load_amp2p_corpus,
    load_bgr,
    load_corpus,
    load_score_corpus,
    score_corpus_dir,
)
from .fingerprint import FingerprintConfig
from .highlight import _cell_bounds, build_selection_calibration, read_selection
from .metadata import (
    MENU_LAYOUTS,
    amp2p_score_from_filename,
    score_from_filename,
    selected_item_from_filename,
)
from .score import build_score_catalog, calibrate_score, read_score
from .screens import screen_id_for_filename
from .navigator import NavController, plan_practice_run
from .simgame import SimActuator, SimConfig, SimGame, SimObserver
from .songselect import build_song_catalog, read_song


def _digit_str(digits: tuple[int, ...]) -> str:
    return "".join(str(d) for d in digits)


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
    elif result.screen_id == "in_song":
        score_samples = load_score_corpus()
        if score_samples:
            catalog = build_score_catalog(score_samples)
            calib = calibrate_score([s.image for s in score_samples], catalog)
            r = read_score(image, catalog, calib)
            print(f"  score: {r.value}\t(margin={r.margin:.1f} dist={r.dist:.0f})")
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


def cmd_score(args: argparse.Namespace) -> int:
    """Debug the score reader: segmented digit read for one in-song image."""
    score_samples = load_score_corpus()
    if not score_samples:
        print("read-score: no score corpus found (tools/gameplay/data/scores/)", file=sys.stderr)
        return 2
    image = load_bgr(args.image)
    catalog = build_score_catalog(score_samples, mode=args.mode)
    calib = calibrate_score([s.image for s in score_samples], catalog)
    r = read_score(image, catalog, calib)
    print(f"score: {r.value}\t(digits={_digit_str(r.digits)} margin={r.margin:.2f} dist={r.dist:.1f})")
    parsed = score_from_filename(args.image.rsplit("/", 1)[-1])
    if parsed is not None:
        true_v = parsed[1]
        print(f"predicted={r.value}  true={true_v}  {'OK' if r.value == true_v else 'WRONG'}")
    return 0


def cmd_score_monotonic(args: argparse.Namespace) -> int:
    """Label-free score check over an extracted-capture dir: count monotonic violations."""
    res = evaluate.score_monotonic_eval(args.frames_dir)
    print(
        f"score-monotonic: {res.n_frames} frames, {res.n_violations} violations "
        f"({res.clean_frac:.2%} clean); range {res.first}..{res.last}; "
        f"digit-count hist {res.digit_hist}"
    )
    for name, prev, got in res.violations:
        print(f"  violation {name}: running-max {prev} -> read {got}")
    return 0 if res.n_violations == 0 else 1


def cmd_amp2p(args: argparse.Namespace) -> int:
    """Debug the 2-player amp score reader on one image (block crop or full frame)."""
    samples = load_amp2p_corpus()
    if not samples:
        print("read-amp2p: no 2p score corpus (tools/gameplay/data/scores/score2p__*.png)",
              file=sys.stderr)
        return 2
    image = load_bgr(args.image)
    bank = amp2p.build_amp2p_bank(samples)
    ref = amp2p.build_reference(args.side)
    calib = amp2p.calibrate(args.side, [image], ref, search=args.search)
    r = amp2p.read_amp2p_score(image, bank, calib, args.side)
    shown = r.value if r.value is not None else f"none ({r.reason})"
    layout = f" layout={r.layout}{'' if r.layout_measured else ' EXTRAPOLATED'}" if r.layout else ""
    print(f"amp2p {args.side}: {shown}\t(digits={_digit_str(r.digits)} cells={r.n_cells} "
          f"dist={r.dist:.0f} margin={r.margin:.0f} reg=({calib.dx},{calib.dy})"
          f"{layout}{' LAYOUT-UNKNOWN' if r.layout_unknown else ''})")
    parsed = amp2p_score_from_filename(args.image.rsplit("/", 1)[-1])
    if parsed is not None:
        true_v = parsed[1]
        print(f"predicted={r.value}  true={true_v}  {'OK' if r.value == true_v else 'WRONG'}")
    return 0


def cmd_amp2p_monotonic(args: argparse.Namespace) -> int:
    """Label-free 2-player score check over an extracted-capture dir."""
    res = evaluate.amp2p_score_monotonic_eval(args.frames_dir, side=args.side)
    print(
        f"amp2p-monotonic ({res.side}): {res.n_frames} frames, {res.n_absent} with no amp "
        f"on screen, {res.n_present} gated in; {res.n_resets} song reset(s), "
        f"{res.n_violations} violations ({res.clean_frac:.2%} clean), "
        f"{res.n_false_reads} false reads, {res.n_unreadable} unreadable, "
        f"{res.n_layout_unknown} layout-unknown, {res.n_extrapolated} on an "
        f"extrapolated 6-digit layout; {res.first}..{res.last}, peak {res.max_value}; "
        f"cell-count hist {res.digit_hist}; {res.n_idle_composite} idle-composite "
        f"frames filtered (tracked {res.tracked_value}); worst dist {res.worst_dist:.0f}, "
        f"min margin {res.min_margin:.0f}"
    )
    print(f"  distinct deltas: {res.deltas}")
    for name, prev, got in res.violations:
        print(f"  violation {name}: running-max {prev} -> read {got}")
    for name, reason in res.unreadable:
        print(f"  unreadable {name}: {reason}")
    return 0 if res.n_violations == 0 and res.n_false_reads == 0 else 1


def cmd_amp2p_grow(args: argparse.Namespace) -> int:
    """Propose frames from a new capture to add to the labelled 2-player corpus."""
    samples = load_amp2p_corpus()
    if not samples:
        print("amp2p-grow: the corpus is empty, so there is no bank to grow. See "
              "docs/journal.md for the cold-start (cluster-and-label) procedure.",
              file=sys.stderr)
        return 2
    bank = amp2p.build_amp2p_bank(samples)
    calib = amp2p.calibrate_on_corpus(args.side, samples)
    have = {v for v in (amp2p_score_from_filename(s.path.name) for s in samples) if v}
    cands, n_settled, n_frames = amp2p.grow_candidates(
        args.frames_dir, bank, args.side, calib, limit=args.limit,
        have_values={v for _side, v in have},
    )
    print(f"amp2p-grow ({args.side}): {n_frames} frames, {n_settled} settled+confident, "
          f"{len(cands)} proposed (corpus has {len(samples)}, bank {len(bank.templates)} templates)")
    if not cands:
        print("  nothing to add — the bank already reads this capture confidently.")
        return 0
    out = pathlib.Path(args.out)
    for c in cands:
        src = pathlib.Path(c.path)
        name = amp2p.corpus_name(args.side, c.value, args.source or f"cap{src.stem.split('-')[-1]}")
        print(f"  {src.name} -> {name}  (dist={c.dist:.0f} margin={c.margin:.0f})")
        if args.commit:
            shutil.copyfile(src, out / name)
    if args.commit:
        print(f"  wrote {len(cands)} frame(s) to {out}/ — verify each label with read-amp2p, "
              f"then re-run amp2p-monotonic.")
    else:
        print("  dry run; pass --commit to write these into the corpus.")
    return 0


def cmd_eval(args: argparse.Namespace) -> int:
    config = _config_from_args(args)
    print(evaluate.run_report(config=config, sweep=not args.no_sweep))
    return 0


def cmd_export_c(args: argparse.Namespace) -> int:
    """Write the recognizer metadata as a C header for the firmware port."""
    from .export_c import build_metadata_header

    header = build_metadata_header()
    if args.out:
        from pathlib import Path

        Path(args.out).write_text(header)
        print(f"wrote {args.out} ({len(header)} bytes)", file=sys.stderr)
    else:
        sys.stdout.write(header)
    return 0


def cmd_navigate(args: argparse.Namespace) -> int:
    """Plan a practice run and execute it closed-loop against the simulated menu."""
    plan = plan_practice_run(args.song, args.difficulty, part=args.part)
    print(f"plan: practice-run song #{args.song}, {args.difficulty}, {args.part}")
    for i, step in enumerate(plan.steps):
        print(f"  {i}. [{step.expected_from}] {step.desc} -> {step.expected_to}")

    cfg = SimConfig(part_present=not args.skip_part, sticky=({"difficulty_select": 2} if args.sticky else {}))
    game = SimGame("main_menu", cfg)
    controller = NavController(SimObserver(game), SimActuator(game))
    end = controller.run(plan)
    print("\nsim trace:")
    for line in controller.trace:
        print(f"  {line}")
    print(f"\nend: {end.screen}  chosen={game.state.chosen}")
    return 0 if end.screen == plan.goal else 1


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

    p_score = sub.add_parser("read-score", help="Debug the in-song score reader on one image.")
    p_score.add_argument("image", help="path to a PNG/BGR frame")
    p_score.add_argument("--mode", default="training", help="score mode/font (default training)")
    p_score.set_defaults(func=cmd_score)

    p_mono = sub.add_parser(
        "score-monotonic",
        help="Label-free score check over an extracted-capture dir (count monotonic violations).",
    )
    p_mono.add_argument("frames_dir", help="dir of extracted score-block PNGs (marvin-perf export-region)")
    p_mono.set_defaults(func=cmd_score_monotonic)

    p_amp = sub.add_parser("read-amp2p", help="Debug the 2-player amp score reader on one image.")
    p_amp.add_argument("image", help="path to an amp-block crop or a full 720x480 frame")
    p_amp.add_argument("--side", choices=amp2p.SIDES, default="right")
    p_amp.add_argument("--search", type=int, default=4, help="registration search radius (px)")
    p_amp.set_defaults(func=cmd_amp2p)

    p_amono = sub.add_parser(
        "amp2p-monotonic",
        help="Label-free 2-player score check over an extracted-capture dir.",
    )
    p_amono.add_argument("frames_dir", help="dir of extracted amp-block PNGs (marvin-perf export-region)")
    p_amono.add_argument("--side", choices=amp2p.SIDES, default="right")
    p_amono.set_defaults(func=cmd_amp2p_monotonic)

    p_agrow = sub.add_parser(
        "amp2p-grow",
        help="Propose frames from a new capture to add to the labelled 2p corpus.",
    )
    p_agrow.add_argument("frames_dir", help="dir of extracted amp-block PNGs")
    p_agrow.add_argument("--side", choices=amp2p.SIDES, default="right")
    p_agrow.add_argument("--limit", type=int, default=8, help="max frames to propose")
    p_agrow.add_argument("--out", default=str(score_corpus_dir()), help="corpus directory")
    p_agrow.add_argument("--source", help="source tag for the filenames (default: frame number)")
    p_agrow.add_argument("--commit", action="store_true", help="write the frames (default: dry run)")
    p_agrow.set_defaults(func=cmd_amp2p_grow)

    p_export = sub.add_parser("export-c", help="Emit recognizer metadata as a C header for the firmware.")
    p_export.add_argument("--out", help="output .h path (default: stdout)")
    p_export.set_defaults(func=cmd_export_c)

    p_nav = sub.add_parser("navigate", help="Plan + run a practice run against the simulated menu.")
    p_nav.add_argument("--song", type=int, default=0, help="song index (default 0)")
    p_nav.add_argument("--difficulty", default="hard", help="easy|medium|hard|expert (default hard)")
    p_nav.add_argument("--part", default="lead", help="lead|rhythm (default lead)")
    p_nav.add_argument("--skip-part", action="store_true", help="simulate a song with no part_select")
    p_nav.add_argument("--sticky", action="store_true", help="simulate a sticky (non-zero) difficulty default")
    p_nav.set_defaults(func=cmd_navigate)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command is None:
        args = parser.parse_args(["eval"])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
