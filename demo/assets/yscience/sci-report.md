# Lab notebook — 2026-07-14

## Session summary

One terminal, one scrollback: the simulation, its diagnostics, the
reference datasets, the paper and this note — nothing left the prompt.

## Results

| experiment | headline number | status |
|---|---|---|
| three-body figure-8, RK4 | energy drift 8e-15 over one period | reproduced |
| Keeling curve (NOAA GML) | 430 ppm and climbing | updated |
| GW150914 strain (GWOSC) | peak strain 1.0e-21 at 0.42 s | matches template |
| global seismicity (USGS) | 532 events M>=4.5 in 30 days | plates visible |
| caffeine (PubChem CID 2519) | 24 atoms, 25 bonds | rendered in 3D |

## Notes

- The RK4 energy check is machine-precision flat; step size can double.
- The matched-filter act needs the log-scale axes to look right — pending.
- Next session: stream the residuals live instead of plotting post-hoc.
