#!/bin/bash
# yplot tool — the error function.
#
# Showcases the erf / erfc opcodes (Abramowitz & Stegun rational
# approximation). erf is the integral of the Gaussian and the backbone of
# the normal distribution's CDF; erfc = 1 - erf is its complement.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/error-function.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YPLOT="${YPLOT:-$ROOT/build-desktop-ytrace-release/tools/yplot/yplot}"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YPLOT" ]; then
    YPLOT="$(command -v "${YPLOT##*/}" 2>/dev/null || true)"
fi
if [ -z "$YPLOT" ] || [ ! -x "$YPLOT" ]; then
    echo "yplot binary not found in build dir or on \$PATH" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== yplot — error function ===\n\n'
p

# (1) erf and its complement erfc. erf saturates to +/-1; erfc runs from 2
# down to 0. erfc is what survives in the tail probabilities.
printf '(1) erf(x) and erfc(x):\n'
"$YPLOT" -w 600 -H 240 --xrange=-3..3 --yrange=-1.2..2.2 \
    'erf_x=erf(x)' \
    'erfc_x=erfc(x)' \
    '@erf_x.color=#74C5A5' \
    '@erfc_x.color=#F38181'
p

# (2) The normal CDF, Phi(x) = (1 + erf(x/sqrt2)) / 2 — the cumulative
# probability of the standard bell curve, beside the bell (pdf) itself.
printf '\n(2) Normal CDF  Phi(x)  with its bell-curve pdf:\n'
"$YPLOT" -w 600 -H 240 --xrange=-4..4 --yrange=-0.1..1.1 \
    'cdf=0.5*(1+erf(x/sqrt(2)))' \
    'pdf=exp(-x*x/2)/sqrt(2*pi)' \
    '@cdf.color=#6BA892' \
    '@pdf.color=#FFE66D'
p

# (3) erf next to tanh — two look-alike sigmoids. erf rises a touch steeper
# through the middle and is the one with a probabilistic meaning.
printf '\n(3) erf vs tanh — sigmoid look-alikes:\n'
"$YPLOT" -w 600 -H 240 --xrange=-3..3 --yrange=-1.2..1.2 \
    'erf_x=erf(x)' \
    'tanh_x=tanh(x)' \
    '@erf_x.color=#74C5A5' \
    '@tanh_x.color=#556162'

printf '\n=== done — holding open ===\n'
