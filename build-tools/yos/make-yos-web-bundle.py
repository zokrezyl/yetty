#!/usr/bin/env python3
"""Pack the yos web bundle — the static files the yos session type needs
at runtime — from the in-tree yos/, in the exact layout yos-iframe.html
fetches and GitHub Pages serves:

  engine/               the browser engine modules (+ lua/liblua.wasm)
  yfs/                  the lazy web filesystem (see docs/yfs.md):
    current.json        {"version": V} — version pointer
    <V>/guest.yfs       aggregate manifest (all listings, one fetch)
    <V>/guest/**        path-mirrored guest tree + per-dir .yfs listings
    blob/<sha256>       content-addressed bodies >= BLOB_THRESHOLD

The guest tree contains: /bin (the yos umbrella tool set + the yetty
client tools from build-yos-guest/), /usr/share (the umbrella share
tree) and /usr/share/yetty/demos (the demo files).

The version tracks yetty itself (same scheme as yetty-rootfs-riscv):
tag yos-web-<VER>, file yos-web-<VER>.tar.gz, where <VER> comes from the
latest yetty-X.Y.Z tag. CI (build-yos-web.yml) builds and publishes it
per release; this script serves local builds — it drops the tarball into
the local 3rdparty cache, where the webasm configure picks it up.

Prerequisites in the yos tree: `nix build .#all --out-link result` (the
tool set + share tree) and, for nvim's Lua,
src/yos/platform/web/lua/build-liblua.sh (needs the .#sysroot output).

Usage:
  ./build-tools/yos/make-yos-web-bundle.py [--yos-root DIR] [--version X.Y.Z]

Defaults: --yos-root probes <repo>/yos then ../yos; --version derives
from `git describe --match 'yetty-*'`.
"""

import argparse
import hashlib
import json
import os
import stat as stat_module
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

LISTING_NAME = ".yfs"
BLOB_THRESHOLD = 64 * 1024

# The engine's browser import closure (yos-iframe.html imports the first
# three; yos_proc.mjs statically imports the manifest + lua bridge; the
# bridge fetches liblua.wasm relative to itself).
ENGINE_FILES = [
    "yos_proc.mjs",
    "wasm_patch.mjs",
    "fs_mount.mjs",
    "yfs_client.mjs",
    "import_manifest.mjs",
    "lua/lua_bridge.mjs",
    "lua/liblua.wasm",
]


