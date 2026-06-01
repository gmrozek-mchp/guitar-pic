"""FastAPI endpoint tests. Skipped if viewer dep group not installed."""

from __future__ import annotations

from pathlib import Path

import pytest

pytest.importorskip("fastapi")
pytest.importorskip("PIL")

from fastapi.testclient import TestClient

from marvin_perf.capture import (
    BIN_NAME,
    CaptureSource,
    finalize_capture_dir,
    init_capture_dir,
)
from marvin_perf.records import Stage, TaskId
from marvin_perf.web import api as api_module
from marvin_perf.web.server import build_app

from .conftest import (
    build_drop_payload,
    build_session_payload,
    build_stamp_payload,
    build_task_highwater_payload,
    build_task_runtime_payload,
    wrap_frame,
)


@pytest.fixture(autouse=True)
def _reset_registry():
    """Each test gets a fresh capture registry."""
    api_module._REGISTRY = api_module._Registry()
    yield


@pytest.fixture
def client() -> TestClient:
    return TestClient(build_app())


def _populated_capture(tmp_path: Path) -> Path:
    cap_dir = tmp_path / "cap"
    init_capture_dir(cap_dir)
    payloads = [
        build_session_payload(timer_freq_hz=1_000_000, schema_version=3),
        build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=1, ts_counter=0),
        build_stamp_payload(stage=Stage.VIDEO_PUBLISH, frame_epoch=1, ts_counter=500),
        build_stamp_payload(stage=Stage.ISC_IRQ, frame_epoch=2, ts_counter=16_667),
        build_drop_payload(dropped_state=0, dropped_strip=0, dropped_sink=128),
        build_task_highwater_payload(task_id=int(TaskId.PERF_DRAIN), words=200),
        build_task_highwater_payload(task_id=int(TaskId.VIDEO), words=300),
    ]
    with (cap_dir / BIN_NAME).open("wb") as fh:
        for p in payloads:
            fh.write(wrap_frame(p))
    finalize_capture_dir(cap_dir, source=CaptureSource(kind="file", file=str(cap_dir)))
    return cap_dir


# ─── /api/capture/open ───────────────────────────────────────────────────────


