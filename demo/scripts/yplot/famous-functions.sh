#!/bin/bash
# yplot tool — famous applications of basic functions.
#
# A captioned gallery: each entry is a pure y=f(x) expression that shows up
# in real physics / signals / statistics. Nothing here needs buffers or
# external data — just the built-in function set, including the
# singularity-safe `sinc` and the `smoothstep` easing curve.
#
# Run inside a yetty session, e.g.:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/yplot/famous-functions.sh

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

printf '=== yplot — famous applications of basic functions ===\n\n'
p

# (1) Fourier synthesis of a square wave. One sine, then six odd harmonics
# weighted 1/n. The persistent overshoot at the jumps is the Gibbs
# phenomenon — it never goes away, it just narrows.
printf '(1) Fourier square wave — Gibbs phenomenon:\n'
"$YPLOT" -w 600 -H 240 --xrange=-6.28..6.28 --yrange=-1.4..1.4 \
    'target=sign(sin(x))' \
    'first=4/pi*sin(x)' \
    'sum=4/pi*(sin(x)+sin(3*x)/3+sin(5*x)/5+sin(7*x)/7+sin(9*x)/9+sin(11*x)/11)' \
    '@target.color=#556162' \
    '@first.color=#74C5A5' \
    '@sum.color=#FF6B6B'
p

# (2) Damped harmonic oscillator: a sine inside a decaying exponential
# envelope. The ringdown of an RLC circuit, a plucked string, a car
# suspension settling after a bump.
printf '\n(2) Damped harmonic oscillator — ringdown:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..12.56 --yrange=-1.1..1.1 \
    'envelope=exp(-x/4)' \
    'ring=sin(x*3)*exp(-x/4)' \
    '@envelope.color=#364A47' \
    '@ring.color=#6BA892'
p

# (3) Two bell-shaped peaks that look alike but are not: the Gaussian
# (normal distribution / heat diffusion / image blur) falls off far faster
# in the tails than the Lorentzian (a resonance line / forced oscillator).
printf '\n(3) Gaussian vs Lorentzian — bell curves:\n'
"$YPLOT" -w 600 -H 240 --xrange=-6..6 --yrange=-0.1..1.15 \
    'gaussian=exp(-x*x/2)' \
    'lorentzian=1/(1+x*x)' \
    '@gaussian.color=#6BA892' \
    '@lorentzian.color=#FFE66D'
p

# (4) Saturating S-curves. The logistic sigmoid is the neuron activation and
# the epidemic / technology-adoption curve; tanh is the same shape centred
# differently and is the other classic activation.
printf '\n(4) Logistic sigmoid vs tanh — S-curves:\n'
"$YPLOT" -w 600 -H 240 --xrange=-6..6 --yrange=-1.1..1.1 \
    'logistic=1/(1+exp(-2*x))' \
    'hyperbolic=tanh(x)' \
    '@logistic.color=#74C5A5' \
    '@hyperbolic.color=#F38181'
p

# (5) The catenary: cosh(x), the exact shape a chain or power line takes
# under its own weight (NOT a parabola, though it looks close near the
# bottom). Flip it and you have the ideal free-standing arch.
printf '\n(5) Catenary — a hanging chain (cosh):\n'
"$YPLOT" -w 600 -H 240 --xrange=-2.5..2.5 --yrange=0..6.5 \
    'chain=cosh(x)' \
    '@chain.color=#6BA892'
p

# (6) The cardinal sine, sin(x)/x. The diffraction pattern of a single slit
# and the ideal reconstruction filter for a sampled signal. Note the clean
# peak of 1 at the origin — the built-in `sinc` opcode removes the 0/0
# singularity that bare `sin(x)/x` leaves behind.
printf '\n(6) sinc — diffraction / signal reconstruction:\n'
"$YPLOT" -w 600 -H 240 --xrange=-15.7..15.7 --yrange=-0.3..1.1 \
    'cardinal=sinc(x)' \
    '@cardinal.color=#74C5A5'
p

# (7) A wave packet: a carrier wave under a Gaussian envelope. The picture
# of a localised quantum particle, and of a short pulse in optics or radar.
printf '\n(7) Wave packet — Gaussian-enveloped carrier:\n'
"$YPLOT" -w 600 -H 240 --xrange=-6..6 --yrange=-1.1..1.1 \
    'packet=exp(-x*x/4)*cos(x*6)' \
    'envelope=exp(-x*x/4)' \
    '@envelope.color=#364A47' \
    '@packet.color=#6BA892'
p

# (8) Easing with smoothstep: a cubic Hermite curve with zero slope at both
# ends, versus a straight linear ramp. The standard "ease in/out" used in
# animation and procedural shading — here straight from the `smoothstep`
# opcode.
printf '\n(8) smoothstep easing vs linear ramp:\n'
"$YPLOT" -w 600 -H 240 --xrange=0..1 --yrange=-0.1..1.1 \
    'linear=x' \
    'eased=smoothstep(0,1,x)' \
    '@linear.color=#556162' \
    '@eased.color=#74C5A5'

printf '\n=== done — holding open ===\n'
