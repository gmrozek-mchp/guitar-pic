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
