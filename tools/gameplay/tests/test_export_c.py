from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

from gameplay.classifier import build_templates
from gameplay.evaluate import labelled_fps, recommend_thresholds
from gameplay.export_c import build_metadata_header
from gameplay.fingerprint import FingerprintConfig


def test_header_is_deterministic(corpus):
    assert build_metadata_header(corpus) == build_metadata_header(corpus)


def test_header_dimensions(corpus):
    h = build_metadata_header(corpus)
    assert "#define GP_N_SCREENS 19" in h
    assert "#define GP_N_MENUS 10" in h
    assert "#define GP_N_SONGS 70" in h
    assert "#define GP_FP_LEN 288" in h
    assert "#define GP_SONG_LEN 192" in h


def test_thresholds_match_artifacts(corpus):
    cfg = FingerprintConfig()
    rec = recommend_thresholds(corpus, cfg)
    h = build_metadata_header(corpus)
    t_abs = int(re.search(r"#define GP_CLS_T_ABS (\d+)", h).group(1))
    t_margin = int(re.search(r"#define GP_CLS_T_MARGIN (\d+)", h).group(1))
    assert (t_abs, t_margin) == (rec.rec_t_abs, rec.rec_t_margin)


def test_first_centroid_row_matches(corpus):
    cfg = FingerprintConfig()
    templates = build_templates(labelled_fps(corpus, cfg), cfg)
    expected = "{" + ",".join(str(int(v)) for v in templates.centroids[0]) + "}"
    assert expected in build_metadata_header(corpus)


@pytest.mark.skipif(shutil.which("cc") is None, reason="no C compiler")
def test_header_compiles(corpus, tmp_path: Path):
    (tmp_path / "gameplay_metadata.h").write_text(build_metadata_header(corpus))
    stub = tmp_path / "t.c"
    stub.write_text(
        '#include "gameplay_metadata.h"\n'
        "int main(void){return (int)gp_centroids[GP_N_SCREENS-1][0]"
        "+gp_menus[GP_N_MENUS-1].count"
        "+(int)gp_song_templates[GP_N_SONGS-1].vec[0]"
        "+(int)gp_setlist_warmth_threshold;}\n"
    )
    r = subprocess.run(
        ["cc", "-I", str(tmp_path), "-std=c11", "-Wall", "-Wextra",
         "-c", str(stub), "-o", str(tmp_path / "t.o")],
        capture_output=True, text=True,
    )
    assert r.returncode == 0, r.stderr
