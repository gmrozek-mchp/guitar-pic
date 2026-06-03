"""Export a marvin-perf capture as a SensiML-compatible CSV for MPLAB ML.

One row per PERF_REC_FRETBOARD_RAW (~240 Hz). Each row carries the five raw
ADC values plus a set of binary labels whose meaning depends on `labels`:

- `labels="detector"` (default, back-compat): five per-fret labels drawn from
  the most-recent PERF_REC_DETECTOR.pressed_mask, broadcast forward by
  frame_epoch (~4 fretboard samples per cv frame). Columns:
      timestamp,ph_green,...,ph_orange,
      label_green,label_red,label_yellow,label_blue,label_orange
- `labels="actuator"` (edge-ai distillation target): six labels drawn from the
  most-recent PERF_REC_ACTUATOR.intended_mask — five frets verbatim plus one
  collapsed strum bit (logical OR of strum-down and strum-up, since alternating
  direction is a human ergonomic constraint the controller doesn't enforce).
  Columns:
      timestamp,ph_green,...,ph_orange,
      fret_green,fret_red,fret_yellow,fret_blue,fret_orange,strum

The collapse is a property of *this* exporter mode, not the raw capture:
PERF_REC_ACTUATOR keeps both strum bits as marvin emitted them, so a future
direction-aware corpus can preserve them. See tools/edge-ai/docs/training.md §2.

`timestamp` is decimal seconds since the first emitted row's ts_counter.

Edge cases:
- FretboardRaw records before the first label-source record (Detector or
  Actuator, depending on mode) are emitted with all labels = 0 by default;
  pass strict=True to drop them instead.
- Capture missing the SESSION record raises ExportError (no timer_freq_hz
  → no way to convert ts_counter to seconds).
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

from ..capture import open_capture
from ..decode import decode_record
from ..framing import FrameStats, iter_frames
from ..records import Actuator, Detector, FretboardRaw, Session
from ..transport import FileSource


_HEADER_DETECTOR = (
    "timestamp",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "label_green", "label_red", "label_yellow", "label_blue", "label_orange",
)

_HEADER_ACTUATOR = (
    "timestamp",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
    "strum",
)

LABEL_MODES = ("detector", "actuator")


class ExportError(Exception):
    """Raised when a capture can't be exported (missing SESSION etc)."""


def _collapse_strum(intended_mask: int) -> int:
    """Collapse the 7-bit wire byte's two strum bits into one.

    Bit 5 is strum-down, bit 6 is strum-up. The edge-ai model only learns
    *when* to strum, not direction, so the two collapse to a single bit.
    """
    return ((intended_mask >> 5) | (intended_mask >> 6)) & 1


@dataclass(frozen=True)
class ExportStats:
    n_rows: int
    n_skipped_unlabeled: int
    n_detector_records: int
    n_fretboard_records: int
    t_start_s: float
    t_end_s: float
    timer_freq_hz: int
    labels: str = "detector"
    n_actuator_records: int = 0
    # Rising-edge count of the collapsed strum bit (actuator mode only): the
    # number of distinct strums in the corpus, for sanity against the song.
    n_strum_events: int = 0

    @property
    def duration_s(self) -> float:
        return self.t_end_s - self.t_start_s


def export_sensiml_csv(
    capture_path: str | Path,
    out_path: str | Path,
    *,
    labels: str = "detector",
    strict: bool = False,
) -> ExportStats:
    """Read `capture_path`, write SensiML-format CSV to `out_path`.

    `labels` selects the label source: "detector" (per-fret pressed_mask,
    default, back-compat) or "actuator" (5 frets + collapsed strum from
    Actuator.intended_mask — the edge-ai distillation target).

    `strict=True` drops FretboardRaw rows that arrive before the first
    label-source record (so every emitted row has a real label). Default is
    to emit them with all labels = 0 — useful when the capture starts before
    the label source has produced its first record.
    """
    if labels not in LABEL_MODES:
        raise ExportError(
            f"unknown labels mode {labels!r}; expected one of {LABEL_MODES}"
        )

    capture = open_capture(capture_path)

    session: Session | None = None
    last_detector: Detector | None = None
    last_actuator: Actuator | None = None
    n_skipped_unlabeled = 0
    n_detector_records = 0
    n_actuator_records = 0
    n_fretboard_records = 0
    n_rows = 0
    n_strum_events = 0
    prev_strum = 0
    t_baseline: int | None = None  # ts_counter of the first emitted row
    t_end_s = 0.0

    header = _HEADER_ACTUATOR if labels == "actuator" else _HEADER_DETECTOR

    out_path = Path(out_path)
    with FileSource(capture.bin_path) as src, out_path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        stats = FrameStats()

        for frame in iter_frames(src, stats):
            try:
                rec = decode_record(frame.payload)
            except Exception:
                continue

            if isinstance(rec, Session) and session is None:
                session = rec
                continue

            if isinstance(rec, Detector):
                last_detector = rec
                n_detector_records += 1
                continue

            if isinstance(rec, Actuator):
                last_actuator = rec
                n_actuator_records += 1
                continue

            if isinstance(rec, FretboardRaw):
                n_fretboard_records += 1
                if session is None:
                    # No timer_freq_hz yet → can't write a real timestamp.
                    # Defer rejection to after the loop so we still tally
                    # how many records were skipped for diagnostics.
                    n_skipped_unlabeled += 1
                    continue

                have_label = (
                    last_actuator is not None
                    if labels == "actuator"
                    else last_detector is not None
                )
                if not have_label and strict:
                    n_skipped_unlabeled += 1
                    continue

                if t_baseline is None:
                    t_baseline = rec.hdr.ts_counter
                t_s = (rec.hdr.ts_counter - t_baseline) / float(
                    session.timer_freq_hz
                )
                t_end_s = t_s

                adc = (
                    rec.adc[0], rec.adc[1], rec.adc[2], rec.adc[3], rec.adc[4],
                )
                if labels == "actuator":
                    mask = last_actuator.intended_mask if last_actuator else 0
                    strum = _collapse_strum(mask)
                    if strum and not prev_strum:
                        n_strum_events += 1
                    prev_strum = strum
                    row = [
                        f"{t_s:.6f}", *adc,
                        (mask >> 0) & 1,
                        (mask >> 1) & 1,
                        (mask >> 2) & 1,
                        (mask >> 3) & 1,
                        (mask >> 4) & 1,
                        strum,
                    ]
                else:
                    pressed = last_detector.pressed_mask if last_detector else 0
                    row = [
                        f"{t_s:.6f}", *adc,
                        (pressed >> 0) & 1,
                        (pressed >> 1) & 1,
                        (pressed >> 2) & 1,
                        (pressed >> 3) & 1,
                        (pressed >> 4) & 1,
                    ]
                writer.writerow(row)
                n_rows += 1

    if session is None:
        # Tear out the partial file before raising — exporting half a file
        # with no header is more confusing than an absent file.
        try:
            out_path.unlink()
        except FileNotFoundError:
            pass
        raise ExportError(
            f"capture {capture.path} has no SESSION record — cannot derive "
            "timer_freq_hz to compute timestamps"
        )

    return ExportStats(
        n_rows=n_rows,
        n_skipped_unlabeled=n_skipped_unlabeled,
        n_detector_records=n_detector_records,
        n_fretboard_records=n_fretboard_records,
        t_start_s=0.0,
        t_end_s=t_end_s,
        timer_freq_hz=session.timer_freq_hz,
        labels=labels,
        n_actuator_records=n_actuator_records,
        n_strum_events=n_strum_events,
    )
