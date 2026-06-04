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

- `labels="actuator-fb"` (preferred edge-ai target, schema v4+): same six
  labels, but sourced from the actuator bitmask the fretboard reports *inside*
  each FRETBOARD_RAW frame (`applied_mask`) — paired with the ADC atomically on
  the device, with no cross-stream forward-fill. Adds an `fb_seq` column (the
  fretboard's monotonic sample counter) so downstream can detect dropped frames.
  Columns:
      timestamp,fb_seq,ph_green,...,ph_orange,
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

# actuator-fb adds fb_seq (the fretboard's own sample counter) so downstream
# can detect frames dropped in transit and avoid windowing across a gap.
_HEADER_ACTUATOR_FB = (
    "timestamp", "fb_seq",
    "ph_green", "ph_red", "ph_yellow", "ph_blue", "ph_orange",
    "fret_green", "fret_red", "fret_yellow", "fret_blue", "fret_orange",
    "strum",
)

LABEL_MODES = ("detector", "actuator", "actuator-fb", "detector-fb")

# Modes that clock row timestamps off the fretboard's own fb_sample_seq (no
# SESSION/ts_counter needed) and prepend an fb_seq column.
_FB_CLOCK_MODES = ("actuator-fb", "detector-fb")

# Fretboard tick rate — actuator-fb derives row timestamps from fb_sample_seq
# at this rate (fb_seq is the clock; no SESSION/ts_counter needed).
FRETBOARD_HZ = 240.0


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
    # Rising-edge count of the collapsed strum bit (actuator modes only): the
    # number of distinct strums in the corpus, for sanity against the song.
    n_strum_events: int = 0
    # Discontinuities in fb_sample_seq (actuator-fb mode): frames dropped in
    # transit. 0 means a clean, fully-contiguous capture.
    n_seq_gaps: int = 0

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
    default, back-compat), "actuator" (5 frets + collapsed strum from
    Actuator.intended_mask, cross-stream), "actuator-fb" (same from the
    fretboard's in-frame applied_mask + fb_seq column), or "detector-fb" (a
    diagnostic probe: detector pressed_mask frets — clean per-note structure
    without the pipeline's legato hold — plus the in-frame applied strum and
    fb_seq). The fb modes clock off fb_seq and need no SESSION.

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
    n_seq_gaps = 0
    prev_strum = 0
    prev_seq: int | None = None
    t_baseline: int | None = None  # ts_counter of the first emitted row
    t_end_s = 0.0

    fb_clock = labels in _FB_CLOCK_MODES
    header = {
        "actuator": _HEADER_ACTUATOR,
        "actuator-fb": _HEADER_ACTUATOR_FB,
        "detector-fb": _HEADER_ACTUATOR_FB,  # same schema, detector-sourced frets
    }.get(labels, _HEADER_DETECTOR)

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

                # Track dropped frames via the fretboard's own sample counter.
                if prev_seq is not None and rec.fb_sample_seq != (prev_seq + 1):
                    n_seq_gaps += 1
                prev_seq = rec.fb_sample_seq

                # fb-clock modes derive the timestamp from fb_sample_seq (the
                # fretboard's own 240 Hz clock), so they need no SESSION; other
                # modes need timer_freq_hz from SESSION for the ts_counter clock.
                if session is None and not fb_clock:
                    # Defer rejection to after the loop so we still tally
                    # how many records were skipped for diagnostics.
                    n_skipped_unlabeled += 1
                    continue

                # actuator-fb labels live inside the frame → always present;
                # other modes need their forward-filled source record.
                if labels == "actuator":
                    have_label = last_actuator is not None
                elif labels == "actuator-fb":
                    have_label = True
                else:  # detector, detector-fb
                    have_label = last_detector is not None
                if not have_label and strict:
                    n_skipped_unlabeled += 1
                    continue

                if fb_clock:
                    if t_baseline is None:
                        t_baseline = rec.fb_sample_seq
                    t_s = (rec.fb_sample_seq - t_baseline) / FRETBOARD_HZ
                else:
                    if t_baseline is None:
                        t_baseline = rec.hdr.ts_counter
                    t_s = (rec.hdr.ts_counter - t_baseline) / float(
                        session.timer_freq_hz
                    )
                t_end_s = t_s

                adc = (
                    rec.adc[0], rec.adc[1], rec.adc[2], rec.adc[3], rec.adc[4],
                )
                if labels == "detector":
                    pressed = last_detector.pressed_mask if last_detector else 0
                    row = [f"{t_s:.6f}", *adc, *[(pressed >> b) & 1 for b in range(5)]]
                else:
                    # Fret-bit source differs by mode; strum source is the
                    # in-frame applied_mask for fb modes, else the actuator.
                    if labels == "actuator":
                        fret_src = last_actuator.intended_mask if last_actuator else 0
                        strum_src = fret_src
                    elif labels == "actuator-fb":
                        fret_src = rec.applied_mask
                        strum_src = rec.applied_mask
                    else:  # detector-fb: detector frets + in-frame applied strum
                        fret_src = last_detector.pressed_mask if last_detector else 0
                        strum_src = rec.applied_mask
                    strum = _collapse_strum(strum_src)
                    if strum and not prev_strum:
                        n_strum_events += 1
                    prev_strum = strum
                    fret_bits = [(fret_src >> b) & 1 for b in range(5)]
                    if fb_clock:
                        row = [f"{t_s:.6f}", rec.fb_sample_seq, *adc, *fret_bits, strum]
                    else:
                        row = [f"{t_s:.6f}", *adc, *fret_bits, strum]
                writer.writerow(row)
                n_rows += 1

    if session is None and not fb_clock:
        # Tear out the partial file before raising — exporting half a file
        # with no header is more confusing than an absent file.
        try:
            out_path.unlink()
        except FileNotFoundError:
            pass
        raise ExportError(
            f"capture {capture.path} has no SESSION record — cannot derive "
            "timer_freq_hz to compute timestamps (not needed for "
            "--labels=actuator-fb, which clocks off fb_seq)"
        )

    return ExportStats(
        n_rows=n_rows,
        n_skipped_unlabeled=n_skipped_unlabeled,
        n_detector_records=n_detector_records,
        n_fretboard_records=n_fretboard_records,
        t_start_s=0.0,
        t_end_s=t_end_s,
        timer_freq_hz=session.timer_freq_hz if session else 0,
        labels=labels,
        n_actuator_records=n_actuator_records,
        n_strum_events=n_strum_events,
        n_seq_gaps=n_seq_gaps,
    )
