"""Evaluation harness for the screen classifier.

Proves the fingerprint+nearest-centroid design against the corpus and reports
everything needed to pick parameters and thresholds before any firmware port:

1. Leave-one-out cross-validation -> per-class accuracy + confusion matrix.
2. Whole-class hold-out -> impostor distances, validating the UNKNOWN reject.
3. Threshold recommendation from the in-class vs impostor distance separation.
4. Robustness pass over the analog-slop envelope (`perturb.envelope`).
5. SAM9X75 time-per-classification estimate at the configured sample density.

All distances are integer L1 over uint8 fingerprints.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

import numpy as np

from . import perturb
from .classifier import (
    DEFAULT_T_ABS,
    DEFAULT_T_MARGIN,
    Templates,
    _l1_to_centroids,
    build_templates,
    classify_fp,
)
from . import amp2p
from .corpus import Sample, load_amp2p_corpus, load_bgr, load_corpus, load_score_corpus
from .fingerprint import CANONICAL_H, CANONICAL_W, BPP, FingerprintConfig, fingerprint
from .highlight import build_selection_calibration, read_selection
from .metadata import (
    MENU_LAYOUTS,
    SCORE_BLOCK_ROI,
    score_from_filename,
    selected_item_from_filename,
    song_from_filename,
)
from .score import build_score_catalog, calibrate_score, read_score
from .screens import UNKNOWN
from .songselect import build_song_catalog, read_setlist, read_song

# Uncached-DDR read model for the SAM9X75 port (see plan's hardware-cost section).
_DDR_BW_MB_S = (50.0, 150.0)  # dense sequential, single-beat, no cache-line burst
_UNCACHED_LAT_NS = (50.0, 150.0)  # per strided single-pixel access (subsampled)


def labelled_fps(samples: list[Sample], config: FingerprintConfig) -> dict[str, list[np.ndarray]]:
    out: dict[str, list[np.ndarray]] = defaultdict(list)
    for s in samples:
        out[s.screen_id].append(fingerprint(s.image, config))
    return dict(out)


# ─── 1. Leave-one-out CV ──────────────────────────────────────────────────────


@dataclass
class LooResult:
    config: FingerprintConfig
    t_abs: int
    t_margin: int
    n_total: int
    n_correct: int
    n_unknown: int
    per_class_acc: dict[str, tuple[int, int]]  # id -> (correct, total)
    confusion: dict[tuple[str, str], int]  # (true, pred) -> count
    single_sample_classes: tuple[str, ...]

    @property
    def accuracy(self) -> float:
        return self.n_correct / self.n_total if self.n_total else 0.0


def loo_eval(
    samples: list[Sample],
    config: FingerprintConfig,
    t_abs: int = DEFAULT_T_ABS,
    t_margin: int = DEFAULT_T_MARGIN,
) -> LooResult:
    fps = [fingerprint(s.image, config) for s in samples]
    ids = [s.screen_id for s in samples]
    counts = defaultdict(int)
    for sid in ids:
        counts[sid] += 1
    single = tuple(sorted(c for c, n in counts.items() if n == 1))

    per_class: dict[str, list[int]] = {sid: [0, 0] for sid in counts}
    confusion: dict[tuple[str, str], int] = defaultdict(int)
    n_correct = n_unknown = 0

    for i, (fp_i, true_id) in enumerate(zip(fps, ids)):
        held: dict[str, list[np.ndarray]] = defaultdict(list)
        for j, (fp_j, id_j) in enumerate(zip(fps, ids)):
            if j != i:
                held[id_j].append(fp_j)
        templates = build_templates(dict(held), config)
        pred = classify_fp(fp_i, templates, t_abs=t_abs, t_margin=t_margin).screen_id

        per_class[true_id][1] += 1
        confusion[(true_id, pred)] += 1
        if pred == true_id:
            per_class[true_id][0] += 1
            n_correct += 1
        elif pred == UNKNOWN:
            n_unknown += 1

    return LooResult(
        config=config,
        t_abs=t_abs,
        t_margin=t_margin,
        n_total=len(samples),
        n_correct=n_correct,
        n_unknown=n_unknown,
        per_class_acc={k: (v[0], v[1]) for k, v in per_class.items()},
        confusion=dict(confusion),
        single_sample_classes=single,
    )


# ─── 2. Whole-class hold-out (UNKNOWN reject) + 3. threshold recommendation ────


# Accept legitimate in-class matches with this much headroom above the worst one,
# so normal frame-to-frame and analog-slop variation doesn't get falsely rejected.
_ABS_HEADROOM = 0.20

# The margin gate only exists to reject near-ties; keep it well below the smallest
# legitimate in-class margin so it never rejects a real screen.
_MARGIN_FRACTION = 0.5


@dataclass
class ThresholdRecommendation:
    in_class_dist_max: int  # worst correct-match distance (LOO)
    in_class_margin_min: int  # worst correct-match margin (LOO)
    impostor_dist_min: int  # nearest a held-out class got to a wrong centroid
    impostor_margin_max: int
    rec_t_abs: int
    rec_t_margin: int
    impostor_leak: int  # held-out impostors that t_abs would accept (false positives)
    n_impostors: int


def _loo_in_class_stats(samples, config) -> tuple[list[int], list[int]]:
    """LOO best-distance and margin for samples whose nearest class is correct."""
    fps = [fingerprint(s.image, config) for s in samples]
    ids = [s.screen_id for s in samples]
    dists: list[int] = []
    margins: list[int] = []
    for i, (fp_i, true_id) in enumerate(zip(fps, ids)):
        held: dict[str, list[np.ndarray]] = defaultdict(list)
        for j, (fp_j, id_j) in enumerate(zip(fps, ids)):
            if j != i:
                held[id_j].append(fp_j)
        if true_id not in held:  # single-sample class: no own centroid under LOO
            continue
        templates = build_templates(dict(held), config)
        d = _l1_to_centroids(fp_i, templates.centroids)
        order = np.argsort(d, kind="stable")
        if templates.ids[int(order[0])] != true_id:
            continue  # misclassified — not an in-class exemplar
        dists.append(int(d[order[0]]))
        margins.append(int(d[order[1]] - d[order[0]]) if len(order) > 1 else 1 << 30)
    return dists, margins


def _holdout_impostor_stats(samples, config) -> tuple[list[int], list[int]]:
    """For each class, remove it entirely and record how close its samples land to
    the *nearest surviving* class (distance + margin) — these must be rejected."""
    by_class: dict[str, list[np.ndarray]] = defaultdict(list)
    for s in samples:
        by_class[s.screen_id].append(fingerprint(s.image, config))
    dists: list[int] = []
    margins: list[int] = []
    classes = sorted(by_class)
    for held_out in classes:
        kept = {c: v for c, v in by_class.items() if c != held_out}
        if not kept:
            continue
        templates = build_templates(kept, config)
        for fp in by_class[held_out]:
            d = _l1_to_centroids(fp, templates.centroids)
            order = np.argsort(d, kind="stable")
            dists.append(int(d[order[0]]))
            margins.append(int(d[order[1]] - d[order[0]]) if len(order) > 1 else 0)
    return dists, margins


def recommend_thresholds(samples, config) -> ThresholdRecommendation:
    in_d, in_m = _loo_in_class_stats(samples, config)
    imp_d, imp_m = _holdout_impostor_stats(samples, config)

    in_d_max = max(in_d) if in_d else 0
    in_m_min = min(in_m) if in_m else 0
    imp_d_min = min(imp_d) if imp_d else (1 << 30)
    imp_m_max = max(imp_m) if imp_m else 0

    # Primary gate: accept every legitimate in-class match plus headroom. Never
    # tune t_abs down to chase impostor rejection — that just rejects real screens.
    rec_t_abs = round(in_d_max * (1.0 + _ABS_HEADROOM)) if in_d_max else (1 << 30)
    # Secondary gate: soft margin, strictly below the smallest legit in-class margin.
    rec_t_margin = max(0, int(in_m_min * _MARGIN_FRACTION))

    # Honest secondary metric: how many whole-class-holdout impostors slip past
    # t_abs (genuinely-unseen screens that resemble a known one). Not a false
    # rejection of real screens — reported, not minimized at their expense.
    leak = sum(1 for d in imp_d if d <= rec_t_abs)

    return ThresholdRecommendation(
        in_class_dist_max=in_d_max,
        in_class_margin_min=in_m_min,
        impostor_dist_min=imp_d_min,
        impostor_margin_max=imp_m_max,
        rec_t_abs=rec_t_abs,
        rec_t_margin=rec_t_margin,
        impostor_leak=leak,
        n_impostors=len(imp_d),
    )


# ─── 4. Robustness pass ────────────────────────────────────────────────────────


@dataclass
class RobustnessResult:
    per_category: dict[str, tuple[int, int]]  # category -> (stable, total)
    n_total: int
    n_stable: int

    @property
    def stability(self) -> float:
        return self.n_stable / self.n_total if self.n_total else 0.0


def robustness_eval(
    samples: list[Sample],
    config: FingerprintConfig,
    t_abs: int,
    t_margin: int,
    seed: int = 1234,
) -> RobustnessResult:
    """Train on the full clean corpus, then classify perturbed copies of each
    sample. Stable = perturbed prediction still equals the true class."""
    templates = build_templates(labelled_fps(samples, config), config)
    rng = np.random.default_rng(seed)
    per_cat: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    n_total = n_stable = 0
    for s in samples:
        for category, _name, pimg in perturb.envelope(s.image, rng):
            pred = classify_fp(
                fingerprint(pimg, config), templates, t_abs=t_abs, t_margin=t_margin
            ).screen_id
            ok = pred == s.screen_id
            per_cat[category][0] += int(ok)
            per_cat[category][1] += 1
            n_total += 1
            n_stable += int(ok)
    return RobustnessResult(
        per_category={k: (v[0], v[1]) for k, v in per_cat.items()},
        n_total=n_total,
        n_stable=n_stable,
    )


# ─── 5. Hardware time estimate ─────────────────────────────────────────────────


def hw_time_estimate_ms(config: FingerprintConfig) -> tuple[float, float]:
    """Estimate SAM9X75 ms/classification for `config`'s sample density.

    Dense touches every pixel (bandwidth-bound); subsampled is strided single
    accesses (latency-bound). Returns (fast_est, slow_est) ms.
    """
    if config.samples_per_region is None:
        nbytes = CANONICAL_W * CANONICAL_H * BPP
        fast = nbytes / (_DDR_BW_MB_S[1] * 1e6) * 1e3
        slow = nbytes / (_DDR_BW_MB_S[0] * 1e6) * 1e3
        return (fast, slow)
    reads = config.pixel_reads
    fast = reads * _UNCACHED_LAT_NS[0] / 1e6
    slow = reads * _UNCACHED_LAT_NS[1] / 1e6
    return (fast, slow)


# ─── Static-list highlight (selection) reader ──────────────────────────────────


@dataclass
class SelectionEvalResult:
    per_screen: dict[str, tuple[int, int]]  # screen_id -> (correct, total)
    failures: list[tuple[str, str, str]]  # (filename, true_item, pred_item)
    n_total: int
    n_correct: int

    @property
    def accuracy(self) -> float:
        return self.n_correct / self.n_total if self.n_total else 0.0


def _labelled_selection_samples(samples: list[Sample]):
    """Yield (sample, layout, true_item) for samples whose filename encodes a
    selection on a modelled static-list screen."""
    for s in samples:
        layout = MENU_LAYOUTS.get(s.screen_id)
        if layout is None:
            continue
        true_item = selected_item_from_filename(s.path.name)
        if true_item is None or true_item not in layout.items:
            continue
        yield s, layout, true_item


def selection_eval(
    samples: list[Sample], perturb_envelope: bool = False, seed: int = 1234
) -> SelectionEvalResult:
    calibration = build_selection_calibration(samples)
    per: dict[str, list[int]] = defaultdict(lambda: [0, 0])
    failures: list[tuple[str, str, str]] = []
    rng = np.random.default_rng(seed)
    for s, layout, true_item in _labelled_selection_samples(samples):
        variants = [(s.path.name, s.image)]
        if perturb_envelope:
            variants = [
                (f"{s.path.name}[{name}]", pimg)
                for _cat, name, pimg in perturb.envelope(s.image, rng)
            ]
        for fname, img in variants:
            pred = read_selection(img, layout, calibration).item
            per[s.screen_id][1] += 1
            if pred == true_item:
                per[s.screen_id][0] += 1
            else:
                failures.append((fname, true_item, pred))
    n_total = sum(t for _c, t in per.values())
    n_correct = sum(c for c, _t in per.values())
    return SelectionEvalResult(
        per_screen={k: (v[0], v[1]) for k, v in per.items()},
        failures=failures,
        n_total=n_total,
        n_correct=n_correct,
    )


# ─── song_select reader (fixed-slot bitmap match) ──────────────────────────────


@dataclass
class SongEvalResult:
    n_total: int
    n_song_ok: int  # song + setlist correct via match-all (the primary path)
    n_setlist_ok: int  # independent setlist read from page bg colour
    n_setlist_slop_ok: int  # ...under the analog-slop envelope
    margin_min: float  # worst nearest-wrong-song margin (clean)
    slop_ok: int
    slop_total: int
    failures: list[tuple[str, str, str]]  # (filename, true song_id, pred setlist:song_id)

    @property
    def song_acc(self) -> float:
        return self.n_song_ok / self.n_total if self.n_total else 0.0

    @property
    def setlist_acc(self) -> float:
        return self.n_setlist_ok / self.n_total if self.n_total else 0.0

    @property
    def setlist_slop_acc(self) -> float:
        return self.n_setlist_slop_ok / self.slop_total if self.slop_total else 0.0

    @property
    def slop_acc(self) -> float:
        return self.slop_ok / self.slop_total if self.slop_total else 0.0


def song_eval(samples: list[Sample], seed: int = 1234) -> SongEvalResult:
    catalog = build_song_catalog(samples)
    rng = np.random.default_rng(seed)
    n = song_ok = setlist_ok = setlist_slop_ok = 0
    slop_ok = slop_total = 0
    margins: list[float] = []
    failures: list[tuple[str, str, str]] = []
    for s in samples:
        parsed = song_from_filename(s.path.name)
        if parsed is None:
            continue
        setlist, _index, song_id = parsed
        n += 1
        setlist_ok += read_setlist(s.image, catalog) == setlist
        r = read_song(s.image, catalog)  # match-all: yields song + setlist
        margins.append(r.margin)
        if r.song_id == song_id and r.setlist == setlist:
            song_ok += 1
        else:
            failures.append((s.path.name, song_id, f"{r.setlist}:{r.song_id}"))
        for _cat, _name, pimg in perturb.envelope(s.image, rng):
            slop_total += 1
            rp = read_song(pimg, catalog)
            slop_ok += rp.song_id == song_id and rp.setlist == setlist
            setlist_slop_ok += read_setlist(pimg, catalog) == setlist
    return SongEvalResult(
        n_total=n, n_song_ok=song_ok, n_setlist_ok=setlist_ok,
        n_setlist_slop_ok=setlist_slop_ok,
        margin_min=min(margins) if margins else 0.0,
        slop_ok=slop_ok, slop_total=slop_total, failures=failures,
    )


# ─── in-song score reader (per-digit glyph OCR) ─────────────────────────────────


# Fixed per-rig position shifts to exercise auto-registration (position is stable
# on a rig, so this models re-registering on a *different* rig, not per-frame slop).
_SCORE_REG_SHIFTS = ((4, 0), (0, 3), (-6, 4), (6, -3), (8, -4))


@dataclass
class ScoreEvalResult:
    n_total: int
    n_exact_ok: int        # clean full-value exact match, locked geometry
    n_loo_exact_ok: int    # LOO of the digit classifier (geometry locked on session)
    n_digit_ok: int        # clean per-cell class matches
    n_digit_total: int
    a2d_ok: int            # exact match under A2D slop (gain/offset/noise), fixed calib
    a2d_total: int
    reg_ok: int            # exact match after re-registering on a shifted session
    reg_total: int
    margin_min: float      # worst per-frame weakest-digit margin (clean)
    failures: list[tuple[str, str, str]]  # (filename, true value, pred value)

    @property
    def exact_acc(self) -> float:
        return self.n_exact_ok / self.n_total if self.n_total else 0.0

    @property
    def loo_exact_acc(self) -> float:
        return self.n_loo_exact_ok / self.n_total if self.n_total else 0.0

    @property
    def digit_acc(self) -> float:
        return self.n_digit_ok / self.n_digit_total if self.n_digit_total else 0.0

    @property
    def a2d_acc(self) -> float:
        return self.a2d_ok / self.a2d_total if self.a2d_total else 0.0

    @property
    def reg_acc(self) -> float:
        return self.reg_ok / self.reg_total if self.reg_total else 0.0


def score_eval(samples: list[Sample], mode: str = "training", seed: int = 1234) -> ScoreEvalResult:
    """Evaluate the score reader on the labelled score corpus.

    Registers once on the whole session (as at gameplay start), then:
    - clean: search-free per-frame exact-match + per-cell accuracy + worst margin;
    - LOO: the digit classifier's generalization (templates from the other frames);
    - A2D: exact-match under gain/offset/noise at the *fixed* calibration (the
      continual-operation model — position is locked, only the capture drifts);
    - registration: re-register on whole-session position shifts, then exact-match.
    """
    labelled = [s for s in samples if (p := score_from_filename(s.path.name)) and p[0] == mode]
    catalog = build_score_catalog(labelled, mode=mode)
    calib = calibrate_score([s.image for s in labelled], catalog)
    rng = np.random.default_rng(seed)

    n = exact = loo_exact = digit_ok = digit_total = 0
    a2d_ok = a2d_total = 0
    margins: list[float] = []
    failures: list[tuple[str, str, str]] = []
    for s in labelled:
        value = score_from_filename(s.path.name)[1]
        n += 1
        r = read_score(s.image, catalog, calib)
        margins.append(r.margin)
        if r.value == value:
            exact += 1
        else:
            failures.append((s.path.name, str(value), str(r.value)))
        truth = [int(c) for c in str(value)]
        digit_ok += sum(a == b for a, b in zip(r.digits, truth))
        digit_total += len(truth)
        # LOO: rebuild the classifier without this frame (geometry stays locked).
        loo_cat = build_score_catalog([o for o in labelled if o.path.name != s.path.name], mode=mode)
        loo_exact += read_score(s.image, loo_cat, calib).value == value
        # A2D slop at the fixed calibration.
        for cat_name, _name, pimg in perturb.envelope(s.image, rng):
            if cat_name not in ("gain", "offset", "noise"):
                continue
            a2d_total += 1
            a2d_ok += read_score(pimg, catalog, calib).value == value

    # Registration robustness: shift the whole session, re-register, read.
    reg_ok = reg_total = 0
    for dx, dy in _SCORE_REG_SHIFTS:
        shifted = [perturb.translate(s.image, dx, dy) for s in labelled]
        cal2 = calibrate_score(shifted, catalog)
        for s, img in zip(labelled, shifted):
            reg_total += 1
            reg_ok += read_score(img, catalog, cal2).value == score_from_filename(s.path.name)[1]

    return ScoreEvalResult(
        n_total=n, n_exact_ok=exact, n_loo_exact_ok=loo_exact,
        n_digit_ok=digit_ok, n_digit_total=digit_total,
        a2d_ok=a2d_ok, a2d_total=a2d_total, reg_ok=reg_ok, reg_total=reg_total,
        margin_min=min(margins) if margins else 0.0, failures=failures,
    )


@dataclass
class ScoreMonotonicResult:
    n_frames: int
    n_violations: int           # reads that decreased vs the running max (a play only climbs)
    digit_hist: dict[int, int]  # digit-count → frame count
    first: int
    last: int
    violations: list[tuple[str, int, int]]  # (filename, prev_max, read) — first few

    @property
    def clean_frac(self) -> float:
        return 1.0 - (self.n_violations / self.n_frames) if self.n_frames else 0.0


def score_monotonic_eval(
    frames_dir, samples: list[Sample] | None = None, mode: str = "training"
) -> ScoreMonotonicResult:
    """Label-free accuracy proxy over a whole extracted-capture directory.

    A play's score only climbs, so any read that *decreases* vs the running max is
    a misread. Reads every `*.png` (sorted) with templates from the committed
    corpus, registered once, and reports the violation count + digit-count
    histogram. The capture isn't committed, so this is a local check.
    """
    from pathlib import Path

    if samples is None:
        samples = load_score_corpus()
    catalog = build_score_catalog(samples, mode=mode)
    # Register once on the (reliable) corpus, not the target dir's first file.
    calib = calibrate_score([s.image for s in samples], catalog)
    # Only score-block-sized (or full-frame) images — skip stray files (montages).
    x0, y0, x1, y1 = SCORE_BLOCK_ROI
    block_shape = (y1 - y0, x1 - x0)

    prev_max = -1
    viol = 0
    hist: dict[int, int] = defaultdict(int)
    first = last = -1
    violations: list[tuple[str, int, int]] = []
    files = [f for f in sorted(Path(frames_dir).glob("*.png"))
             if load_bgr(f).shape[:2] in (block_shape, (CANONICAL_H, CANONICAL_W))]
    for f in files:
        r = read_score(load_bgr(f), catalog, calib)
        hist[len(r.digits)] += 1
        if first < 0:
            first = r.value
        last = r.value
        if r.value < prev_max:
            viol += 1
            if len(violations) < 20:
                violations.append((f.name, prev_max, r.value))
        prev_max = max(prev_max, r.value)
    return ScoreMonotonicResult(
        n_frames=len(files), n_violations=viol, digit_hist=dict(sorted(hist.items())),
        first=first, last=last, violations=violations,
    )


@dataclass(frozen=True)
class Amp2pMonotonicResult:
    side: str
    n_frames: int
    n_violations: int           # reads that decreased vs the running max
    n_unreadable: int           # value is None (gated, misaligned, or off-grid)
    n_layout_unknown: int       # the grid table cannot describe what is on screen
    digit_hist: dict[int, int]  # powered-cell count → frame count
    first: int
    last: int
    worst_dist: float
    min_margin: float
    deltas: list[int]           # distinct non-zero frame-to-frame changes
    violations: list[tuple[str, int, int]]  # (filename, prev_max, read) — first few
    unreadable: list[tuple[str, str]]       # (filename, reason) — first few

    @property
    def clean_frac(self) -> float:
        return 1.0 - (self.n_violations / self.n_frames) if self.n_frames else 0.0


def amp2p_score_monotonic_eval(
    frames_dir, side: str = "right", samples: list[Sample] | None = None
) -> Amp2pMonotonicResult:
    """Label-free accuracy proxy over a whole extracted 2-player amp capture.

    A play's score only climbs, so any read that *decreases* vs the running max is a
    misread. Reads every `*.png` (sorted) with the bank from the committed corpus,
    registers once on the corpus, and reports violations, gate rejections, the
    powered-cell histogram and the distinct frame-to-frame deltas — real note/sustain
    scoring shows up there as small sustain steps plus multiples of 50. Captures are
    not committed, so this is a local check (see docs/journal.md for the reference
    numbers).
    """
    from pathlib import Path

    if samples is None:
        samples = load_amp2p_corpus()
    bank = amp2p.build_amp2p_bank(samples)
    # Register on the (reliable) corpus, not the target dir's first frame.
    ref = amp2p.build_reference(side)
    calib = amp2p.calibrate(side, [s.image for s in samples[:4]], ref, search=4)

    block_shape = amp2p.block_size(side)
    files = [f for f in sorted(Path(frames_dir).glob("*.png"))
             if load_bgr(f).shape[:2] in (block_shape, (CANONICAL_H, CANONICAL_W))]

    prev_max = -1
    viol = unread = unknown = 0
    hist: dict[int, int] = defaultdict(int)
    first = last = -1
    worst_dist = 0.0
    min_margin = float("inf")
    seq: list[int] = []
    violations: list[tuple[str, int, int]] = []
    unreadable: list[tuple[str, str]] = []
    for f in files:
        r = amp2p.read_amp2p_score(load_bgr(f), bank, calib, side)
        hist[r.n_cells] += 1
        if r.layout_unknown:
            unknown += 1
        if r.value is None:
            unread += 1
            if len(unreadable) < 20:
                unreadable.append((f.name, r.reason))
            continue
        worst_dist = max(worst_dist, r.dist)
        min_margin = min(min_margin, r.margin)
        seq.append(r.value)
        if first < 0:
            first = r.value
        last = r.value
        if r.value < prev_max:
            viol += 1
            if len(violations) < 20:
                violations.append((f.name, prev_max, r.value))
        prev_max = max(prev_max, r.value)
    return Amp2pMonotonicResult(
        side=side, n_frames=len(files), n_violations=viol, n_unreadable=unread,
        n_layout_unknown=unknown, digit_hist=dict(sorted(hist.items())),
        first=first, last=last, worst_dist=worst_dist,
        min_margin=0.0 if min_margin == float("inf") else min_margin,
        deltas=sorted({b - a for a, b in zip(seq, seq[1:]) if b != a}),
        violations=violations, unreadable=unreadable,
    )


# ─── Orchestration / reporting ─────────────────────────────────────────────────


def run_report(
    samples: list[Sample] | None = None,
    config: FingerprintConfig = FingerprintConfig(),
    sweep: bool = True,
) -> str:
    if samples is None:
        samples = load_corpus()
    lines: list[str] = []

    lines.append(f"corpus: {len(samples)} samples")
    counts = defaultdict(int)
    for s in samples:
        counts[s.screen_id] += 1
    lines.append("  classes: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    lines.append("")

    # Recommend thresholds on the chosen config first, then evaluate with them.
    rec = recommend_thresholds(samples, config)
    t_abs, t_margin = rec.rec_t_abs, rec.rec_t_margin
    loo = loo_eval(samples, config, t_abs=t_abs, t_margin=t_margin)

    lines.append(
        f"config: {config.cols}x{config.rows} grid, "
        f"samples/region={config.samples_per_region}, normalize={config.normalize} "
        f"(fp length {config.length}, {config.pixel_reads} pixel reads)"
    )
    fast, slow = hw_time_estimate_ms(config)
    lines.append(f"  est. SAM9X75 time/classification: {fast:.2f}-{slow:.2f} ms")
    dfast, dslow = hw_time_estimate_ms(FingerprintConfig(samples_per_region=None))
    lines.append(f"  (dense full-frame for reference: {dfast:.1f}-{dslow:.1f} ms)")
    lines.append("")

    lines.append("threshold recommendation:")
    lines.append(
        f"  in-class:  dist_max={rec.in_class_dist_max}  margin_min={rec.in_class_margin_min}"
    )
    lines.append(
        f"  impostor:  dist_min={rec.impostor_dist_min}  margin_max={rec.impostor_margin_max}"
    )
    lines.append(f"  -> t_abs={rec.rec_t_abs}, t_margin={rec.rec_t_margin}")
    lines.append(
        f"  impostor leak (unseen screens accepted): {rec.impostor_leak}/{rec.n_impostors}"
    )
    lines.append("")

    lines.append(
        f"leave-one-out: {loo.n_correct}/{loo.n_total} correct "
        f"({loo.accuracy:.1%}), {loo.n_unknown} unknown"
    )
    for sid in sorted(loo.per_class_acc):
        c, n = loo.per_class_acc[sid]
        flag = "  [single-sample: no LOO centroid]" if sid in loo.single_sample_classes else ""
        lines.append(f"  {sid:<20} {c}/{n}{flag}")
    # Confusion: only print off-diagonal (the interesting part).
    off = {k: v for k, v in loo.confusion.items() if k[0] != k[1]}
    if off:
        lines.append("  misclassifications (true -> pred):")
        for (t, p), v in sorted(off.items()):
            lines.append(f"    {t} -> {p}: {v}")
    lines.append("")

    rob = robustness_eval(samples, config, t_abs=t_abs, t_margin=t_margin)
    lines.append(f"robustness (analog-slop envelope): {rob.n_stable}/{rob.n_total} stable ({rob.stability:.1%})")
    for cat in sorted(rob.per_category):
        c, n = rob.per_category[cat]
        lines.append(f"  {cat:<12} {c}/{n}")
    lines.append("")

    # Static-list highlight reader (slice 2).
    sel = selection_eval(samples)
    sel_slop = selection_eval(samples, perturb_envelope=True)
    lines.append(
        f"selection reader (static lists): {sel.n_correct}/{sel.n_total} correct "
        f"({sel.accuracy:.1%}); under analog-slop: {sel_slop.accuracy:.1%}"
    )
    for sid in sorted(sel.per_screen):
        c, n = sel.per_screen[sid]
        lines.append(f"  {sid:<20} {c}/{n}")
    if sel.failures:
        lines.append("  clean-frame failures (file: true -> pred):")
        for fname, true_item, pred in sel.failures:
            lines.append(f"    {fname}: {true_item} -> {pred}")
    lines.append("")

    # song_select reader (slice 3): fixed-slot bitmap match against the 64 templates.
    song = song_eval(samples)
    lines.append(
        f"song_select reader: {song.n_song_ok}/{song.n_total} songs correct "
        f"({song.song_acc:.1%}); under analog-slop: {song.slop_acc:.1%}"
    )
    lines.append(
        f"  setlist read (bg colour): {song.n_setlist_ok}/{song.n_total} "
        f"({song.setlist_acc:.1%}); under slop: {song.setlist_slop_acc:.1%}; "
        f"worst nearest-song margin: {song.margin_min:.1f}"
    )
    for fname, true_song, pred in song.failures:
        lines.append(f"    {fname}: {true_song} -> {pred}")
    lines.append("")

    # in-song score reader (slice 5): per-digit glyph OCR of the open-ended value.
    score_samples = load_score_corpus()
    if score_samples:
        sc = score_eval(score_samples)
        lines.append(
            f"score reader (training): {sc.n_exact_ok}/{sc.n_total} exact "
            f"({sc.exact_acc:.1%}); per-digit {sc.n_digit_ok}/{sc.n_digit_total} "
            f"({sc.digit_acc:.1%}); LOO exact {sc.n_loo_exact_ok}/{sc.n_total} ({sc.loo_exact_acc:.1%})"
        )
        lines.append(
            f"  A2D slop (gain/offset/noise, fixed calib): {sc.a2d_ok}/{sc.a2d_total} "
            f"({sc.a2d_acc:.1%}); re-register on position shift: {sc.reg_ok}/{sc.reg_total} "
            f"({sc.reg_acc:.1%}); worst digit margin: {sc.margin_min:.1f}"
        )
        for fname, true_v, pred in sc.failures:
            lines.append(f"    {fname}: {true_v} -> {pred}")
    else:
        lines.append("score reader: (no score corpus found; skipped)")
    lines.append("")

    if sweep:
        lines.append("parameter sweep (LOO accuracy / robustness at recommended thresholds):")
        configs = [
            FingerprintConfig(cols=cols, rows=rows, samples_per_region=spr, normalize=norm)
            for (cols, rows) in ((8, 6), (12, 8), (16, 12))
            for spr in (5, None)
            for norm in (False, True)
        ]
        lines.append(f"  {'config':<34}{'LOO':>8}{'robust':>9}{'ms':>14}")
        for cfg in configs:
            r = recommend_thresholds(samples, cfg)
            lv = loo_eval(samples, cfg, t_abs=r.rec_t_abs, t_margin=r.rec_t_margin)
            rb = robustness_eval(samples, cfg, t_abs=r.rec_t_abs, t_margin=r.rec_t_margin)
            f2, s2 = hw_time_estimate_ms(cfg)
            label = (
                f"{cfg.cols}x{cfg.rows} spr={cfg.samples_per_region} norm={int(cfg.normalize)}"
            )
            lines.append(
                f"  {label:<34}{lv.accuracy:>7.1%}{rb.stability:>9.1%}{f'{f2:.2f}-{s2:.2f}':>14}"
            )

    return "\n".join(lines)
