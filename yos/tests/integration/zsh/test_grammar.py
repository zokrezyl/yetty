"""zsh grammar / control-flow smoke tests — happy-path, rc-only.

These don't assert on stdout: the wasm guest's stdout is currently
swallowed (see test_silent_builtins.py for the pinned xfail). What they
DO check is that zsh's parser + executor reach the end without trapping
or wedging — covering arithmetic, for/while/if/case, functions,
parameter expansion, and arrays. If any of these regress, zsh's wasm
build broke something fundamental.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh = find_zsh_wasm(repo)
    if not zsh:
        print("SKIP: zsh.wasm not found (run `nix build .#zsh`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos binary not found: {yos}")
        sys.exit(1)

    def rc(code, expect_rc=0):
        r = subprocess.run([yos, zsh, "-c", code], capture_output=True, timeout=10)
        if r.returncode != expect_rc:
            print(f"FAIL: {code!r} → rc={r.returncode}, expected {expect_rc}")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)

    # ── arithmetic + parameter expansion ─────────────────────────────
    rc("(( 1 + 1 == 2 ))")
    rc("(( 1 + 1 == 3 ))", expect_rc=1)         # arith comparison fails → rc 1
    rc("x=$((2*3+1)); [ $x -eq 7 ]")
    rc("x=foo; [ ${#x} -eq 3 ]")                # ${#x} = string length
    rc("x=foobar; [ ${x:0:3} = foo ]")          # substring slice

    # ── for / while / if / case ──────────────────────────────────────
    rc("for i in a b c; do :; done")
    rc("i=0; while (( i < 3 )); do (( i++ )); done; [ $i -eq 3 ]")
    rc("if true; then :; fi")
    rc("if false; then exit 99; fi")            # untaken branch
    rc("case yes in yes) :;; *) exit 99;; esac")

    # ── functions + early return propagates exit code ────────────────
    rc("f() { return 5; }; f", expect_rc=5)
    rc("f() { local x=42; return $((x-42)); }; f")

    # ── arrays (zsh-specific, 1-indexed) ─────────────────────────────
    rc("a=(x y z); [ ${#a[@]} -eq 3 ]")
    rc("a=(x y z); [ ${a[2]} = y ]")            # zsh: a[2] == "y"

    # ── short-circuits ───────────────────────────────────────────────
    rc("true && true")
    rc("false || true")
    rc("true && false", expect_rc=1)
    rc("false && exit 99", expect_rc=1)         # second arm not reached → rc=1 from `false`
    rc("true; false", expect_rc=1)

    # ── exit codes through nested constructs ─────────────────────────
    rc("(exit 11)", expect_rc=11)               # subshell — runs in same process under -c
    rc("{ exit 13; }", expect_rc=13)            # group cmd

    print("PASS: zsh grammar (arith/loops/conditions/functions/arrays/exits)")


if __name__ == "__main__":
    main()
