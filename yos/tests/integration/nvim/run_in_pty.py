"""Run a command inside a pty of controlled size, drive it with a script,
collect output, and report exit status.

Used by every nvim integration test in this directory. Wraps `script(1)` so
the recorded log captures everything the inner program wrote (including
ANSI sequences) and includes a timing-friendly read loop.

Usage:
  from run_in_pty import run_in_pty
  out, status = run_in_pty(
      ["./build-linux/src/yos/yos", "build-linux/wasm-pkgs/nvim-0.10.4/out/bin/nvim.wasm"],
      rows=60, cols=200,
      driver=[(2.0, b""), (0.5, b":q!\r"), (3.0, b"")],
  )

`driver` is a list of (wait_seconds, bytes_to_send) — wait, then write. An
empty bytes entry just reads/discards. The function returns the full pty
output and the child process exit status (as returned by waitpid; a SIGKILL
fallback fires if the program is still alive after the script finishes).
"""

import os
import sys
import pty
import fcntl
import termios
import struct
import select
import time
import signal


# Wall-clock timing multiplier. The wasm interpreter is much slower than
# native and on macOS in particular the hard-coded settle waits / quit
# grace below can run short under any kind of host CPU contention
# (parallel meson tests, background activity, etc.). Scale every wait
# uniformly so the same driver script is robust on both platforms.
# Override per-test via env if the default 2x isn't enough.
def _timing_mult():
    # darwin needs 3x — empirically 2x still flakes ~2/3 of test runs
    # because wasm3 interpretation under parallel meson load is slow
    # enough that the (2.5s settle, 0.5s, 3.0s, 2.0s kill_after) sum
    # leaves too little headroom for nvim to draw + receive :q! +
    # exit. Bumping to 3x gives ~25s grace per test, which is
    # comfortably above the worst-case ~15s wasm cold start observed
    # under load. The whole suite still finishes in <2 min.
    try:
        return max(1.0, float(os.environ.get('YOS_PTY_TIMING_MULT',
                                             '3.0' if sys.platform == 'darwin' else '1.0')))
    except ValueError:
        return 1.0


def run_in_pty(argv, rows=24, cols=80, driver=None, kill_after=2.0,
               env=None, setup=None):
    """Run argv in a pty.

    setup: optional callable(fd) invoked AFTER the child is forked and
    AFTER the window size is set, BEFORE any driver entries fire. Use
    it to put the pty into raw mode (cfmakeraw) for tests that send
    single bytes and don't want the kernel's cooked-mode line buffering.
    """
    mult = _timing_mult()
    if driver is None:
        driver = [(3.0, b"")]
    # Scale every settle/quit-grace window by the platform multiplier.
    driver = [(w * mult, p) for (w, p) in driver]
    kill_after = kill_after * mult
    pid, fd = pty.fork()
    if pid == 0:
        if env is not None:
            os.environ.clear()
            os.environ.update(env)
        try:
            os.execvp(argv[0], argv)
        except Exception as e:
            os.write(2, f"exec failed: {e}\n".encode())
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    if setup is not None:
        setup(fd)
    buf = bytearray()
    for wait, payload in driver:
        end = time.time() + wait
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
        if payload:
            try:
                os.write(fd, payload)
            except OSError:
                pass
    end = time.time() + kill_after
    status = None
    while time.time() < end:
        try:
            p, st = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            status = -1
            break
        if p:
            status = st
            break
        rs, _, _ = select.select([fd], [], [], 0.2)
        if rs:
            try:
                d = os.read(fd, 65536)
                if d:
                    buf.extend(d)
            except OSError:
                pass
    if status is None:
        try:
            os.kill(pid, signal.SIGKILL)
            _, status = os.waitpid(pid, 0)
        except ProcessLookupError:
            status = -1
    try:
        os.close(fd)
    except OSError:
        pass
    return bytes(buf), status
