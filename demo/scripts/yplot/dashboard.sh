#!/bin/bash
# yplot tool — dashboard-style sequence. Several plots stacking down the
# scrolling layer, each from a separate yplot invocation.
#
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/dashboard.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-2}"

if [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found at $YPLOT" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yplot dashboard ===\n\n'
p

# CPU.
printf 'CPU usage (last 60s, normalized):\n'
"$YPLOT" -w 520 -H 140 --xrange=0..6.28 --yrange=0..1 --no-labels \
    'cpu=0.5+0.3*sin(x)+0.1*sin(3*x)' '@cpu.color=#FF6B6B'
p

# Memory + swap.
printf '\nmemory and swap:\n'
"$YPLOT" -w 520 -H 140 --xrange=0..6.28 --yrange=0..1 --no-labels \
    'mem=0.6+0.2*sin(x/2)' \
    'swap=0.1+0.05*sin(x*4)' \
    '@mem.color=#4ECDC4' \
    '@swap.color=#AA96DA'
p

# RX/TX traffic.
printf '\nnetwork traffic (rx / tx):\n'
"$YPLOT" -w 520 -H 140 --xrange=0..6.28 --yrange=-1..1 --no-labels \
    'rx=sin(x)*cos(x/3)' \
    'tx=cos(x)*sin(x/2)' \
    '@rx.color=#95E1D3' \
    '@tx.color=#FCBF49'
p

# Polynomial.
printf '\nlatency model (cubic vs linear):\n'
"$YPLOT" -w 520 -H 160 --xrange=-2..2 --yrange=-4..4 \
    'cubic=x*x*x' \
    'linear=2*x' \
    '@cubic.color=#F38181' \
    '@linear.color=#72D6C9'

printf '\n=== done — holding open ===\n'
sleep 600
