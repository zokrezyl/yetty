"""External commands that don't exist must fail cleanly with rc=127.

zsh's `command not found` path goes through fork+exec. Under yos that's
the asyncify-rewind dance + a process-table allocation. Most ways for
this to go wrong make zsh hang (the parent waits for a child that never
comes back) instead of returning 127, so this test bounds the run with
a timeout and fails on either a hang or a wrong exit code.
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

    # Three flavours of "spawn external", each must come back rc=127
    # within 5 s and not deadlock yos's exec/wait machinery.
    cases = [
        "/bin/this-binary-does-not-exist-xyz",
        "command this-other-missing-cmd",
        "exec /bin/no-such 2>/dev/null; exit 0",   # exec-failure is also 127
    ]
    for code in cases:
        try:
            r = subprocess.run([yos, zsh, "-c", code],
                               capture_output=True, timeout=5)
        except subprocess.TimeoutExpired:
            print(f"FAIL: {code!r} hung (>5s)")
            sys.exit(1)
        if r.returncode != 127:
            print(f"FAIL: {code!r} → rc={r.returncode}, expected 127")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)

    # Not-found via PATH (no leading /) — same expected behaviour.
    try:
        r = subprocess.run([yos, zsh, "-c", "definitely-not-on-path"],
                           capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        print("FAIL: PATH-based missing command hung (>5s)")
        sys.exit(1)
    if r.returncode != 127:
        print(f"FAIL: PATH-miss → rc={r.returncode}, expected 127")
        sys.exit(1)

    print("PASS: zsh missing-external returns 127 within bounded time")


if __name__ == "__main__":
    main()