class YfsBuilder:
    """Accumulates a guest tree, then emits the yfs layout.

    Sources are added as (guest_path, kind, payload):
      file    payload = source Path (bytes read lazily at emit)
      symlink payload = target string
    Directories are implied by the paths.
    """

    def __init__(self):
        # guest-relative dir path ("" = root) -> {name: entry-dict}
        self.dirs = {"": {}}
        self.sources = {}   # guest path -> source Path (files only)
        self.next_inode = 2

    def alloc_inode(self):
        inode = self.next_inode
        self.next_inode += 1
        return inode

    def ensure_dir(self, path):
        if path in self.dirs:
            return
        parent, _slash, name = path.rpartition("/")
        self.ensure_dir(parent)
        if name == LISTING_NAME:
            sys.exit(f"yfs: reserved name used by a directory: {path}")
        if name not in self.dirs[parent]:
            self.dirs[parent][name] = {
                "n": name, "t": "d", "i": self.alloc_inode(), "m": 0o755,
            }
        self.dirs[path] = {}

    def add_file(self, guest_path, source, force_mode=None):
        parent, _slash, name = guest_path.rpartition("/")
        if name == LISTING_NAME:
            sys.exit(f"yfs: reserved name used by a file: {guest_path}")
        self.ensure_dir(parent)
        info = source.stat()
        self.dirs[parent][name] = {
            "n": name, "t": "f", "i": self.alloc_inode(),
            "s": info.st_size,
            "m": (force_mode if force_mode is not None
                  else stat_module.S_IMODE(info.st_mode)),
            "mt": int(info.st_mtime),
        }
        self.sources[guest_path] = source

    def add_symlink(self, guest_path, target):
        parent, _slash, name = guest_path.rpartition("/")
        self.ensure_dir(parent)
        self.dirs[parent][name] = {
            "n": name, "t": "l", "i": self.alloc_inode(), "tgt": target,
        }

    def add_tree(self, guest_prefix, source_dir, force_mode=None):
        """Walk source_dir (following symlinks — nix store trees are
        symlink-heavy) into the guest tree under guest_prefix. A symlink
        whose target is a plain sibling name (e.g. the umbrella's
        libexec/sh -> zsh) becomes a symlink entry; everything else is a
        file. force_mode overrides the source mode bits — nix store
        files are 0444, but /bin entries must carry the execute bit for
        the guest shell's PATH probing."""
        source_dir = Path(source_dir)
        for dirpath, _dirnames, filenames in os.walk(source_dir,
                                                     followlinks=True):
            relative_dir = Path(dirpath).relative_to(source_dir).as_posix()
            for filename in sorted(filenames):
                full = Path(dirpath) / filename
                parts = [guest_prefix]
                if relative_dir != ".":
                    parts.append(relative_dir)
                parts.append(filename)
                guest_path = "/".join(part for part in parts if part)
                if full.is_symlink():
                    target = os.readlink(full)
                    if "/" not in target and (full.parent / target).exists():
                        self.add_symlink(guest_path, target)
                        continue
                try:
                    full.read_bytes()
                except OSError:
                    continue
                self.add_file(guest_path, full, force_mode=force_mode)

    def emit(self, out_dir, version):
        """Write yfs/{current.json,<version>/...,blob/...} under out_dir.
        Returns (file_count, blob_count, total_bytes)."""
        yfs_dir = out_dir / "yfs"
        version_dir = yfs_dir / version
        guest_dir = version_dir / "guest"
        blob_dir = yfs_dir / "blob"
        blob_dir.mkdir(parents=True, exist_ok=True)
        guest_dir.mkdir(parents=True, exist_ok=True)

        total_bytes = 0
        blob_count = 0
        for guest_path, source in self.sources.items():
            data = source.read_bytes()
            total_bytes += len(data)
            digest = hashlib.sha256(data).hexdigest()
            parent, _slash, name = guest_path.rpartition("/")
            entry = self.dirs[parent][name]
            entry["h"] = digest
            entry["s"] = len(data)
            if len(data) >= BLOB_THRESHOLD:
                entry["b"] = 1
                blob_count += 1
                blob_path = blob_dir / digest
                if not blob_path.exists():
                    blob_path.write_bytes(data)
            else:
                destination = guest_dir / guest_path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)

        aggregate = {"v": 1, "dirs": {}}
        for dir_path, entries in sorted(self.dirs.items()):
            listing = {"v": 1,
                       "entries": [entries[name]
                                   for name in sorted(entries)]}
            destination = guest_dir / dir_path / LISTING_NAME
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(listing,
                                              separators=(",", ":")))
            aggregate["dirs"][dir_path] = listing["entries"]

        (version_dir / "guest.yfs").write_text(
            json.dumps(aggregate, separators=(",", ":")))
        (yfs_dir / "current.json").write_text(
            json.dumps({"version": version}))
        return len(self.sources), blob_count, total_bytes


def demo_entries():
    """Demo files for /usr/share/yetty/demos. Text demos are authored in
    build-tools/yos/demos/; the binary ones reuse files already tracked
    elsewhere in the repo instead of duplicating them."""
    demos_dir = REPO_ROOT / "build-tools" / "yos" / "demos"
    entries = [(path.name, path)
               for path in sorted(demos_dir.iterdir()) if path.is_file()]
    entries += [
        ("logo.jpeg", REPO_ROOT / "assets" / "logo-2.jpeg"),
        ("sample.pdf",
         REPO_ROOT / "test" / "ut" / "ypdf" / "pdf-sample.pdf"),
    ]
    missing = [str(source) for _name, source in entries
               if not Path(source).is_file()]
    if missing:
        sys.exit("demo files missing: " + ", ".join(missing))
    return entries


