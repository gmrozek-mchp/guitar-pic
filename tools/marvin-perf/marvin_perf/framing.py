"""Wire-frame parser for the perf-log USB CDC stream.

Wire layout (mirrors firmware perf_log_sink_cdc.c:234-245):

    SOF(4)  | LEN(u16 LE) | PAYLOAD(LEN bytes) | CRC(u16 LE)

CRC is CRC-16/CCITT-FALSE computed over `LEN || PAYLOAD` (NOT including SOF).

This module is purely byte-level. It does not understand record types — that's
decode.py's job. It exists to (a) resync after mid-stream join or byte loss,
(b) reject corrupt frames, and (c) yield validated payloads upward.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass

from .records import MAX_RECORD_BYTES, SOF_BYTES


# ─── CRC-16/CCITT-FALSE ──────────────────────────────────────────────────────

_CRC_TABLE: list[int] = []


def _build_crc_table() -> list[int]:
    table: list[int] = []
    for byte in range(256):
        crc = byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
        table.append(crc)
    return table


_CRC_TABLE = _build_crc_table()


def crc16_ccitt_false(data: bytes | memoryview, init: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.

    Mirrors firmware perf_log_sink_cdc.c:58-70 byte-for-byte.
    """
    crc = init
    for b in data:
        crc = ((crc << 8) ^ _CRC_TABLE[(crc >> 8) ^ b]) & 0xFFFF
    return crc


# ─── Frame iterator ──────────────────────────────────────────────────────────


@dataclass(frozen=True)
class FrameBytes:
    """A successfully-parsed wire frame's payload + provenance."""

    payload: bytes
    bytes_skipped_before: int  # diagnostic: bytes dropped resyncing to this frame


class FrameError(Exception):
    """Wire-level frame error (CRC mismatch, length out of range)."""


class FrameStats:
    """Mutable counters surfaced by `iter_frames` for the CLI."""

    def __init__(self) -> None:
        self.frames_ok: int = 0
        self.bytes_resync_dropped: int = 0
        self.crc_mismatches: int = 0
        self.bad_lengths: int = 0


def iter_frames(
    chunks: Iterable[bytes],
    stats: FrameStats | None = None,
) -> Iterator[FrameBytes]:
    """Consume an iterable of byte chunks, yield validated frame payloads.

    The iterator handles arbitrary chunk boundaries (a frame may straddle as
    many chunks as it likes), resyncs on the 4-byte SOF after byte loss, and
    drops frames whose CRC does not match. Counters are accumulated into
    `stats` if supplied.
    """
    if stats is None:
        stats = FrameStats()

    buf = bytearray()
    skipped_since_last_ok = 0

    for chunk in chunks:
        if not chunk:
            continue
        buf.extend(chunk)

        while True:
            sof_idx = buf.find(SOF_BYTES)
            if sof_idx < 0:
                # No SOF in buffer. Drop everything but the last 3 bytes (a
                # SOF could still be straddling the boundary).
                if len(buf) > 3:
                    skipped_since_last_ok += len(buf) - 3
                    stats.bytes_resync_dropped += len(buf) - 3
                    del buf[: len(buf) - 3]
                break

            if sof_idx > 0:
                skipped_since_last_ok += sof_idx
                stats.bytes_resync_dropped += sof_idx
                del buf[:sof_idx]

            # buf now starts with SOF. Need 4 (SOF) + 2 (LEN) before we can
            # bound the frame.
            if len(buf) < 6:
                break

            length = buf[4] | (buf[5] << 8)

            # Sanity-bound the length. A bogus LEN (e.g. mid-payload bytes
            # that happened to align with SOF) will trip this; treat the SOF
            # as a false positive and step past it.
            if length < 16 or length > MAX_RECORD_BYTES:
                stats.bad_lengths += 1
                skipped_since_last_ok += 1
                stats.bytes_resync_dropped += 1
                del buf[0]
                continue

            total = 6 + length + 2
            if len(buf) < total:
                break

            payload = bytes(buf[6 : 6 + length])
            crc_observed = buf[6 + length] | (buf[6 + length + 1] << 8)
            crc_expected = crc16_ccitt_false(buf[4 : 6 + length])

            if crc_observed != crc_expected:
                # Bad CRC. Likely a false-positive SOF in the middle of an
                # earlier frame; step past one byte and rescan.
                stats.crc_mismatches += 1
                skipped_since_last_ok += 1
                stats.bytes_resync_dropped += 1
                del buf[0]
                continue

            # Good frame — consume it from the buffer and yield.
            del buf[:total]
            stats.frames_ok += 1
            yield FrameBytes(
                payload=payload,
                bytes_skipped_before=skipped_since_last_ok,
            )
            skipped_since_last_ok = 0