def test_open_returns_id_and_manifest(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    resp = client.post("/api/capture/open", json={"path": str(cap_dir)})
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert "capture_id" in body
    m = body["manifest"]
    assert m["schema_version"] == 3
    assert m["timer_freq_hz"] == 1_000_000
    assert m["frame_epoch_first"] == 1
    assert m["frame_epoch_last"] == 2
    assert "Stamp" in m["producer_capabilities"]
    assert "TaskHighwater" in m["producer_capabilities"]


def test_open_missing_path_returns_404(client: TestClient, tmp_path: Path) -> None:
    resp = client.post("/api/capture/open", json={"path": str(tmp_path / "nope")})
    assert resp.status_code == 404


# ─── /manifest ───────────────────────────────────────────────────────────────


def test_manifest_round_trip(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    open_resp = client.post("/api/capture/open", json={"path": str(cap_dir)})
    cid = open_resp.json()["capture_id"]
    resp = client.get(f"/api/capture/{cid}/manifest")
    assert resp.status_code == 200
    assert resp.json()["schema_version"] == 3


# ─── /summary ────────────────────────────────────────────────────────────────


def test_summary_reports_record_counts_and_drops(
    client: TestClient, tmp_path: Path
) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    resp = client.get(f"/api/capture/{cid}/summary")
    assert resp.status_code == 200
    s = resp.json()
    assert s["schema_ok"] is True
    assert s["timer_freq_hz"] == 1_000_000
    assert s["record_type_counts"]["Stamp"] == 3
    assert s["record_type_counts"]["TaskHighwater"] == 2
    assert s["drops"]["n_drop_records"] == 1
    # Latency entry for ISC_IRQ → VIDEO_PUBLISH should have one sample at 500 µs.
    pair = next(
        x for x in s["latencies"]
        if x["pair"] == ["ISC_IRQ", "VIDEO_PUBLISH"]
    )
    assert pair["n"] == 1
    assert pair["p50"] == 500.0


# ─── /records pagination + filters ───────────────────────────────────────────


def test_records_filter_by_type(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    resp = client.get(
        f"/api/capture/{cid}/records",
        params={"types": "TaskHighwater"},
    )
    body = resp.json()
    assert body["returned"] == 2
    assert {r["type"] for r in body["records"]} == {"TaskHighwater"}


def test_records_epoch_window(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    # epoch 2 only
    resp = client.get(
        f"/api/capture/{cid}/records",
        params={"from": 2, "to": 2, "types": "Stamp"},
    )
    body = resp.json()
    assert body["returned"] == 1
    assert body["records"][0]["frame_epoch"] == 2


def test_records_pagination(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    page = client.get(
        f"/api/capture/{cid}/records", params={"limit": 2, "offset": 0}
    ).json()
    assert page["returned"] == 2
    page2 = client.get(
        f"/api/capture/{cid}/records", params={"limit": 2, "offset": 2}
    ).json()
    assert page2["returned"] == 2
    # Pages must be disjoint (no record returned twice).
    seen = {(r["type"], r["ts_counter"]) for r in page["records"]}
    seen.update((r["type"], r["ts_counter"]) for r in page2["records"])
    assert len(seen) == 4


# ─── /strip — Phase 1 has no STRIP records, must 404 cleanly ─────────────────


def test_strip_returns_404_when_not_present(
    client: TestClient, tmp_path: Path
) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    resp = client.get(f"/api/capture/{cid}/strip/1/sensing.png")
    assert resp.status_code == 404


# ─── /health ─────────────────────────────────────────────────────────────────


def test_health_phase1_shape(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    resp = client.get(f"/api/capture/{cid}/health")
    assert resp.status_code == 200
    h = resp.json()
    assert h["framing"]["frames_ok"] >= 1
    # v2-only fields must be present and explicitly null in v1.
    assert h["strip_rate_per_s"] is None
    assert h["cdc_write_complete_p95_us"] is None


# ─── /rtos ───────────────────────────────────────────────────────────────────


def test_rtos_reports_hwm_per_task(client: TestClient, tmp_path: Path) -> None:
    cap_dir = _populated_capture(tmp_path)
    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    resp = client.get(f"/api/capture/{cid}/rtos")
    assert resp.status_code == 200
    r = resp.json()
    names = {t["task_name"] for t in r["tasks"]}
    assert {"PERF_DRAIN", "VIDEO"} <= names
    perf = next(t for t in r["tasks"] if t["task_name"] == "PERF_DRAIN")
    assert perf["hwm_words"]["min"] == 200
    # v2-only fields must be present and null.
    assert perf["cpu_pct"] is None
    assert r["totals"]["cpu_pct_idle"] is None


def test_rtos_reports_cpu_snapshot_when_runtime_records_present(
    client: TestClient, tmp_path: Path
) -> None:
    cap_dir = tmp_path / "rt"
    init_capture_dir(cap_dir)
    # Two emissions one second apart at 1 MHz timer. CV_MARVIN_V1 70%, IDLE 30%.
    payloads = [
        build_session_payload(timer_freq_hz=1_000_000, schema_version=3),
        build_task_runtime_payload(task_id=int(TaskId.CV_MARVIN_V1), state=0, priority=4,
                                   run_time_counter=0, ts_counter=0),
        build_task_runtime_payload(task_id=int(TaskId.IDLE), state=1, priority=0,
                                   run_time_counter=0, ts_counter=0),
        build_task_runtime_payload(task_id=int(TaskId.CV_MARVIN_V1), state=0, priority=4,
                                   run_time_counter=700_000, ts_counter=1_000_000),
        build_task_runtime_payload(task_id=int(TaskId.IDLE), state=1, priority=0,
                                   run_time_counter=300_000, ts_counter=1_000_000),
    ]
    with (cap_dir / BIN_NAME).open("wb") as fh:
        for p in payloads:
            fh.write(wrap_frame(p))
    finalize_capture_dir(cap_dir, source=CaptureSource(kind="file", file=str(cap_dir)))

    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    r = client.get(f"/api/capture/{cid}/rtos").json()
    by_name = {t["task_name"]: t for t in r["tasks"]}
    assert by_name["CV_MARVIN_V1"]["cpu_pct"] == pytest.approx(70.0, abs=1e-9)
    assert by_name["IDLE"]["cpu_pct"] == pytest.approx(30.0, abs=1e-9)
    assert r["totals"]["cpu_pct_idle"] == pytest.approx(30.0, abs=1e-9)
    assert r["totals"]["cpu_pct_busy"] == pytest.approx(70.0, abs=1e-9)


def test_rtos_flags_stack_pressure_below_64(
    client: TestClient, tmp_path: Path
) -> None:
    cap_dir = tmp_path / "low_hwm"
    init_capture_dir(cap_dir)
    payloads = [
        build_session_payload(timer_freq_hz=1_000_000),
        # Only one task, with a HWM well below 64.
        build_task_highwater_payload(task_id=int(TaskId.PERF_DRAIN), words=40),
    ]
    with (cap_dir / BIN_NAME).open("wb") as fh:
        for p in payloads:
            fh.write(wrap_frame(p))
    finalize_capture_dir(cap_dir, source=CaptureSource(kind="file", file=str(cap_dir)))

    cid = client.post("/api/capture/open", json={"path": str(cap_dir)}).json()[
        "capture_id"
    ]
    r = client.get(f"/api/capture/{cid}/rtos").json()
    assert r["totals"]["any_task_below_64_words"] is True
    assert r["totals"]["any_task_below_32_words"] is False  # 40 ≥ 32
