"""Pinned regression: a pipe between two zsh-built-in commands hangs.

`echo a | read v` is the smallest case — both sides are builtins, so no
external process is involved. zsh forks the right-hand side into a
subshell that reads from a pipe. Under yos today this wedges:
subprocess.run hits its timeout and the parent never collects the
child. Most likely yos's pipe + asyncify-fork combo isn't waking the
reader, but root cause is open.

xfail until the underlying fork/pipe path is fixed. When it works, this
test flips to UNEXPECTEDPASS and meson surfaces it.
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

    # Pipe between two builtins. Bound at 5 s; current behaviour is to
    # hang (TimeoutExpired) — the test FAILS in that case so meson sees
    # it as a real failure under should_fail=true.
    code = 'echo a | read v; [ "$v" = a ]'
    try:
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        print("FAIL: zsh pipe-of-builtins hung (>5s)")
        sys.exit(1)
    if r.returncode != 0:
        print(f"FAIL: {code!r} → rc={r.returncode}")
        print(f"  stdout: {r.stdout!r}")
        print(f"  stderr: {r.stderr!r}")
        sys.exit(1)

    print("PASS: zsh pipe between builtins works")


if __name__ == "__main__":
    main()
