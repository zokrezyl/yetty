#!/bin/bash
# ynet — network capture and observability. Renders a capture's conversations
# as a GPU *topology figure* in the host yetty pane: hosts are nodes on a ring,
# each conversation an edge coloured by transport protocol and thickened by
# traffic, node size by degree. This is the payoff a plain packet table cannot
# express — the figure ships to the pane as a YDRAW_BIN OSC envelope (the same
# path ycat / yflame use).
#
# Usage — run inside a real yetty window so the figure renders:
#   ./build-desktop-ytrace-release/yetty -e demo/scripts/ynet/basic.sh
#
# The plain-text packet table is still available for pipelines: `ynet --dump FILE`.

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
YNET="${YNET:-$ROOT/build-desktop-ytrace-release/tools/ynet/ynet}"
ASSETS="$ROOT/demo/assets/ynet"
SAMPLE="$ASSETS/sample.pcap"
PAUSE="${DEMO_PAUSE:-0}"

# Probe: prefer the build-dir binary, else fall back to `ynet` on $PATH.
if [ ! -x "$YNET" ]; then
    YNET="$(command -v "${YNET##*/}" 2>/dev/null || true)"
fi
if [ -z "$YNET" ] || [ ! -x "$YNET" ]; then
    echo "ynet binary not found in build dir or on \$PATH — set YNET=path/to/ynet" >&2
    exit 1
fi

# Regenerate the committed sample capture if it is missing (needs python3).
if [ ! -f "$SAMPLE" ]; then
    if command -v python3 >/dev/null 2>&1; then
        python3 "$ASSETS/make-sample.py" "$SAMPLE" >/dev/null
    else
        echo "sample capture missing and python3 unavailable: $SAMPLE" >&2
        exit 1
    fi
fi

p() { [ "$PAUSE" = 0 ] || sleep "$PAUSE"; }

printf '=== ynet — flow topology figure ===\n\n'
p

# A bare YDRAW_BIN envelope is raw bytes to a plain terminal — only meaningful
# inside yetty (or a tmux hosted by yetty). Outside, fall back to the text table
# so the demo is never just garbage on screen.
rendered_figure=0
case "${TERM_PROGRAM:-}" in
    yetty | tmux)
        echo "\$ ynet $SAMPLE      # renders the topology graph in this pane"
        "$YNET" "$SAMPLE"
        rendered_figure=1
        ;;
    *)
        echo "(not inside yetty — showing the text table; run via 'yetty -e' for the figure)" >&2
        echo "\$ ynet --dump $SAMPLE"
        "$YNET" --dump "$SAMPLE"
        ;;
esac
p

printf '\n=== done ===\n'
echo "(figure: hosts = nodes, conversations = protocol-coloured edges · TCP mint, UDP blue, ICMP amber)"
echo "(also: ynet --dump FILE for the plain-text packet table · next: interactive pane + live capture)"

# When launched as `yetty -e demo/scripts/ynet/basic.sh`, the yetty session ends
# the instant this script returns — closing the pane and taking the figure with
# it (the figure is anchored in scrollback and persists fine; it is the SESSION
# ending, not the figure being cleared). Hold the pane open so the graph stays
# visible. Guarded on a TTY so piped / automated runs never block.
if [ "$rendered_figure" = 1 ]; then
    printf '\n(the figure stays in scrollback — press Enter to close this demo pane)\n'
    # Read from the controlling terminal, not stdin: under `yetty -e` stdin is
    # not always a TTY, and a plain `read` would then return instantly and let
    # the pane close. `< /dev/tty` blocks on the real terminal; the `|| true`
    # keeps non-interactive runs (no controlling tty) from erroring out.
    read -r _ < /dev/tty 2>/dev/null || true
fi
