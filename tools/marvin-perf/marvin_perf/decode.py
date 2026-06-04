"""Validated frame payload → typed record dataclass."""

from __future__ import annotations

from typing import Union

from .records import (
    Actuator,
    Detector,
    DetectorConfig,
    Drop,
    FretboardRaw,
    FRET_COUNT,
    Header,
    HDR_SIZE,
    PERF_LOG_HDR_MAGIC,
    RecordType,
    Session,
    Stamp,
    STRIP_BPP,
    STRIP_HDR_BYTES,
    STRIP_MAX_BYTES,
    Strip,
    TaskHighwater,
    TaskRuntime,
    Timing,
    UnknownRecord,
    _STRIP_BODY,
)


Record = Union[
    Session, Stamp, Detector, Timing, Drop, TaskHighwater, TaskRuntime,
    Strip, DetectorConfig, Actuator, FretboardRaw, UnknownRecord,
]


class DecodeError(Exception):
    pass


def _check(payload: bytes, expected_size: int, name: str) -> None:
    if len(payload) != expected_size:
        raise DecodeError(
            f"{name}: payload length {len(payload)} != expected {expected_size}"
        )


def _decode_session(hdr: Header, payload: bytes) -> Session:
    _check(payload, Session.SIZE, "Session")
    timer_freq_hz, schema_version, _reserved, fw_git_short, _reserved2 = (
        Session._BODY.unpack_from(payload, HDR_SIZE)
    )
    return Session(
        hdr=hdr,
        timer_freq_hz=timer_freq_hz,
        schema_version=schema_version,
        fw_git_short=fw_git_short,
    )


def _decode_stamp(hdr: Header, payload: bytes) -> Stamp:
    _check(payload, Stamp.SIZE, "Stamp")
    stage_id, _reserved, aux, _reserved2, _reserved3 = Stamp._BODY.unpack_from(
        payload, HDR_SIZE
    )
    return Stamp(hdr=hdr, stage_id=stage_id, aux=aux)


def _decode_detector(hdr: Header, payload: bytes) -> Detector:
    _check(payload, Detector.SIZE, "Detector")
    fields = Detector._BODY.unpack_from(payload, HDR_SIZE)
    hold = tuple(fields[0:5])
    edge = tuple(fields[5:10])
    pressed_mask = fields[10]
    edge_active_mask = fields[11]
    return Detector(
        hdr=hdr,
        hold_dist=hold,  # type: ignore[arg-type]
        edge_dist=edge,  # type: ignore[arg-type]
        pressed_mask=pressed_mask,
        edge_active_mask=edge_active_mask,
    )


def _decode_timing(hdr: Header, payload: bytes) -> Timing:
    _check(payload, Timing.SIZE, "Timing")
    fields = Timing._BODY.unpack_from(payload, HDR_SIZE)
    (
        now_ms,
        chord_open, chord_mask, chord_age_ms,
        note_q_count, note_head_mask, note_tail_mask, _note_pad,
        note_head_at_ms,
        strum_q_count, strum_head_mask, strum_dir_next, _strum_pad,
        strum_head_at_ms,
        frets_active, strum_active, release_pending_mask, publish_mask,
        strum_release_at_ms, release_min_at_ms,
    ) = fields
    return Timing(
        hdr=hdr,
        now_ms=now_ms,
        chord_open=chord_open,
        chord_mask=chord_mask,
        chord_age_ms=chord_age_ms,
        note_q_count=note_q_count,
        note_head_mask=note_head_mask,
        note_tail_mask=note_tail_mask,
        note_head_at_ms=note_head_at_ms,
        strum_q_count=strum_q_count,
        strum_head_mask=strum_head_mask,
        strum_dir_next=strum_dir_next,
        strum_head_at_ms=strum_head_at_ms,
        frets_active=frets_active,
        strum_active=strum_active,
        release_pending_mask=release_pending_mask,
        publish_mask=publish_mask,
        strum_release_at_ms=strum_release_at_ms,
        release_min_at_ms=release_min_at_ms,
    )


def _decode_detector_config(hdr: Header, payload: bytes) -> DetectorConfig:
    _check(payload, DetectorConfig.SIZE, "DetectorConfig")
    fields = DetectorConfig._BODY.unpack_from(payload, HDR_SIZE)
    n = FRET_COUNT
    return DetectorConfig(
        hdr=hdr,
        sensor_hx=tuple(fields[0 * n : 1 * n]),
        sensor_hy=tuple(fields[1 * n : 2 * n]),
        sensor_ex=tuple(fields[2 * n : 3 * n]),
        sensor_ey=tuple(fields[3 * n : 4 * n]),
        hold_thresh=fields[4 * n + 0],
        hold_release_frac=fields[4 * n + 1],
        edge_thresh=fields[4 * n + 2],
        color_target_b=tuple(fields[4 * n + 3 + 0 * n : 4 * n + 3 + 1 * n]),
        color_target_g=tuple(fields[4 * n + 3 + 1 * n : 4 * n + 3 + 2 * n]),
        color_target_r=tuple(fields[4 * n + 3 + 2 * n : 4 * n + 3 + 3 * n]),
        color_reject_b=tuple(fields[4 * n + 3 + 3 * n : 4 * n + 3 + 4 * n]),
        color_reject_g=tuple(fields[4 * n + 3 + 4 * n : 4 * n + 3 + 5 * n]),
        color_reject_r=tuple(fields[4 * n + 3 + 5 * n : 4 * n + 3 + 6 * n]),
    )


