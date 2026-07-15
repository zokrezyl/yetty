#!/usr/bin/env python3
"""Derive the committed sci-tour chart assets from the raw downloads.

Inputs are the files fetched by ../fetch-data.sh (pass the download
directory as argv[1]); outputs are written next to this script's parent
directory as ychart JSON documents. Re-run only to refresh the committed
snapshots — the clip plays from the committed files, not from the network.

    ./fetch-data.sh tmp/yscience-raw
    ./gen/make-charts.py tmp/yscience-raw

Pure stdlib on purpose: no numpy/pandas dependency for a demo asset.
"""

import csv
import json
import sys
from pathlib import Path


def write_chart(output_path: Path, document: dict) -> None:
    output_path.write_text(json.dumps(document, separators=(",", ":")) + "\n")
    print(f"wrote {output_path} ({output_path.stat().st_size} bytes)")


def keeling_curve(raw_dir: Path, out_dir: Path) -> None:
    """NOAA GML Mauna Loa monthly mean CO2 -> scatter over decimal year."""
    points = []
    with open(raw_dir / "co2_mm_mlo.csv") as raw_file:
        rows = csv.reader(line for line in raw_file if not line.startswith("#"))
        header = next(rows)
        decimal_date_column = header.index("decimal date")
        average_column = header.index("average")
        for row in rows:
            average_ppm = float(row[average_column])
            if average_ppm < 0:  # -9.99 marks a missing month
                continue
            points.append({"x": round(float(row[decimal_date_column]), 3),
                           "y": round(average_ppm, 2)})
    write_chart(out_dir / "co2-keeling.json", {
        "chart": "scatter",
        "title": "Atmospheric CO2 at Mauna Loa - the Keeling curve (NOAA GML)",
        "x": "year",
        "y": "CO2 (ppm)",
        "series": [{"name": "monthly mean", "values": points}],
    })


def gistemp_anomaly(raw_dir: Path, out_dir: Path) -> None:
    """NASA GISTEMP v4 global land-ocean annual (J-D) anomaly vs 1951-1980."""
    points = []
    with open(raw_dir / "gistemp.csv") as raw_file:
        lines = raw_file.read().splitlines()
    header = lines[1].split(",")
    annual_column = header.index("J-D")
    for line in lines[2:]:
        row = line.split(",")
        annual_value = row[annual_column]
        if not row[0].isdigit() or annual_value.startswith("*"):
            continue
        points.append({"x": int(row[0]), "y": float(annual_value)})
    write_chart(out_dir / "gistemp-anomaly.json", {
        "chart": "scatter",
        "title": "Global mean temperature anomaly vs 1951-1980 (NASA GISTEMP v4)",
        "x": "year",
        "y": "anomaly (deg C)",
        "series": [{"name": "annual mean", "values": points}],
    })
    # Column form for yplot --data (year anomaly, whitespace-separated).
    annual_path = out_dir / "gistemp-annual.txt"
    with open(annual_path, "w") as out:
        out.write("# NASA GISTEMP v4 global land-ocean annual (J-D) anomaly, deg C\n")
        for point in points:
            out.write(f"{point['x']} {point['y']}\n")
    print(f"wrote {annual_path} ({annual_path.stat().st_size} bytes)")


def read_strain_series(path: Path, decimate: int) -> list:
    points = []
    with open(path) as strain_file:
        for line_index, line in enumerate(strain_file):
            if line.startswith("#"):
                continue
            if line_index % decimate:
                continue
            time_seconds, strain_scaled = line.split()
            points.append({"x": round(float(time_seconds), 4),
                           "y": round(float(strain_scaled), 3)})
    return points


def gw150914(raw_dir: Path, out_dir: Path) -> None:
    """GWOSC GW150914 figure-1 data: H1 observed strain vs NR template.

    Source files carry strain already scaled by 1e21; time axis is seconds
    around the event. Decimated 4:1 (16384 Hz -> 4096 Hz), far above the
    ~300 Hz chirp content.
    """
    observed = read_strain_series(raw_dir / "gw-observed-H.txt", decimate=4)
    template = read_strain_series(raw_dir / "gw-template-H.txt", decimate=4)
    write_chart(out_dir / "gw150914.json", {
        "chart": "scatter",
        "title": "GW150914 - first gravitational-wave detection, LIGO Hanford (GWOSC)",
        "x": "time (s)",
        "y": "strain (1e-21)",
        "series": [
            {"name": "H1 observed", "values": observed},
            {"name": "NR template", "values": template},
        ],
    })


def quakes_world(raw_dir: Path, out_dir: Path) -> None:
    """USGS M>=4.5 last-30-days feed -> lon/lat scatter (plate boundaries)."""
    points = []
    with open(raw_dir / "quakes-45-month.csv") as raw_file:
        for row in csv.DictReader(raw_file):
            points.append({"x": round(float(row["longitude"]), 2),
                           "y": round(float(row["latitude"]), 2)})
    write_chart(out_dir / "quakes-world.json", {
        "chart": "scatter",
        "title": f"Global seismicity, M>=4.5, last 30 days - {len(points)} events (USGS)",
        "x": "longitude",
        "y": "latitude",
        "series": [{"name": "epicenters", "values": points}],
    })


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    raw_dir = Path(sys.argv[1])
    out_dir = Path(__file__).resolve().parent.parent
    keeling_curve(raw_dir, out_dir)
    gistemp_anomaly(raw_dir, out_dir)
    gw150914(raw_dir, out_dir)
    quakes_world(raw_dir, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
