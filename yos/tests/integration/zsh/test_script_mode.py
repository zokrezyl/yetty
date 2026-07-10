"""zsh -c <command> non-interactive smoke tests.
Pins the parts of zsh that DO work today: passing exit codes through,
parsing scripts, --version. None of these go anywhere near zle/runhook.
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

    def run(*args, expect_rc=0, expect_substr=None, stdin=None):
        r = subprocess.run([yos, zsh, *args], capture_output=True, input=stdin, timeout=15)
        if r.returncode != expect_rc:
            print(f"FAIL: {args} → rc={r.returncode}, expected {expect_rc}")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)
        if expect_substr is not None and expect_substr not in r.stdout:
            print(f"FAIL: {args} stdout missing {expect_substr!r}")
            print(f"  stdout: {r.stdout!r}")
            sys.exit(1)
        return r

    # --version goes straight to fd 1 and prints the build banner.
    run("--version", expect_substr=b"zsh 5.9")

    # `-c 'exit N'` propagates N through yos cleanly.
    run("-c", "exit 0",  expect_rc=0)
    run("-c", "exit 7",  expect_rc=7)
    run("-c", "exit 42", expect_rc=42)

    # Multi-line script via stdin (-s reads stdin as the script).
    run("-s", expect_rc=0, stdin=b"true\nfalse || true\n")

    print("PASS: zsh script-mode (--version + exit codes + stdin script)")


if __name__ == "__main__":
    main()
