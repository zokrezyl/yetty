"""Pinned regression: command substitution `$(...)` hangs zsh under yos.

`x=$(echo inner)` runs `echo inner` in a subshell and captures its
stdout into `x`. zsh forks a child for the subshell and reads from a
pipe. Under yos today this wedges — same general failure mode as the
plain pipe test (the child never satisfies the parent's read), but
worth pinning separately because the fix may land independently:
command substitution involves an extra rc-propagation path.

xfail until the underlying fork/pipe path is fixed.
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

    # `x=$(echo inner)` should leave x="inner". Today this hangs.
    code = 'x=$(echo inner); [ "$x" = inner ]'
    try:
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        print("FAIL: zsh command substitution hung (>5s)")
        sys.exit(1)
    if r.returncode != 0:
        print(f"FAIL: {code!r} → rc={r.returncode}")
        print(f"  stdout: {r.stdout!r}")
        print(f"  stderr: {r.stderr!r}")
        sys.exit(1)

    print("PASS: zsh command substitution works")


if __name__ == "__main__":
    main()
