"""zsh interactive prompt — pins the runhookdef indirect-call-type
mismatch trap that fires the moment zsh tries to display the first
interactive prompt under yos.

The crashing call chain (from the wasm trap backtrace):

    main → zsh_main → loop → parse_event → zshlex → ihgetc → ingetc
    → zleentry → zle_main_entry → zleread → zlecore → execzlefunc
    → runhookdef                       ← INDIRECT CALL TYPE MISMATCH

What's happening at the wasm level: zle (line editor) registers
hooks via function-pointer-into-the-wasm-table; one of the
registered entries' signature doesn't match the type expected at
the call_indirect site that fires from runhookdef. wasm3's runtime
type check refuses to dispatch and traps.

Hypothesis (not yet confirmed):
    Our zsh build had `--disable-zle` ignored ("WARNING: unrecognized
    options: --disable-zle") so zle compiled in. The hook table is
    populated with default callbacks whose signatures came from the
    cross-compile path; some signature got widened/narrowed by the
    sysroot/typedef shape (32-bit pointers, FreeBSD-i386 alignment),
    while the call site in execzlefunc still expects the canonical
    signature.

This test:
    1. Spawns zsh in a real pty so it follows the interactive path
       (which is what triggers zle / runhookdef).
    2. Reads the prompt for ~2 seconds.
    3. Sends `exit\\n`.
    4. Asserts the process exited 0 and no `[trap]` text appears in
       the captured pty output.

Currently EXPECTED TO FAIL until the runhookdef bug is isolated and
fixed (either in the recipe — actually disabling zle, dropping the
modules list, etc. — or in yos's bridge surface).
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_in_pty import run_in_pty
from _zsh_path import find_zsh_wasm


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh = find_zsh_wasm(repo)
    if not zsh:
        print("SKIP: zsh.wasm not found (run `nix build .#zsh`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos binary not found: {yos}")
        sys.exit(1)

    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    # Force HOME at a non-existent path so zsh skips `.zshrc`/`.zlogout`
    # sourcing and history-save on exit — those paths take seconds under
    # yos (file I/O through bridges) and would push this test past its
    # kill_after timeout. The trap we're pinning fires during zle init,
    # not during rc-file processing, so skipping rcs is safe here.
    env["HOME"] = "/tmp/yos-zsh-test-no-such-home"
    env["ZDOTDIR"] = "/tmp/yos-zsh-test-no-such-home"

    out, status = run_in_pty(
        # `-i` forces interactive even with stdin not a tty (belt and
        # braces: the pty.fork already gives us a tty).
        [yos, zsh, "-i"],
        rows=24, cols=80,
        driver=[
            (3.0, b""),                  # let zsh reach its first prompt
            (0.5, b"exit\r"),            # ask it to leave cleanly
            (3.0, b""),                  # wait for the exit
        ],
        kill_after=2.0,
        env=env,
    )

    text = out.decode("utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)  # strip CSI

    failed = False
    if "[trap]" in text or "indirect call type mismatch" in text:
        print("FAIL: zsh trapped during interactive startup")
        print("--- pty tail (printable, escape-stripped) ---")
        print(text[-1000:])
        failed = True

    if status != 0:
        print(f"FAIL: zsh exit status={status} (expected 0)")
        failed = True

    if failed:
        sys.exit(1)
    print("PASS: zsh interactive prompt + exit cleanly")


if __name__ == "__main__":
    main()
