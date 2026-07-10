#!/usr/bin/env python3
"""Snapshot the build host's libc headers as a self-contained tree.

This tree becomes the "host64" reference for yos's struct converter:
when yos's host runtime makes a passthrough syscall (e.g. `stat()`),
host libc expects the host's struct layout, not the guest's. The
extractor walks this snapshot under -nostdinc + the recorded include
search path so libclang sees exactly what `cc /tmp/x.c` would see.

Per-host strategy:

  Linux:    ask clang for its #include search path; symlink each entry
            into headers/ as `01-…/`, `02-…/`, … with a manifest.txt.
            Doesn't copy gigabytes — just enough that abi.yaml's
            extraction step has a stable view.

  Darwin:   `xcrun --show-sdk-path` + same symlink trick into headers/.

  FreeBSD:  /usr/include directly (FreeBSD's libc).

The output is per-host; there's no point caching across machines.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


def _msvc_include_paths() -> list[Path]:
    """On Windows under a Developer-prompt-style environment (vcvars
    has been sourced), %INCLUDE% lists every header search directory
    the MSVC toolchain would use — MSVC's CRT, the Windows SDK shared
    + um + ucrt trees, plus any addon SDKs. Use that authoritative list
    instead of asking a preprocessor."""
    import os
    raw = os.environ.get('INCLUDE', '')
    paths: list[Path] = []
    for p in raw.split(';'):
        p = p.strip()
        if p:
            paths.append(Path(p))
    return paths


def _clang_include_paths() -> list[Path]:
    """Ask clang (or gcc, cc) where it would look for system headers.
    On Windows we prefer %INCLUDE% (set by vcvars64) — see
    `_msvc_include_paths`."""
    import os
    if os.name == 'nt':
        msvc = _msvc_include_paths()
        if msvc:
            return msvc
    devnull = 'NUL' if os.name == 'nt' else '/dev/null'
    last_err: Exception | None = None
    for cc in ('clang', 'gcc', 'cc'):
        try:
            out = subprocess.check_output(
                [cc, '-xc', '-E', '-v', devnull],
                stderr=subprocess.STDOUT, text=True,
            )
            break
        except (FileNotFoundError, subprocess.CalledProcessError) as e:
            last_err = e
    else:
        raise SystemExit(f'[host-libc] no usable preprocessor found: {last_err}')

    # The relevant block:
    #   #include <...> search starts here:
    #    /usr/include/x86_64-linux-gnu
    #    /usr/include
    #    /nix/store/.../include
    #   End of search list.
    in_block = False
    paths: list[Path] = []
    for line in out.splitlines():
        if '#include <...> search starts here:' in line:
            in_block = True
            continue
        if 'End of search list.' in line:
            in_block = False
            continue
        if in_block:
            p = line.strip()
            # Some paths have a trailing "(framework directory)" tag on
            # macOS — strip it.
            p = re.sub(r'\s*\(framework directory\)\s*$', '', p)
            if p:
                paths.append(Path(p))
    return paths


def cmd_snapshot(out_root: Path) -> int:
    """Produce a tree under out_root/ describing the host's libc view.

    Layout:
      out_root/
        manifest.txt       one path per line, in clang's search order
        include/01-<base>/ symlink to first include dir
        include/02-<base>/ symlink to second
        …
        .snapshotted       stamp file
    """
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)

    paths = _clang_include_paths()
    if not paths:
        print('[host-libc] clang reported no include search paths', file=sys.stderr)
        return 2

    manifest = []
    inc = out_root / 'include'
    inc.mkdir()
    for i, p in enumerate(paths, 1):
        if not p.is_dir():
            print(f'[host-libc] skipping non-existent: {p}', file=sys.stderr)
            continue
        # base name disambiguated by ordinal so collisions don't happen
        # (e.g. clang's resource-dir 'include' and /usr/include both
        # named 'include').
        slug = f'{i:02d}-{p.name or "root"}'
        link = inc / slug
        try:
            link.symlink_to(p, target_is_directory=True)
        except (OSError, NotImplementedError):
            # Windows without dev-mode forbids symlinks for unprivileged
            # users — fall back to a directory junction via `mklink /J`,
            # which works without admin and is transparent to anything
            # that walks the file tree.
            import os, subprocess
            if os.name == 'nt':
                rc = subprocess.run(
                    ['cmd', '/c', 'mklink', '/J', str(link), str(p)],
                    capture_output=True, text=True).returncode
                if rc != 0:
                    print(f'[host-libc] mklink /J failed for {link} → {p}',
                          file=sys.stderr)
        manifest.append(f'{i:02d} {p}')

    (out_root / 'manifest.txt').write_text('\n'.join(manifest) + '\n')
    (out_root / '.snapshotted').touch()

    print(f'[host-libc] snapshot at {out_root}', file=sys.stderr)
    for line in manifest:
        print(f'  {line}', file=sys.stderr)
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--out', required=True,
                   help='output directory; will be wiped + populated')
    args = p.parse_args()
    return cmd_snapshot(Path(args.out))


if __name__ == '__main__':
    sys.exit(main())
