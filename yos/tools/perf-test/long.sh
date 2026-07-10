#!/usr/bin/env bash
# long.sh — perf-stress with the built-in `-l` preset (~5× default).
# Equivalent to: -f 100 -d 10,10,5 -p 16 -e 50 -r 16 -k 20 -T 32
# -R 12 -t 20 -i 5. ~12 s wall-clock on a release build.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p tmp
LOG="tmp/perf-long.log"
./tools/yos.sh perf-stress -l > "$LOG" 2>&1
echo "--- $LOG (tail) ---"
tail -30 "$LOG"
grep -q "^perf-stress ok$" "$LOG"
