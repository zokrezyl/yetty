#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.10"
# ///
"""Pack the yos web bundle — the static files the yos session type needs
at runtime — from a yos checkout, in the exact layout yos-iframe.html
fetches and GitHub Pages serves:

  engine/               the browser engine modules (+ lua/liblua.wasm)
  tools/list.json       every wasm tool name
  tools/<name>.wasm     the tool binaries (nix store symlinks followed)
  fs/pack.bin           the guest /usr/share tree as one YFS1 blob

The tarball follows the first-party release-asset convention
(yetty-asset-fetch.cmake): tag yos-web-<VER>, file yos-web-<VER>.tar.gz.
The script drops the tarball into the local 3rdparty cache (so a build
picks it up immediately, before the release exists) and prints the
gh command that publishes it.

Usage:
  ./build-tools/yos/make-yos-web-bundle.py [--yos-root DIR] [--version X.Y.Z]

Defaults: --yos-root probes <repo>/yos then ../yos (sibling checkout);
--version reads build-tools/yos/yos-web/version.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PACK_MAGIC = b"YFS1"

# The engine's browser import closure (yos-iframe.html imports the first
# three; yos_proc.mjs statically imports the manifest + lua bridge; the
# bridge fetches liblua.wasm relative to itself).
ENGINE_FILES = [
    "yos_proc.mjs",
    "wasm_patch.mjs",
    "fs_mount.mjs",
    "import_manifest.mjs",
    "lua/lua_bridge.mjs",
    "lua/liblua.wasm",
]


def build_share_pack(share_dir):
    """YFS1 blob (magic | u32le index-len | index JSON | blobs) — the format
    fs_mount.mjs parsePack() reads. Follows symlinks (nix store trees are
    symlink-heavy). Offsets are relative to the end of the index."""
    index = []
    blobs = []
    offset = 0
    for dirpath, _dirnames, filenames in os.walk(share_dir, followlinks=True):
        for filename in sorted(filenames):
            full = Path(dirpath) / filename
            try:
                data = full.read_bytes()
            except OSError:
                continue
            relative = full.relative_to(share_dir).as_posix()
            index.append({"p": relative, "o": offset, "s": len(data)})
            blobs.append(data)
            offset += len(data)
    index_bytes = json.dumps(index, separators=(",", ":")).encode()
    return b"".join([PACK_MAGIC, struct.pack("<I", len(index_bytes)),
                     index_bytes] + blobs)


def resolve_yos_roots(explicit):
    candidates = ([Path(explicit)] if explicit else
                  [REPO_ROOT / "yos", REPO_ROOT.parent / "yos"])
    roots = [root for root in candidates if (root / "src").is_dir()]
    if not roots:
        sys.exit("no yos checkout found (tried: " +
                 ", ".join(str(c) for c in candidates) + ")")
    return roots


def find_engine_file(roots, relative):
    """Probe every root — liblua.wasm is an untracked build artifact that
    may live only in a sibling checkout where build-liblua.sh was run."""
    for root in roots:
        candidate = root / "src" / "yos" / "platform" / "web" / relative
        if candidate.is_file():
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--yos-root", default=None,
                        help="yos checkout (default: <repo>/yos, ../yos)")
    parser.add_argument("--version", default=None,
                        help="bundle version (default: build-tools/yos/yos-web/version)")
    parser.add_argument("--cache-dir", default=None,
                        help="3rdparty cache (default: ~/.cache/yetty/3rdparty)")
    args = parser.parse_args()

    version = args.version or \
        (REPO_ROOT / "build-tools" / "yos" / "yos-web" / "version").read_text().strip()
    cache_dir = Path(args.cache_dir or
                     os.environ.get("YETTY_3RDPARTY_CACHE_DIR",
                                    Path.home() / ".cache" / "yetty" / "3rdparty"))
    roots = resolve_yos_roots(args.yos_root)
    result_dir = next((root / "result" for root in roots
                       if (root / "result" / "libexec").is_dir()), None)
    if not result_dir:
        sys.exit("no yos result/libexec found — run `nix build .#all` in the yos tree")

    print(f"yos roots : {', '.join(str(root) for root in roots)}")
    print(f"result    : {result_dir}")
    print(f"version   : {version}")

    with tempfile.TemporaryDirectory() as staging_name:
        staging = Path(staging_name)

        engine_dir = staging / "engine"
        for relative in ENGINE_FILES:
            source = find_engine_file(roots, relative)
            if not source:
                sys.exit(f"engine file not found in any yos checkout: {relative}")
            destination = engine_dir / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read_bytes())
        print(f"engine    : {len(ENGINE_FILES)} files")

        tools_dir = staging / "tools"
        tools_dir.mkdir(parents=True)
        names = sorted(entry.name for entry in (result_dir / "libexec").iterdir()
                       if entry.is_file())
        for name in names:
            # read_bytes follows the nix store symlink — exactly what the
            # dev server does; never realpath-and-prefix-check.
            (tools_dir / (name + ".wasm")).write_bytes(
                (result_dir / "libexec" / name).read_bytes())
        (tools_dir / "list.json").write_text(json.dumps(names))
        print(f"tools     : {len(names)} wasm binaries")

        fs_dir = staging / "fs"
        fs_dir.mkdir(parents=True)
        pack = build_share_pack(result_dir / "share")
        (fs_dir / "pack.bin").write_bytes(pack)
        print(f"share pack: {len(pack) / 1e6:.1f} MB")

        cache_dir.mkdir(parents=True, exist_ok=True)
        tarball = cache_dir / f"yos-web-{version}.tar.gz"
        with tarfile.open(tarball, "w:gz") as archive:
            for top in ("engine", "tools", "fs"):
                archive.add(staging / top, arcname=top)
        size_mb = tarball.stat().st_size / 1e6
        print(f"tarball   : {tarball} ({size_mb:.1f} MB)")

    print()
    print("Staged in the local 3rdparty cache — builds pick it up now.")
    print("Publish it with:")
    print(f"  gh release create yos-web-{version} --repo zokrezyl/yetty \\")
    print(f"      --title 'yos web bundle {version}' --notes 'yos web bundle' \\")
    print(f"      {tarball}")


if __name__ == "__main__":
    main()
