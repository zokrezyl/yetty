#!/usr/bin/env python3
"""tools/libc-bridge-audit.py — cross-check yos's bridge classifications
against the libc surface every host platform actually exports.

Why: the codegen pipeline auto-classifies each FreeBSD libc function
into one of {compatible, mechanical, needs_policy, unsupported,
guest_only, variadic_skipped}. hooks.yaml lets a human override the
classification — most importantly, `stub:` forces ENOSYS regardless
of what the analyser said. That's a footgun: a function listed under
`stub:` because the bridge couldn't auto-generate ONE platform's
version stays stubbed on EVERY platform, even ones where a clean
passthrough would work.

The fts info-leak (`Undefined error: 0`), the ssh `fstat: Bad file
descriptor`, and the `ssh nixem: Could not resolve hostname` all fit
that shape: a real bridge was wrongly classified as a stub and nobody
noticed until a user hit it.

This script answers two questions:

  1. Of every bridge currently stubbed, how many are AVAILABLE on at
     least one host platform (darwin / iOS / tvOS / linux)?
     → Those are candidates for removal from hooks.yaml's stub list.

  2. Of every bridge currently real (passthrough or custom_*), how
     many have zero test coverage in tests/ut/libc/?
     → Those are the next bugs.

Reads (per build dir found):
    build-<host>/src/yos/codegen/host-api.yaml
    build-<host>/src/yos/codegen/guest-api-i386-freebsd.yaml
    build-<host>/src/yos/codegen/analyse-report.yaml
    build-<host>/src/yos/codegen/yos_bridge.c     (linked-bridge list)

Optional comparison hosts: pass --host build-linux to include
Linux's host-api when that build dir has been populated. Without it
the audit only knows about Apple platforms.
"""
from __future__ import annotations
import argparse
import re
import sys
from pathlib import Path

import yaml  # PyYAML — already in .venv via uv sync

REPO = Path(__file__).resolve().parent.parent


def load_host_api(build_dir: Path) -> set[str]:
    f = build_dir / "src" / "yos" / "codegen" / "host-api.yaml"
    if not f.exists():
        return set()
    d = yaml.safe_load(f.read_text())
    return set((d.get("functions") or {}).keys())


def load_bridge_classification(bridge_c: Path):
    """Walk yos_bridge.c, return:
       linked   = set of every env.* fn name LinkRawFunction'd
       stubs    = subset that's a stub body (hooks.yaml -> stub,
                  TODO: complex, or refused by globals policy)
    """
    src = bridge_c.read_text()
    linked = set(re.findall(r'm3_LinkRawFunction\([^,]+,\s*"env",\s*"([^"]+)"', src))
    stubs = set()
    for name in linked:
        body_re = (rf'^(?:int32_t|uint32_t|void|float|double|int64_t)\s+'
                   rf'yos_{re.escape(name)}\b.*?(?=^\}}\n)')
        m = re.search(body_re, src, re.M | re.S)
        if not m: continue
        b = m.group(0)
        if ("hooks.yaml -> stub" in b
            or "TODO: complex arg/return types" in b
            or "refused by globals policy" in b):
            stubs.add(name)
    return linked, stubs


def load_analyse(build_dir: Path):
    f = build_dir / "src" / "yos" / "codegen" / "analyse-report.yaml"
    if not f.exists():
        return {}
    return yaml.safe_load(f.read_text()) or {}


def collect_test_calls(test_dir: Path) -> set[str]:
    """Every identifier called as a function in any test_*.c file.
       Not perfect, but flags coverage at the name level."""
    called = set()
    for p in test_dir.glob("test_*.c"):
        txt = p.read_text()
        called.update(re.findall(r'\b([a-zA-Z_]\w*)\s*\(', txt))
    return called


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", action="append", default=[],
                    help="extra build-dir to include in the host union "
                         "(e.g. build-linux). Apple build dirs are "
                         "auto-discovered.")
    ap.add_argument("--show", type=int, default=40,
                    help="rows to print per section (default 40)")
    args = ap.parse_args()

    apple_builds = [REPO / d for d in
                    ("build-darwin", "build-ios-sim-x86_64", "build-tvos-arm64")
                    if (REPO / d / "src" / "yos" / "codegen" / "host-api.yaml").exists()]
    extra_builds = [Path(p) if Path(p).is_absolute() else REPO / p for p in args.host]

    all_builds = apple_builds + extra_builds
    if not all_builds:
        print("error: no build-<host>/src/yos/codegen/host-api.yaml found. "
              "Run `nix build .#all` (Apple host) or pass --host build-linux.",
              file=sys.stderr)
        return 2

    host_union = set()
    print(f"{'platform':30s}  {'host libc fns'}")
    for b in all_builds:
        fns = load_host_api(b)
        host_union |= fns
        print(f"  {b.name:28s}  {len(fns):5d}")
    print(f"  {'UNION':28s}  {len(host_union):5d}")
    print()

    # Bridge classification — use the darwin build's bridge as canonical;
    # the surface is identical across Apple platforms.
    canonical = apple_builds[0] if apple_builds else all_builds[0]
    bridge_c = canonical / "src" / "yos" / "codegen" / "yos_bridge.c"
    linked, stubs = load_bridge_classification(bridge_c)
    print(f"yos bridges linked to env.*  : {len(linked)}")
    print(f"  real (passthrough/custom)  : {len(linked) - len(stubs)}")
    print(f"  stubbed (-ENOSYS body)     : {len(stubs)}")
    print()

    # ── (1) stubs that ARE available on the host union ───────────────
    miscat = sorted(stubs & host_union)
    print(f"Stubbed but host libc HAS them: {len(miscat)}")
    print("  (these are candidates to remove from hooks.yaml `stub:` —")
    print("   codegen will auto-classify and emit a real bridge)")
    for n in miscat[:args.show]:
        print(f"    {n}")
    if len(miscat) > args.show:
        print(f"    ... and {len(miscat) - args.show} more (use --show N to see)")
    print()

    # Cross-check against analyse-report so we know WHICH category
    # they'd auto-promote to.
    ar = load_analyse(canonical)
    compat   = set(ar.get("compatible") or [])
    mech     = set((ar.get("mechanical") or {}).keys())
    needs    = set((ar.get("needs_policy") or {}).keys())
    variadic = set(ar.get("variadic_skipped") or [])

    safe_to_unstub = sorted((stubs & host_union) & (compat | mech))
    risky_to_unstub = sorted((stubs & host_union) & (needs | variadic))
    print(f"Of those, SAFE to unstub (compatible/mechanical): {len(safe_to_unstub)}")
    for n in safe_to_unstub[:args.show]:
        cat = "compatible" if n in compat else "mechanical"
        print(f"    {n:30s} {cat}")
    print()
    print(f"Of those, RISKY to unstub (needs_policy/variadic): {len(risky_to_unstub)}")
    for n in risky_to_unstub[:args.show]:
        cat = "needs_policy" if n in needs else "variadic"
        print(f"    {n:30s} {cat}")
    print()

    # ── (2) real bridges with no test mention ────────────────────────
    test_calls = collect_test_calls(REPO / "tests" / "ut" / "libc")
    real = linked - stubs
    real_untested = sorted(real - test_calls)
    print(f"Real bridges with NO test mention: {len(real_untested)} / {len(real)} "
          f"({len(real_untested)*100//len(real)}% of real bridges)")
    for n in real_untested[:args.show]:
        print(f"    {n}")
    if len(real_untested) > args.show:
        print(f"    ... and {len(real_untested) - args.show} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
