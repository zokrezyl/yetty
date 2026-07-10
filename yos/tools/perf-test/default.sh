#!/usr/bin/env bash
# default.sh — perf-stress at its built-in defaults (~111 procs total,
# 8 pthreads, 4 chaos-proc rounds, 3 chaos-thread rounds). Smoke test:
# fastest of the three runners; confirms the bridge surface still
# functions before reaching for heavier configs.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p tmp
LOG="tmp/perf-default.log"
./tools/yos.sh perf-stress > "$LOG" 2>&1
echo "--- $LOG (tail) ---"
tail -30 "$LOG"
grep -q "^perf-stress ok$" "$LOG"
