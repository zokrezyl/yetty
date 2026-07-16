#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["wcwidth==0.8.2"]
# ///
"""Regenerate the character-width interval tables used by unicode.c.

Emits src/fullwidth.inc (East Asian Wide/Fullwidth -> 2 cells) and
src/combining.inc (zero-width/combining -> 0 cells) from the tables of
the `wcwidth` python library, pinned to one Unicode version. That
library is the width reference the `ucs-detect` terminal test suite
measures against, so generating from the same tables guarantees the
terminal and the test suite agree.

The tables are written to every known vendored libvterm copy. Run from
anywhere:

    ./src/libvterm-0.3.3/gen-width-tables.py

Then rebuild. The generated .inc files must never be edited by hand.
"""

import pathlib

from wcwidth import list_versions
from wcwidth._constants import _ISC_VIRAMA_SET
from wcwidth.table_mc import CATEGORY_MC
from wcwidth.table_wide import WIDE_EASTASIAN
from wcwidth.table_zero import ZERO_WIDTH

# Codepoints below U+00A0 (NUL, C0/C1 controls) are handled explicitly
# in mk_wcwidth(); keeping them out of the combining table also keeps
# vterm_unicode_is_combining() from classifying control bytes.
MIN_TABLE_CODEPOINT = 0xA0

LIBVTERM_SOURCE_DIRS = (
    "src/libvterm-0.3.3/src",
    "yos/src/libvterm/src",
)


def format_intervals(intervals: list[tuple[int, int]]) -> str:
    lines = []
    for first_codepoint, last_codepoint in intervals:
        lines.append("  { 0x%x, 0x%x }," % (first_codepoint, last_codepoint))
    return "\n".join(lines) + "\n"


def banner(table_name: str, unicode_version: str) -> str:
    return (
        "/* GENERATED FILE - do not edit.\n"
        " * %s intervals for Unicode %s, emitted from the python\n"
        " * `wcwidth` library tables by gen-width-tables.py (same source the\n"
        " * ucs-detect suite measures against). Regenerate with:\n"
        " *   ./src/libvterm-0.3.3/gen-width-tables.py\n"
        " */\n" % (table_name, unicode_version)
    )


def set_to_intervals(codepoints) -> list[tuple[int, int]]:
    """Collapse a flat set of codepoints into sorted non-overlapping intervals."""
    intervals = []
    for codepoint in sorted(codepoints):
        if intervals and codepoint == intervals[-1][1] + 1:
            intervals[-1] = (intervals[-1][0], codepoint)
        else:
            intervals.append((codepoint, codepoint))
    return intervals


def clamp_low_entries(intervals) -> list[tuple[int, int]]:
    clamped = []
    for first_codepoint, last_codepoint in intervals:
        if last_codepoint < MIN_TABLE_CODEPOINT:
            continue
        clamped.append((max(first_codepoint, MIN_TABLE_CODEPOINT), last_codepoint))
    return clamped


def subtract_intervals(minuend, subtrahend) -> list[tuple[int, int]]:
    """Remove every codepoint of `subtrahend` from the `minuend` intervals."""
    result = []
    for first_codepoint, last_codepoint in minuend:
        segments = [(first_codepoint, last_codepoint)]
        for cut_first, cut_last in subtrahend:
            next_segments = []
            for segment_first, segment_last in segments:
                if cut_last < segment_first or cut_first > segment_last:
                    next_segments.append((segment_first, segment_last))
                    continue
                if segment_first < cut_first:
                    next_segments.append((segment_first, cut_first - 1))
                if cut_last < segment_last:
                    next_segments.append((cut_last + 1, segment_last))
            segments = next_segments
        result.extend(segments)
    return result


def main() -> None:
    unicode_version = list_versions()[-1]
    wide_intervals = list(WIDE_EASTASIAN[unicode_version])
    # Spacing combining marks (category Mc) are kept out of the zero
    # table because they are not non-spacing marks; vterm_unicode_cluster()
    # instead folds a base + Mc into one cluster capped at 2 cells via the
    # separate spacing-mark table below, matching the cluster-aware width
    # the wcwidth reference reports.
    zero_intervals = clamp_low_entries(
        subtract_intervals(ZERO_WIDTH[unicode_version], CATEGORY_MC[unicode_version])
    )
    # Spacing combining marks (Mc) and virama (halant) codepoints drive the
    # cluster-width rules in vterm_unicode_cluster(): a spacing mark caps its
    # cluster at 2, and a virama joins the following consonant into one
    # conjunct cluster also capped at 2.
    spacing_mark_intervals = clamp_low_entries(list(CATEGORY_MC[unicode_version]))
    virama_intervals = clamp_low_entries(set_to_intervals(_ISC_VIRAMA_SET))

    repo_root = pathlib.Path(__file__).resolve().parent.parent.parent
    outputs = {
        "fullwidth.inc": banner("East Asian Wide/Fullwidth (width 2)", unicode_version)
        + format_intervals(wide_intervals),
        "combining.inc": banner(
            "Zero-width/combining (width 0, Mc spacing marks excluded)", unicode_version
        )
        + format_intervals(zero_intervals),
        "spacing-mark.inc": banner(
            "Spacing combining marks (category Mc, cluster caps at width 2)",
            unicode_version,
        )
        + format_intervals(spacing_mark_intervals),
        "virama.inc": banner(
            "Virama / halant conjunct linkers (cluster caps at width 2)",
            unicode_version,
        )
        + format_intervals(virama_intervals),
    }

    for source_dir in LIBVTERM_SOURCE_DIRS:
        target_dir = repo_root / source_dir
        if not target_dir.is_dir():
            print("skipping missing dir: %s" % target_dir)
            continue
        for file_name, content in outputs.items():
            target_file = target_dir / file_name
            target_file.write_text(content)
            print(
                "wrote %s (%d intervals, Unicode %s)"
                % (
                    target_file.relative_to(repo_root),
                    content.count("{"),
                    unicode_version,
                )
            )


if __name__ == "__main__":
    main()
