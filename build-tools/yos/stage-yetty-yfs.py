#!/usr/bin/env python3
"""Stage yetty's webasm asset set as a yfs subtree (docs/yfs.md phase 2).

Consumes the manifest webasm-stage-assets.cmake writes under
build/yetty-assets/ and emits, into an existing yos-web yfs directory:

  <yfs>/<version>/yetty.yfs       aggregate manifest for the yetty tree
  <yfs>/<version>/yetty/**/.yfs   per-directory listings (canonical format)
  <yfs>/blob/<sha256>             the asset bodies (wire bytes — brotli'd
                                  files stay brotli'd; entries carry
                                  z="br" and the preload decodes on fetch)

The demo files also land in the tree under demos/ so they are reachable
through yetty's asset VFS as well as the guest one.

Invoked at webasm configure time from platform/webasm/cmake.cmake, after
the yos-web bundle (which owns <yfs>/current.json and the guest tree)
has been staged.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

LISTING_NAME = ".yfs"


def demo_files():
    demos_dir = REPO_ROOT / "build-tools" / "yos" / "demos"
    entries = [(path.name, path)
               for path in sorted(demos_dir.iterdir()) if path.is_file()]
    entries += [
        ("logo.jpeg", REPO_ROOT / "assets" / "logo-2.jpeg"),
        ("sample.pdf",
         REPO_ROOT / "test" / "ut" / "ypdf" / "pdf-sample.pdf"),
    ]
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True,
                        help="yetty-assets/manifest.json")
    parser.add_argument("--assets-dir", required=True,
                        help="the staged yetty-assets directory")
    parser.add_argument("--yfs-dir", required=True,
                        help="the yos-web yfs directory to merge into")
    parser.add_argument("--version", required=True,
                        help="tree version (must match the bundle's)")
    parser.add_argument("--tree", action="append", default=[],
                        metavar="NAME=DIR",
                        help="stage a raw directory tree as subtree NAME/ "
                             "(uncompressed, demand-paged — replaces the "
                             "emscripten --preload-file package for the "
                             "demo feature's /demo and /src trees)")
    args = parser.parse_args()

    assets_dir = Path(args.assets_dir)
    yfs_dir = Path(args.yfs_dir)
    blob_dir = yfs_dir / "blob"
    version_dir = yfs_dir / args.version
    blob_dir.mkdir(parents=True, exist_ok=True)
    version_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads(Path(args.manifest).read_text())

    dirs = {"": {}}
    next_inode = [2]

    def alloc_inode():
        next_inode[0] += 1
        return next_inode[0]

    def ensure_dir(path):
        if path in dirs:
            return
        parent, _slash, name = path.rpartition("/")
        ensure_dir(parent)
        if name not in dirs[parent]:
            dirs[parent][name] = {"n": name, "t": "d", "i": alloc_inode(),
                                  "m": 0o755}
        dirs[path] = {}

    def add_body(tree_path, wire_bytes, compressed):
        parent, _slash, name = tree_path.rpartition("/")
        if name == LISTING_NAME:
            sys.exit(f"yfs: reserved name used by a file: {tree_path}")
        ensure_dir(parent)
        digest = hashlib.sha256(wire_bytes).hexdigest()
        entry = {"n": name, "t": "f", "i": alloc_inode(),
                 "s": len(wire_bytes), "m": 0o644, "mt": 0,
                 "h": digest, "b": 1}
        if compressed:
            entry["z"] = "br"
        dirs[parent][name] = entry
        blob_path = blob_dir / digest
        if not blob_path.exists():
            blob_path.write_bytes(wire_bytes)

    total = 0
    for manifest_entry in manifest.get("entries", []):
        source = assets_dir / manifest_entry["url"]
        wire = source.read_bytes()
        total += len(wire)
        tree_path = manifest_entry["dest"].lstrip("/")
        add_body(tree_path, wire, bool(manifest_entry.get("brotli")))

    for name, source in demo_files():
        if not source.is_file():
            sys.exit(f"yfs: demo file missing: {source}")
        data = source.read_bytes()
        total += len(data)
        add_body(f"demos/{name}", data, False)

    for spec in args.tree:
        name, _eq, tree_dir = spec.partition("=")
        if not name or not tree_dir:
            sys.exit(f"yfs: bad --tree spec: {spec}")
        tree_root = Path(tree_dir)
        for source in sorted(tree_root.rglob("*")):
            if not source.is_file():
                continue
            data = source.read_bytes()
            total += len(data)
            relative = source.relative_to(tree_root).as_posix()
            add_body(f"{name}/{relative}", data, False)

    aggregate = {"v": 1, "dirs": {}}
    for dir_path, entries in sorted(dirs.items()):
        listing = {"v": 1, "entries": [entries[name]
                                       for name in sorted(entries)]}
        destination = version_dir / "yetty" / dir_path / LISTING_NAME
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(listing, separators=(",", ":")))
        aggregate["dirs"][dir_path] = listing["entries"]

    (version_dir / "yetty.yfs").write_text(
        json.dumps(aggregate, separators=(",", ":")))

    file_count = sum(1 for entries in dirs.values()
                     for entry in entries.values() if entry["t"] == "f")
    print(f"yfs yetty tree: {file_count} files ({total / 1e6:.1f} MB wire) "
          f"-> {version_dir / 'yetty.yfs'}")


if __name__ == "__main__":
    main()
