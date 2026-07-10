#!/usr/bin/env python3
"""tools/struct-offsets.py — dump the byte layout of a C struct under
the FreeBSD-i386 (wasm32 ABI) sysroot we extract for the guest.

Why: bridging libc fns that operate on opaque structs by sysctl-mib
(struct kinfo_proc, struct meminfo, etc.) needs the per-field offsets
+ sizes so the bridge can fill the wasm buffer in the right shape.
The codegen pipeline (src/yos/codegen/extract.py) only extracts the
structs that are reachable from function signatures, so any struct
accessed only via sysctl is invisible to it.

Usage:
    tools/struct-offsets.py kinfo_proc            # default header: sys/user.h
    tools/struct-offsets.py sigaction sys/signal.h
    tools/struct-offsets.py rusage   sys/resource.h

Output: one record per field — offset (decimal), size, type, name.
Plus the total struct sizeof at the top.

The tool reads headers from
    build-tools/freebsd/headers-i386/usr/include/
checked in at repo root (or wherever the meson custom_target stages
them). If you haven't run `nix build .#freebsd-src` yet, point
YOS_FBSD_HEADERS at a working sysroot/usr/include path.
"""

from __future__ import annotations
import argparse
import os
import sys
from pathlib import Path

# Project-vendored clang.cindex (same one the codegen pipeline uses).
try:
    import clang.cindex as cidx
except ImportError:
    print("error: 'clang' python module not found. Run inside the nix devShell "
          "or `uv venv && uv sync`.", file=sys.stderr)
    sys.exit(2)

REPO = Path(__file__).resolve().parent.parent
DEFAULT_SYSROOT_CANDIDATES = [
    REPO / "build-darwin" / "build-tools" / "freebsd" / "headers-i386" / "usr" / "include",
    REPO / "build-linux"  / "build-tools" / "freebsd" / "headers-i386" / "usr" / "include",
    REPO / "build-tvos-arm64" / "build-tools" / "freebsd" / "headers-i386" / "usr" / "include",
    REPO / "build-ios-sim-x86_64" / "build-tools" / "freebsd" / "headers-i386" / "usr" / "include",
]


def find_sysroot() -> Path:
    env = os.environ.get("YOS_FBSD_HEADERS")
    if env:
        p = Path(env)
        if (p / "sys" / "types.h").exists():
            return p
        sys.exit(f"YOS_FBSD_HEADERS={env} doesn't look like a sysroot (no sys/types.h).")
    for c in DEFAULT_SYSROOT_CANDIDATES:
        if (c / "sys" / "types.h").exists():
            return c
    sys.exit("could not locate the FreeBSD-i386 sysroot; "
             "run `nix build .#all` first or set YOS_FBSD_HEADERS=…")


def parse_struct(struct_name: str, header: str, sysroot: Path) -> tuple[int, list[tuple[int,int,str,str]]]:
    """Returns (sizeof, [(offset, size, type_spelling, field_name), ...])."""
    src = f"#include <{header}>\n"
    args = [
        "-target", "wasm32-unknown-unknown",
        "-nostdinc",
        "-isystem", str(sysroot),
        "-x", "c", "-std=c11",
        # FreeBSD headers gate arch-specific decls (machine/_types.h,
        # x86/ucontext.h, sys/user.h's KINFO_PROC_SIZE) on __i386__.
        # -target wasm32 doesn't set it; match what
        # src/yos/codegen/meson.build's guest extraction does.
        "-D__i386__=1",
        "-D__BSD_VISIBLE=1",
        "-D__XSI_VISIBLE=700",
        "-D_DEFAULT_SOURCE=1",
        # FreeBSD headers chain into <machine/segments.h> from sys/user.h
        # before any TU directly includes <sys/cdefs.h>; without that
        # macro the `... } __packed;` trailers parse as variable
        # declarations and collide as duplicate-typed redefinitions.
        # Same idea for <sys/types.h> (provides int32_t/uint16_t/...
        # via <sys/_stdint.h>) and <stdint.h>: force-include them so
        # any struct that mentions those names parses cleanly even
        # when its own include chain doesn't pull them in.
        "-include", "sys/cdefs.h",
        "-include", "sys/types.h",
        "-include", "stdint.h",
    ]
    idx = cidx.Index.create()
    tu = idx.parse("input.c",
                   args=args,
                   unsaved_files=[("input.c", src)],
                   options=cidx.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)

    fatal = [d for d in tu.diagnostics if d.severity >= cidx.Diagnostic.Error]
    if fatal:
        for d in fatal:
            print(f"  clang: {d.spelling}", file=sys.stderr)
        sys.exit(f"failed to parse <{header}>")

    target = None
    for c in tu.cursor.walk_preorder():
        if c.kind == cidx.CursorKind.STRUCT_DECL and c.spelling == struct_name and c.is_definition():
            target = c
            break
    if target is None:
        sys.exit(f"struct {struct_name!r} not found in <{header}>")

    sizeof = target.type.get_size()
    fields = []
    for f in target.type.get_fields():
        off_bits = target.type.get_offset(f.spelling)
        off = off_bits // 8
        size = f.type.get_size()
        fields.append((off, size, f.type.spelling, f.spelling))
    return sizeof, fields


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("struct", help="struct name (no `struct ` prefix)")
    ap.add_argument("header", nargs="?", default="sys/user.h",
                    help="header that defines it (default: sys/user.h)")
    args = ap.parse_args()

    sysroot = find_sysroot()
    sizeof, fields = parse_struct(args.struct, args.header, sysroot)
    print(f"sysroot:  {sysroot}")
    print(f"header:   {args.header}")
    print(f"struct:   {args.struct}")
    print(f"sizeof:   {sizeof}")
    print()
    print(f"  {'off':>4} {'sz':>4}  {'type':<30} name")
    print(f"  {'---':>4} {'--':>4}  {'-'*30} {'-'*30}")
    for off, size, ty, name in fields:
        print(f"  {off:>4} {size:>4}  {ty:<30} {name}")


if __name__ == "__main__":
    main()
