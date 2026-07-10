"""TTY input via kqueue, AFTER a fork.

Same pattern as test_input_kqueue, but the wasm program forks first
(reaps the child) and then does the kqueue + stdin dance in the
parent. This mirrors nvim's TUI which forks the embedded server
before running its input loop. If kqueue+pty works pre-fork but
breaks post-fork, the bug is in our fork-time kqueue/fd handoff.
"""

import os
import sys
import termios

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "nvim"))
from run_in_pty import run_in_pty


def _set_pty_raw(fd):
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
                        "pty_echo_fork.wasm")
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
            (0.5, b"X"),       # selector byte (ignored — fork variant
                               # always uses kqueue post-fork).
            (0.8, b"a"),
            (0.3, b"b"),
            (0.3, b"c"),
            (0.5, b"q"),
            (1.0, b""),
        ],
        kill_after=2.0,
    )

    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out[-400:])
        return 1

    needed = [b"READY-fork-kqueue", b"<a>", b"<b>", b"<c>", b"QUIT"]
    missing = [n for n in needed if n not in out]
    if missing:
        print(f"FAIL: missing markers: {missing}")
        sys.stdout.buffer.write(b"--- captured ---\n")
        sys.stdout.buffer.write(out)
        return 1

    print("PASS: fork+kqueue pty input round-trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())
