#!/usr/bin/env python3
"""Run one tests/ut/libc/<name>.wasm through yos and check the result.

Each test is described by an inline header in the source .c file:

    Expected: exit <code>[, stdout contains "<substring>"].

This runner re-reads that header from the .c, runs the compiled .wasm
through the host yos binary, and asserts both conditions. Failure
prints what was expected vs what happened so the test report tells
you exactly which assertion broke.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


def parse_expectations(c_source: Path) -> tuple[int, list[str], list[str]]:
    # Read explicitly as UTF-8 — Windows Python opens files cp1252
    # which trips on non-ASCII test docstrings (em-dash etc.).
    text = c_source.read_text(encoding='utf-8')
    m = re.search(r'Expected:\s*exit\s+(-?\d+)', text)
    if m:
        code = int(m.group(1))
    elif re.search(r'Expected:', text):
        # Tests that pin behaviour by stdout/stderr substring only —
        # default to exit 0 instead of refusing to run.
        code = 0
    else:
        raise SystemExit(f'{c_source}: no `Expected:` block in header')
    subs       = re.findall(r'stdout\s+contains\s+"([^"]+)"', text)
    err_subs   = re.findall(r'stderr\s+contains\s+"([^"]+)"', text)
    return code, subs, err_subs


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--yos',  required=True, type=Path,
                   help='Host yos binary')
    p.add_argument('--src',  required=True, type=Path,
                   help='The test\'s .c source (read for `Expected:` header)')
    p.add_argument('--wasm', required=True, type=Path,
                   help='Compiled .wasm to run')
    p.add_argument('--timeout', type=int, default=30,
                   help='Per-test timeout in seconds')
    args = p.parse_args()

    expected_code, expected_subs, _ = parse_expectations(args.src)

    proc = subprocess.run(
        [str(args.yos), str(args.wasm)],
        capture_output=True, text=True, timeout=args.timeout,
    )

    name = args.wasm.stem
    fail = []
    if proc.returncode != expected_code:
        fail.append(f'exit={proc.returncode}, expected {expected_code}')
    for sub in expected_subs:
        if sub not in proc.stdout:
            fail.append(f'stdout missing {sub!r}')

    if fail:
        print(f'FAIL  {name}', file=sys.stderr)
        for f in fail:
            print(f'        {f}', file=sys.stderr)
        if proc.stdout:
            print(f'        stdout: {proc.stdout!r}', file=sys.stderr)
        if proc.stderr:
            print(f'        stderr: {proc.stderr!r}', file=sys.stderr)
        return 1
    print(f'PASS  {name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
