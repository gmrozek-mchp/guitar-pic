"""Export a marvin-perf capture as a SensiML-compatible CSV for MPLAB ML.

One row per PERF_REC_FRETBOARD_RAW (~240 Hz). Each row carries the five raw
ADC values plus five binary labels (one per fret), where the label comes
from the most-recent PERF_REC_DETECTOR.pressed_mask broadcast forward by
frame_epoch (~4 fretboard samples per cv frame).

CSV schema matches the MPLAB Data Visualizer "SensiML CSV" preset:
    timestamp,ph_green,ph_red,ph_yellow,ph_blue,ph_orange,
    label_green,label_red,label_yellow,label_blue,label_orange

`timestamp` is decimal seconds since the SESSION record's ts_counter.

Edge cases:
- FretboardRaw records before any Detector record are emitted with all
  labels = 0 by default; pass strict=True to drop them instead.
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
from ..records import Detector, FretboardRaw, Session
from ..transport import FileSource


_HEADER = (
    "timestamp",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "label_green", "label_red", "label_yellow", "label_blue", "label_orange",
)


class ExportError(Exception):
    """Raised when a capture can't be exported (missing SESSION etc)."""


@dataclass(frozen=True)
class ExportStats:
    n_rows: int
    n_skipped_unlabeled: int
    n_detector_records: int
    n_fretboard_records: int
    t_start_s: float
    t_end_s: float
    timer_freq_hz: int

    @property
    def duration_s(self) -> float:
        return self.t_end_s - self.t_start_s


def export_sensiml_csv(
    capture_path: str | Path,
    out_path: str | Path,
    *,
    strict: bool = False,
) -> ExportStats:
    """Read `capture_path`, write SensiML-format CSV to `out_path`.

    `strict=True` drops FretboardRaw rows that arrive before the first
    Detector record (so every emitted row has a real label). Default is
    to emit them with all labels = 0 — useful when the capture starts
    before cv_marvin_v1 has produced its first frame.
    """
    capture = open_capture(capture_path)

    session: Session | None = None
    last_detector: Detector | None = None
    n_skipped_unlabeled = 0
    n_detector_records = 0
    n_fretboard_records = 0
    n_rows = 0
    t_baseline: int | None = None  # ts_counter of the first emitted row
    t_end_s = 0.0

    out_path = Path(out_path)
    with FileSource(capture.bin_path) as src, out_path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(_HEADER)
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

            if isinstance(rec, FretboardRaw):
                n_fretboard_records += 1
                if session is None:
                    # No timer_freq_hz yet → can't write a real timestamp.
                    # Defer rejection to after the loop so we still tally
                    # how many records were skipped for diagnostics.
                    n_skipped_unlabeled += 1
                    continue
                if last_detector is None and strict:
                    n_skipped_unlabeled += 1
                    continue

                if t_baseline is None:
                    t_baseline = rec.hdr.ts_counter
                t_s = (rec.hdr.ts_counter - t_baseline) / float(
                    session.timer_freq_hz
                )
                t_end_s = t_s

                pressed = last_detector.pressed_mask if last_detector else 0
                row = [
                    f"{t_s:.6f}",
                    rec.adc[0], rec.adc[1], rec.adc[2], rec.adc[3], rec.adc[4],
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
        t_start_s=0.0 if n_rows > 0 else 0.0,
        t_end_s=t_end_s,
        timer_freq_hz=session.timer_freq_hz,
    )
