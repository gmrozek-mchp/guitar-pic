"""Wire-frame parser for the perf-log USB CDC stream.

Wire layout (mirrors firmware perf_log_sink_cdc.c):

    SOF(4)  | LEN(u16 LE) | PAYLOAD(LEN bytes) | FCS(u16 LE)

FCS is Fletcher-16 (mod 255, init 0xFFFF) computed over `LEN || PAYLOAD`
(NOT including SOF). USB hardware already CRCs the wire; the framing
checksum is just for resync alignment + firmware-bug detection.

This module is purely byte-level. It does not understand record types — that's
decode.py's job. It exists to (a) resync after mid-stream join or byte loss,
(b) reject corrupt frames, and (c) yield validated payloads upward.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass

from .records import MAX_RECORD_BYTES, SOF_BYTES


# ─── Fletcher-16 (mod 255, init 0xFFFF) ──────────────────────────────────────


def fletcher16(data: bytes | memoryview, init: int = 0xFFFF) -> int:
    """Fletcher-16 with mod 255. Mirrors firmware perf_log_sink_cdc.c.

    Detection: single-byte changes, adjacent swaps, most non-adjacent swaps.
    Strong enough for framing-layer alignment + firmware-bug detection on
    top of USB's wire-level CRC. ~3× cheaper than CRC-16 in C and Python.
    """
    s1 = init & 0xFF
    s2 = (init >> 8) & 0xFF
    for b in data:
        s1 = (s1 + b) % 255
        s2 = (s2 + s1) % 255
    return (s2 << 8) | s1


def frame_encode(payload: bytes) -> bytes:
    """SOF + LEN(u16LE) + payload + Fletcher-16(u16LE).

    Mirror of the device-side framer; FCS covers LEN || PAYLOAD (not SOF).
    """
    length = len(payload)
    if length > 0xFFFF:
        raise ValueError(f"payload too large: {length} bytes")
    len_bytes = bytes((length & 0xFF, (length >> 8) & 0xFF))
    fcs = fletcher16(len_bytes + payload)
    fcs_bytes = bytes((fcs & 0xFF, (fcs >> 8) & 0xFF))
    return SOF_BYTES + len_bytes + payload + fcs_bytes


# ─── Frame iterator ──────────────────────────────────────────────────────────


@dataclass(frozen=True)
class FrameBytes:
    """A successfully-parsed wire frame's payload + provenance."""

    payload: bytes
    bytes_skipped_before: int  # diagnostic: bytes dropped resyncing to this frame
    framed: bytes = b""        # full SOF+LEN+payload+CRC for re-emission to disk


class FrameError(Exception):
    """Wire-level frame error (FCS mismatch, length out of range)."""


class FrameStats:
    """Mutable counters surfaced by `iter_frames` for the CLI."""

    def __init__(self) -> None:
        self.frames_ok: int = 0
        self.bytes_resync_dropped: int = 0
        self.fcs_mismatches: int = 0
        self.bad_lengths: int = 0


def iter_frames(
    chunks: Iterable[bytes],
    stats: FrameStats | None = None,
) -> Iterator[FrameBytes]:
    """Consume an iterable of byte chunks, yield validated frame payloads.

    The iterator handles arbitrary chunk boundaries (a frame may straddle as
    many chunks as it likes), resyncs on the 4-byte SOF after byte loss, and
    drops frames whose FCS does not match. Counters are accumulated into
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
            fcs_observed = buf[6 + length] | (buf[6 + length + 1] << 8)
            fcs_expected = fletcher16(buf[4 : 6 + length])

            if fcs_observed != fcs_expected:
                # Bad FCS. Likely a false-positive SOF in the middle of an
                # earlier frame; step past one byte and rescan.
                stats.fcs_mismatches += 1
                skipped_since_last_ok += 1
                stats.bytes_resync_dropped += 1
                del buf[0]
                continue

            # Good frame — capture the full framed bytes, consume, yield.
            framed = bytes(buf[:total])
            del buf[:total]
            stats.frames_ok += 1
            yield FrameBytes(
                payload=payload,
                bytes_skipped_before=skipped_since_last_ok,
                framed=framed,
            )
            skipped_since_last_ok = 0
