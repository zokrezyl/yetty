#!/usr/bin/env python3
"""Aggregate per-TU libc-call JSON dumps from tools/wasm-clang-trace
into a per-package coverage report joined against bridge state.

Reads:
    --trace-dir DIR    directory of *.libccalls.json files
    --bridge   PATH    build-linux/src/yos/codegen/yos_bridge.c

Outputs a markdown report:

    package: zsh
    total libc calls: 1234   (across 87 source files)
    unique fns: 156

    | function | call sites | bridge state |
    |----------|------------|--------------|
    | strdup   | 42         | real (codegen RET_NEW_DUP)  |
    | setproctitle | 1      | STUB (hooks.yaml -> stub)   |
    | pthread_sigmask | 8   | real (impl/sig.c)           |
    | ...
"""
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


# Match the per-fn body comment that bridge.py emits to classify each
# auto-generated bridge. Patterns we care about:
#   "/* foo: hooks.yaml -> stub (Linux-only or unportable). */"
#       → explicit stub from hooks.yaml
#   "/* TODO: complex arg/return types — extend bridge.py to render. */"
#       → bridge.py couldn't generate a real body; auto-stub
# Anything else with a body that doesn't match those is treated as a
# real bridge (passthrough or custom_*).
STUB_COMMENT_RE = re.compile(
    r"/\*\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*hooks\.yaml\s*->\s*stub\b"
)
COMPLEX_RE = re.compile(r"/\*\s*TODO:\s*complex arg/return types")


REPO_ROOT = Path(__file__).resolve().parent.parent


def collect_extra_link_names(bridge_c_path):
    """Functions bound via m3_LinkRawFunction(module, "env",
    "<name>", ...) — covers hand-written sources (main.c, impl/signal.c,
    impl/freebsd_userland.c, impl/proc.c's m3_execvp etc.) PLUS the
    auto-generated yos_bridge.c. The latter is critical because many
    bridges (write, read, open, …) have their implementations in
    impl/io/io.c — they don't appear as yos_<name> bodies inside
    yos_bridge.c itself, only as m3_LinkRawFunction → m3w_<name> →
    yos_<name>() call sites. Without scanning the bridge C the
    aggregator misclassifies those names as 'missing'."""
    pat = re.compile(r'm3_LinkRawFunction(?:Ex)?\([^,]+,\s*"env"\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"')
    names = set()
    sources = list(REPO_ROOT.glob("src/yos/**/*.c"))
    if bridge_c_path.exists():
        sources.append(bridge_c_path)
    for c in sources:
        try:
            for m in pat.finditer(c.read_text(errors="ignore")):
                names.add(m.group(1))
        except OSError:
            continue
    return names


def classify_bridge_states(bridge_c_path):
    """Return dict: fn_name -> 'stub' | 'auto-stub' | 'real' | 'missing'.

    A function is 'missing' if it doesn't appear in yos_bridge.c at all
    AND isn't bound via m3_LinkRawFunction elsewhere. """
    states = {}
    for n in collect_extra_link_names(bridge_c_path):
        states[n] = "real"  # hand-bound; might be a no-op stub still,
                            # but at least the import resolves at load time
    if not bridge_c_path.exists():
        return states
    # Find every yos_<fn>(struct yos_exec_ctx ...) {  ... }
    # Look at the first non-blank comment inside the body to classify.
    fn_def = re.compile(
        r"^(?:uint32_t|int32_t|int64_t|uint64_t|void|double|float)\s+yos_(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(struct\s+yos_exec_ctx"
    )
    text = bridge_c_path.read_text()
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = fn_def.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group("name")
        # Look at next ~6 lines for a classifying comment
        sniff = "\n".join(lines[i:i+8])
        # If we already classified this name as "real" via
        # m3_LinkRawFunction discovery, keep that — it means a manual
        # bridge wins the binding race, regardless of the auto-generated
        # body's content. Same applies if main.c's
        # yos_freebsd_userland_link or impl/signal.c::yos_signal_link
        # binds the name; both flow through m3_LinkRawFunction.
        if states.get(name) == "real":
            i += 1
            continue
        if STUB_COMMENT_RE.search(sniff):
            states[name] = "stub"
        elif COMPLEX_RE.search(sniff):
            states[name] = "auto-stub"
        else:
            states[name] = "real"
        i += 1
    return states


def collect_calls(trace_dir):
    """Read every *.libccalls.json and collect (fn, src, line) tuples."""
    sites = defaultdict(list)   # fn -> [(src, line)]
    srcs = set()
    for jp in sorted(Path(trace_dir).glob("*.libccalls.json")):
        try:
            d = json.loads(jp.read_text())
        except Exception:
            continue
        srcs.add(d.get("src", str(jp)))
        for c in d.get("calls", []):
            sites[c["fn"]].append((d.get("src", str(jp)), c["line"]))
    return sites, srcs


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--trace-dir", required=True)
    p.add_argument("--bridge", default="build-linux/src/yos/codegen/yos_bridge.c")
    p.add_argument("--package", default=None, help="package name for the heading")
    p.add_argument("--out", default=None, help="markdown output (default: stdout)")
    args = p.parse_args()

    sites, srcs = collect_calls(args.trace_dir)
    states = classify_bridge_states(Path(args.bridge))

    pkg = args.package or Path(args.trace_dir).name

    lines = []
    lines.append(f"# libc coverage — {pkg}\n")
    lines.append(f"- source files traced: **{len(srcs)}**")
    lines.append(f"- unique libc fns called: **{len(sites)}**")
    lines.append(f"- total call sites: **{sum(len(v) for v in sites.values())}**\n")

    # Per-state buckets.
    buckets = defaultdict(list)  # state -> [(fn, count, first_site)]
    for fn, ss in sites.items():
        state = states.get(fn, "missing")
        first = ss[0]
        buckets[state].append((fn, len(ss), first))

    order = ["stub", "auto-stub", "missing", "real"]
    headings = {
        "stub":      "🟥 stubs reached by this package (hooks.yaml → stub)",
        "auto-stub": "🟧 auto-stubs reached by this package (bridge.py couldn't render)",
        "missing":   "⬜ called but NOT emitted as a bridge (would trap as unresolved import)",
        "real":      "🟩 real bridges (passthrough / custom_* / handcrafted)",
    }

    for state in order:
        if not buckets[state]:
            continue
        lines.append(f"\n## {headings[state]}  ({len(buckets[state])})\n")
        lines.append("| fn | sites | first call |")
        lines.append("|----|-------|------------|")
        for fn, n, (src, line) in sorted(buckets[state], key=lambda x: -x[1]):
            short = src.split("/")[-1] if "/" in src else src
            lines.append(f"| `{fn}` | {n} | `{short}:{line}` |")

    out = "\n".join(lines) + "\n"
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(out)
    else:
        sys.stdout.write(out)


if __name__ == "__main__":
    main()