def _decode_actuator(hdr: Header, payload: bytes) -> Actuator:
    _check(payload, Actuator.SIZE, "Actuator")
    (
        intended_mask, asserted_mask, strum_dir, producer_id,
        last_ack_result, last_ack_ts_counter,
    ) = Actuator._BODY.unpack_from(payload, HDR_SIZE)
    return Actuator(
        hdr=hdr,
        intended_mask=intended_mask,
        asserted_mask=asserted_mask,
        strum_dir=strum_dir,
        producer_id=producer_id,
        last_ack_result=last_ack_result,
        last_ack_ts_counter=last_ack_ts_counter,
    )


def _decode_fretboard_raw(hdr: Header, payload: bytes) -> FretboardRaw:
    _check(payload, FretboardRaw.SIZE, "FretboardRaw")
    fields = FretboardRaw._BODY.unpack_from(payload, HDR_SIZE)
    # fields: adc[0..4], fb_sample_seq, applied_mask, reserved
    return FretboardRaw(
        hdr=hdr,
        adc=tuple(fields[:FRET_COUNT]),
        fb_sample_seq=fields[FRET_COUNT],
        applied_mask=fields[FRET_COUNT + 1],
    )


def _decode_drop(hdr: Header, payload: bytes) -> Drop:
    _check(payload, Drop.SIZE, "Drop")
    dropped_state, dropped_strip, dropped_sink, _reserved = Drop._BODY.unpack_from(
        payload, HDR_SIZE
    )
    return Drop(
        hdr=hdr,
        dropped_state=dropped_state,
        dropped_strip=dropped_strip,
        dropped_sink=dropped_sink,
    )


def _decode_task_highwater(hdr: Header, payload: bytes) -> TaskHighwater:
    _check(payload, TaskHighwater.SIZE, "TaskHighwater")
    task_id, _reserved, words = TaskHighwater._BODY.unpack_from(payload, HDR_SIZE)
    return TaskHighwater(hdr=hdr, task_id=task_id, words=words)


def _decode_task_runtime(hdr: Header, payload: bytes) -> TaskRuntime:
    _check(payload, TaskRuntime.SIZE, "TaskRuntime")
    task_id, state, priority, _reserved, run_time_counter, _reserved2 = (
        TaskRuntime._BODY.unpack_from(payload, HDR_SIZE)
    )
    return TaskRuntime(
        hdr=hdr,
        task_id=task_id,
        state=state,
        priority=priority,
        run_time_counter=run_time_counter,
    )


def _decode_strip(hdr: Header, payload: bytes) -> Strip:
    if len(payload) < STRIP_HDR_BYTES:
        raise DecodeError(
            f"Strip: payload {len(payload)} < header {STRIP_HDR_BYTES}"
        )
    x, y, w, h, kind, _reserved = _STRIP_BODY.unpack_from(payload, HDR_SIZE)
    expected_pixels = w * h * STRIP_BPP
    if expected_pixels > STRIP_MAX_BYTES:
        raise DecodeError(
            f"Strip: w*h*bpp {expected_pixels} > max {STRIP_MAX_BYTES}"
        )
    expected_total = STRIP_HDR_BYTES + expected_pixels
    if len(payload) != expected_total:
        raise DecodeError(
            f"Strip: payload {len(payload)} != expected {expected_total} "
            f"({w}×{h}×{STRIP_BPP})"
        )
    bgr = bytes(payload[STRIP_HDR_BYTES:expected_total])
    return Strip(hdr=hdr, x=x, y=y, w=w, h=h, kind=kind, bgr=bgr)


_DISPATCH = {
    RecordType.SESSION: _decode_session,
    RecordType.STAMP: _decode_stamp,
    RecordType.DETECTOR: _decode_detector,
    RecordType.TIMING: _decode_timing,
    RecordType.DROP: _decode_drop,
    RecordType.TASK_HIGHWATER: _decode_task_highwater,
    RecordType.TASK_RUNTIME: _decode_task_runtime,
    RecordType.STRIP: _decode_strip,
    RecordType.DETECTOR_CONFIG: _decode_detector_config,
    RecordType.ACTUATOR: _decode_actuator,
    RecordType.FRETBOARD_RAW: _decode_fretboard_raw,
}


def decode_record(payload: bytes) -> Record:
    """Decode an FCS-validated frame payload into a typed Record dataclass.

    Caller (framing.iter_frames) has already verified FCS and length sanity.
    Unknown record types are surfaced as UnknownRecord so a forward-schema
    firmware does not crash an older host.
    """
    if len(payload) < HDR_SIZE:
        raise DecodeError(f"payload {len(payload)} < HDR_SIZE {HDR_SIZE}")
    hdr = Header.unpack(payload)
    if hdr.magic != PERF_LOG_HDR_MAGIC:
        raise DecodeError(f"bad magic 0x{hdr.magic:04x}")

    decoder = _DISPATCH.get(hdr.type)
    if decoder is None:
        return UnknownRecord(hdr=hdr, raw=payload)
    return decoder(hdr, payload)
