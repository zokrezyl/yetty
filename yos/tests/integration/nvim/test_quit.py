"""nvim quit-cleanly test: send :q!, check it exits with status 0.

Regression for the EVFILT_PROC|NOTE_EXIT shutdown path: libuv on
__FreeBSD__ uses kqueue PROC events (not SIGCHLD) to detect that the
embedded server child has exited. Without yos's PROC delivery + the
4-byte rwlock fix that this depends on, the parent TUI would either
hang waiting for a kevent that never fires or trap with
out-of-bounds memory access in uv__io_poll.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_in_pty import run_in_pty
from _nvim_path import find_nvim


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    paths = find_nvim(repo)
    if not paths:
        print("SKIP: nvim wasm not found (run `nix build .#all`)")
        sys.exit(0)
    if not paths["has_runtime"]:
        print("SKIP: nvim runtime tree missing — need `.#all` umbrella "
              "(legacy build-linux/wasm-pkgs/nvim only ships the bin)")
        sys.exit(0)
    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    # Force a clean HOME so the user's ~/.config/nvim/init.lua doesn't
    # taint the test (it'd source plugins missing from the wasm guest
    # and crash before :q! ever fires).
    env["HOME"] = "/tmp/yos-nvim-test-no-such-home"
    out, status = run_in_pty(
        [paths["yos"], paths["nvim"], "--clean"],
        rows=24, cols=80,
        driver=[(2.5, b""), (0.5, b":q!\r"), (3.0, b"")],
        kill_after=2.0,
        env=env,
    )
    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- last 400 bytes ---\n")
        sys.stdout.buffer.write(out[-400:])
        sys.exit(1)
    print("PASS: nvim quit cleanly")


if __name__ == "__main__":
    main()
