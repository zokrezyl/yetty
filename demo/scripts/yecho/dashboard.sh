#!/bin/bash
# yecho dashboard demo — text + glyphs + yplot all driven through the same
# tool, all flowing through OSC 600001 (ydraw scrolling layer).
#
# Run inside yetty:  ./build-desktop-ytrace-release/yetty -e demo/scripts/yecho/dashboard.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YECHO="${YECHO:-$ROOT/build-desktop-ytrace-release/tools/yecho/yecho}"
PAUSE="${DEMO_PAUSE:-2}"

if [ ! -x "$YECHO" ]; then
    YECHO="$(command -v "${YECHO##*/}" 2>/dev/null || true)"
fi
if [ -z "$YECHO" ] || [ ! -x "$YECHO" ]; then
    echo "yecho binary not found in build dir or on \$PATH — set YECHO=path/to/yecho" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

# Header.
"$YECHO" '@gem  {color=#FFE66D; style=bold: Yetty Status Dashboard}  @gem'
p

# Status indicators row.
"$YECHO" 'CPU @bouncing-ball  Memory @pulse  Network @signal-bars  Disk @hourglass'
p

# CPU history plot.
"$YECHO" '{color=#888888: CPU usage (last 60s, normalized)}'
"$YECHO" '{plot; w=520; h=140; xrange=0..6.28; yrange=0..1; nolabels:
    cpu=0.5+0.3*sin(x)+0.1*sin(3*x);
    @cpu.color=#FF6B6B
}'
p

# Memory + swap plot.
"$YECHO" '{color=#888888: Memory and swap}'
"$YECHO" '{plot; w=520; h=140; xrange=0..6.28; yrange=0..1; nolabels:
    mem=0.6+0.2*sin(x/2);
    swap=0.1+0.05*sin(x*4);
    @mem.color=#4ECDC4;
    @swap.color=#AA96DA
}'
p

# Multi-trace overlay.
"$YECHO" '{color=#888888: Network throughput (rx / tx)}'
"$YECHO" '{plot; w=520; h=140; xrange=0..6.28; yrange=-1..1; nolabels:
    rx=sin(x)*cos(x/3);
    tx=cos(x)*sin(x/2);
    @rx.color=#95E1D3;
    @tx.color=#FCBF49
}'
p

# Footer.
"$YECHO" '{color=#888888: refreshed} @typing-dots'

printf '\n=== done — holding open ===\n'
sleep 600
