"""Ctrl-C delivery to wasm guest via pty in raw mode.

Drives pty_echo.wasm and sends 0x03 (Ctrl-C). Two scenarios:

  raw: the test puts the pty into raw mode (cfmakeraw) before sending
       Ctrl-C. On a raw pty, ISIG is cleared so the kernel does NOT
       turn 0x03 into SIGINT for the foreground pgrp; the byte arrives
       at the program as a literal 0x03. The wasm program sees 0x03
       and prints "CTRL-C\\n". This is what nvim relies on.

  cooked: pty stays in default cooked mode. ISIG is on; kernel sends
       SIGINT to yos's pgrp. Without explicit handling, yos exits
       (status != 0 with WIFSIGNALED). The test asserts that — it pins
       the CURRENT behaviour so a later fix (yos catching SIGINT and
       forwarding to wasm) shows up as an expected-pass change.

The test runs both scenarios and reports separately.
"""

import os
import sys
import termios

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "nvim"))
import pty
import fcntl
import struct
import select
import time
import signal


def run_with_optional_raw(argv, raw, payload_bytes, env=None):
    """Fork yos under a pty. If raw=True, put the pty in raw mode
    BEFORE sending the payload so kernel doesn't intercept Ctrl-C."""
    pid, fd = pty.fork()
    if pid == 0:
        if env is not None:
            os.environ.clear()
            os.environ.update(env)
        os.execvp(argv[0], argv)
        os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))

    if raw:
        attrs = termios.tcgetattr(fd)
        # cfmakeraw equivalent: clear ICANON, ECHO, ISIG, IEXTEN, etc.
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                      termios.ISTRIP | termios.INLCR | termios.IGNCR |
                      termios.ICRNL | termios.IXON)
        attrs[1] &= ~termios.OPOST
        attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                      termios.ISIG | termios.IEXTEN)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)

    # Read the READY banner first so we know the wasm is in its loop.
    buf = bytearray()
    deadline = time.time() + 3.0
    while time.time() < deadline:
        rs, _, _ = select.select([fd], [], [], 0.2)
        if rs:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            buf.extend(d)
            if b"READY" in buf:
                break

    # Send payload bytes one at a time with brief pauses.
    for b in payload_bytes:
        try:
            os.write(fd, bytes([b]))
        except OSError:
            pass
        time.sleep(0.1)

    # Drain remaining output, wait for exit.
    deadline = time.time() + 3.0
    status = None
    while time.time() < deadline:
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
                buf.extend(os.read(fd, 4096))
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


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    wasm = os.path.join(repo, build_dir, "tests", "integration", "pty",
                        "pty_echo.wasm")
    if not os.path.exists(yos) or not os.path.exists(wasm):
        print(f"FAIL: missing yos or wasm")
        return 1

    fails = []

    # Scenario 1: pty in RAW mode, Ctrl-C arrives as a byte.
    out, status = run_with_optional_raw(
        [yos, wasm], raw=True,
        # Selector + Ctrl-C (program prints "CTRL-C\n" and exits 0).
        payload_bytes=b"B\x03",
    )
    if status != 0 or b"CTRL-C" not in out:
        fails.append(("raw", status, out[-200:]))

    # Scenario 2: pty in COOKED mode (ISIG on). Kernel sends SIGINT.
    # Currently yos has no signal handler that forwards to wasm, so the
    # process is killed by SIGINT (status WIFSIGNALED with sig 2).
    # The test pins the CURRENT broken behaviour so a fix is visible.
    out2, status2 = run_with_optional_raw(
        [yos, wasm], raw=False,
        payload_bytes=b"B\x03",
    )
    # WIFSIGNALED & WTERMSIG: low 7 bits of status = signal number (or
    # status == signal in some runtimes). Either way, status != 0 and
    # CTRL-C marker is absent.
    if status2 == 0 or b"CTRL-C" in out2:
        fails.append(("cooked-unexpected-success", status2, out2[-200:]))

    if fails:
        print("FAIL: ctrl-c scenarios")
        for kind, st, tail in fails:
            print(f"  {kind}: status={st} tail={tail!r}")
        return 1

    print(f"PASS: ctrl-c raw delivers byte; cooked is killed by SIGINT "
          f"(status={status2}) — pinning current behaviour")
    return 0


if __name__ == "__main__":
    sys.exit(main())
