"""Pinned regression: INTERACTIVE `yos zsh` running ssh must not
crash zsh with a TTY EBADF / 0xff garbage cascade.

This is the test the user's report actually requires. The sibling
`test_external_tty_survival.py` uses `PATH=/bin:/usr/bin`, so when
it types `ssh<Enter>` zsh just says "command not found" — it never
actually exec'd the wasm ssh, so it never exercised the fork+exec
fd path that breaks the TTY. That test passing for `ssh` is a
false positive.

Here we spawn `yos zsh -i` in a PTY with `PATH=$libexec` so the
wasm ssh IS found and exec'd, then assert:

  1. The output does NOT contain the leak signature
     - run of >=4 0xff bytes anywhere in the byte stream
     - the truncated string "ad file descriptor" (the visible
       form of '\\xff\\xff…\\xff\\xff' + "Bad file descriptor"
       where 0xff bytes are unrenderable, eating the 'B')
     - "error on TTY read" (zsh's own death cry when its
       controlling TTY fd has gone EBADF after the exec'd child
       trampled the host fd table)
  2. The shell SURVIVES: a marker echo after the ssh invocation
     comes back on the PTY.

Marked xfail until the interactive code path is fixed.
"""

import os
import pty
import fcntl
import termios
import struct
import select
import time
import signal
import sys
import re

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


LEAK_RE      = re.compile(rb"\xff{4,}")
TRUNC_BAD_RE = re.compile(rb"\bad file descriptor\b")
TTY_DEATH_RE = re.compile(rb"error on TTY read")


def spawn_zsh_pty(yos: str, zsh_wasm: str, libexec: str,
                  rows=24, cols=80):
    # Capture HOME/USER BEFORE clearing — zsh needs HOME to find
    # .zshrc (the user's antidote setup), and the bug only fires
    # when those code paths are loaded.
    real_home = os.environ.get("HOME", "/tmp")
    real_user = os.environ.get("USER", "yos")
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.clear()
        os.environ["TERM"]   = "xterm-256color"
        os.environ["HOME"]   = real_home
        os.environ["USER"]   = real_user
        # Crucial: point PATH at the libexec dir that contains the
        # wasm ssh binary, so zsh actually finds it and forks+execs
        # it. Without this, typing `ssh` makes zsh just emit
        # "command not found" — no fork, no exec, bug not triggered.
        os.environ["PATH"]   = libexec
        # CRUCIAL: do NOT pass `-i`. The user invokes
        # `./tools/yos.sh zsh` which runs `yos $libexec/zsh` (no
        # -i). zsh detects interactivity from TTY but the fd setup
        # path it takes differs from explicit `-i`, and the bug
        # only fires in this no-explicit-i path.
        os.execvp(yos, [yos, zsh_wasm])
    fcntl.ioctl(fd, termios.TIOCSWINSZ,
                struct.pack("HHHH", rows, cols, 0, 0))
    return pid, fd


def drain_until(fd, needle: bytes, timeout: float, buf: bytearray):
    end = time.time() + timeout
    while time.time() < end:
        rs, _, _ = select.select([fd], [], [], 0.2)
        if not rs:
            continue
        try:
            d = os.read(fd, 65536)
        except OSError:
            break
        if not d:
            break
        buf.extend(d)
        if needle in buf:
            return True
    return False


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh_wasm = find_zsh_wasm(repo)
    if not zsh_wasm:
        print("SKIP: zsh.wasm not found (run `nix build .#all`)")
        sys.exit(0)
    libexec = os.path.dirname(zsh_wasm)
    if not os.path.exists(os.path.join(libexec, "ssh")):
        print(f"SKIP: ssh wasm not in {libexec} (run `nix build .#all`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos not found: {yos}")
        sys.exit(1)

    pid, fd = spawn_zsh_pty(yos, zsh_wasm, libexec)
    buf = bytearray()
    try:
        # Wait for first prompt.
        if not drain_until(fd, b"%", timeout=8.0, buf=buf):
            print(f"FAIL: zsh prompt never appeared. got={bytes(buf)!r}")
            sys.exit(1)
        # Type `ssh<Enter>`.
        os.write(fd, b"ssh\n")
        # Give ssh time to exec + write its usage / fail / etc.
        time.sleep(2.0)
        # Marker — if shell survived, this should echo back.
        marker = b"YOS_SURVIVOR_42"
        os.write(fd, b"echo " + marker + b"\n")
        survived = drain_until(fd, marker, timeout=5.0, buf=buf)
    finally:
        try:
            os.write(fd, b"\nexit\n")
        except OSError:
            pass
        time.sleep(0.3)
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(pid, 0)
        except ChildProcessError:
            pass
        try:
            os.close(fd)
        except OSError:
            pass

    failures = []
    m = LEAK_RE.search(bytes(buf))
    if m:
        failures.append(f"  0xff leak at byte {m.start()}: "
                        f"{bytes(buf)[max(0, m.start()-4):m.end()+16]!r}")
    m = TRUNC_BAD_RE.search(bytes(buf))
    if m:
        failures.append(f"  truncated 'Bad file descriptor' (visible as "
                        f"'ad file descriptor') at byte {m.start()}: "
                        f"{bytes(buf)[max(0, m.start()-4):m.end()+8]!r}")
    if TTY_DEATH_RE.search(bytes(buf)):
        failures.append(f"  zsh died with 'error on TTY read' "
                        f"after ssh — its controlling tty fd went EBADF")
    if not survived:
        failures.append(f"  shell did not survive ssh invocation "
                        f"(marker never came back)")

    if failures:
        print("FAIL: interactive `yos zsh` -> ssh leaks/crashes the shell")
        for f in failures:
            print(f)
        print(f"  full buffer ({len(buf)} bytes): {bytes(buf)[:400]!r}...")
        sys.exit(1)

    print("PASS: interactive `yos zsh` -> ssh leaves the shell intact")


if __name__ == "__main__":
    main()
