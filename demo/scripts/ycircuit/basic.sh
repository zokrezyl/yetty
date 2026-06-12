#!/bin/bash
# ycircuit — basic showcase. Runs ycat on the real-circuit assets from
# demo/assets/ycircuit/ and lets the OSC envelopes scroll into the ydraw
# layer of the host yetty terminal.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ycircuit/basic.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YCAT="${YCAT:-$ROOT/build-desktop-ytrace-release/tools/ycat/ycat}"
ASSETS="$ROOT/demo/assets/ycircuit"
PAUSE="${DEMO_PAUSE:-2}"

if [ ! -x "$YCAT" ]; then
    echo "ycat binary not found at $YCAT — set YCAT=path/to/ycat" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

printf '=== ycircuit basics ===\n\n'
p

echo '$ ycat demo/assets/ycircuit/voltage-divider.circuit'
"$YCAT" "$ASSETS/voltage-divider.circuit"
p

echo
echo '$ ycat demo/assets/ycircuit/rc-lowpass.circuit'
"$YCAT" "$ASSETS/rc-lowpass.circuit"
p

echo
echo '$ ycat demo/assets/ycircuit/rectifier.circuit'
"$YCAT" "$ASSETS/rectifier.circuit"
p

echo
echo '$ ycat demo/assets/ycircuit/inverting-amp.circuit'
"$YCAT" "$ASSETS/inverting-amp.circuit"
p

echo
echo '$ ycat demo/assets/ycircuit/555-blinker.circuit'
"$YCAT" "$ASSETS/555-blinker.circuit"
p

# Piped through stdin — no extension hint; the leading `circuit` keyword
# is sniffed by ycat's content detector.
echo
echo '$ cat demo/assets/ycircuit/common-emitter.circuit | ycat -'
cat "$ASSETS/common-emitter.circuit" | "$YCAT" -

printf '\n=== done ===\n'
