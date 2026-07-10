#!/usr/bin/env python3
"""Clone (or update) mimalloc at a pinned commit.
Same shape as build-tools/wasm3/fetch.py — see there for rationale."""
from __future__ import annotations
import argparse, shutil, subprocess, sys
from pathlib import Path


def run(*args: str, cwd: Path | None = None) -> str:
    r = subprocess.run(args, cwd=cwd, check=True, capture_output=True, text=True)
    return r.stdout.strip()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument('--repo',   default='https://github.com/microsoft/mimalloc.git')
    p.add_argument('--commit', required=True)
    p.add_argument('--out',    required=True, type=Path)
    args = p.parse_args()

    dest: Path = args.out
    dest.mkdir(parents=True, exist_ok=True)
    repo = dest / 'mimalloc'

    # Pre-staged path (Nix sandbox): if the stamp already records our
    # exact commit AND a checkout exists, we trust whoever populated the
    # tree (typically the yos Nix derivation, which extracts a
    # fetchFromGitHub source and writes the stamp before invoking meson).
    # Without this short-circuit we'd shell out to `git` which isn't on
    # PATH in the Nix sandbox, the dir-exists check below would call
    # `git rev-parse` and fail with FileNotFoundError.
    stamp = dest / 'mimalloc.stamp'
    if stamp.exists() and stamp.read_text().strip() == args.commit and repo.exists():
        return 0

    if repo.exists():
        try:
            current = run('git', 'rev-parse', 'HEAD', cwd=repo)
        except (subprocess.CalledProcessError, FileNotFoundError):
            shutil.rmtree(repo); current = None
        if current == args.commit:
            (dest / 'mimalloc.stamp').write_text(args.commit + '\n')
            return 0
        if current is not None:
            run('git', 'fetch', '--depth=1', 'origin', args.commit, cwd=repo)
            run('git', 'checkout', '--detach', args.commit, cwd=repo)
            (dest / 'mimalloc.stamp').write_text(args.commit + '\n')
            return 0

    print(f'[mimalloc] cloning {args.repo} @ {args.commit[:12]}', file=sys.stderr)
    run('git', 'clone', '--no-checkout', args.repo, str(repo))
    run('git', '-C', str(repo), 'fetch', '--depth=1', 'origin', args.commit)
    run('git', '-C', str(repo), 'checkout', '--detach', args.commit)
    (dest / 'mimalloc.stamp').write_text(args.commit + '\n')
    print(f'[mimalloc] cloned to {repo}', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
