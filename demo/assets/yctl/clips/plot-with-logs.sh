#!/bin/bash
set -e

echo "==> build started"
echo "[1/5] compile parser.c"
echo "[2/5] compile renderer.c"
echo "[3/5] link cli"
echo
echo "latency during the same run:"
yplot -w 720 -H 220 --xrange=0..6.28 --yrange=0..1.2 \
    'p50=0.35+0.08*sin(x*2)' \
    'p95=0.7+0.18*sin(x*3)+0.08*cos(x*5)' \
    'errors=0.08+0.06*sin(x*8)' \
    '@p50.color=#4ECDC4' \
    '@p95.color=#FF6B6B' \
    '@errors.color=#F7B801'
echo
echo "[4/5] integration test: rich output stayed in scrollback"
echo "[5/5] package artifact"
echo "==> deploy candidate ready"
