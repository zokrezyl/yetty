#!/bin/bash
# Demo: ydraw text rendering with NON-DEFAULT system fonts.
#
# The default canvas font is DejaVu Sans Mono (monospace). This demo emits
# text spans using a different font name (resolved via fontconfig at the
# server) so the canvas materialises a fresh msdf-font instance and the
# layer dispatcher routes glyphs through it. Pure isolation test for the
# multi-font path — no PDF involved.
#
# Pick a NON-MONOSPACE family that's typically installed on Linux. If your
# system doesn't have it, fc-match will substitute the next-best face and
# the warning shows up in the trace. Override via:
#     FONT="Liberation Serif" ./text-enhanced.sh
#
# Run:
#     ./yetty -e ./demo/scripts/ydraw/scrolling/text-enhanced.sh

FONT="${FONT:-DejaVu Serif}"

YAML="body:
  - text:
      position: [50, 60]
      content: \"Hello from $FONT (system font)\"
      font: \"$FONT\"
      font-size: 32
      color: \"#ff8000\"
  - text:
      position: [50, 120]
      content: \"optimization quick brown fox jumps over the lazy dog 0123456789\"
      font: \"$FONT\"
      font-size: 24
      color: \"#80ff80\"
  - text:
      position: [50, 180]
      content: \"Same text in DEFAULT font for comparison:\"
      font-size: 18
      color: \"#aaaaaa\"
  - text:
      position: [50, 220]
      content: \"optimization quick brown fox jumps over the lazy dog 0123456789\"
      font-size: 24
      color: \"#ffffff\"
"

PAYLOAD=$(printf '%s' "$YAML" | base64 -w0)

# OSC 600002 = ydraw canvas (YAML payload).
printf '\033]600002;;%s\007' "$PAYLOAD"

