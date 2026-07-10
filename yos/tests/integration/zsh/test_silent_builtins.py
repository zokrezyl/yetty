"""Pinned regression: zsh -c builtin output never reaches the host stdout.

`echo`, `print`, and `printf` are zsh BUILTINS — no fork, no exec; they
write directly to fd 1 in-process. Under yos today, that fd-1 write
returns success (rc=0 from zsh) but the host pipe sees no bytes. Likely
either:

  (a) the wasm guest's libc-side write buffering doesn't flush before
      the guest exits, OR
  (b) yos's write() bridge to fd 1 dispatches into a sink that isn't
      the host's stdout (Tier-2 sidecar? wrong fd-table entry?).

This test pins the broken behaviour as `should_fail = true` so the
suite stays green while the bug exists. When it's fixed (host sees the
bytes) the test flips to UNEXPECTEDPASS — meson's signal that the xfail
needs to be removed and the test promoted to a regular passing test.

Tracked as task #15 — "Investigate zsh stdout buffering bug".
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

    def out_for(code):
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=10)
        if r.returncode != 0:
            print(f"FAIL (setup): {code!r} → rc={r.returncode}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)
        return r.stdout

    # Each of these MUST emit the marker on stdout. They all currently
    # return empty.
    cases = [
        ("echo zsh-marker-1",          b"zsh-marker-1"),
        ("print zsh-marker-2",         b"zsh-marker-2"),
        ('printf "%s\\n" zsh-marker-3', b"zsh-marker-3"),
        # arithmetic substitution into echo — exercises a different
        # code path inside zsh than a literal arg.
        ("echo $((1+2+3))",            b"6"),
        # Variable expansion → echo.
        ('x=hello; echo "$x"',         b"hello"),
    ]

    failed = 0
    for code, marker in cases:
        out = out_for(code)
        if marker not in out:
            print(f"  silent: {code!r} (expected {marker!r}, got {out!r})")
            failed += 1

    if failed:
        print(f"FAIL: {failed}/{len(cases)} zsh -c builtin invocations "
              f"returned no stdout (task #15: stdout buffering bug)")
        sys.exit(1)

    print("PASS: zsh builtin stdout reaches host (task #15 appears fixed)")


if __name__ == "__main__":
    main()
