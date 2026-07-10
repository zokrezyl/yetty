#!/usr/bin/env bash
# scan-imports.sh — Tier 1 libc inventory from a finished .wasm binary.
#
# Runs `wasm-objdump -j Import` on the wasm artefact and extracts every
# import on the `env` module. Pipes through tools/libc-coverage.py to
# produce a markdown report joined against bridge state.
#
# Usage:
#   tools/scan-imports.sh <pkg-name> <path/to/binary.wasm>
#
# Example:
#   tools/scan-imports.sh zsh result/libexec/zsh
#
# Output: tmp/imports/<pkg>.txt (raw import list) and tmp/libc-coverage-<pkg>.md
# (markdown report). The aggregator runs against the same yos_bridge.c
# state as the Tier 2 scanner; the two reports complement each other.
set -e

PKG="${1:-}"
WASM="${2:-}"
if [ -z "$PKG" ] || [ -z "$WASM" ]; then
    echo "usage: $0 <pkg-name> <path/to/binary.wasm>" >&2
    exit 1
fi
if [ ! -f "$WASM" ]; then
    echo "scan-imports: $WASM not found" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/tmp/imports"
mkdir -p "$OUT_DIR"
LIST="$OUT_DIR/$PKG.txt"

# wasm-objdump format:
#    - func[N] sig=M <type> <- env.<name>
# Extract just `env.<name>` lines.
wasm-objdump -j Import -x "$WASM" \
    | awk '/<- env\./ { sub(".*<- env\\.", ""); print }' \
    | sort -u > "$LIST"

n=$(wc -l < "$LIST")
echo "scan-imports: $PKG → $n env imports → $LIST"

# Produce a fake per-TU JSON so libc-coverage.py can ingest it. Each
# import becomes one "call" with src=<imports> line=0 — good enough
# for the classification join.
TRACE_DIR="$ROOT/tmp/libc-trace/$PKG-imports"
mkdir -p "$TRACE_DIR"
SYNTH="$TRACE_DIR/_imports.libccalls.json"
{
    printf '{\n  "src": "<imports of %s>",\n  "diag_errors": 0,\n  "calls": [\n' "$WASM"
    first=1
    while read -r name; do
        [ -z "$name" ] && continue
        if [ $first = 1 ]; then first=0; else printf ',\n'; fi
        printf '    {"fn": "%s", "line": 0, "col": 0, "via": "import"}' "$name"
    done < "$LIST"
    printf '\n  ]\n}\n'
} > "$SYNTH"

REPORT="$ROOT/tmp/libc-coverage-$PKG.md"
(cd "$ROOT" && uv run python tools/libc-coverage.py \
    --trace-dir "$TRACE_DIR" \
    --package "$PKG (imports)" \
    --out "$REPORT")

echo "scan-imports: report → $REPORT"
