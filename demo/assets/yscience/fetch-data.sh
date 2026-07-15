#!/bin/bash
# Fetch the raw scientific datasets behind the sci-tour assets.
#
#   ./fetch-data.sh [target-dir]     (default: tmp/yscience-raw)
#
# The committed chart/mesh assets in this directory are SNAPSHOTS derived
# from these downloads (see gen/make-charts.py, gen/make-molecule-glb.py and
# README.md for provenance). The clip plays from the committed files; run
# this only to refresh the snapshots, or at record time for the assets that
# are fetched rather than committed (the open-access paper PDF).
#
# All sources are freely licensed / public data services; each download is
# best-effort so an offline run degrades instead of aborting.

set -u
TARGET_DIR="${1:-tmp/yscience-raw}"
mkdir -p "$TARGET_DIR"

fetch() {
    local output_name="$1" url="$2"
    if [ -s "$TARGET_DIR/$output_name" ]; then
        echo "have  $output_name"
        return 0
    fi
    if curl -sL --max-time 90 -o "$TARGET_DIR/$output_name" "$url"; then
        echo "fetch $output_name"
    else
        echo "FAILED $output_name ($url)" >&2
        rm -f "$TARGET_DIR/$output_name"
    fi
}

# NOAA GML: Mauna Loa monthly mean CO2 (the Keeling curve). Public data.
fetch co2_mm_mlo.csv \
    "https://gml.noaa.gov/webdata/ccgg/trends/co2/co2_mm_mlo.csv"

# NASA GISS: GISTEMP v4 global land-ocean temperature anomaly. Public data.
fetch gistemp.csv \
    "https://data.giss.nasa.gov/gistemp/tabledata_v4/GLB.Ts+dSST.csv"

# GWOSC: GW150914 figure-1 strain data (observed H1 + NR template), CC.
fetch gw-observed-H.txt \
    "https://gwosc.org/s/events/GW150914/P150914/fig1-observed-H.txt"
fetch gw-template-H.txt \
    "https://gwosc.org/s/events/GW150914/P150914/fig1-waveform-H.txt"

# USGS: M>=4.5 earthquakes, last 30 days (live feed - snapshot varies).
fetch quakes-45-month.csv \
    "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_month.csv"
fetch quakes-45-month.geojson \
    "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/4.5_month.geojson"

# PubChem: caffeine (CID 2519) 3D conformer, SDF. Public data.
fetch caffeine.sdf \
    "https://pubchem.ncbi.nlm.nih.gov/rest/pug/compound/CID/2519/record/SDF?record_type=3d"

# arXiv: the GW150914 discovery paper (PRL 116, 061102), open access CC-BY.
# Fetched at record time, not committed (~1.5 MB binary).
fetch gw150914-paper.pdf \
    "https://arxiv.org/pdf/1602.03837"
