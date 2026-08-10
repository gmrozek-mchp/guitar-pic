"""Cross-validate the firmware C classifier against the Python prototype.

The marvin port can't be built here (MPLAB/XC32 on Greg's side), but the *pure*
classification math lives in a FreeRTOS-free unit
(`firmware/marvin/default/src/game/gameplay_classify.c`) driven by the generated
`gameplay_metadata.h`. This test compiles that unit with `cc` into a tiny driver,
runs it on raw BGR dumps of real corpus frames, and asserts the C screen
decision matches `classify_image` from the prototype — proving the port (and the
exported metadata) reproduce the proven algorithm before it ever hits hardware.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest
from gameplay import amp2p as _amp2p

from gameplay.classifier import build_templates, classify_image
from gameplay.evaluate import labelled_fps, recommend_thresholds
from gameplay.export_c import build_metadata_header
from gameplay.fingerprint import CANONICAL_H, CANONICAL_W, FingerprintConfig
from gameplay.screens import UNKNOWN

_REPO = Path(__file__).resolve().parents[3]
_FW_SRC = _REPO / "firmware" / "marvin" / "default" / "src"
_UNKNOWN_IDX = 255

_DRIVER_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/gameplay_classify.h"
#include "game/gameplay_select.h"
#include "game/gameplay_score.h"
#include "game/gameplay_present.h"
#include "game/gameplay_amp2p.h"
#include "game/gameplay_endprobe.h"
#include "game/gameplay_metadata.h"
int main(int argc, char **argv) {
    if (argc < 5) return 2;
    int w = atoi(argv[2]), h = atoi(argv[3]);
    const char *mode = argv[4];
    long n = (long)w * h * 3;
    unsigned char *buf = malloc((size_t)n);
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(buf, 1, (size_t)n, f) != (size_t)n) return 3;
    fclose(f);
    if (strcmp(mode, "classify") == 0) {
        int32_t bd = 0, mg = 0;
        printf("%d\n", (int)gp_classify(buf, w, h, &bd, &mg));
    } else if (strcmp(mode, "select") == 0) {
        const gp_menu_layout_t *m = gp_menu_for_screen((uint8_t)atoi(argv[5]));
        printf("%d\n", m ? gp_read_selection(buf, w, h, m) : -1);
    } else if (strcmp(mode, "song") == 0) {
        gp_song_t s;
        gp_read_song(buf, w, h, &s);
        printf("%d %d\n", (int)s.setlist, (int)s.index);
    } else if (strcmp(mode, "score") == 0) {
        gp_score_t s;
        gp_read_score(buf, w, h, (uint8_t)atoi(argv[5]), &s);
        printf("%d\n", (int)s.value);
    } else if (strcmp(mode, "present") == 0) {
        int32_t sad[GP_N_PROBES];
        int scr = gp_present(buf, w, h, sad);
        printf("%d %d %d %d\n", scr, (int)sad[0], (int)sad[1], (int)sad[2]);
    } else if (strcmp(mode, "ready") == 0) {
        int32_t sad = 0;
        int r = gp_ready_p1_present(buf, w, h, &sad);
        printf("%d %d\n", r, (int)sad);
    } else if (strcmp(mode, "amp2p") == 0) {
        gp_amp2p_t a;
        gp_read_amp2p(buf, w, h, (uint8_t)atoi(argv[5]), &a);
        printf("%d %d %d %d %d\n", (int)a.value, (int)a.ncells,
               (int)a.layout_measured, (int)a.layout_w, (int)a.layout_pitch);
    } else if (strcmp(mode, "endprobe") == 0) {
        int16_t con[GP_END_N_BRIGHT];
        uint8_t anchor = 0;
        int hits = gp_end_probe(buf, w, h, con, &anchor);
        printf("%d %d", hits, (int)anchor);
        for (int k = 0; k < GP_END_N_BRIGHT; k++) printf(" %d", (int)con[k]);
        printf("\n");
    } else if (strcmp(mode, "mult") == 0) {
        printf("%d\n", gp_read_multiplier(buf, w, h));
    } else if (strcmp(mode, "streak") == 0) {
        gp_streak_raw_t r;
        gp_read_streak(buf, w, h, &r);
        printf("%d %d %d %d %d %d %d\n", (int)r.present,
               (int)r.digit[0], (int)r.digit[1], (int)r.digit[2],
               (int)r.known[0], (int)r.known[1], (int)r.known[2]);
    }
    return 0;
}
"""

