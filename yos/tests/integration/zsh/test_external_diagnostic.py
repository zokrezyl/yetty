"""Pinned regression: zsh's `command not found` diagnostic is mangled.

Run zsh under yos and ask it to invoke a binary that doesn't exist
(`ps`, `/bin/nope`, …). zsh should print something like
`zsh: command not found: ps` to stderr and exit 127. Under yos today:

  - The exit code IS 127 (handled by `test_external_missing.py`).
  - But the diagnostic itself is mangled. stdout receives the literal
    bytes `1: ` (3 chars: "1", ":", " ") — a fragment of zsh's
    `prog:LINE: msg` format that's been truncated to nothing useful.
    stderr is completely empty.
  - The 3-byte truncation strongly suggests the SAME root cause as
    test_silent_builtins.py — yos's fd-1/fd-2 write bridge is
    swallowing most of what zsh writes, and the surviving fragment
    is going to the wrong fd.

So an interactive user typing `ps<Enter>` sees nothing happen at all,
which is what the bug report described. xfail until task #15 (the
write-bridge bug) is fixed; this test will then flip to
UNEXPECTEDPASS along with `silent-builtins`.
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

    # Each command should produce a recognisable diagnostic on stderr.
    # The exact wording varies (zsh prints `zsh:1: command not found: X`,
    # `zsh: no such file or directory: X`, etc.) so we look for either
    # the binary name OR the phrase "not found" / "no such file" — any
    # one of those is enough to call the diagnostic "intelligible".
    cases = [
        ("ps",                       [b"ps", b"not found"]),
        ("/bin/this-binary-no-exist", [b"this-binary-no-exist",
                                       b"not found", b"no such file"]),
    ]

    failed = 0
    for code, markers in cases:
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=5)
        # Combined output — let zsh print to whichever fd it chose.
        combined = r.stdout + r.stderr
        if not any(m in combined for m in markers):
            print(f"  unintelligible: {code!r} → "
                  f"stdout={r.stdout!r} stderr={r.stderr!r} "
                  f"(expected any of {markers!r})")
            failed += 1

    if failed:
        print(f"FAIL: {failed}/{len(cases)} missing-command invocations "
              f"produced no readable diagnostic (task #15 family)")
        sys.exit(1)

    print("PASS: zsh missing-command diagnostic reaches host stderr")


if __name__ == "__main__":
    main()
