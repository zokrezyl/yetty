#!/usr/bin/env python3
"""Cross-platform wasm-clang wrapper.

The bash sibling (build-tools/wasm-clang) handles darwin-specific clang
flags injected by nix's clang-wrapper:

  -arch X                   — wasm-ld rejects it
  -mmacos-version-min=X     — wasm-ld rejects it
  -mmacosx-version-min=X    — same

It also injects -resource-dir / -idirafter from $YOS_WASM_CLANG_RESOURCE_DIR
so clang's freestanding builtin headers (stdarg.h, stddef.h, limits.h)
survive -nostdinc.

This Python port does the same thing on every platform; on Windows the
darwin-flag filtering is a no-op (none ever appear).
"""
from __future__ import annotations

import os
import sys

CC      = os.environ.get('YOS_WASM_CLANG', 'clang')
RESDIR  = os.environ.get('YOS_WASM_CLANG_RESOURCE_DIR', '')


def main(argv: list[str]) -> int:
    out: list[str] = []
    skip_next = False
    for a in argv[1:]:
        if skip_next:
            skip_next = False
            continue
        if a == '-arch':
            skip_next = True
            continue
        if a.startswith('-mmacos-version-min=') or a.startswith('-mmacosx-version-min='):
            continue
        out.append(a)

    extra: list[str] = []
    if RESDIR:
        extra += ['-resource-dir', RESDIR, '-idirafter', os.path.join(RESDIR, 'include')]

    cmd = [CC] + extra + out

    if os.name == 'nt':
        # On Windows, os.execvp can lose argv quoting; subprocess gives
        # us reliable forwarding and a clean exit-code path.
        import subprocess
        return subprocess.call(cmd)
    else:
        os.execvp(cmd[0], cmd)
        return 1   # unreachable


if __name__ == '__main__':
    sys.exit(main(sys.argv))