pytestmark = pytest.mark.skipif(shutil.which("cc") is None, reason="no C compiler")


@pytest.fixture(scope="module")
def driver(tmp_path_factory):
    d = tmp_path_factory.mktemp("fwcls")
    # Use the same generated header the firmware compiles in.
    (_FW_SRC / "game" / "gameplay_metadata.h").write_text(
        build_metadata_header()
    ) if not (_FW_SRC / "game" / "gameplay_metadata.h").exists() else None
    (d / "driver.c").write_text(_DRIVER_C)
    exe = d / "driver"
    r = subprocess.run(
        ["cc", "-I", str(_FW_SRC), "-std=c11", "-O1",
         str(d / "driver.c"),
         str(_FW_SRC / "game" / "gameplay_classify.c"),
         str(_FW_SRC / "game" / "gameplay_select.c"),
         str(_FW_SRC / "game" / "gameplay_score.c"),
         str(_FW_SRC / "game" / "gameplay_present.c"),
         str(_FW_SRC / "game" / "gameplay_amp2p.c"),
         str(_FW_SRC / "game" / "gameplay_endprobe.c"),
         "-lm", "-o", str(exe)],
        capture_output=True, text=True,
    )
    assert r.returncode == 0, r.stderr
    return exe


def _c_run(exe: Path, image, tmp: Path, *mode_args: str) -> str:
    raw = tmp / "frame.bgr"
    raw.write_bytes(image.tobytes())
    out = subprocess.run(
        [str(exe), str(raw), str(CANONICAL_W), str(CANONICAL_H), *mode_args],
        capture_output=True, text=True, check=True,
    )
    return out.stdout.strip()


def _c_classify(exe: Path, image, tmp: Path) -> int:
    return int(_c_run(exe, image, tmp, "classify"))


def test_c_matches_python_on_corpus(corpus, driver, tmp_path):
    cfg = FingerprintConfig()
    templates = build_templates(labelled_fps(corpus, cfg), cfg)
    rec = recommend_thresholds(corpus, cfg)
    ids = list(templates.ids)

    # One frame per screen class + a few song_select variants (subprocess per frame).
    seen: set[str] = set()
    sample = []
    for s in corpus:
        if s.screen_id not in seen:
            seen.add(s.screen_id)
            sample.append(s)
    sample += [s for s in corpus if s.screen_id == "song_select"][:5]

    for s in sample:
        py = classify_image(s.image, templates, t_abs=rec.rec_t_abs, t_margin=rec.rec_t_margin)
        py_idx = _UNKNOWN_IDX if py.screen_id == UNKNOWN else ids.index(py.screen_id)
        c_idx = _c_classify(driver, s.image, tmp_path)
        assert c_idx == py_idx, f"{s.path.name}: C={c_idx} Python={py_idx}"


def test_c_selection_matches_python(corpus, driver, tmp_path):
    from gameplay.classifier import build_templates as _bt  # screen ids order
    from gameplay.highlight import build_selection_calibration, read_selection
    from gameplay.metadata import MENU_LAYOUTS, selected_item_from_filename

    cfg = FingerprintConfig()
    ids = list(_bt(labelled_fps(corpus, cfg), cfg).ids)
    calib = build_selection_calibration(corpus)
    for s in corpus:
        layout = MENU_LAYOUTS.get(s.screen_id)
        if layout is None or selected_item_from_filename(s.path.name) not in layout.items:
            continue
        py_cell = read_selection(s.image, layout, calib).index
        c_cell = int(_c_run(driver, s.image, tmp_path, "select", str(ids.index(s.screen_id))))
        assert c_cell == py_cell, f"{s.path.name}: C={c_cell} Python={py_cell}"


def test_c_song_matches_python(corpus, driver, tmp_path):
    from gameplay.metadata import song_from_filename
    from gameplay.songselect import build_song_catalog, read_song

    catalog = build_song_catalog(corpus)
    songs = [s for s in corpus if song_from_filename(s.path.name) is not None]
    for s in songs:  # all 64 — subprocess per frame
        r = read_song(s.image, catalog)
        py = (0 if r.setlist == "main" else 1, r.index)
        c = tuple(int(x) for x in _c_run(driver, s.image, tmp_path, "song").split())
        assert c == py, f"{s.path.name}: C={c} Python={py}"


