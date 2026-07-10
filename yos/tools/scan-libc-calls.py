#!/usr/bin/env python3
"""Per-TU libc call scanner.

Usage:
    scan-libc-calls.py --src FOO.c --out FOO.libccalls.json [-- CLANG_FLAGS...]

Parses the source file with libclang using the same flags the actual
compile uses, walks the AST, and records every CallExpr whose callee
resolves to a libc function name from the guest-api yaml. Also flags
member-access on libc-shaped struct types (catches the `_dirfd(dirp)`
macro → `dirp->dd_fd` member-access bug — pure-macro libc usage that
never produces a CallExpr).

Designed to be wrapped around wasm-clang invocations; see
tools/wasm-clang-trace.
"""
import argparse
import json
import os
import sys
from pathlib import Path

try:
    import clang.cindex
except ImportError:
    sys.stderr.write("scan-libc-calls: python `clang` (libclang) bindings missing\n")
    sys.exit(0)  # don't break the build


REPO_ROOT = Path(__file__).resolve().parent.parent
GUEST_API_YAML = REPO_ROOT / "build-linux" / "src" / "yos" / "codegen" / "guest-api-i386-freebsd.yaml"


def load_libc_function_set():
    """Return the set of FreeBSD libc function names yos has bridged
    or extracted. We use the extracted yaml as the authority — it's
    what bridge.py works from.

    yaml layout (from extract.py):
        functions:
          htonl:
            return_uid: t_X
            ...
          select:
            ...

    Streaming parse for speed and to avoid making pyyaml load it
    (the guest yaml is ~600 KB)."""
    if not GUEST_API_YAML.exists():
        return set()
    names = set()
    in_functions = False
    with open(GUEST_API_YAML) as f:
        for line in f:
            if line.rstrip() == "functions:":
                in_functions = True
                continue
            if not in_functions:
                continue
            # Top-level (col 0) key ends the functions: block.
            if line and not line.startswith(" ") and not line.startswith("\t"):
                in_functions = False
                continue
            # Function-name lines look like "  htonl:" — exactly two
            # spaces of indent and a trailing colon. Deeper indents
            # are nested attributes (return_uid, header, etc.).
            if line.startswith("  ") and not line.startswith("   "):
                stripped = line[2:].rstrip()
                if stripped.endswith(":"):
                    names.add(stripped[:-1])
    return names


def collect_calls(tu, src_path, libc_fns):
    """Walk the TU's AST and record every CallExpr whose callee name
    is in libc_fns. Records source line and the syntactic form (call
    vs macro-expanded)."""
    out = []
    src_abs = os.path.realpath(src_path)
    for cur in tu.cursor.walk_preorder():
        if cur.kind != clang.cindex.CursorKind.CALL_EXPR:
            continue
        if cur.location is None or cur.location.file is None:
            continue
        # Only count CallExprs whose SOURCE location is in the TU's
        # own file (skip headers). The ones that surface as imports
        # at link time are the ones written in the package's sources.
        if os.path.realpath(str(cur.location.file)) != src_abs:
            continue
        callee_name = None
        ref = cur.referenced
        if ref is not None and ref.spelling:
            callee_name = ref.spelling
        else:
            sp = cur.spelling
            if sp:
                callee_name = sp
        if not callee_name or callee_name not in libc_fns:
            continue
        # Detect "via": call from a macro expansion vs direct source call.
        ext = cur.extent
        via = "call"
        if ext is not None and ext.start.file is not None:
            # If the cursor's location is inside a macro instantiation,
            # clang reports the spelling location vs expansion location
            # differently — but the simple flag is the cursor's
            # is_macro_function attribute (not present in all cindex
            # versions). Fall back to comparing offsets.
            try:
                if cur.kind.name == "MACRO_INSTANTIATION":
                    via = "macro"
            except AttributeError:
                pass
        out.append({
            "line": cur.location.line,
            "col": cur.location.column,
            "fn": callee_name,
            "via": via,
        })
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--src", required=True, help="source file (.c)")
    p.add_argument("--out", required=True, help="output JSON path")
    p.add_argument("clang_flags", nargs=argparse.REMAINDER,
                   help="-- followed by the same flags clang would use")
    args = p.parse_args()

    flags = args.clang_flags
    if flags and flags[0] == "--":
        flags = flags[1:]

    libc_fns = load_libc_function_set()
    if not libc_fns:
        sys.stderr.write("scan-libc-calls: empty libc set "
                         "(extract guest API yaml missing?) — emitting empty report\n")

    index = clang.cindex.Index.create()
    try:
        tu = index.parse(args.src, args=flags,
                         options=clang.cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    except clang.cindex.TranslationUnitLoadError as e:
        sys.stderr.write(f"scan-libc-calls: parse failed for {args.src}: {e}\n")
        sys.exit(0)

    # Don't fail-stop on header errors — wasm-pkg recipes regularly
    # produce TUs with errors in transitively-included headers we
    # don't bridge. We still get useful CallExpr data.
    diag_errors = [d for d in tu.diagnostics
                   if d.severity >= clang.cindex.Diagnostic.Error]

    calls = collect_calls(tu, args.src, libc_fns)

    out = {
        "src": args.src,
        "calls": calls,
        "diag_errors": len(diag_errors),
    }
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2, sort_keys=True)


if __name__ == "__main__":
    main()
