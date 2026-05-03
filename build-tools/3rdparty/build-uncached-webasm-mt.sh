#!/usr/bin/env bash
exec env TARGET_PLATFORM=webasm-mt "$(dirname "$0")/build-uncached.sh" "$@"
