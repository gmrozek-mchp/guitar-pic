"""Capture container: directory holding `manifest.json` + `perf.bin`.

A capture is a directory. The bin is byte-for-byte identical to today's raw
serial dump — `decode` and `summarize` of just the bin still work. The
sidecar manifest carries metadata expensive to recompute on every viewer load
plus a forward-compatible slot for `video.mp4` when full HDMI lands.

Pure module: no FastAPI, no Pillow. Imports only the existing decode stack.
"""

from __future__ import annotations

import getpass
import json
import os
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .decode import Record, decode_record
from .framing import FrameStats, iter_frames
from .records import Drop, Session, TaskHighwater, TaskRuntime
from .transport import FileSource


MANIFEST_NAME = "manifest.json"
BIN_NAME = "perf.bin"


@dataclass(frozen=True)
class CaptureSource:
    kind: str  # "serial" | "file"
    port: str | None = None
    file: str | None = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"kind": self.kind}
        if self.port is not None:
            d["port"] = self.port
        if self.file is not None:
            d["file"] = self.file
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "CaptureSource":
        return cls(
            kind=str(d.get("kind", "file")),
            port=d.get("port"),
            file=d.get("file"),
        )


@dataclass(frozen=True)
class Manifest:
    schema_version: int | None
    fw_git_short: int | None
    timer_freq_hz: int | None
    captured_at: str
    host_user: str
    source: CaptureSource
    frame_epoch_first: int | None
    frame_epoch_last: int | None
    producer_capabilities: list[str]
    n_records: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": self.schema_version,
            "fw_git_short": self.fw_git_short,
            "timer_freq_hz": self.timer_freq_hz,
            "captured_at": self.captured_at,
            "host_user": self.host_user,
            "source": self.source.to_dict(),
            "frame_epoch_first": self.frame_epoch_first,
            "frame_epoch_last": self.frame_epoch_last,
            "producer_capabilities": list(self.producer_capabilities),
            "n_records": self.n_records,
        }

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Manifest":
        return cls(
            schema_version=d.get("schema_version"),
            fw_git_short=d.get("fw_git_short"),
            timer_freq_hz=d.get("timer_freq_hz"),
            captured_at=str(d.get("captured_at", "")),
            host_user=str(d.get("host_user", "")),
            source=CaptureSource.from_dict(d.get("source", {"kind": "file"})),
            frame_epoch_first=d.get("frame_epoch_first"),
            frame_epoch_last=d.get("frame_epoch_last"),
            producer_capabilities=list(d.get("producer_capabilities", [])),
            n_records=int(d.get("n_records", 0)),
        )


def _now_utc_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _host_user() -> str:
    try:
        return getpass.getuser()
    except (KeyError, OSError):
        return os.environ.get("USER", "unknown")


def synthesize_manifest(
    bin_path: Path,
    *,
    source: CaptureSource | None = None,
    captured_at: str | None = None,
) -> Manifest:
    """Scan a `perf.bin` end-to-end and derive a minimal manifest.

    Used both to build the sidecar when finalizing a fresh capture and to give
    the viewer a usable manifest when opening a bare legacy `.bin`.
    """
    schema_version: int | None = None
    fw_git_short: int | None = None
    timer_freq_hz: int | None = None
    epoch_first: int | None = None
    epoch_last: int | None = None
    types: set[str] = set()
    n_records = 0

    stats = FrameStats()
    with FileSource(bin_path) as src:
        for frame in iter_frames(src, stats):
            try:
                rec = decode_record(frame.payload)
            except Exception:
                continue
            n_records += 1
            types.add(type(rec).__name__)
            if isinstance(rec, Session) and schema_version is None:
                schema_version = rec.schema_version
                fw_git_short = rec.fw_git_short
                timer_freq_hz = rec.timer_freq_hz
            # frame_epoch == 0 records (SESSION/DROP/TASK_HIGHWATER/TASK_RUNTIME)
            # are not frame-tied — skip them when bounding the epoch range.
            if isinstance(rec, (Session, Drop, TaskHighwater, TaskRuntime)):
                continue
            ep = rec.hdr.frame_epoch
            if epoch_first is None or ep < epoch_first:
                epoch_first = ep
            if epoch_last is None or ep > epoch_last:
                epoch_last = ep

    return Manifest(
        schema_version=schema_version,
        fw_git_short=fw_git_short,
        timer_freq_hz=timer_freq_hz,
        captured_at=captured_at or _now_utc_iso(),
        host_user=_host_user(),
        source=source or CaptureSource(kind="file", file=str(bin_path)),
        frame_epoch_first=epoch_first,
        frame_epoch_last=epoch_last,
        producer_capabilities=sorted(types),
        n_records=n_records,
    )


def write_manifest(dir_path: Path, manifest: Manifest) -> Path:
    out = dir_path / MANIFEST_NAME
    out.write_text(json.dumps(manifest.to_dict(), indent=2) + "\n")
    return out


def read_manifest(dir_path: Path) -> Manifest:
    return Manifest.from_dict(json.loads((dir_path / MANIFEST_NAME).read_text()))


@dataclass(frozen=True)
class Capture:
    """A loaded capture: paths + manifest. Records are decoded on demand."""

    path: Path
    bin_path: Path
    manifest: Manifest
    is_legacy_bin: bool  # True if opened from a bare .bin (no on-disk manifest)


def open_capture(path: str | Path) -> Capture:
    """Open a capture directory or a bare legacy `.bin` file.

    For a directory, manifest.json is read if present; otherwise it is
    synthesized from the bin (and not written back). For a bare file, the
    manifest is always synthesized and the capture is flagged as legacy.
    """
    p = Path(path)
    if p.is_dir():
        bin_path = p / BIN_NAME
        if not bin_path.exists():
            raise FileNotFoundError(f"capture directory missing {BIN_NAME}: {p}")
        manifest_path = p / MANIFEST_NAME
        if manifest_path.exists():
            manifest = read_manifest(p)
        else:
            manifest = synthesize_manifest(bin_path)
        return Capture(path=p, bin_path=bin_path, manifest=manifest, is_legacy_bin=False)
    if p.is_file():
        manifest = synthesize_manifest(
            p, source=CaptureSource(kind="file", file=str(p))
        )
        return Capture(path=p.parent, bin_path=p, manifest=manifest, is_legacy_bin=True)
    raise FileNotFoundError(f"no such capture path: {p}")


def init_capture_dir(
    dir_path: str | Path,
    *,
    exist_ok: bool = False,
) -> Path:
    """Create an empty capture directory ready to receive `perf.bin`.

    Returns the absolute directory path. `manifest.json` is not written here —
    it is finalized by `finalize_capture_dir` after the bin is closed.
    """
    p = Path(dir_path)
    p.mkdir(parents=True, exist_ok=exist_ok)
    return p


def finalize_capture_dir(
    dir_path: str | Path,
    *,
    source: CaptureSource,
    captured_at: str | None = None,
) -> Manifest:
    """Scan the just-written bin, write manifest.json, return the manifest."""
    p = Path(dir_path)
    bin_path = p / BIN_NAME
    manifest = synthesize_manifest(
        bin_path, source=source, captured_at=captured_at or _now_utc_iso()
    )
    write_manifest(p, manifest)
    return manifest
