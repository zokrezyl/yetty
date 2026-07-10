"""TTY input via blocking read(2) on stdin.

Drives pty_echo.wasm in 'blocking' mode through a pty, sends bytes,
asserts they round-trip back. If THIS fails, the bare read(stdin)
path through yos's bridge can't see pty input — i.e. fd 0 isn't
properly the pty slave inside the wasm guest.

This is the simplest possible failure mode: no kqueue, no libuv,
just read(0, ..., 1) → write(1, ..., 1).
"""

import os
import sys
import termios
import fcntl
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "nvim"))
from run_in_pty import run_in_pty


def _set_pty_raw(fd):
    """Put a pty (master or slave end) into raw mode so bytes flow
    through unaltered — no line buffering, no ECHO, no ISIG. nvim
    does this on its own pty via tcsetattr; for our test the
    program-under-test is a tiny echo helper that doesn't, so we set
    it from the driver side. Both sides share the same termios state."""
    attrs = termios.tcgetattr(fd)
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                  termios.ISTRIP | termios.INLCR | termios.IGNCR |
                  termios.ICRNL | termios.IXON)
    attrs[1] &= ~termios.OPOST
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                  termios.ISIG | termios.IEXTEN)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    wasm = os.path.join(repo, build_dir, "tests", "integration", "pty",
                        "pty_echo.wasm")
    if not os.path.exists(yos):
        print(f"FAIL: yos not found at {yos}")
        return 1
    if not os.path.exists(wasm):
        print(f"FAIL: wasm not found at {wasm}")
        return 1

    out, status = run_in_pty(
        [yos, wasm],
        rows=24, cols=80,
        setup=_set_pty_raw,
        driver=[
            (0.5, b"B"),       # mode selector: blocking
            (0.5, b"a"),
            (0.3, b"b"),
            (0.3, b"c"),
            (0.5, b"q"),       # quit
            (1.0, b""),        # let it exit
        ],
        kill_after=2.0,
    )

    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out[-400:])
        return 1

    # The wasm should have written: READY-blocking, then <a>, <b>, <c>, QUIT.
    # The pty may also echo input back to us in cooked mode, so substring
    # checks instead of exact match.
    needed = [b"READY-blocking", b"<a>", b"<b>", b"<c>", b"QUIT"]
    missing = [n for n in needed if n not in out]
    if missing:
        print(f"FAIL: missing markers: {missing}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out)
        return 1

    print("PASS: blocking pty input round-trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())
