#!/bin/bash
# Test scroll sync between text-layer and ydraw-layer
# Alternates between text output and ydraw content

SCRIPT_DIR="$(dirname "$0")"

for i in {1..30}; do
    echo "Text line $i"
    bash "$SCRIPT_DIR/text.sh"
done

sleep 2
