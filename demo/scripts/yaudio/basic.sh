#!/bin/bash
# yaudio — the audio-analyzer app: opens a window showing a WAV's RMS envelope
# as a yplot, with Prev/Next buttons (and ←/→) that walk the detected
# noise intervals.
#
# NOTE: yaudio is a STANDALONE app — it brings up its own window/GPU surface,
# it does NOT emit into a host terminal's ydraw layer. So, unlike the scrolling
# and interactive figure demos, this one is launched directly and is NOT part
# of the single-window all.sh tour.
#
# There is no committed audio asset, so this script synthesizes a short WAV
# (three noise/tone bursts separated by silence — good material for the
# interval walker) with python3, then opens it in yaudio.
#
# Usage (run directly, not via -e inside another yetty):
#   demo/scripts/yaudio/basic.sh
#   YAUDIO_WAV=/path/to/your.wav demo/scripts/yaudio/basic.sh   # use your own

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YAUDIO="${YAUDIO:-$ROOT/build-desktop-ytrace-release/tools/yaudio/yaudio}"

if [ ! -x "$YAUDIO" ]; then
    YAUDIO="$(command -v "${YAUDIO##*/}" 2>/dev/null || true)"
fi
if [ -z "$YAUDIO" ] || [ ! -x "$YAUDIO" ]; then
    echo "yaudio binary not found in build dir or on \$PATH — set YAUDIO=path/to/yaudio" >&2
    exit 1
fi

WAV="${YAUDIO_WAV:-}"
CLEANUP=""
if [ -z "$WAV" ]; then
    if ! command -v python3 >/dev/null 2>&1; then
        echo "no committed WAV asset and python3 not found — set YAUDIO_WAV=path/to/file.wav" >&2
        exit 1
    fi
    WAV="$(mktemp --suffix=.wav 2>/dev/null || mktemp)"
    CLEANUP="$WAV"
    # Synthesize 6 s @ 22050 Hz mono: three 1 s bursts (a tone, white noise,
    # a chord) each followed by ~0.5 s of silence, so the RMS envelope has
    # clearly separated intervals for the Prev/Next walker.
    python3 - "$WAV" <<'PY'
import math, struct, sys, wave, random

rate = 22050
def tone(freqs, dur, amp=0.35):
    n = int(rate * dur)
    for i in range(n):
        t = i / rate
        s = sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs)
        yield int(max(-1.0, min(1.0, amp * s)) * 32767)

def noise(dur, amp=0.30):
    for _ in range(int(rate * dur)):
        yield int(amp * (random.random() * 2 - 1) * 32767)

def silence(dur):
    for _ in range(int(rate * dur)):
        yield 0

samples = []
samples += list(tone([440.0], 1.0))          # A4 tone
samples += list(silence(0.5))
samples += list(noise(1.0))                   # white-noise burst
samples += list(silence(0.5))
samples += list(tone([261.6, 329.6, 392.0], 1.0))  # C-major chord
samples += list(silence(0.5))

with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(b"".join(struct.pack("<h", s) for s in samples))
PY
    trap 'rm -f "$CLEANUP"' EXIT
fi

printf '=== yaudio — RMS envelope + noise-interval walker ===\n'
printf 'Prev/Next buttons or Left/Right arrows walk the intervals; q/close to quit\n\n'

exec "$YAUDIO" "$WAV"
