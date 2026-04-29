#!/usr/bin/env bash
exec env TARGET_PLATFORM=linux-riscv64 "$(dirname "$0")/build-uncached.sh" "$@"
