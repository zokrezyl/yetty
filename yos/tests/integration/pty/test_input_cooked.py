"""TTY input in COOKED mode (the default), terminated with CR.

The pty stays in cooked mode (line-buffered, ECHO, ISIG on); the
test driver sends bytes followed by '\\r'. The kernel buffers each
line and delivers it on the carriage return — bytes should arrive at
the wasm program one chunk per line. If THIS fails but the raw-mode
variants pass, the bug is around how cooked-mode line buffering /
delivery interacts with our kqueue+TTY bridge.

This is the mode nvim's pty is in when the test driver sends ':q!\\r' —
nvim doesn't put its own pty in raw mode for the input fd; libuv on
non-windows treats fd 0 as a UV_NAMED_PIPE through uv_pipe_open
rather than calling uv_tty_set_mode (see nvim/event/stream.c
stream_init: only the MSWIN branch uses uv_tty_init for input).
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "nvim"))
from run_in_pty import run_in_pty


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

    # Cooked mode buffers until \r. Send each line terminated. The wasm
    # echo program reads one byte at a time and echoes <c>. With CR-
    # terminated lines, the kernel will deliver them in chunks.
    out, status = run_in_pty(
        [yos, wasm],
        rows=24, cols=80,
        # NO setup= → pty stays in cooked mode.
        driver=[
            (0.5, b"B\r"),     # selector + CR to flush
            (0.5, b"a\r"),
            (0.3, b"b\r"),
            (0.3, b"c\r"),
            (0.5, b"q\r"),
            (1.0, b""),
        ],
        kill_after=2.0,
    )

    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out[-400:])
        return 1

    needed = [b"READY-blocking", b"<a>", b"<b>", b"<c>", b"QUIT"]
    missing = [n for n in needed if n not in out]
    if missing:
        print(f"FAIL: missing markers: {missing}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out)
        return 1

    print("PASS: cooked-mode pty input round-trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())