def test_c_score_matches_python(score_corpus, driver, tmp_path):
    """gp_read_score (fixed band, mode training) == score.py read_score on all frames.

    Firmware v0 has no chrome registration, so compare against read_score with
    calibration=None (fixed band). The reader takes a full 720x480 frame, so feed
    the block embedded exactly as score.py does internally (_ensure_full_frame).
    """
    import numpy as np

    from gameplay.metadata import score_from_filename
    from gameplay.score import _ensure_full_frame, build_score_catalog, read_score

    catalog = build_score_catalog(score_corpus)
    for s in score_corpus:
        py = read_score(s.image, catalog, None).value
        full = np.ascontiguousarray(_ensure_full_frame(s.image))
        c = int(_c_run(driver, full, tmp_path, "score", "0"))  # GP_SCORE_MODE_TRAINING
        assert c == py, f"{s.path.name}: C={c} Python={py} (true {score_from_filename(s.path.name)[1]})"


def test_c_multiplier_matches_python(score_corpus, driver, tmp_path):
    """gp_read_multiplier == score.py read_multiplier on all corpus frames (colour-count)."""
    import numpy as np

    from gameplay.score import _ensure_full_frame, read_multiplier

    for s in score_corpus:
        py = read_multiplier(s.image)
        full = np.ascontiguousarray(_ensure_full_frame(s.image))
        c = int(_c_run(driver, full, tmp_path, "mult"))
        assert c == py, f"{s.path.name}: C={c} Python={py}"


@pytest.mark.skipif(
    _amp2p.mask_origin_mismatch() is not None,
    reason="gameplay_metadata.h still carries the pre-re-registration amp block/mask; "
           "re-export once the masks are re-painted at the new AMP2P_BLOCK",
)
def test_c_present_matches_python(corpus, driver, tmp_path):
    """gp_present == present.classify_present on every corpus frame.

    Asserts the gameplay-screen decision (in_song / in_song_2p / not-gameplay) matches
    exactly, and each probe's L1-per-masked-pixel matches within a small tolerance
    (C float32 + lroundf vs Python float64 + np.round differ by <1 in these units;
    the 2.9x margin means that never flips the decision)."""
    from gameplay.classifier import build_templates as _bt
    from gameplay.present import build_probes, classify_present, probe_sad

    cfg = FingerprintConfig()
    ids = list(_bt(labelled_fps(corpus, cfg), cfg).ids)
    probes = build_probes()
    keys = ("1p", "2pL", "2pR")

    for s in corpus:
        py = classify_present(s.image, probes)
        py_idx = _UNKNOWN_IDX if py.screen_id is None else ids.index(py.screen_id)
        out = _c_run(driver, s.image, tmp_path, "present").split()
        c_idx = int(out[0])
        assert c_idx == py_idx, f"{s.path.name}: C={c_idx} Python={py_idx}"
        c_sad = [int(x) / 1000.0 for x in out[1:4]]
        py_sad = [probe_sad(s.image, probes[k]) for k in keys]
        for k, cs, ps in zip(keys, c_sad, py_sad):
            assert abs(cs - ps) < 0.5, f"{s.path.name} {k}: C={cs:.2f} Python={ps:.2f}"


def test_c_streak_matches_python(streak_corpus, driver, tmp_path):
    """gp_read_streak (stateless raw read) == score.py read_streak on all corpus frames.

    Compares presence + per-wheel best-match digit + per-wheel confidence flag. The
    stateful tracker is exercised separately on the host (test_streak.py); this pins
    the CV/OCR half — the part that must reproduce bit-for-bit on the device.
    """
    import numpy as np

    from gameplay.score import _ensure_full_frame, build_streak_catalog, read_streak

    catalog = build_streak_catalog(streak_corpus)
    for s in streak_corpus:
        r = read_streak(s.image, catalog)
        py = (int(r.present), *r.digits, *(int(k) for k in r.known))
        full = np.ascontiguousarray(_ensure_full_frame(s.image))
        c = tuple(int(x) for x in _c_run(driver, full, tmp_path, "streak").split())
        # Integer coverage → C reproduces Python bit-for-bit: presence, every wheel's
        # digit (even unreadable ones), and every confidence flag must match exactly.
        assert c == py, f"{s.path.name}: C={c} Python={py}"