def resolve_yos_roots(explicit):
    candidates = ([Path(explicit)] if explicit else
                  [REPO_ROOT / "yos", REPO_ROOT.parent / "yos"])
    roots = [root for root in candidates if (root / "src").is_dir()]
    if not roots:
        sys.exit("no yos tree found (tried: " +
                 ", ".join(str(c) for c in candidates) + ")")
    return roots


def find_engine_file(roots, relative):
    """Probe every root — liblua.wasm is a build artifact that may live
    only where build-liblua.sh was run."""
    for root in roots:
        candidate = root / "src" / "yos" / "platform" / "web" / relative
        if candidate.is_file():
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--yos-root", default=None,
                        help="yos tree (default: <repo>/yos, ../yos)")
    parser.add_argument("--version", default=None,
                        help="bundle version (default: latest yetty-X.Y.Z tag)")
    parser.add_argument("--cache-dir", default=None,
                        help="3rdparty cache (default: ~/.cache/yetty/3rdparty)")
    parser.add_argument("--guest-tools-dir", default=None,
                        help="directory with extra guest tool wasm binaries "
                             "(<name>.wasm) for /bin — the yetty client "
                             "tools from build-tools/yos/"
                             "build-guest-tools.sh. Default: probe "
                             "<repo>/build-yos-guest; pass explicitly to "
                             "make it mandatory.")
    args = parser.parse_args()

    version = args.version
    if not version:
        described = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "describe", "--tags", "--abbrev=0",
             "--match", "yetty-[0-9]*.[0-9]*.[0-9]*", "HEAD"],
            capture_output=True, text=True)
        tag = described.stdout.strip()
        if described.returncode != 0 or not tag:
            sys.exit("cannot resolve version: no yetty-X.Y.Z tag reachable "
                     "from HEAD — pass --version")
        version = tag.removeprefix("yetty-")
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
                sys.exit(f"engine file not found in any yos tree: {relative}")
            destination = engine_dir / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read_bytes())
        print(f"engine    : {len(ENGINE_FILES)} files")

        builder = YfsBuilder()
        builder.add_tree("bin", result_dir / "libexec", force_mode=0o755)
        builder.add_tree("usr/share", result_dir / "share")

        # yetty client tools (ycat, yecho) — built from the yetty tree by
        # build-tools/yos/build-guest-tools.sh, not part of the yos nix
        # umbrella. Explicit --guest-tools-dir is mandatory; the default
        # probe is best-effort so a plain local pack still works.
        guest_tools_dir = (Path(args.guest_tools_dir) if args.guest_tools_dir
                           else REPO_ROOT / "build-yos-guest")
        guest_wasms = (sorted(guest_tools_dir.glob("*.wasm"))
                       if guest_tools_dir.is_dir() else [])
        if args.guest_tools_dir and not guest_wasms:
            sys.exit(f"no *.wasm in --guest-tools-dir {guest_tools_dir} — "
                     "run build-tools/yos/build-guest-tools.sh first")
        for wasm in guest_wasms:
            builder.add_file(f"bin/{wasm.stem}", wasm, force_mode=0o755)
        if not guest_wasms:
            print("guest tools: none found — bundle ships without "
                  "ycat/yecho (run build-tools/yos/build-guest-tools.sh)")
        else:
            print(f"guest tools: {', '.join(w.stem for w in guest_wasms)}")

        for name, source in demo_entries():
            builder.add_file(f"usr/share/yetty/demos/{name}", source)

        files, blobs, total = builder.emit(staging, version)
        print(f"yfs       : {files} files ({total / 1e6:.1f} MB), "
              f"{blobs} in blob store")

        cache_dir.mkdir(parents=True, exist_ok=True)
        tarball = cache_dir / f"yos-web-{version}.tar.gz"
        with tarfile.open(tarball, "w:gz") as archive:
            for top in ("engine", "yfs"):
                archive.add(staging / top, arcname=top)
        size_mb = tarball.stat().st_size / 1e6
        print(f"tarball   : {tarball} ({size_mb:.1f} MB)")

    print()
    print("Staged in the local 3rdparty cache — builds pick it up now.")
    print("CI (build-yos-web.yml) publishes the yos-web-<ver> release on "
          "yetty release tags; no manual upload needed.")


if __name__ == "__main__":
    main()
