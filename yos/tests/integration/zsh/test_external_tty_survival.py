"""Each external wasm command runs from zsh-under-yos, AND zsh stays
usable afterwards. Catches a whole class of yos infrastructure bugs:

  - Forked child's fd_map close-cleanup that takes the parent's TTY
    down with it.
  - Asyncify-state leak emitting 0xff garbage on the first stderr
    write of an exec'd child.
  - tcsetpgrp / TTY-ownership flips that leave zsh unable to read
    from the controlling terminal after the child exits.

Failure mode this test is shaped against:
    zsh% <cmd>          ← user types
    [garbage bytes]ad file descriptor   ← something went wrong
    zsh: error on TTY read: Bad file descriptor    ← shell dies
    [shell exits, prompt never returns]

The test runs an interactive zsh under yos in a PTY, types each
candidate command, then types `echo MARKER_<n>` and checks that the
marker echoes back. If the shell died (or its TTY went bad), the
marker never appears.

Failing commands are marked xfail so the bar stays high without
red-blocking unrelated work. Promote one to expect-pass after the
underlying yos fix lands.
"""

import os
import select
import pty
import sys
import time
import signal
import termios
import fcntl
import struct


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


# (command-to-run, allowed-to-fail). The allowed-to-fail set is
# keyed on the BUG, not on whether the command itself is useful:
# we expect zsh to survive every entry; if it doesn't, we mark
# it xfail with a one-line reason until the underlying yos issue
# is fixed.
CASES = [
    ("echo hello",          False, ""),
    ("ls /",                False, ""),
    ("true",                False, ""),
    ("false",               False, ""),
    ("pwd",                 False, ""),
    # ssh: in this PTY harness ssh exits cleanly and zsh survives.
    # The interactive failure (user types `ssh` and zsh's tty goes
    # to EBADF on the next read) is exposed by the libc-level test
    # `test_post_fork_stderr_clean` instead — same root cause:
    # fcntl(F_DUPFD) result is invalidated by a concurrent close
    # somewhere along the fork+exec path. Keep ssh here as a
    # smoke-test of "tool doesn't kill the shell" even though it
    # doesn't repro the EBADF in this harness.
    ("ssh",                 False, ""),
]


def spawn_zsh_pty(yos: str, zsh_wasm: str, rows=24, cols=80):
    pid, fd = pty.fork()
    if pid == 0:
        # Pristine env; zsh's job control wants TERM at minimum.
        os.environ.clear()
        os.environ["TERM"]   = "xterm-256color"
        os.environ["HOME"]   = os.environ.get("HOME", "/tmp")
        os.environ["USER"]   = os.environ.get("USER", "yos")
        os.environ["PATH"]   = "/bin:/usr/bin"
        os.execvp(yos, [yos, zsh_wasm, "-i"])
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


def run_case(yos: str, zsh_wasm: str, cmd: str, marker: str) -> bool:
    """Returns True if the shell SURVIVED running `cmd` (marker
    came back on stdout), False if it didn't."""
    pid, fd = spawn_zsh_pty(yos, zsh_wasm)
    buf = bytearray()
    # Wait for first prompt.
    drain_until(fd, b"%", timeout=8.0, buf=buf)
    # Type the command + Enter.
    os.write(fd, cmd.encode() + b"\n")
    # Give the command time to fail/finish.
    time.sleep(2.0)
    # Send the survivor marker.
    os.write(fd, f"echo {marker}\n".encode())
    survived = drain_until(fd, marker.encode(),
                           timeout=5.0, buf=buf)
    # Tear down.
    try:
        os.write(fd, b"\nexit\n")
    except OSError:
        pass
    time.sleep(0.5)
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
    return survived


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh_wasm = find_zsh_wasm(repo)
    if not zsh_wasm:
        print("SKIP: zsh.wasm not found (run `nix build .#zsh`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos not found: {yos}")
        sys.exit(1)

    failures = []
    unexpected_passes = []

    for n, (cmd, xfail, reason) in enumerate(CASES):
        marker = f"YOS_SURVIVOR_{n}"
        try:
            survived = run_case(yos, zsh_wasm, cmd, marker)
        except Exception as e:
            print(f"ERROR  {cmd!r}: {e}")
            failures.append((cmd, xfail, str(e)))
            continue
        if survived and not xfail:
            print(f"PASS   {cmd!r}")
        elif survived and xfail:
            print(f"XPASS  {cmd!r}  (was xfail: {reason}) — "
                  "promote to expected-pass")
            unexpected_passes.append((cmd, reason))
        elif not survived and xfail:
            print(f"XFAIL  {cmd!r}  ({reason})")
        else:
            print(f"FAIL   {cmd!r}  — shell did NOT survive")
            failures.append((cmd, xfail, "shell-died"))

    if failures:
        print(f"\n{len(failures)} unexpected failure(s)")
        sys.exit(1)
    if unexpected_passes:
        # XPASS means a previously-broken case now works. Surface it
        # but don't fail the whole suite — the user wants progress
        # signal, not a wedge.
        print(f"\n{len(unexpected_passes)} unexpected pass(es) — "
              "edit CASES to promote")
    print("OK")
    sys.exit(0)


if __name__ == "__main__":
    main()
