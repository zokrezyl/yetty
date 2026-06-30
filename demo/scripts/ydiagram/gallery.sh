#!/bin/bash
# ydiagram — full gallery. Renders every Mermaid asset in demo/assets/ydiagram/
# once: the five diagram families (flowchart, state, ER, sequence, subgraphs)
# plus the node-shape catalogue. Each emits an OSC YDRAW_BIN envelope that
# scrolls into the host yetty ydraw layer.
#
# Usage (from inside yetty):
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ydiagram/gallery.sh

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YDIAGRAM="${YDIAGRAM:-$ROOT/build-desktop-ytrace-release/tools/ydiagram/ydiagram}"
ASSETS="$ROOT/demo/assets/ydiagram"
PAUSE="${DEMO_PAUSE:-0}"

if [ ! -x "$YDIAGRAM" ]; then
    YDIAGRAM="$(command -v "${YDIAGRAM##*/}" 2>/dev/null || true)"
fi
if [ -z "$YDIAGRAM" ] || [ ! -x "$YDIAGRAM" ]; then
    echo "ydiagram binary not found in build dir or on \$PATH — set YDIAGRAM=path/to/ydiagram" >&2
    exit 1
fi

p() { sleep "$PAUSE"; }

# asset → one-line description
show() {
    echo
    echo "\$ ydiagram demo/assets/ydiagram/$1"
    [ -n "$2" ] && echo "  # $2"
    "$YDIAGRAM" "$ASSETS/$1"
    p
}

printf '=== ydiagram gallery — families + shapes ===\n'
p

show directions.mmd        "flowchart — node/edge layout in all directions"
show edges-arrows.mmd      "flowchart — every arrow/edge style"
show subgraphs.mmd         "flowchart — nested subgraphs"
show long-edges.mmd        "flowchart — long routed edges"
show state-machine.mmd     "state diagram — transitions"
show state-diagram.mmd     "state diagram — initial/final pseudostates"
show er-diagram.mmd        "entity-relationship — crow's-foot cardinality"
show class-diagram.mmd     "UML class — inheritance / composition"
show sequence-diagram.mmd  "sequence — participants and messages"
show cycle.mmd             "flowchart — a cyclic graph"

printf '\n--- node shapes ---\n'
for f in shapes-rectangle shapes-rounded-rect shapes-stadium shapes-circle \
         shapes-diamond shapes-hexagon shapes-parallelogram shapes-cylinder \
         shapes-all; do
    show "$f.mmd"
done

printf '\n=== done ===\n'