def test_c_ready_badge_matches_python(driver, tmp_path):
    """gp_ready_p1_present == ready.read_ready on every guitar_select_2p frame.

    The badge decision gates the 2-player guitar step, so the port has to agree with
    the host prototype on both the verdict and the SAD before it reaches hardware.
    """
    from gameplay.corpus import corpus_dir, load_bgr
    from gameplay.ready import build_ready_probe, read_ready

    probe = build_ready_probe()
    frames = sorted(corpus_dir().glob("guitar_select_2p__*.png"))
    assert frames, "no guitar_select_2p corpus frames"
    for f in frames:
        img = load_bgr(f)
        py_ready, py_sad = read_ready(img, probe)
        out = _c_run(driver, img, tmp_path, "ready").split()
        c_ready, c_sad = int(out[0]), int(out[1]) / 1000.0
        assert c_ready == int(py_ready), f"{f.name}: C={c_ready} Python={int(py_ready)}"
        assert abs(c_sad - py_sad) < 0.5, f"{f.name}: C sad={c_sad:.2f} Python={py_sad:.2f}"


# ─── 2-player amp score reader ────────────────────────────────────────────────
#
# The firmware reader must reproduce the host's *values*, not merely be close: the
# host is what was validated on ~30 000 real frames, so any divergence is a port bug.
# One trap is checked implicitly by the corpus pass below — the amp cell's coverage
# bbox tightens rows only, and column-cropping it (as the odometer cells legitimately
# do) collapses `1` into every other narrow glyph.

_AMP_SIDE = {"left": "1", "right": "2"}   # GP_AMP2P_SIDE_* == index into gp_probes[]


def _c_amp2p(driver, image, tmp_path, side):
    """(value, ncells, layout_measured, cell_w, pitch) from the C reader."""
    import numpy as np

    full = np.ascontiguousarray(_amp2p._ensure_full_frame(image, side))
    out = _c_run(driver, full, tmp_path, "amp2p", _AMP_SIDE[side]).split()
    return tuple(int(v) for v in out)


def _amp_skip():
    stale = _amp2p.mask_origin_mismatch()
    if stale is not None:
        return stale
    from gameplay.corpus import load_amp2p_corpus
    if not load_amp2p_corpus():
        return "no 2p amp digit corpus"
    return None


@pytest.mark.skipif(_amp_skip() is not None, reason=_amp_skip() or "")
def test_c_amp2p_matches_python_on_the_corpus(driver, tmp_path):
    """gp_read_amp2p == amp2p.read_amp2p_score on every labelled frame, both sides."""
    from gameplay.corpus import load_amp2p_corpus
    from gameplay.metadata import amp2p_score_from_filename

    samples = load_amp2p_corpus()
    bank = _amp2p.build_amp2p_bank(samples)
    for s in samples:
        side, value = amp2p_score_from_filename(s.path.name)
        py = _amp2p.read_amp2p_score(s.image, bank, _amp2p.AmpCalibration(side=side), side)
        c_val, c_n, c_meas, _cw, _pitch = _c_amp2p(driver, s.image, tmp_path, side)
        assert c_val == py.value == value, f"{s.path.name}: C={c_val} Python={py.value}"
        assert c_n == py.n_cells and c_meas == 1


@pytest.mark.skipif(_amp_skip() is not None, reason=_amp_skip() or "")
def test_c_amp2p_matches_python_on_the_screen_corpus(driver, tmp_path):
    """Same, on the 2p screen corpus — frames the digit bank never trained on."""
    from gameplay.corpus import corpus_dir, load_amp2p_corpus, load_bgr

    bank = _amp2p.build_amp2p_bank(load_amp2p_corpus())
    frames = sorted(corpus_dir().glob("in_song_2p__*.png"))
    assert frames, "no in_song_2p corpus frames"
    for f in frames:
        img = load_bgr(f)
        for side in _amp2p.SIDES:
            cal = _amp2p.calibrate(side, [img], _amp2p.build_reference(side), search=4)
            py = _amp2p.read_amp2p_score(img, bank, cal, side)
            if (cal.dx, cal.dy) != (0, 0):
                continue   # the C reader has no registration in v0 (see its header)
            c_val = _c_amp2p(driver, img, tmp_path, side)[0]
            expect = -1 if py.value is None else py.value
            assert c_val == expect, f"{f.name} {side}: C={c_val} Python={py.value}"


