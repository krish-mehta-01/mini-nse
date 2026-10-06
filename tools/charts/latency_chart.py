"""Draws docs/results/latency.png from mini_nse_bench's --csv output: latency at each percentile, per kind of event.

    ./build-release/mini_nse_bench morning.txt --csv latency.csv
    python3 tools/charts/latency_chart.py latency.csv docs/results/latency.png
"""
import csv
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import matplotlib.ticker  # noqa: E402

SERIES = [  # (kind in the CSV, label, colour: categorical slots 1-4, checked with the palette validator)
    ("normal market: new order", "Normal market: new order", "#2a78d6"),
    ("normal market: cancel", "Normal market: cancel", "#eb6834"),
    ("normal market: modify", "Normal market: modify", "#1baf7a"),
    ("pre-open: new order", "Pre-open: new order", "#eda100"),
]
PERCENTILES = [50.0, 90.0, 99.0, 99.9]


def main(csv_path, png_path):
    values = {}
    with open(csv_path, encoding="utf-8") as f:
        for row in csv.DictReader(f):
            values[(row["kind"], float(row["percentile"]))] = int(row["nanoseconds"])

    fig, ax = plt.subplots(figsize=(9, 5), dpi=170)
    for surface in (fig, ax):
        surface.set_facecolor("#fcfcfb")
    x = list(range(len(PERCENTILES)))
    for kind, label, colour in SERIES:
        y = [values[(kind, p)] for p in PERCENTILES]
        ax.plot(x, y, color=colour, linewidth=2, marker="o", markersize=7, markeredgecolor="#fcfcfb",
                markeredgewidth=2, label=label, solid_capstyle="round")
        ax.annotate(f"{label}  {y[-1]:,} ns", (x[-1], y[-1]), xytext=(10, 0), textcoords="offset points",
                    va="center", fontsize=8.5, color="#52514e")

    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:,.0f} ns"))
    ax.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    ax.tick_params(axis="y", which="minor", length=0)
    ax.set_xticks(x, ["p50\n(typical)", "p90", "p99", "p99.9\n(1 in 1,000)"], fontsize=9, color="#52514e")
    ax.set_ylabel("nanoseconds per event (log scale)", fontsize=9, color="#52514e")
    ax.tick_params(axis="y", labelsize=8.5, colors="#898781", length=0)
    ax.tick_params(axis="x", length=0)
    ax.yaxis.grid(True, color="#e1e0d9", linewidth=0.8, which="major")
    ax.set_axisbelow(True)
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.set_xlim(-0.3, len(x) + 0.9)  # room for the end labels
    ax.legend(loc="upper left", frameon=False, fontsize=8.5, labelcolor="#52514e")
    ax.set_title("How long one event takes: the typical case and the slow tail", loc="left", fontsize=12,
                 color="#0b0b0b", pad=12)
    fig.text(0.01, 0.01, "Release build, WSL2 on a laptop (Intel Core 5 120U), 205,004-event morning. "
             "Includes ~20-30 ns of clock-reading overhead per event.", fontsize=7.5, color="#898781")
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    fig.savefig(png_path, facecolor="#fcfcfb")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
