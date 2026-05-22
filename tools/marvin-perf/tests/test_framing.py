"""Wire-framing tests: CRC, SOF resync, length sanity, partial chunks."""

from __future__ import annotations

import struct

import pytest

from marvin_perf.framing import FrameStats, crc16_ccitt_false, frame_encode, iter_frames
from marvin_perf.records import (
    MAX_RECORD_BYTES,
    PERF_CMD_HDR_MAGIC,
    PERF_CMD_SET_TYPE_MASK,
    SOF_BYTES,
    encode_set_mask_payload,
)

from .conftest import build_session_payload, build_drop_payload, wrap_frame


# ─── CRC vector ──────────────────────────────────────────────────────────────


def test_crc_known_vector() -> None:
    # CRC-16/CCITT-FALSE("123456789") = 0x29B1, the canonical test vector.
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_crc_empty() -> None:
    # init=0xFFFF, no input => stays 0xFFFF.
    assert crc16_ccitt_false(b"") == 0xFFFF


# ─── Happy-path framing ──────────────────────────────────────────────────────


def test_single_frame_round_trip() -> None:
    frame = wrap_frame(build_session_payload())
    stats = FrameStats()
    out = list(iter_frames([frame], stats))
    assert len(out) == 1
    assert out[0].payload == build_session_payload()
    assert out[0].bytes_skipped_before == 0
    assert stats.frames_ok == 1
    assert stats.bytes_resync_dropped == 0
    assert stats.crc_mismatches == 0


def test_two_frames_back_to_back() -> None:
    a = wrap_frame(build_session_payload())
    b = wrap_frame(build_drop_payload(dropped_state=3))
    stats = FrameStats()
    out = list(iter_frames([a + b], stats))
    assert len(out) == 2
    assert stats.frames_ok == 2


# ─── Chunk-boundary resilience ───────────────────────────────────────────────


def test_frame_split_across_arbitrary_chunks() -> None:
    frame = wrap_frame(build_session_payload())
    # Slice into single bytes — the framer must coalesce across boundaries.
    chunks = [bytes((b,)) for b in frame]
    stats = FrameStats()
    out = list(iter_frames(chunks, stats))
    assert len(out) == 1
    assert stats.frames_ok == 1


def test_two_frames_split_across_chunks_at_random_offsets() -> None:
    a = wrap_frame(build_session_payload())
    b = wrap_frame(build_drop_payload())
    blob = a + b
    chunks = [blob[:5], blob[5:7], blob[7:60], blob[60:]]
    stats = FrameStats()
    out = list(iter_frames(chunks, stats))
    assert len(out) == 2
    assert stats.frames_ok == 2


# ─── Mid-stream join (resync) ────────────────────────────────────────────────


def test_resync_after_garbage_prefix() -> None:
    garbage = b"\x00\x11\x22\x33\xaa\xbb\xcc"  # 7 bytes of noise, no SOF
    frame = wrap_frame(build_session_payload())
    stats = FrameStats()
    out = list(iter_frames([garbage + frame], stats))
    assert len(out) == 1
    assert out[0].bytes_skipped_before == len(garbage)
    assert stats.bytes_resync_dropped == len(garbage)
    assert stats.frames_ok == 1


def test_resync_after_truncated_lookalike() -> None:
    # SOF appears in noise but no valid frame follows; framer must skip past
    # the false SOF and find the real one.
    fake = SOF_BYTES + b"\x00\x00" + b"x" * 20  # plausible LEN=0 (rejected)
    frame = wrap_frame(build_drop_payload())
    stats = FrameStats()
    out = list(iter_frames([fake + frame], stats))
    assert len(out) == 1
    assert stats.frames_ok == 1
    assert stats.bad_lengths >= 1


# ─── CRC mismatch ────────────────────────────────────────────────────────────


def test_corrupt_crc_drops_frame() -> None:
    bad = wrap_frame(build_session_payload(), corrupt_crc=True)
    good = wrap_frame(build_drop_payload())
    stats = FrameStats()
    out = list(iter_frames([bad + good], stats))
    # The bad frame is rejected; the good one still arrives.
    assert len(out) == 1
    assert stats.crc_mismatches >= 1
    assert stats.frames_ok == 1


# ─── Length sanity ───────────────────────────────────────────────────────────


def test_oversized_length_is_rejected() -> None:
    # Build a frame with a LEN above MAX_RECORD_BYTES; the SOF should be
    # treated as a false positive.
    huge = MAX_RECORD_BYTES + 1
    fake = SOF_BYTES + struct.pack("<H", huge) + b"\x00" * 4
    frame = wrap_frame(build_drop_payload())
    stats = FrameStats()
    out = list(iter_frames([fake + frame], stats))
    assert len(out) == 1
    assert stats.bad_lengths >= 1


def test_undersized_length_is_rejected() -> None:
    # LEN below the 16-byte header minimum.
    fake = SOF_BYTES + struct.pack("<H", 4) + b"\x00" * 6
    frame = wrap_frame(build_drop_payload())
    stats = FrameStats()
    out = list(iter_frames([fake + frame], stats))
    assert len(out) == 1
    assert stats.bad_lengths >= 1


# ─── Empty-input safety ──────────────────────────────────────────────────────


def test_empty_input_yields_nothing() -> None:
    stats = FrameStats()
    out = list(iter_frames([], stats))
    assert out == []
    assert stats.frames_ok == 0


def test_empty_chunks_skipped() -> None:
    frame = wrap_frame(build_session_payload())
    stats = FrameStats()
    out = list(iter_frames([b"", frame, b""], stats))
    assert len(out) == 1


# ─── frame_encode (host→device) ──────────────────────────────────────────────


def test_encode_round_trip() -> None:
    # 16-byte payload satisfies iter_frames's LEN≥16 sanity check.
    payload = build_session_payload()
    framed = frame_encode(payload)
    # SOF(4) + LEN(2) + payload + CRC(2)
    assert framed[:4] == SOF_BYTES
    assert framed[4:6] == struct.pack("<H", len(payload))
    out = list(iter_frames([framed], FrameStats()))
    assert len(out) == 1
    assert out[0].payload == payload


def test_encode_command_known_vector() -> None:
    # SET_TYPE_MASK with mask=0x000000FE: magic=0x4D43, cmd=0x01, rsv=0x00,
    # mask=0xFE 0x00 0x00 0x00. Total payload 8 bytes.
    payload = encode_set_mask_payload(0x000000FE)
    expected_payload = bytes(
        (
            PERF_CMD_HDR_MAGIC & 0xFF, (PERF_CMD_HDR_MAGIC >> 8) & 0xFF,
            PERF_CMD_SET_TYPE_MASK,
            0x00,
            0xFE, 0x00, 0x00, 0x00,
        )
    )
    assert payload == expected_payload

    framed = frame_encode(payload)
    assert framed[:4] == SOF_BYTES
    assert framed[4:6] == struct.pack("<H", 8)
    assert framed[6:6 + 8] == expected_payload
    # CRC over LEN || PAYLOAD.
    expected_crc = crc16_ccitt_false(framed[4:6 + 8])
    assert framed[6 + 8 : 6 + 8 + 2] == struct.pack("<H", expected_crc)
