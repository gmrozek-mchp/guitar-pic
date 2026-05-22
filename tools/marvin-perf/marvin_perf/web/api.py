"""FastAPI routes — thin wrapper over the pure decode/analyze/render stack.

Phase 1 covers offline review: open / manifest / summary / records / strip /
health / rtos. Live WebSocket + serial-reader thread land in Phase 2.

Capture loading is memoized by (resolved-path, mtime) so re-opening the same
capture is a hash lookup. The pump that walks `perf.bin` once and indexes
records by `frame_epoch` lives in `_LoadedCapture` so the per-request endpoints
can be O(1) lookups against pre-built dicts.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException, Query
from fastapi.responses import Response
from pydantic import BaseModel

from ..analyze import (
    SchemaVersionMismatch,
    check_frame_epoch_monotonic,
    check_schema,
    check_video_publish_cadence,
    compute_drops,
    compute_hwm,
    compute_latencies,
    find_session,
)
from ..capture import Capture, Manifest, open_capture
from ..decode import Record, decode_record
from ..framing import FrameStats, iter_frames
from ..records import (
    Drop,
    Patch,
    Session,
    Stage,
    Stamp,
    TaskHighwater,
    TaskId,
)
from ..transport import FileSource
from .render import StripRenderError, render_strip_png


# ─── Loaded-capture model ────────────────────────────────────────────────────


@dataclass
class _LoadedCapture:
    """A capture pulled fully into memory + lookup indexes.

    Phase-1 captures are MB-scale (state-only at ~9 KB/s × minutes). Strips
    arrive in Phase 2 and bump that to ~10 MB/min, still comfortable to keep
    in RAM for the offline-review use case.
    """

    capture: Capture
    records: list[Record]
    framing_stats: FrameStats
    by_epoch: dict[int, list[int]] = field(default_factory=dict)  # epoch → record indexes

    @property
    def manifest(self) -> Manifest:
        return self.capture.manifest

    @property
    def timer_freq_hz(self) -> int:
        s = find_session(self.records)
        return s.timer_freq_hz if s else 0

    @classmethod
    def load(cls, path: str | Path) -> "_LoadedCapture":
        capture = open_capture(path)
        records: list[Record] = []
        stats = FrameStats()
        with FileSource(capture.bin_path) as src:
            for frame in iter_frames(src, stats):
                try:
                    records.append(decode_record(frame.payload))
                except Exception:
                    continue

        by_epoch: dict[int, list[int]] = defaultdict(list)
        for i, rec in enumerate(records):
            ep = rec.hdr.frame_epoch  # type: ignore[union-attr]
            by_epoch[ep].append(i)

        return cls(
            capture=capture,
            records=records,
            framing_stats=stats,
            by_epoch=dict(by_epoch),
        )


class _Registry:
    """Module-level cache: (resolved-path, mtime) → _LoadedCapture."""

    def __init__(self) -> None:
        self._by_id: dict[str, _LoadedCapture] = {}

    def open(self, path: str | Path) -> tuple[str, _LoadedCapture]:
        p = Path(path).resolve()
        # Use the bin's mtime so editing manifest.json doesn't force a reload,
        # but appending bytes (live capture, future) does.
        bin_path = p / "perf.bin" if p.is_dir() else p
        mtime = bin_path.stat().st_mtime if bin_path.exists() else 0.0
        capture_id = f"{p}@{mtime}"
        loaded = self._by_id.get(capture_id)
        if loaded is None:
            loaded = _LoadedCapture.load(p)
            self._by_id[capture_id] = loaded
        return capture_id, loaded

    def get(self, capture_id: str) -> _LoadedCapture:
        loaded = self._by_id.get(capture_id)
        if loaded is None:
            raise HTTPException(404, f"capture not loaded: {capture_id}")
        return loaded


_REGISTRY = _Registry()
_PRELOADED_ID: str | None = None


def _preload(path: str | Path) -> None:
    """Eagerly load a capture passed via `marvin-perf serve --capture PATH`."""
    global _PRELOADED_ID
    capture_id, _ = _REGISTRY.open(path)
    _PRELOADED_ID = capture_id


# ─── Pydantic request/response models ────────────────────────────────────────


class OpenRequest(BaseModel):
    path: str


class OpenResponse(BaseModel):
    capture_id: str
    manifest: dict[str, Any]


# ─── Routes ──────────────────────────────────────────────────────────────────


router = APIRouter(prefix="/api")


@router.post("/capture/open", response_model=OpenResponse)
def capture_open(req: OpenRequest) -> OpenResponse:
    try:
        capture_id, loaded = _REGISTRY.open(req.path)
    except FileNotFoundError as e:
        raise HTTPException(404, str(e))
    return OpenResponse(capture_id=capture_id, manifest=loaded.manifest.to_dict())


@router.get("/preloaded")
def preloaded() -> dict[str, Any]:
    """Capture loaded by `serve --capture PATH`. Returns null id if none."""
    if _PRELOADED_ID is None:
        return {"capture_id": None, "manifest": None}
    loaded = _REGISTRY.get(_PRELOADED_ID)
    return {"capture_id": _PRELOADED_ID, "manifest": loaded.manifest.to_dict()}


@router.get("/capture/{capture_id:path}/manifest")
def capture_manifest(capture_id: str) -> dict[str, Any]:
    return _REGISTRY.get(capture_id).manifest.to_dict()


@router.get("/capture/{capture_id:path}/summary")
def capture_summary(capture_id: str) -> dict[str, Any]:
    loaded = _REGISTRY.get(capture_id)
    session = find_session(loaded.records)
    try:
        check_schema(session)
        schema_ok = True
        schema_msg = None
    except SchemaVersionMismatch as e:
        schema_ok = False
        schema_msg = str(e)

    timer_freq_hz = loaded.timer_freq_hz
    drops = compute_drops(loaded.records)
    hwm = compute_hwm(loaded.records)
    latencies = compute_latencies(loaded.records, timer_freq_hz)
    warns = check_frame_epoch_monotonic(loaded.records) + check_video_publish_cadence(
        loaded.records, timer_freq_hz
    )

    type_counts: dict[str, int] = defaultdict(int)
    for rec in loaded.records:
        type_counts[type(rec).__name__] += 1

    return {
        "schema_ok": schema_ok,
        "schema_message": schema_msg,
        "timer_freq_hz": timer_freq_hz,
        "framing": {
            "frames_ok": loaded.framing_stats.frames_ok,
            "bytes_resync_dropped": loaded.framing_stats.bytes_resync_dropped,
            "crc_mismatches": loaded.framing_stats.crc_mismatches,
            "bad_lengths": loaded.framing_stats.bad_lengths,
        },
        "record_type_counts": dict(type_counts),
        "drops": {
            "n_drop_records": drops.n_drop_records,
            "final_state": drops.final_state,
            "final_patch": drops.final_patch,
            "final_sink_bytes": drops.final_sink_bytes,
            "since_session_state": drops.since_session_state,
            "since_session_patch": drops.since_session_patch,
            "since_session_sink_bytes": drops.since_session_sink_bytes,
            "max_state_delta": drops.max_state_delta,
            "max_patch_delta": drops.max_patch_delta,
            "max_sink_bytes_delta": drops.max_sink_bytes_delta,
        },
        "hwm": {
            int(task_id): {
                "task_name": series.task_name,
                "min_words": series.min_words,
                "samples": series.samples,
            }
            for task_id, series in hwm.items()
        },
        "latencies": [
            {
                "pair": [h.pair[0].name, h.pair[1].name],
                "n": h.n,
                "p50": h.p50 if h.n else None,
                "p95": h.p95 if h.n else None,
                "p99": h.p99 if h.n else None,
                "max": h.max if h.n else None,
            }
            for h in latencies
        ],
        "warnings": [{"kind": w.kind, "message": w.message} for w in warns],
    }


@router.get("/capture/{capture_id:path}/records")
def capture_records(
    capture_id: str,
    from_epoch: int | None = Query(None, alias="from"),
    to_epoch: int | None = Query(None, alias="to"),
    types: str | None = Query(
        None, description="Comma-separated record-type names (e.g. 'Stamp,Drop')"
    ),
    limit: int = Query(5000, ge=1, le=100_000),
    offset: int = Query(0, ge=0),
) -> dict[str, Any]:
    loaded = _REGISTRY.get(capture_id)
    type_filter = set(types.split(",")) if types else None

    out: list[dict[str, Any]] = []
    skipped = 0
    for rec in loaded.records:
        ep = rec.hdr.frame_epoch  # type: ignore[union-attr]
        if from_epoch is not None and ep < from_epoch:
            continue
        if to_epoch is not None and ep > to_epoch:
            continue
        type_name = type(rec).__name__
        if type_filter is not None and type_name not in type_filter:
            continue
        if skipped < offset:
            skipped += 1
            continue
        if len(out) >= limit:
            break
        out.append(_record_to_dict(rec))

    return {"records": out, "limit": limit, "offset": offset, "returned": len(out)}


@router.get("/capture/{capture_id:path}/strip/{epoch}/{kind}.png")
def capture_strip_png(capture_id: str, epoch: int, kind: str) -> Response:
    """Render the strip at (epoch, kind) as a PNG.

    Phase 1: PATCH records (v1 schema) carry per-fret 5×5 thumbnails — there
    are no STRIP records yet. Returning 404 here is the correct, forward-
    compatible behavior; Phase 2 wires v2 STRIP records to the same lookup.
    """
    loaded = _REGISTRY.get(capture_id)
    indexes = loaded.by_epoch.get(epoch, [])
    for i in indexes:
        rec = loaded.records[i]
        # v2 STRIP record is wired via decode dispatch; until then there is
        # nothing to render. We still type-check defensively so the path is
        # ready when v2 lands.
        strip_kind = getattr(rec, "kind_name", None)
        if strip_kind == kind:
            try:
                png = render_strip_png(
                    rec.bgr,  # type: ignore[attr-defined]
                    w=rec.w,  # type: ignore[attr-defined]
                    h=rec.h,  # type: ignore[attr-defined]
                )
            except StripRenderError as e:
                raise HTTPException(500, str(e))
            return Response(
                content=png,
                media_type="image/png",
                headers={
                    "Cache-Control": "public, max-age=31536000, immutable",
                    "ETag": f'"{capture_id}:{epoch}:{kind}"',
                },
            )
    raise HTTPException(404, f"no strip for epoch={epoch} kind={kind}")


@router.get("/capture/{capture_id:path}/health")
def capture_health(capture_id: str) -> dict[str, Any]:
    """Bandwidth-health snapshot — Phase 1 returns what's measurable today.

    Strip-rate / strip-queue metrics arrive with v2; this endpoint reports
    them as null until then so the frontend can render the same widget shape
    for v1 and v2 captures.
    """
    loaded = _REGISTRY.get(capture_id)
    drops = compute_drops(loaded.records)
    return {
        "framing": {
            "frames_ok": loaded.framing_stats.frames_ok,
            "bytes_resync_dropped": loaded.framing_stats.bytes_resync_dropped,
            "crc_mismatches": loaded.framing_stats.crc_mismatches,
            "bad_lengths": loaded.framing_stats.bad_lengths,
        },
        "drops_since_session": {
            "state_records": drops.since_session_state,
            "patch_records": drops.since_session_patch,
            "sink_bytes": drops.since_session_sink_bytes,
        },
        "drops_max_delta": {
            "state_records": drops.max_state_delta,
            "patch_records": drops.max_patch_delta,
            "sink_bytes": drops.max_sink_bytes_delta,
        },
        # Phase-2 fields wired here when v2 schema lands:
        "strip_rate_per_s": None,
        "frame_epoch_gaps": None,
        "cdc_write_complete_p95_us": None,
        "strip_q_depth_max": None,
    }


@router.get("/capture/{capture_id:path}/rtos")
def capture_rtos(capture_id: str) -> dict[str, Any]:
    """Per-task RTOS health.

    Phase 1: HWM trend only (TASK_HIGHWATER records). CPU% / state histogram
    arrive with TASK_RUNTIME at v2.
    """
    loaded = _REGISTRY.get(capture_id)
    hwm = compute_hwm(loaded.records)
    tasks = []
    for task_id, series in sorted(hwm.items()):
        first = series.samples[0][1] if series.samples else None
        last = series.samples[-1][1] if series.samples else None
        tasks.append({
            "task_id": int(task_id),
            "task_name": series.task_name,
            "hwm_words": {
                "first": first,
                "last": last,
                "min": series.min_words if series.samples else None,
                "samples": series.samples,
            },
            # Phase-2 fields:
            "cpu_pct": None,
            "state_histogram": None,
            "priority_history": None,
            "warnings": [],
        })
    any_below_64 = any(
        t["hwm_words"]["min"] is not None and t["hwm_words"]["min"] < 64 for t in tasks
    )
    any_below_32 = any(
        t["hwm_words"]["min"] is not None and t["hwm_words"]["min"] < 32 for t in tasks
    )
    return {
        "tasks": tasks,
        "totals": {
            "any_task_below_64_words": any_below_64,
            "any_task_below_32_words": any_below_32,
            "cpu_pct_idle": None,  # Phase 2
        },
    }


# ─── Record → JSON ───────────────────────────────────────────────────────────


def _record_to_dict(rec: Record) -> dict[str, Any]:
    """Compact, JSON-friendly view of a record. Avoid sending raw pixel bytes
    over JSON — strips travel out as PNG via the dedicated endpoint."""
    hdr = rec.hdr  # type: ignore[union-attr]
    base: dict[str, Any] = {
        "type": type(rec).__name__,
        "frame_epoch": hdr.frame_epoch,
        "ts_counter": hdr.ts_counter,
        "flags": hdr.flags,
    }
    if isinstance(rec, Session):
        base.update(
            timer_freq_hz=rec.timer_freq_hz,
            schema_version=rec.schema_version,
            fw_git_short=rec.fw_git_short,
        )
    elif isinstance(rec, Stamp):
        try:
            base["stage"] = Stage(rec.stage_id).name
        except ValueError:
            base["stage"] = f"stage_{rec.stage_id:#04x}"
        base["stage_id"] = rec.stage_id
        base["aux"] = rec.aux
    elif isinstance(rec, Drop):
        base.update(
            dropped_state=rec.dropped_state,
            dropped_patch=rec.dropped_patch,
            dropped_sink=rec.dropped_sink,
        )
    elif isinstance(rec, TaskHighwater):
        try:
            base["task_name"] = TaskId(rec.task_id).name
        except ValueError:
            base["task_name"] = f"task_{rec.task_id}"
        base["task_id"] = rec.task_id
        base["words"] = rec.words
    elif isinstance(rec, Patch):
        base["frame_w"] = rec.frame_w
        base["frame_h"] = rec.frame_h
        # Pixels travel via the strip PNG endpoint — don't inline them.
    else:
        # Detector, Timing, UnknownRecord — keep generic field projection.
        for fname in getattr(rec, "__dataclass_fields__", {}):
            if fname == "hdr":
                continue
            v = getattr(rec, fname)
            if isinstance(v, (bytes, bytearray)):
                base[fname] = f"<{len(v)} B>"
            elif isinstance(v, tuple):
                base[fname] = list(v)
            else:
                base[fname] = v
    return base
