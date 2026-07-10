#!/usr/bin/env python3
"""Fetch + extract FreeBSD source for header extraction.

Invoked by build-tools/freebsd/meson.build. Two modes:

  --mode download   download src.txz to <out>/src.txz, verify sha256
                    (if provided) or print observed sha256 to stderr
                    so the user can pin it.
  --mode extract    extract <archive> to <out>/src/, idempotent.
                    Touches <out>/src/.extracted so meson can use that
                    as a dependency stamp.

Designed to fail loudly on integrity issues (mismatched sha256, partial
download) and succeed quickly on cache hits (file exists + size > 0
+ sha256 matches → skip work).

The output goes wherever meson points us (typically
<build-dir>/build-tools/freebsd/) — this script does NOT pick its own
output location.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def cmd_download(args: argparse.Namespace) -> int:
    out_dir  = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    tarball  = out_dir / 'src.txz'

    expected = (args.sha256 or '').strip().lower() or None

    # Cache hit: file exists, optionally hash matches.
    if tarball.is_file() and tarball.stat().st_size > 0:
        if expected is None:
            observed = sha256_file(tarball)
            print(f'[freebsd-fetch] cached: {tarball} sha256={observed}',
                  file=sys.stderr)
            return 0
        observed = sha256_file(tarball)
        if observed == expected:
            print(f'[freebsd-fetch] cached + verified: {tarball}',
                  file=sys.stderr)
            return 0
        # Hash mismatch on cached file — drop and re-fetch.
        print(f'[freebsd-fetch] cached file sha256={observed} but expected '
              f'{expected}; re-downloading', file=sys.stderr)
        tarball.unlink()

    # Download.
    print(f'[freebsd-fetch] GET {args.url}', file=sys.stderr)
    tmp = tarball.with_suffix('.txz.partial')

    def _open(url, ctx=None):
        return urllib.request.urlopen(url, context=ctx) if ctx is not None \
            else urllib.request.urlopen(url)

    try:
        try:
            resp = _open(args.url)
        except Exception as e:
            # Windows Python typically lacks an OS-managed CA bundle and
            # urlopen raises SSL: CERTIFICATE_VERIFY_FAILED. We hash-
            # verify the downloaded content below, so falling back to
            # an unverified TLS context is acceptable when a sha256 was
            # pinned. Without a pin, refuse — there is nothing to
            # cross-check the response against.
            msg = str(e).lower()
            if expected is not None and ('certificate' in msg or 'ssl' in msg):
                import ssl
                ctx = ssl._create_unverified_context()
                print(f'[freebsd-fetch] TLS verify failed ({e}); '
                      f'retrying with unverified TLS (sha256 pinned)',
                      file=sys.stderr)
                resp = _open(args.url, ctx)
            else:
                raise
        try:
            with tmp.open('wb') as out:
                shutil.copyfileobj(resp, out, length=1 << 20)
        finally:
            resp.close()
    except Exception as e:
        if tmp.exists():
            tmp.unlink()
        print(f'[freebsd-fetch] download failed: {e}', file=sys.stderr)
        return 2

    observed = sha256_file(tmp)
    if expected is not None and observed != expected:
        tmp.unlink()
        print(f'[freebsd-fetch] sha256 MISMATCH: expected {expected}, '
              f'got {observed}', file=sys.stderr)
        return 3

    tmp.rename(tarball)
    print(f'[freebsd-fetch] downloaded {tarball} ({tarball.stat().st_size} bytes)',
          file=sys.stderr)
    print(f'[freebsd-fetch] sha256 = {observed}', file=sys.stderr)
    if expected is None:
        print(f'[freebsd-fetch] no sha256 was pinned. Add this to '
              f'meson_options.txt freebsd_src_sha256:', file=sys.stderr)
        print(f'  {observed}', file=sys.stderr)
    return 0


def cmd_extract(args: argparse.Namespace) -> int:
    archive = Path(args.archive)
    out_dir = Path(args.out) / 'src'
    stamp   = out_dir / '.extracted'

    if not archive.is_file():
        print(f'[freebsd-fetch] archive not found: {archive}', file=sys.stderr)
        return 2

    # Cache hit: stamp exists and is newer than the archive.
    if stamp.exists() and stamp.stat().st_mtime >= archive.stat().st_mtime:
        print(f'[freebsd-fetch] extract cached: {out_dir}', file=sys.stderr)
        return 0

    # Wipe + extract. Use the system `tar` because Python's tarfile
    # has historically struggled with FreeBSD-style xz tarballs and
    # sparse files; system tar is universally available on dev hosts.
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    print(f'[freebsd-fetch] extracting {archive} -> {out_dir}', file=sys.stderr)
    rc = subprocess.run(
        ['tar', '-xJf', str(archive), '-C', str(out_dir),
         '--strip-components=0'],
        check=False,
    ).returncode
    if rc != 0:
        # Fall back to Python tarfile if system tar doesn't speak xz.
        print(f'[freebsd-fetch] system tar failed (rc={rc}); '
              f'falling back to Python tarfile', file=sys.stderr)
        with tarfile.open(archive, mode='r:xz') as tf:
            tf.extractall(out_dir)

    stamp.touch()
    print(f'[freebsd-fetch] extracted to {out_dir}', file=sys.stderr)
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description='FreeBSD source fetcher / extractor')
    p.add_argument('--mode', required=True, choices=('download', 'extract'))
    p.add_argument('--out',  required=True,
                   help='output directory (meson @OUTDIR@)')
    # Download-only:
    p.add_argument('--version')
    p.add_argument('--url')
    p.add_argument('--sha256', default='')
    # Extract-only:
    p.add_argument('--archive')
    args = p.parse_args()

    if args.mode == 'download':
        if not (args.url and args.version):
            p.error('--mode download requires --url and --version')
        return cmd_download(args)
    elif args.mode == 'extract':
        if not args.archive:
            p.error('--mode extract requires --archive')
        return cmd_extract(args)
    return 1


if __name__ == '__main__':
    sys.exit(main())
