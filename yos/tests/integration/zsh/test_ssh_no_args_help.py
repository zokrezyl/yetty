"""Pinned regression: `echo ssh | yos zsh` must surface ssh's usage banner.

Reproduction (the simplest possible):

    echo ssh | ./tools/yos.sh

Expected (host openssh behaviour):

    usage: ssh [-46AaCfGgKkMNnqsTtVvXxYy] [-B bind_interface]
               [-b bind_address] [-c cipher_spec] [-D [bind_address:]port]
               ...

Observed under yos today: ssh forks, the child's stderr write surfaces
as

    \\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xffad file descriptor
    zsh: error on TTY read: Bad file descriptor

— eight bytes of asyncify-rewind 0xff garbage, then "Bad" with its
leading 'B' eaten, then zsh dies because the controlling tty fd has
gone EBADF.

Root cause is the same fork+exec EBADF cascade pinned at the libc level
in `tests/ut/libc/test_post_fork_stderr_clean.c`. The integration view
matters too: that libc test exercises the leak with a pipe under a
plain wasm guest; this test exercises it through the realistic shell
path the user actually runs (zsh + the libexec ssh wasm under host
PATH).

PATH must point at the wasm tool directory (the same `libexec/` the
`tools/yos.sh` wrapper sets) — otherwise zsh falls through to "command
not found" and the bug never fires. The default PATH=/bin:/usr/bin
used by other zsh tests would mask this.

Marked xfail; flips to UNEXPECTEDPASS when the fork-stderr leak is
fixed.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


# Match ssh's OpenSSH usage banner. The exact flag set drifts between
# OpenSSH releases, so we anchor on the line prefix only.
USAGE_RE = re.compile(rb"usage:\s*ssh\b")

# Symptoms of the bug — any of these must NOT appear.
LEAK_PATTERNS = [
    re.compile(rb"\xff{4,}"),                 # asyncify-rewind 0xff prefix
    re.compile(rb"\bad file descriptor\b"),   # truncated "Bad file descriptor"
    re.compile(rb"error on TTY read"),        # zsh's death cry
]


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh_wasm = find_zsh_wasm(repo)
    if not zsh_wasm:
        print("SKIP: zsh.wasm not found (run `nix build .#zsh` or `.#all`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos binary not found: {yos}")
        sys.exit(1)

    libexec = os.path.dirname(zsh_wasm)
    ssh_wasm = os.path.join(libexec, "ssh")
    if not os.path.exists(ssh_wasm):
        print(f"SKIP: ssh wasm not in libexec ({libexec}); "
              "run `nix build .#all` to build the full tool umbrella")
        sys.exit(0)

    env = {
        "PATH": libexec,
        "TERM": os.environ.get("TERM", "xterm-256color"),
        "HOME": os.environ.get("HOME", "/tmp"),
        "USER": os.environ.get("USER", "yos"),
    }

    try:
        r = subprocess.run(
            [yos, zsh_wasm],
            input=b"ssh\n",
            capture_output=True,
            timeout=10,
            env=env,
        )
    except subprocess.TimeoutExpired:
        print("FAIL: `echo ssh | yos zsh` hung (>10s) — fork/exec wedged")
        sys.exit(1)

    combined = r.stdout + r.stderr

    failures = []

    for pat in LEAK_PATTERNS:
        m = pat.search(combined)
        if m:
            failures.append(
                f"  leak symptom {pat.pattern!r} matched at byte {m.start()}: "
                f"{combined[max(0, m.start()-8):m.end()+16]!r}"
            )

    if not USAGE_RE.search(combined):
        failures.append(
            f"  ssh usage banner missing from output "
            f"(stdout={r.stdout[:200]!r}, stderr={r.stderr[:200]!r})"
        )

    if failures:
        print("FAIL: `echo ssh | yos zsh` did not produce a clean ssh usage")
        for f in failures:
            print(f)
        sys.exit(1)

    print("PASS: ssh's usage banner reaches host stdout/stderr cleanly")


if __name__ == "__main__":
    main()
