#!/usr/bin/env bash
# wide-deep.sh — perf-stress beyond the `-l` preset. ~22 s wall-clock
# on a release build. Pushes:
#   - 4-level fork tree (10×10×5×2 = 1610 procs across the run)
#   - 902 chaos procs (50/round × ~32 rounds, near YOS_MAX_PROCS=256
#     concurrent live)
#   - 64 pthreads + 663 chaos threads (lock churn × iter_mult=10)
#   - 4 MiB single-write throughput probe
# Intended as a heavier confidence run before merging any bridge
# change that touches fork / pthread / fd-map / mmap.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p tmp
./tools/yos.sh perf-stress \
    -f 200 -d 10,10,5,2 -p 32 -e 100 \
    -r 32 -k 50 -T 64 -R 24 -t 50 -i 10 -o 4096 
