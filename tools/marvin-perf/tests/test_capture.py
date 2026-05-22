"""Capture container: directory open/save, manifest synth from bare .bin."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from marvin_perf.capture import (
    BIN_NAME,
    MANIFEST_NAME,
    Capture,
    CaptureSource,
    Manifest,
    finalize_capture_dir,
    init_capture_dir,
    open_capture,
    read_manifest,
    synthesize_manifest,
    write_manifest,
)
from marvin_perf.records import Stage, TaskId

from .conftest import (
    build_detector_payload,
    build_drop_payload,
    build_session_payload,
    build_stamp_payload,
    build_task_highwater_payload,
    wrap_frame,
)


def _write_bin(path: Path, payloads: list[bytes]) -> None:
    with path.open("wb") as fh:
        for p in payloads:
            fh.write(wrap_frame(p))


# ─── Manifest serialization ──────────────────────────────────────────────────


def test_manifest_round_trip_through_json(tmp_path: Path) -> None:
    m = Manifest(
        schema_version=1,
        fw_git_short=0xCAFEBABE,
        timer_freq_hz=266_000_000,
        captured_at="2026-05-22T14:33:01Z",
        host_user="tester",
        source=CaptureSource(kind="serial", port="/dev/cu.usbmodem1101"),
        frame_epoch_first=1,
        frame_epoch_last=99,
        producer_capabilities=["Drop", "Session", "Stamp"],
        n_records=42,
    )
    write_manifest(tmp_path, m)
    loaded = read_manifest(tmp_path)
    assert loaded == m


def test_manifest_legacy_file_source(tmp_path: Path) -> None:
    m = Manifest(
        schema_version=None,
        fw_git_short=None,
        timer_freq_hz=None,
        captured_at="2026-05-22T14:33:01Z",
        host_user="tester",
        source=CaptureSource(kind="file", file=str(tmp_path / "legacy.bin")),
        frame_epoch_first=None,
        frame_epoch_last=None,
        producer_capabilities=[],
        n_records=0,
    )
    write_manifest(tmp_path, m)
    on_disk = json.loads((tmp_path / MANIFEST_NAME).read_text())
    assert on_disk["source"]["kind"] == "file"
    assert "port" not in on_disk["source"]
    assert read_manifest(tmp_path) == m


# ─── synthesize_manifest from a bare .bin ────────────────────────────────────


def test_synthesize_manifest_extracts_session_and_epoch_range(tmp_path: Path) -> None:
    bin_path = tmp_path / "perf.bin"
    _write_bin(
        bin_path,
        [
            build_session_payload(
                timer_freq_hz=266_000_000, schema_version=1, fw_git_short=0xDEADBEEF
            ),
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=10, ts_counter=0),
            build_detector_payload(frame_epoch=10),
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=42, ts_counter=100),
            build_drop_payload(dropped_state=1, dropped_patch=2, dropped_sink=3),
            build_task_highwater_payload(task_id=int(TaskId.PERF_DRAIN), words=200),
        ],
    )
    m = synthesize_manifest(bin_path)
    assert m.schema_version == 1
    assert m.timer_freq_hz == 266_000_000
    assert m.fw_git_short == 0xDEADBEEF
    assert m.frame_epoch_first == 10
    assert m.frame_epoch_last == 42
    assert m.n_records == 6
    # SESSION/DROP/TASK_HIGHWATER ride at frame_epoch=0 and must not poison the range.
    assert set(m.producer_capabilities) >= {
        "Session",
        "Stamp",
        "Detector",
        "Drop",
        "TaskHighwater",
    }


def test_synthesize_manifest_no_session_keeps_optional_fields_none(tmp_path: Path) -> None:
    bin_path = tmp_path / "midstream.bin"
    _write_bin(
        bin_path,
        [
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=5, ts_counter=0),
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=7, ts_counter=10),
        ],
    )
    m = synthesize_manifest(bin_path)
    assert m.schema_version is None
    assert m.timer_freq_hz is None
    assert m.fw_git_short is None
    assert m.frame_epoch_first == 5
    assert m.frame_epoch_last == 7
    assert m.n_records == 2


def test_synthesize_manifest_only_session_no_frame_records(tmp_path: Path) -> None:
    bin_path = tmp_path / "session_only.bin"
    _write_bin(bin_path, [build_session_payload()])
    m = synthesize_manifest(bin_path)
    assert m.schema_version == 1
    assert m.frame_epoch_first is None
    assert m.frame_epoch_last is None
    assert m.n_records == 1


# ─── open_capture: directory vs bare .bin ────────────────────────────────────


def test_open_capture_directory_with_manifest(tmp_path: Path) -> None:
    cap_dir = tmp_path / "cap1"
    init_capture_dir(cap_dir)
    _write_bin(
        cap_dir / BIN_NAME,
        [build_session_payload(), build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=1)],
    )
    finalize_capture_dir(
        cap_dir, source=CaptureSource(kind="serial", port="/dev/x")
    )
    cap = open_capture(cap_dir)
    assert isinstance(cap, Capture)
    assert cap.is_legacy_bin is False
    assert cap.bin_path == cap_dir / BIN_NAME
    assert cap.manifest.source.kind == "serial"
    assert cap.manifest.schema_version == 1


def test_open_capture_directory_missing_manifest_synthesizes(tmp_path: Path) -> None:
    cap_dir = tmp_path / "cap2"
    init_capture_dir(cap_dir)
    _write_bin(
        cap_dir / BIN_NAME,
        [build_session_payload(), build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=1)],
    )
    cap = open_capture(cap_dir)
    assert cap.is_legacy_bin is False
    assert cap.manifest.schema_version == 1
    # Synth-only — must not have written manifest.json to disk.
    assert not (cap_dir / MANIFEST_NAME).exists()


def test_open_capture_bare_bin_flagged_legacy(tmp_path: Path) -> None:
    bin_path = tmp_path / "raw.bin"
    _write_bin(bin_path, [build_session_payload()])
    cap = open_capture(bin_path)
    assert cap.is_legacy_bin is True
    assert cap.bin_path == bin_path
    assert cap.manifest.source.kind == "file"


def test_open_capture_missing_path_raises(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        open_capture(tmp_path / "does_not_exist")


def test_open_capture_dir_without_bin_raises(tmp_path: Path) -> None:
    cap_dir = tmp_path / "empty_cap"
    init_capture_dir(cap_dir)
    with pytest.raises(FileNotFoundError):
        open_capture(cap_dir)


# ─── finalize_capture_dir ────────────────────────────────────────────────────


def test_finalize_capture_dir_writes_manifest_and_returns_it(tmp_path: Path) -> None:
    cap_dir = tmp_path / "live"
    init_capture_dir(cap_dir)
    _write_bin(
        cap_dir / BIN_NAME,
        [
            build_session_payload(timer_freq_hz=12_345_678),
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=1),
            build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=2),
        ],
    )
    src = CaptureSource(kind="serial", port="/dev/cu.usbmodemX")
    m = finalize_capture_dir(cap_dir, source=src, captured_at="2026-05-22T00:00:00Z")
    assert (cap_dir / MANIFEST_NAME).exists()
    assert m.timer_freq_hz == 12_345_678
    assert m.captured_at == "2026-05-22T00:00:00Z"
    assert m.source == src
    # Round-trip through disk.
    on_disk = read_manifest(cap_dir)
    assert on_disk == m
