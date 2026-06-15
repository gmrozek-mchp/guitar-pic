"""Offline prototype for marvin's GH3 game-state engine.

Slice 1 is the screen classifier: recognize which GH3 screen a captured frame
shows (or report UNKNOWN), using coarse fixed-region colour fingerprints proven
against the `firmware/marvin/docs/gh3_screens/` corpus. See `docs/journal.md` and
marvin's `spec.md` §4.8.
"""
