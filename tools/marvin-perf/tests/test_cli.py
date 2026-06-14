"""CLI argument parsing — record-type mask spec resolution."""

from __future__ import annotations

import argparse
from pathlib import Path

import pytest

from marvin_perf.cli import _resolve_snapshot_out, parse_types_arg
from marvin_perf.records import RecordType


def test_types_none_returns_none() -> None:
    assert parse_types_arg(None) is None


def test_types_flag_parses() -> None:
    mask = parse_types_arg("STAMP,SESSION")
    assert mask == (1 << RecordType.SESSION) | (1 << RecordType.STAMP)


def test_types_case_insensitive_and_whitespace() -> None:
    assert parse_types_arg("stamp, session") == parse_types_arg("STAMP,SESSION")


def test_types_all_token() -> None:
    assert parse_types_arg("ALL") == 0xFFFFFFFF


def test_types_min_token() -> None:
    expected = (1 << RecordType.SESSION) | (1 << RecordType.DROP)
    assert parse_types_arg("MIN") == expected


def test_types_min_combines_with_more() -> None:
    expected = (1 << RecordType.SESSION) | (1 << RecordType.DROP) | (1 << RecordType.STAMP)
    assert parse_types_arg("MIN,STAMP") == expected


def test_types_unknown_raises() -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        parse_types_arg("BOGUS")


# ─── Snapshot --out resolution ───────────────────────────────────────────────


def test_snapshot_out_explicit_png_used_verbatim(tmp_path: Path) -> None:
    out = tmp_path / "sub" / "screen.png"
    assert _resolve_snapshot_out(str(out)) == out
    assert out.parent.is_dir()  # parent created


def test_snapshot_out_directory_auto_increments(tmp_path: Path) -> None:
    d = tmp_path / "shots"
    first = _resolve_snapshot_out(str(d))
    assert first == d / "snapshot-0001.png"
    first.write_bytes(b"")  # simulate a saved capture
    second = _resolve_snapshot_out(str(d))
    assert second == d / "snapshot-0002.png"


def test_snapshot_out_default_dir(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(tmp_path)
    assert _resolve_snapshot_out(None) == Path("snapshots") / "snapshot-0001.png"


def test_snapshot_out_increment_skips_gaps(tmp_path: Path) -> None:
    d = tmp_path / "shots"
    d.mkdir()
    (d / "snapshot-0007.png").write_bytes(b"")
    (d / "snapshot-0003.png").write_bytes(b"")
    assert _resolve_snapshot_out(str(d)) == d / "snapshot-0008.png"  # max + 1
