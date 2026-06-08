"""Model visualisation: a torchinfo summary table, a torchview layer graph, and
a receptive-field cone plot.

torch / torchinfo / torchview / matplotlib are imported only inside the function
that needs each, so importing this module stays cheap and the stdlib surface
(`data`, `lag`, `metrics`) is unaffected. Install the deps with:

    uv sync --group train --group viz

For publication-quality block diagrams (beyond what these give), export the
float model to ONNX and open it in Netron (https://netron.app), or redraw by
hand in PlotNeuralNet / NN-SVG.
"""

from __future__ import annotations

from . import FRET_COUNT, SAMPLE_RATE_HZ


def receptive_field(kernel: int, dilations: tuple[int, ...]) -> int:
    """Total input samples that influence one output timestep."""
    return 1 + sum((kernel - 1) * d for d in dilations)


def summary_text(model, window: int, n_in: int = FRET_COUNT) -> str:
    """torchinfo summary table: per-layer shapes, params, and mult-adds.

    Mult-adds are the per-window cost (conv over the whole window); the deployed
    streaming path computes only the last timestep, so on-device MACs are lower
    — see docs/model.md §5.
    """
    from torchinfo import summary

    s = summary(
        model,
        input_size=(1, n_in, window),
        col_names=("input_size", "output_size", "num_params", "mult_adds"),
        verbose=0,
    )
    return str(s)


def render_graph(model, window: int, out_path: str, n_in: int = FRET_COUNT) -> str:
    """Render a torchview layer graph to out_path (format from the extension).

    Requires the graphviz binary on PATH (`brew install graphviz`).
    """
    import os

    from torchview import draw_graph

    stem, ext = os.path.splitext(out_path)
    fmt = ext.lstrip(".") or "svg"
    directory = os.path.dirname(stem) or "."
    filename = os.path.basename(stem)

    g = draw_graph(
        model,
        input_size=(1, n_in, window),
        graph_name="StrumNet",
        expand_nested=True,
        depth=3,
        device="meta",
    )
    g.visual_graph.render(
        filename=filename, directory=directory, format=fmt, cleanup=True
    )
    return f"{stem}.{fmt}"


def rf_cone_plot(
    out_path: str,
    *,
    kernel: int,
    dilations: tuple[int, ...],
    sample_rate: float = SAMPLE_RATE_HZ,
    n_in: int = FRET_COUNT,
) -> str:
    """Plot the receptive-field cone: which input timesteps feed one output.

    Each conv layer is a row; dots are the timesteps that influence the single
    output the linear head reads, edges are the dilated kernel taps. The cone
    widens toward the input — its full span is the receptive field, which must
    cover the photo->strum lag (docs/model.md §4).
    """
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n_conv = len(dilations)
    # Expand from the output (offset 0) down to the input. The head reads the
    # last conv's output at one timestep; walking back to the input applies the
    # conv dilations in reverse order. sets[i] holds the contributing offsets at
    # that activation row; edges[i] joins row i to row i+1 along kernel taps.
    rev = dilations[::-1]
    sets: list[list[int]] = [[0]]
    edges: list[list[tuple[int, int]]] = []
    for d in rev:
        parent = sets[-1]
        child: set[int] = set()
        layer_edges: list[tuple[int, int]] = []
        for n in parent:
            for j in range(kernel):
                c = n - j * d
                child.add(c)
                layer_edges.append((n, c))
        sets.append(sorted(child))
        edges.append(layer_edges)

    # Row 0 = last conv output (what the head reads); last row = input.
    labels = [f"conv{n_conv - i} out  (d={dilations[n_conv - 1 - i]})" for i in range(n_conv)]
    labels.append(f"input  ({n_in} ch)")
    n_rows = len(sets)

    def y_of(row: int) -> int:
        return n_rows - 1 - row  # output on top, input on the bottom

    fig, ax = plt.subplots(figsize=(11, 1.1 * n_rows + 1.5))

    for i, layer_edges in enumerate(edges):
        for parent, child in layer_edges:
            ax.plot([parent, child], [y_of(i), y_of(i + 1)],
                    color="0.7", lw=0.6, zorder=1)

    for row, offsets in enumerate(sets):
        y = y_of(row)
        ax.scatter(offsets, [y] * len(offsets), s=22, color="#1f77b4", zorder=3)
        ax.text(2, y, labels[row], va="center", ha="left", fontsize=9)
        span = max(offsets) - min(offsets) + 1
        ax.annotate(f"{span} samp", (min(offsets), y), xytext=(-8, 0),
                    textcoords="offset points", va="center", ha="right",
                    fontsize=8, color="0.4")

    # Mark the output timestep the head reads.
    ax.scatter([0], [y_of(0)], s=70, facecolors="none", edgecolors="#d62728",
               linewidths=1.5, zorder=4)
    ax.text(0, y_of(0) + 0.28, "output[t]", ha="center", fontsize=8, color="#d62728")

    rf = receptive_field(kernel, dilations)
    ax.set_xlabel("input timestep offset (samples; 0 = output time, negative = past)")
    ax.set_yticks([])
    ax.set_ylim(-0.6, n_rows - 0.4)
    ax.set_title(
        f"StrumNet receptive field — kernel {kernel}, dilations {dilations}\n"
        f"RF = {rf} samples ≈ {rf / sample_rate * 1000:.0f} ms at {sample_rate:.0f} Hz",
        fontsize=10,
    )
    ax.spines[["top", "right", "left"]].set_visible(False)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    return out_path
