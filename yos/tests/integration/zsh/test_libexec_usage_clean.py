"""Regression: forked-child stderr from libexec wasm tools must NOT
carry the asyncify-rewind / fd-snapshot 0xff byte leak.

Sister test of `test_ssh_no_args_help.py`. That one is the
end-to-end "ssh prints its usage banner" assertion (still xfail —
ssh exits silently before the banner for separate reasons). This
test isolates the BYTE-LEVEL bug that was the user's original
complaint: garbage 0xff bytes prefixed onto the child's stderr,
breaking openssh's `Bad file descriptor` into `ad file descriptor`
on the user's terminal.

Root cause was in `src/yos/impl/proc.c::fork_thread_func`: the
child thread's F_DUPFD loop ran AFTER the parent's wasm code
resumed, so by the time it duplicated `parent_fd_map[i]`, the
parent had closed and reused that host fd number. A pipe-write fd
became a dup of host stdout, and zsh's 8-byte fork-pipe errno
write surfaced on the user's terminal. Fix: dup synchronously in
`yos_fork_pump` (parent thread, before resume), so the snapshot
holds stable host fds.

This test feeds one short line of zsh script over stdin per tool
and asserts:
  - the run terminated within the timeout,
  - the combined stdout+stderr does NOT contain a run of >=4 0xff
    bytes anywhere.
The exact contents of stderr (whether it's a usage banner or a
`No user exists` diagnostic etc.) are NOT asserted here — that's
left to per-tool expected-pass tests if/when those tools work
end-to-end. The point of THIS test is "the byte stream is clean".
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


LEAK_RE = re.compile(rb"\xff{4,}")

# Each tuple: (zsh-input, libexec-tool-required).
TOOLS = [
    ("ssh\n",        "ssh"),
    ("ssh-keygen\n", "ssh-keygen"),
    ("scp\n",        "scp"),
    ("sftp\n",       "sftp"),
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
    env = {
        "PATH": libexec,
        "TERM": os.environ.get("TERM", "xterm-256color"),
        "HOME": os.environ.get("HOME", "/tmp"),
        "USER": os.environ.get("USER", "yos"),
    }

    available = [t for t in TOOLS
                 if os.path.exists(os.path.join(libexec, t[1]))]
    if not available:
        print(f"SKIP: none of {[t[1] for t in TOOLS]} found under {libexec}; "
              "run `nix build .#all`")
        sys.exit(0)

    failures = []
    for code, tool in available:
        try:
            r = subprocess.run(
                [yos, zsh_wasm],
                input=code.encode(),
                capture_output=True,
                timeout=10,
                env=env,
            )
        except subprocess.TimeoutExpired:
            failures.append(f"  {tool}: hung (>10s)")
            continue

        combined = r.stdout + r.stderr
        m = LEAK_RE.search(combined)
        if m:
            failures.append(
                f"  {tool}: 0xff asyncify leak at byte {m.start()}: "
                f"{combined[max(0, m.start()-4):m.end()+12]!r}"
            )

    if failures:
        print(f"FAIL: {len(failures)}/{len(available)} libexec tool(s) "
              f"leaked 0xff bytes under `yos zsh`")
        for f in failures:
            print(f)
        sys.exit(1)

    print(f"PASS: {len(available)} libexec tool(s) — child stderr is byte-clean")


if __name__ == "__main__":
    main()
