"""nvim user-input via pty: typing 'iHELLO<Esc>:q!<CR>' should
insert text and quit cleanly.

This pins the user-reported "nvim runs but no text input" symptom
on darwin. The pty-level reproducers under tests/integration/pty/
all pass — the bridge handles kqueue+EVFILT_READ on a pty slave
fine, both in raw and cooked mode, single-process or post-fork.
The nvim-specific failure must therefore live in a more complex
interaction (libuv loop with many handles, the TUI's read pipeline,
something around the fd inheritance via the asyncify-fork boundary)
that the isolated tests don't replicate.

Currently EXPECTED to fail on darwin until the upstream bug is
isolated and fixed. The test fails by SIGKILL (exit status 9) after
nvim's TUI ignores the input and the test driver times out.
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
        return 0
    if not paths["has_runtime"]:
        print("SKIP: nvim runtime tree missing — need `.#all` umbrella")
        return 0

    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["HOME"] = "/tmp/yos-nvim-test-no-such-home"

    # Drive: wait for startup, type 'iHELLO<Esc>:q!<CR>'. With a
    # working input pipeline nvim enters insert, types HELLO, exits
    # insert, runs :q!, and the embedded server exits 0.
    out, status = run_in_pty(
        [paths["yos"], paths["nvim"], "-u", "NONE", "-i", "NONE",
         "--noplugin"],
        rows=24, cols=80,
        driver=[
            (3.0, b""),
            (0.3, b"i"),
            (0.3, b"HELLO"),
            (0.3, b"\x1b"),     # Esc
            (0.3, b":q!\r"),
            (3.0, b""),
        ],
        kill_after=2.0,
        env=env,
    )

    if status != 0:
        print(f"FAIL: expected exit 0, got status={status}")
        sys.stdout.buffer.write(b"--- last 400 bytes ---\n")
        sys.stdout.buffer.write(out[-400:])
        return 1

    # Sanity: the typed text should appear in the captured output if
    # nvim received our keystrokes (it echoes them in insert mode).
    if b"HELLO" not in out:
        print("FAIL: typed 'HELLO' never appeared in pty output")
        sys.stdout.buffer.write(out[-400:])
        return 1

    print("PASS: nvim accepted input and quit cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
