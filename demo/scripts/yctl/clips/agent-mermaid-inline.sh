#!/bin/bash
# Records the "AI agent Mermaid inline" clip.
#
#   ./demo/scripts/yctl/clips/agent-mermaid-inline.sh
#   # -> tmp/clips/agent-mermaid-inline/agent-mermaid-inline.mp4

set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
. "$DIR/_record-common.sh"

SCENARIO="$ROOT/demo/assets/yctl/clips/agent-mermaid-inline.yaml"
STAGING_DIR="$ROOT/tmp/clips/agent-mermaid-inline"

mkdir -p "$STAGING_DIR"
cp "$ROOT/demo/assets/yctl/clips/agent-mermaid-inline.mmd" "$STAGING_DIR/agent-plan.mmd"

record_clip "$SCENARIO" "$STAGING_DIR" "agent-mermaid-inline.mp4"
