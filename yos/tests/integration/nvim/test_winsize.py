"""nvim terminal-size test: set the pty to a non-default size, then ask
nvim what columns/lines it sees and check the response matches.

Regression for the TIOCGWINSZ path: parent TUI calls
uv_tty_get_winsize(out_handle) which calls ioctl(fd, TIOCGWINSZ, &ws),
then forwards (cols, rows) to the embedded server via
nvim_ui_try_resize. We test the round trip: set pty to ROWS x COLS,
ask nvim ":set columns? lines?", confirm both values.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_in_pty import run_in_pty
from _nvim_path import find_nvim

ROWS, COLS = 60, 200


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    paths = find_nvim(repo)
    if not paths:
        print("SKIP: nvim wasm not found (run `nix build .#all`)")
        sys.exit(0)
    if not paths["has_runtime"]:
        print("SKIP: nvim runtime tree missing — need `.#all` umbrella")
        sys.exit(0)
    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["HOME"] = "/tmp/yos-nvim-test-no-such-home"
    out, status = run_in_pty(
        [paths["yos"], paths["nvim"], "--clean"],
        rows=ROWS, cols=COLS,
        driver=[
            (3.0, b""),
            (0.3, b":set columns? lines?\r"),
            (2.0, b""),
            (0.3, b":q!\r"),
            (2.0, b""),
        ],
        kill_after=2.0,
        env=env,
    )
    text = out.decode("utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)  # strip CSI
    text = re.sub(r"\x1b\][^\x07]*\x07", "", text)      # strip OSC
    text = re.sub(r"\x1b[()=>]", "", text)              # misc esc
    cm = re.search(r"columns=(\d+)", text)
    lm = re.search(r"lines=(\d+)", text)
    if not cm or not lm:
        print("FAIL: didn't find columns=/lines= in output")
        sys.stdout.buffer.write(b"--- last 600 bytes (stripped) ---\n")
        sys.stdout.buffer.write(text[-600:].encode())
        sys.exit(1)
    got_cols, got_lines = int(cm.group(1)), int(lm.group(1))
    if got_cols != COLS or got_lines != ROWS:
        print(f"FAIL: expected {COLS}x{ROWS}, got columns={got_cols} lines={got_lines}")
        sys.exit(1)
    if status != 0:
        print(f"FAIL: nvim exit status={status}")
        sys.exit(1)
    print(f"PASS: nvim sees {got_cols}x{got_lines}")


if __name__ == "__main__":
    main()
