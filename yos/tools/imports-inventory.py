#!/usr/bin/env python3
"""imports-inventory.py — Tier 1 of issue #4.

Enumerates the `env.<name>` imports of every wasm binary under
`<umbrella>/libexec/`, cross-references against the live set of
bridges yos actually links, and produces a per-package import +
coverage report.

Output (default destination `result-imports/`):
  result-imports/<pkg>/<binary>.txt       — one `env.<name>` per line
  result-imports/<pkg>/<binary>.coverage  — `<name> <BRIDGED|STUB|MISSING>`
  result-imports/summary.txt              — aggregate

Bridged-set is read from a `--bridges-file` (default: the running
yos binary's symbol table — every `yos_<name>` host symbol that
takes a `struct yos_exec_ctx *`). The script doesn't need to know
which bridges are real vs stub bodies at this tier — the goal is
to surface imports that aren't bridged at all (would trap on load
or call) and to diff the set across builds.

Usage:
    tools/imports-inventory.py                       # picks .#all
    tools/imports-inventory.py --umbrella result     # explicit path
    tools/imports-inventory.py --out tmp/imports     # alt output dir
    tools/imports-inventory.py --diff result-imports.old   # vs prior

Requires `wasm-objdump` (wabt) on PATH; nm + grep for the yos
symbol-table reader.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def umbrella_default() -> Path:
    out = subprocess.run(
        ["nix", "path-info", ".#all"],
        capture_output=True, text=True, check=False,
    )
    if out.returncode != 0 or not out.stdout.strip():
        raise SystemExit("imports-inventory: nix path-info .#all failed; "
                         "pass --umbrella explicitly")
    return Path(out.stdout.strip())


def bridge_set(yos_bin: Path) -> set:
    """Symbols of the form `yos_<libc-name>` in the host yos binary."""
    # yos is statically linked → no dynamic symbol table; go straight
    # to the full symbol set. `--defined-only` skips undefined externs.
    out = subprocess.run(
        ["nm", "--defined-only", str(yos_bin)],
        capture_output=True, text=True, check=False,
    )
    if out.returncode != 0:
        raise SystemExit(f"nm failed on {yos_bin}: {out.stderr}")
    names = set()
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        sym = parts[-1]
        # Four bridge-symbol shapes:
        #   yos_<libc-name>      — hand-written or codegen-emitted C bridge
        #   m3_yos_<libc-name>   — wasm3 raw-function wrapper (auto-bridge)
        #   m3_<libc-name>       — hand-rolled wasm3 wrapper (no C body),
        #                          e.g. syslog/openlog/closelog
        #   yos__<libc-name>     — names starting with `_` like _exit
        if sym.startswith("m3_yos_"):
            names.add(sym[len("m3_yos_"):])
        elif sym.startswith("yos__"):
            names.add("_" + sym[len("yos__"):])
        elif sym.startswith("yos_"):
            names.add(sym[len("yos_"):])
        elif sym.startswith("m3_"):
            names.add(sym[len("m3_"):])
    return names


def wasm_imports(wasm: Path) -> list:
    out = subprocess.run(
        ["wasm-objdump", "-j", "Import", "-x", str(wasm)],
        capture_output=True, text=True, check=False,
    )
    if out.returncode != 0:
        return []
    imports = []
    for line in out.stdout.splitlines():
        line = line.strip()
        # Examples:
        #   - func[0] sig=2 <env.write> <- env.write
        #   - memory[0] pages: initial=2 <- env.memory
        if "<- env." not in line:
            continue
        name = line.split("<- env.", 1)[1].strip()
        if name and not name.startswith("memory") and not name.startswith("__"):
            imports.append(name)
    return imports


def classify(imports: list, bridged: set) -> dict:
    seen = {}
    for name in imports:
        if name in seen:
            continue
        seen[name] = "BRIDGED" if name in bridged else "MISSING"
    return seen


def find_wasm(libexec: Path) -> list:
    """Find every wasm binary under libexec. Some are file-magic-only;
    most have a recognisable name. We probe with `file` to avoid
    listing shell wrappers and the like."""
    items = []
    for p in sorted(libexec.iterdir()):
        if not p.is_file():
            continue
        try:
            with p.open("rb") as fh:
                magic = fh.read(4)
        except OSError:
            continue
        if magic == b"\x00asm":
            items.append(p)
    return items


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--umbrella", type=Path, default=None,
                    help="Nix umbrella path (default: nix path-info .#all)")
    ap.add_argument("--yos-bin", type=Path, default=None,
                    help="yos host binary (default: <umbrella>/bin/yos)")
    ap.add_argument("--out", type=Path, default=Path("result-imports"),
                    help="Output dir (default: result-imports/)")
    ap.add_argument("--diff", type=Path, default=None,
                    help="Compare against a prior --out tree")
    args = ap.parse_args()

    if shutil.which("wasm-objdump") is None:
        raise SystemExit("imports-inventory: wasm-objdump not on PATH "
                         "(wabt package)")

    umbrella = args.umbrella or umbrella_default()
    yos_bin = args.yos_bin or (umbrella / "bin" / "yos")
    libexec = umbrella / "libexec"
    if not yos_bin.exists():
        raise SystemExit(f"yos binary not found: {yos_bin}")
    if not libexec.is_dir():
        raise SystemExit(f"libexec dir not found: {libexec}")

    bridged = bridge_set(yos_bin)
    if args.out.exists():
        shutil.rmtree(args.out)
    args.out.mkdir(parents=True)

    pkg_dir = args.out / "_all"
    pkg_dir.mkdir(exist_ok=True)
    summary = {}
    for wasm in find_wasm(libexec):
        name = wasm.name
        imports = wasm_imports(wasm)
        with (pkg_dir / f"{name}.txt").open("w") as fh:
            fh.write("\n".join(sorted(set(imports))) + "\n")
        cov = classify(imports, bridged)
        with (pkg_dir / f"{name}.coverage").open("w") as fh:
            for n in sorted(cov):
                fh.write(f"{cov[n]:8s} {n}\n")
        missing = [n for n, v in cov.items() if v == "MISSING"]
        summary[name] = (len(cov), len(missing), missing)

    with (args.out / "summary.txt").open("w") as fh:
        fh.write(f"Umbrella: {umbrella}\n")
        fh.write(f"Bridged-symbol count: {len(bridged)}\n\n")
        for name, (total, missing, names) in sorted(summary.items()):
            fh.write(f"{name}: {total} imports, {missing} missing\n")
            for n in sorted(names):
                fh.write(f"    MISSING env.{n}\n")
            fh.write("\n")
    print(f"Wrote inventory: {args.out}/summary.txt")

    if args.diff and args.diff.exists():
        print(f"\nDiff vs {args.diff}:")
        for name in summary:
            old = args.diff / "_all" / f"{name}.txt"
            new = pkg_dir / f"{name}.txt"
            if not old.exists():
                print(f"  + {name} (new)")
                continue
            old_set = set(old.read_text().splitlines())
            new_set = set(new.read_text().splitlines())
            added = new_set - old_set
            removed = old_set - new_set
            if added or removed:
                print(f"  {name}:")
                for a in sorted(added):
                    print(f"    + env.{a}")
                for r in sorted(removed):
                    print(f"    - env.{r}")


if __name__ == "__main__":
    main()
