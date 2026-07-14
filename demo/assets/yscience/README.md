# yscience — real datasets behind the sci-tour clip

Assets for the scientific showcase playbook (`demo/assets/yctl/clips/
sci-tour.yaml`). Everything here is either real measured/computed data or a
generator that derives it; nothing is synthetic filler. The clip plays from
the committed snapshots so a recording never depends on the network (the
one exception, the paper PDF, is fetched at record time and skipped
cleanly when offline).

## Committed data snapshots

| file | contents | source | license/terms |
|---|---|---|---|
| `co2-keeling.json` | Mauna Loa monthly mean CO₂, 1958→ (the Keeling curve) | [NOAA GML trends](https://gml.noaa.gov/ccgg/trends/data.html) `co2_mm_mlo.csv` | public data, cite NOAA GML |
| `gistemp-anomaly.json` | global land-ocean annual temperature anomaly vs 1951-1980, 1880→ | [NASA GISTEMP v4](https://data.giss.nasa.gov/gistemp/) `GLB.Ts+dSST.csv` | public data, cite NASA GISS |
| `gw150914.json` | GW150914 H1 observed strain + numerical-relativity template (×10²¹, 4:1 decimated) | [GWOSC GW150914 fig-1 data](https://gwosc.org/s/events/GW150914/) | open data, cite GWOSC |
| `quakes-world.json` | epicenters of all M≥4.5 earthquakes in the 30 days before the snapshot date | [USGS earthquake feed](https://earthquake.usgs.gov/earthquakes/feed/) `4.5_month.csv` | public domain (USGS) |
| `caffeine.glb` | ball-and-stick mesh of caffeine, built from the PubChem 3D conformer | [PubChem CID 2519](https://pubchem.ncbi.nlm.nih.gov/compound/2519) SDF | public data, cite PubChem |

Snapshot date for the live feeds (USGS, NOAA/GISTEMP tails): **2026-07-14**.

## Scripts and generated-at-play-time data

| file | role |
|---|---|
| `fetch-data.sh` | downloads the raw sources above + the open-access GW150914 discovery paper (arXiv:1602.03837, CC-BY) |
| `gen/make-charts.py` | raw downloads → the committed chart JSON snapshots |
| `gen/make-molecule-glb.py` | PubChem SDF → `caffeine.glb` (single-primitive GLB: POSITION + NORMAL + uint16 indices, the shape ymesh consumes) |
| `solver-threebody.py` | runs *live inside the clip*: RK4 integration of the Chenciner-Montgomery figure-8 three-body orbit; logs the energy-conservation check, writes its trajectories as a ychart scatter document |
| `waves.wgsl` | two-source wave interference evaluated per pixel (shadertoy figure) |
| `pipeline.mmd` | the analysis-pipeline DAG (Mermaid) |
| `sci-report.md` | the closing "lab notebook" markdown |

## Refreshing the snapshots

```sh
./demo/assets/yscience/fetch-data.sh tmp/yscience-raw
./demo/assets/yscience/gen/make-charts.py tmp/yscience-raw
./demo/assets/yscience/gen/make-molecule-glb.py tmp/yscience-raw/caffeine.sdf \
    demo/assets/yscience/caffeine.glb
```

Update the snapshot date above (and in `quakes-world.json`'s title, which
carries the event count) when refreshing.

## Recording the clip

```sh
./demo/scripts/yctl/clips/sci-tour.sh
# -> tmp/clips/sci-tour/sci-tour.mp4
```