@pytest.mark.skipif(_amp_skip() is not None, reason=_amp_skip() or "")
def test_c_amp2p_reads_six_digits_and_flags_the_extrapolation(driver, tmp_path, relay_six):
    """The C side must also read a 6-digit strip *and* report the pitch as a guess.

    Reuses the host's re-lay helper, so the glyphs are real LED renderings at real
    brightness and only their positions are synthetic — the unmeasured part.
    """
    from gameplay.corpus import load_amp2p_corpus
    from gameplay.metadata import amp2p_score_from_filename

    samples = load_amp2p_corpus()
    bank = _amp2p.build_amp2p_bank(samples)
    wide = [s for s in samples if len(str(amp2p_score_from_filename(s.path.name)[1])) == 5]
    assert wide, "no 5-digit corpus frame to re-lay"
    for s in wide[:4]:
        side, value = amp2p_score_from_filename(s.path.name)
        block, expected = relay_six(s.image, side, value, (7, 8))
        py = _amp2p.read_amp2p_score(block, bank, _amp2p.AmpCalibration(side=side), side)
        assert py.value == expected, f"host: {py.value} != {expected}"
        c_val, c_n, c_meas, c_w, c_pitch = _c_amp2p(driver, block, tmp_path, side)
        assert c_val == expected, f"{s.path.name} {side}: C={c_val} Python={expected}"
        assert c_n == 6 and (c_w, c_pitch) == (7, 8)
        assert c_meas == 0, "an extrapolated layout must not claim to be measured"


def _c_endprobe(exe: Path, image, tmp: Path) -> tuple[int, int, list[int]]:
    parts = [int(v) for v in _c_run(exe, image, tmp, "endprobe").split()]
    return parts[0], parts[1], parts[2:]


def test_c_endprobe_matches_python(corpus, driver, tmp_path):
    """The frame-rate end-of-song probe must be byte-faithful, not merely agree.

    The C runs on every frame inside the actuation window, so a divergence would
    show up as either a missed veto (errant presses on the results menu) or a
    spurious one (dropped notes mid-song). Comparing the anchor level and every
    per-patch contrast — not just the hit count — pins the integer rounding that
    both sides floor-divide, which is where a port like this actually drifts.
    """
    from gameplay import endprobe as ep

    for s in corpus:
        if s.image.shape[:2] != (CANONICAL_H, CANONICAL_W):
            continue
        py = ep.read(s.image)
        c_hits, c_anchor, c_con = _c_endprobe(driver, s.image, tmp_path)
        assert c_anchor == py.anchor, f"{s.path.name}: anchor C={c_anchor} Py={py.anchor}"
        assert c_con == list(py.contrast), f"{s.path.name}: contrast C={c_con} Py={py.contrast}"
        assert c_hits == py.hits, f"{s.path.name}: hits C={c_hits} Py={py.hits}"


def test_c_endprobe_fires_on_end_screens_only(corpus, driver, tmp_path):
    from gameplay import endprobe as ep

    fired, missed = [], []
    for s in corpus:
        if s.image.shape[:2] != (CANONICAL_H, CANONICAL_W):
            continue
        hits = _c_endprobe(driver, s.image, tmp_path)[0]
        is_end_screen = s.screen_id in ("practice_end_menu", "faceoff_end_menu")
        if hits >= ep.K_HITS and not is_end_screen:
            fired.append(s.path.name)
        if hits < ep.K_HITS and is_end_screen:
            missed.append(s.path.name)
    assert not fired, f"C probe fired off an end screen: {fired}"
    assert not missed, f"C probe missed an end screen: {missed}"


def test_c_endprobe_rejects_a_non_canonical_frame(driver, tmp_path):
    import numpy as np

    small = np.zeros((240, 320, 3), dtype=np.uint8)
    raw = tmp_path / "small.bgr"
    raw.write_bytes(small.tobytes())
    out = subprocess.run(
        [str(driver), str(raw), "320", "240", "endprobe"],
        capture_output=True, text=True, check=True,
    )
    assert int(out.stdout.split()[0]) == -1
