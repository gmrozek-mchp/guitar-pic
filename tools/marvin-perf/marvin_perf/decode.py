"""Validated frame payload → typed record dataclass."""

from __future__ import annotations

from typing import Union

from .records import (
    Detector,
    Drop,
    FRET_COUNT,
    Header,
    HDR_SIZE,
    PERF_LOG_HDR_MAGIC,
    Patch,
    PatchFret,
    PATCH_FRET_SIZE,
    RecordType,
    Session,
    Stamp,
    TaskHighwater,
    Timing,
    UnknownRecord,
    _PATCH_FRET_FMT,
)


Record = Union[
    Session, Stamp, Detector, Timing, Drop, TaskHighwater, Patch, UnknownRecord
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
    # 5 hold, 5 edge, pressed_mask, edge_active_mask, reserved
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
    publish_mask, chord_window_fill, fifo_depth, strum_dir, *_ = (
        Timing._BODY.unpack_from(payload, HDR_SIZE)
    )
    return Timing(
        hdr=hdr,
        publish_mask=publish_mask,
        chord_window_fill=chord_window_fill,
        fifo_depth=fifo_depth,
        strum_dir=strum_dir,
    )


def _decode_drop(hdr: Header, payload: bytes) -> Drop:
    _check(payload, Drop.SIZE, "Drop")
    dropped_state, dropped_patch, dropped_sink, _reserved = Drop._BODY.unpack_from(
        payload, HDR_SIZE
    )
    return Drop(
        hdr=hdr,
        dropped_state=dropped_state,
        dropped_patch=dropped_patch,
        dropped_sink=dropped_sink,
    )


def _decode_task_highwater(hdr: Header, payload: bytes) -> TaskHighwater:
    _check(payload, TaskHighwater.SIZE, "TaskHighwater")
    task_id, _reserved, words = TaskHighwater._BODY.unpack_from(payload, HDR_SIZE)
    return TaskHighwater(hdr=hdr, task_id=task_id, words=words)


def _decode_patch(hdr: Header, payload: bytes) -> Patch:
    _check(payload, Patch.SIZE, "Patch")
    frame_w, frame_h, _reserved = Patch._PRELUDE.unpack_from(payload, HDR_SIZE)
    fret_offset = HDR_SIZE + Patch._PRELUDE.size
    fret = []
    for i in range(FRET_COUNT):
        off = fret_offset + i * PATCH_FRET_SIZE
        hx, hy, ex, ey, hold_bgr, edge_bgr = _PATCH_FRET_FMT.unpack_from(payload, off)
        fret.append(
            PatchFret(
                hx=hx, hy=hy, ex=ex, ey=ey,
                hold_bgr=hold_bgr, edge_bgr=edge_bgr,
            )
        )
    return Patch(
        hdr=hdr,
        frame_w=frame_w,
        frame_h=frame_h,
        fret=tuple(fret),  # type: ignore[arg-type]
    )


_DISPATCH = {
    RecordType.SESSION: _decode_session,
    RecordType.STAMP: _decode_stamp,
    RecordType.DETECTOR: _decode_detector,
    RecordType.TIMING: _decode_timing,
    RecordType.DROP: _decode_drop,
    RecordType.TASK_HIGHWATER: _decode_task_highwater,
    RecordType.PATCH: _decode_patch,
}


def decode_record(payload: bytes) -> Record:
    """Decode a CRC-validated frame payload into a typed Record dataclass.

    Caller (framing.iter_frames) has already verified CRC and length sanity.
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
