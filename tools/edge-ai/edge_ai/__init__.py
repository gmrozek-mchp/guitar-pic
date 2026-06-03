"""edge-ai: distill marvin's gameplay commands into a small causal model.

The stdlib-only surface (data loading, causal windowing, lag measurement,
metrics) imports without numpy/torch. Model and training live in modules that
import torch lazily, so `edge_ai.data`, `edge_ai.lag`, and `edge_ai.metrics`
are usable on a stock Python.
"""

FRET_COUNT = 5
N_LABELS = 6  # 5 frets + 1 collapsed strum
SAMPLE_RATE_HZ = 240.0
ADC_MAX = 4095
