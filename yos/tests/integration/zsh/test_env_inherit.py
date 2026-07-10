"""zsh's local env handling — export, unset, default expansion.

What we DO test:
  - HOME / PATH come through as non-empty (yos seeds something even
    when getpwuid stubs to NULL, so [ -n "$HOME" ] is true).
  - Builtins that mutate the local env (export, unset) and the
    `${VAR:-default}` parameter expansion all work in-process.

What we DON'T test here (separately pinned in test_env_passthrough.py
as xfail):
  - Whether host-side env *values* round-trip into the guest.
    Currently they don't — yos's argv_setup either filters or rewrites
    standard names (HOME ends up as "/", arbitrary names are dropped).
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
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=10)
        if r.returncode != expect_rc:
            print(f"FAIL: {code!r} → rc={r.returncode}, expected {expect_rc}")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)

    # HOME shows up as something non-empty (yos seeds it even when
    # getpwuid stubs to NULL — fallback "/").
    rc('[ -n "$HOME" ]')
    rc('[ -n "$PATH" ]')

    # In-process export/unset/default-expansion all work.
    rc('export X=hello; [ "$X" = hello ]')
    rc('export X=hello; unset X; [ -z "$X" ]')
    rc('unset NOTSET; [ "${NOTSET:-fallback}" = fallback ]')
    rc('X=set; [ "${X:-fallback}" = set ]')

    # `${VAR:?msg}` exits 1 when unset.
    rc('unset NOTSET; : ${NOTSET:?missing}', expect_rc=1)

    # Read-only var assignment fails (rc != 0) but doesn't crash.
    rc('readonly X=42; X=99 2>/dev/null', expect_rc=1)

    print("PASS: zsh in-process env handling (export/unset/default-expand)")


if __name__ == "__main__":
    main()
