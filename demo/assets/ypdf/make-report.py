#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = ["matplotlib", "numpy"]
# ///
"""Generate report.pdf — the multi-page demo report used by the
pdf-in-scrollback clip (demo/assets/yctl/clips/pdf-in-scrollback.yaml).

Three 16:9 landscape pages styled like a real engineering report:
title page, telemetry charts, and a capacity outlook page. Deterministic
(fixed RNG seed) so regenerating produces the same document."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_pdf import PdfPages

MINT = "#4f8f7a"
MINT_DARK = "#2f6353"
INK = "#1d2b30"
INK_SOFT = "#4a5a60"
PAPER = "#fbfcfc"
GRID = "#d9e2e0"
AMBER = "#c98a3d"
SLATE = "#5b7a8c"

PAGE_SIZE = (12.0, 6.75)  # 16:9, matches a wide terminal window

rng = np.random.default_rng(7)


def style_axis(axis):
    axis.set_facecolor(PAPER)
    for spine in axis.spines.values():
        spine.set_color(GRID)
    axis.tick_params(colors=INK_SOFT, labelsize=8)
    axis.grid(True, color=GRID, linewidth=0.6, alpha=0.8)
    axis.set_axisbelow(True)


def title_page(pdf):
    figure = plt.figure(figsize=PAGE_SIZE)
    figure.patch.set_facecolor(PAPER)

    figure.text(0.07, 0.78, "Fleet Telemetry", color=INK,
                fontsize=44, fontweight="bold", family="DejaVu Sans")
    figure.text(0.07, 0.68, "Quarterly Engineering Report — Q2 2026",
                color=MINT_DARK, fontsize=20)
    figure.add_artist(plt.Line2D([0.07, 0.93], [0.63, 0.63],
                                 color=MINT, linewidth=3))

    figure.text(0.07, 0.575,
                "Rollout of the adaptive ingestion pipeline cut p99 query\n"
                "latency by 38% while daily events grew from 2.1B to 3.4B.\n"
                "This report covers throughput, latency, error budgets and\n"
                "the capacity outlook for Q3.",
                color=INK_SOFT, fontsize=13, linespacing=1.7,
                verticalalignment="top")

    # Hero sparkline band across the lower third.
    axis = figure.add_axes([0.07, 0.12, 0.86, 0.26])
    days = np.arange(90)
    events = 2.1 + 1.3 * (days / 89) ** 1.4 + 0.08 * np.sin(days / 4.2) \
        + rng.normal(0, 0.035, 90)
    axis.plot(days, events, color=MINT, linewidth=2.2)
    axis.fill_between(days, events, events.min() - 0.1,
                      color=MINT, alpha=0.12)
    style_axis(axis)
    # Keep axis text horizontal — the terminal PDF renderer draws rotated
    # text glyph-by-glyph, which looks broken at zoom.
    axis.set_title("daily events (B)", color=INK_SOFT, fontsize=9,
                   loc="left")
    axis.set_xlabel("day of quarter", color=INK_SOFT, fontsize=9)

    figure.text(0.07, 0.045, "yetty demo asset — synthetic data",
                color=INK_SOFT, fontsize=8, alpha=0.7)
    figure.text(0.93, 0.045, "1 / 3", color=INK_SOFT, fontsize=9,
                ha="right")
    pdf.savefig(figure)
    plt.close(figure)


def telemetry_page(pdf):
    figure = plt.figure(figsize=PAGE_SIZE)
    figure.patch.set_facecolor(PAPER)
    figure.text(0.07, 0.90, "Ingestion & Latency", color=INK,
                fontsize=22, fontweight="bold")
    figure.add_artist(plt.Line2D([0.07, 0.93], [0.865, 0.865],
                                 color=MINT, linewidth=2))

    weeks = np.arange(1, 14)

    # Left: stacked throughput bars per region.
    axis = figure.add_axes([0.07, 0.14, 0.40, 0.62])
    europe = 0.7 + 0.05 * weeks + rng.normal(0, 0.04, 13)
    americas = 0.9 + 0.07 * weeks + rng.normal(0, 0.05, 13)
    asia = 0.5 + 0.09 * weeks + rng.normal(0, 0.04, 13)
    axis.bar(weeks, americas, color=MINT, label="Americas")
    axis.bar(weeks, europe, bottom=americas, color=SLATE, label="EMEA")
    axis.bar(weeks, asia, bottom=americas + europe, color=AMBER,
             label="APAC")
    style_axis(axis)
    axis.set_title("Weekly ingest by region (B events)", color=INK,
                   fontsize=11, loc="left")
    axis.set_xlabel("week", color=INK_SOFT, fontsize=9)
    axis.legend(frameon=False, fontsize=8, labelcolor=INK_SOFT)

    # Right: latency percentiles, log scale.
    axis = figure.add_axes([0.56, 0.14, 0.37, 0.62])
    p50 = 14 - 0.25 * weeks + rng.normal(0, 0.25, 13)
    p95 = 90 - 2.6 * weeks + rng.normal(0, 2.2, 13)
    p99 = 340 - 11.5 * weeks + rng.normal(0, 8.0, 13)
    axis.plot(weeks, p99, color=AMBER, linewidth=2, marker="o",
              markersize=3.5, label="p99")
    axis.plot(weeks, p95, color=SLATE, linewidth=2, marker="o",
              markersize=3.5, label="p95")
    axis.plot(weeks, p50, color=MINT, linewidth=2, marker="o",
              markersize=3.5, label="p50")
    axis.set_yscale("log")
    style_axis(axis)
    axis.set_title("Query latency (ms)", color=INK, fontsize=11,
                   loc="left")
    axis.set_xlabel("week", color=INK_SOFT, fontsize=9)
    axis.legend(frameon=False, fontsize=8, labelcolor=INK_SOFT)

    figure.text(0.93, 0.045, "2 / 3", color=INK_SOFT, fontsize=9,
                ha="right")
    pdf.savefig(figure)
    plt.close(figure)


def outlook_page(pdf):
    figure = plt.figure(figsize=PAGE_SIZE)
    figure.patch.set_facecolor(PAPER)
    figure.text(0.07, 0.90, "Error Budget & Capacity Outlook", color=INK,
                fontsize=22, fontweight="bold")
    figure.add_artist(plt.Line2D([0.07, 0.93], [0.865, 0.865],
                                 color=MINT, linewidth=2))

    # Left: error-budget heatmap, service x week. Drawn as explicit
    # rectangles (pure vector) — the terminal PDF renderer skips raster
    # images, so imshow/pcolormesh would come out blank.
    axis = figure.add_axes([0.07, 0.14, 0.40, 0.62])
    services = ["ingest", "index", "query", "stream", "export"]
    burn = np.clip(rng.normal(0.45, 0.22, (5, 13)), 0.02, 1.0)
    burn[2, 3:6] = [0.88, 0.95, 0.79]  # the query incident in weeks 4-6
    colormap = plt.get_cmap("Greens_r")
    for row in range(5):
        for week in range(13):
            axis.add_patch(plt.Rectangle(
                (week + 0.5, row - 0.4), 0.92, 0.8,
                facecolor=colormap(float(burn[row, week])),
                edgecolor="none"))
    axis.set_xlim(0.4, 13.6)
    axis.set_ylim(4.6, -0.6)
    axis.set_yticks(range(5), services, fontsize=9, color=INK_SOFT)
    axis.set_xticks(range(1, 14, 2), [str(w) for w in range(1, 14, 2)],
                    fontsize=8, color=INK_SOFT)
    axis.grid(False)
    axis.set_facecolor(PAPER)
    for spine in axis.spines.values():
        spine.set_color(GRID)
    axis.tick_params(colors=INK_SOFT, labelsize=8)
    axis.set_title("Error-budget burn (dark = healthy, light = burning)",
                   color=INK, fontsize=11, loc="left")
    axis.set_xlabel("week", color=INK_SOFT, fontsize=9)

    # Right: capacity projection with confidence band.
    axis = figure.add_axes([0.56, 0.14, 0.37, 0.62])
    months = np.arange(12)
    demand = 3.4 * (1.09 ** months)
    upper = demand * 1.18
    lower = demand * 0.88
    capacity = np.full(12, 6.5)
    capacity[6:] = 9.0  # planned expansion at month 7
    axis.fill_between(months, lower, upper, color=MINT, alpha=0.15,
                      label="demand (80% band)")
    axis.plot(months, demand, color=MINT_DARK, linewidth=2.2,
              label="demand forecast")
    axis.step(months, capacity, where="post", color=AMBER,
              linewidth=2.2, linestyle="--", label="provisioned")
    style_axis(axis)
    axis.set_title("Capacity vs. demand (B events/day)", color=INK,
                   fontsize=11, loc="left")
    axis.set_xlabel("months ahead", color=INK_SOFT, fontsize=9)
    axis.legend(frameon=False, fontsize=8, labelcolor=INK_SOFT)

    figure.text(0.93, 0.045, "3 / 3", color=INK_SOFT, fontsize=9,
                ha="right")
    pdf.savefig(figure)
    plt.close(figure)


def main():
    output_path = Path(__file__).parent / "report.pdf"
    with PdfPages(output_path) as pdf:
        title_page(pdf)
        telemetry_page(pdf)
        outlook_page(pdf)
    print(f"wrote {output_path}")


if __name__ == "__main__":
    main()
