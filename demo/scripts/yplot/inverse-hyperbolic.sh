#!/bin/bash
# yplot tool — inverse hyperbolic functions.
#
# Showcases the built-in `asinh`, `acosh`, and `atanh` opcodes. Each is the
# inverse of the corresponding sinh/cosh/tanh and shows up as the natural
# coordinate for a real problem: signed-log compression, rapidity, and the
# Fisher transform.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/inverse-hyperbolic.sh

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

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== yplot — inverse hyperbolic functions ===\n\n'
p

# (1) asinh as a signed-log compressor. It is linear near zero and grows
# only logarithmically in the tails, but unlike log it handles zero and
# negatives. This is the "asinh stretch" used to display astronomical images
# and to tame heavy-tailed data.
printf '(1) asinh — signed-log compression (linear core, log tails):\n'
"$YPLOT" -w 600 -H 240 --xrange=-10..10 --yrange=-3.5..3.5 \
    'identity=x' \
    'signed_log=sign(x)*log(1+abs(x))' \
    'arcsinh=asinh(x)' \
    '@identity.color=#364A47' \
    '@signed_log.color=#556162' \
    '@arcsinh.color=#74C5A5'
p

# (2) atanh, the Fisher z-transform. Defined on (-1, 1) with vertical
# asymptotes at the ends, it turns a bounded correlation coefficient into an
# unbounded, roughly-normal variable. Also the logit's close cousin.
printf '\n(2) atanh — Fisher transform, blows up at +/-1:\n'
"$YPLOT" -w 600 -H 240 --xrange=-0.99..0.99 --yrange=-3..3 \
    'fisher=atanh(x)' \
    '@fisher.color=#6BA892'
p

# (3) acosh, defined for x >= 1. It is the rapidity of a relativistic
# Lorentz factor and the arc-length parameter of the catenary. Note the
# vertical-tangent start at x = 1.
printf '\n(3) acosh — rapidity / catenary arc length (x >= 1):\n'
"$YPLOT" -w 600 -H 240 --xrange=1..10 --yrange=-0.2..3.2 \
    'arccosh=acosh(x)' \
    '@arccosh.color=#FFE66D'
p

# (4) The three together against the diagonal, each on the part of its
# domain where it is real, to compare growth rates.
printf '\n(4) asinh vs atanh vs acosh:\n'
"$YPLOT" -w 600 -H 240 --xrange=-3..3 --yrange=-3..3 \
    'arcsinh=asinh(x)' \
    'fisher=atanh(x)' \
    'arccosh=acosh(x)' \
    '@arcsinh.color=#74C5A5' \
    '@fisher.color=#FF6B6B' \
    '@arccosh.color=#FFE66D'

printf '\n=== done — holding open ===\n'
