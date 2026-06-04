"""Baseline causal 1D-CNN: 5 ADC channels over a window → 6 logits.

Dilated causal conv layers (one per dilation factor), then a linear head on the
last timestep. Output is 6 logits (5 frets + collapsed strum) for independent
weighted-BCE. int8 quantisation is a Phase-3 concern; this trains in float32.

The receptive field must cover the ~48-sample photo->strum lag, or the head
can't hold a fret to the strike line: dilations (1, 4, 16) kernel 5 give RF 85
(~354 ms) and overfit frets to ~0.99, whereas (1, 4) [RF 21] caps at ~0.86 on
actuator-fb labels. See docs/model.md §4. Imports torch — only loaded when
training/eval run.
"""

from __future__ import annotations

import torch
import torch.nn as nn

from . import FRET_COUNT, N_LABELS


class CausalConv1d(nn.Module):
    """Conv1d with left padding only, so output[t] depends on inputs ≤ t."""

    def __init__(self, in_ch: int, out_ch: int, kernel: int, dilation: int):
        super().__init__()
        self.pad = (kernel - 1) * dilation
        self.conv = nn.Conv1d(in_ch, out_ch, kernel, dilation=dilation)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = nn.functional.pad(x, (self.pad, 0))
        return self.conv(x)


class StrumNet(nn.Module):
    def __init__(
        self,
        *,
        channels: int = 8,
        kernel: int = 5,
        dilations: tuple[int, ...] = (1, 4, 16),
        n_in: int = FRET_COUNT,
        n_out: int = N_LABELS,
    ):
        super().__init__()
        layers: list[nn.Module] = []
        prev = n_in
        for d in dilations:
            layers.append(CausalConv1d(prev, channels, kernel, d))
            layers.append(nn.ReLU())
            prev = channels
        self.features = nn.Sequential(*layers)
        self.head = nn.Linear(channels, n_out)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # x: (B, n_in, window) -> features (B, channels, window) -> last step
        h = self.features(x)
        last = h[:, :, -1]
        return self.head(last)

    @torch.no_grad()
    def predict_bits(self, x: torch.Tensor, thresh: float = 0.5) -> torch.Tensor:
        return (torch.sigmoid(self.forward(x)) >= thresh).int()


def count_macs(model: StrumNet, window: int) -> int:
    """Rough MAC count for one inference, to check against the on-device budget."""
    macs = 0
    for m in model.modules():
        if isinstance(m, nn.Conv1d):
            k = m.kernel_size[0]
            macs += m.in_channels * m.out_channels * k * window
        elif isinstance(m, nn.Linear):
            macs += m.in_features * m.out_features
    return macs
