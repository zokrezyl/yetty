"""TTY input via kqueue, pty in COOKED mode (the default).

Same wasm program as test_input_kqueue but the pty stays cooked. The
test driver sends bytes terminated with '\\r' so cooked-mode line
buffering flushes. This is the EXACT combination nvim's TUI hits:
libuv's uv_pipe_open on the TTY fd registers it on kqueue (without
the __APPLE__ uv__stream_try_select workaround, which is not compiled
in for the wasm build), and nvim never puts the pty in raw mode for
its input fd. Test driver sends ':q!\\r' to nvim — same shape as we
send here.

If THIS test fails but test_input_kqueue (raw mode) passes, the bug
is specifically in how host kqueue fires for cooked-mode pty fds.
That is the outstanding nvim no-input symptom.
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

    out, status = run_in_pty(
        [yos, wasm],
        rows=24, cols=80,
        # NO setup= → cooked mode.
        driver=[
            (0.5, b"K\r"),     # selector + CR (cooked mode flushes on \r)
            (0.5, b"a\r"),
            (0.3, b"b\r"),
            (0.3, b"c\r"),
            (0.5, b"q\r"),
            (1.5, b""),
        ],
        kill_after=2.0,
    )

    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out[-400:])
        return 1

    needed = [b"READY-kqueue", b"<a>", b"<b>", b"<c>", b"QUIT"]
    missing = [n for n in needed if n not in out]
    if missing:
        print(f"FAIL: missing markers: {missing}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out)
        return 1

    print("PASS: cooked-mode kqueue pty input round-trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())
