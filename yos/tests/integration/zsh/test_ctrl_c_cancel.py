"""Pinned regression: Ctrl-C at the zsh prompt kills the shell.

In a real terminal, pressing Ctrl-C while sitting at an interactive
prompt sends SIGINT to the foreground process group. Bash, zsh, and
every other interactive shell catch SIGINT, clear the current input
line, and redraw the prompt — the shell stays alive. Only Ctrl-D (EOF)
is supposed to exit the shell.

Under yos today, Ctrl-C kills the entire zsh process. The user's
interactive session ends; they're back in their host shell. That's
two things going wrong at once:

  - yos's signal-delivery from host pty SIGINT to the wasm guest is
    either bypassing zsh's installed handler entirely (so the default
    SIGINT action — terminate — fires), or
  - zsh's signal handler IS invoked but its longjmp back to the input
    loop hits a wasm-side issue (asyncify? signal-trampoline?) and the
    process exits anyway.

This test drives a zsh in a real pty:
  1. Wait for the first prompt (~3s — zle init takes time).
  2. Send Ctrl-C (\x03).
  3. Wait briefly for the prompt to redraw.
  4. Send `exit\r`.
  5. Assert the process exited 0 (i.e., it was alive enough to receive
     `exit` after the SIGINT).

Currently fails because step 2 kills the shell, so step 4's `exit\r`
goes nowhere and we collect a non-zero status.

xfail until yos's signal/asyncify path is fixed.
"""

import os
import re
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
    # Skip ~/.zshrc / ~/.zlogout sourcing on this test path too —
    # see test_runhookdef_trap.py for the rationale.
    env["HOME"] = "/tmp/yos-zsh-test-no-such-home"
    env["ZDOTDIR"] = "/tmp/yos-zsh-test-no-such-home"

    out, status = run_in_pty(
        [yos, zsh, "-i"],
        rows=24, cols=80,
        driver=[
            (3.0, b""),         # let zsh reach its first prompt
            (0.5, b"\x03"),     # Ctrl-C — should NOT exit the shell
            (1.5, b""),         # give zsh time to redraw the prompt
            (0.5, b"exit\r"),   # only succeeds if shell is still alive
            (2.0, b""),         # wait for clean exit
        ],
        kill_after=2.0,
        env=env,
    )

    text = out.decode("utf-8", errors="replace")
    text = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)  # strip CSI

    if status != 0:
        print(f"FAIL: zsh exit status={status} after SIGINT + exit "
              f"(shell did not survive Ctrl-C)")
        print("--- pty tail (printable, escape-stripped) ---")
        print(text[-800:])
        sys.exit(1)

    # If the shell actually survived SIGINT we expect to see at least
    # two prompts (the original + the post-^C redraw) before `exit`.
    # `nixem%` is the prompt we observed in this build; allow any
    # `<word>%` since hostnames vary.
    prompts = len(re.findall(r"%\s*$", text, re.MULTILINE))
    if prompts < 2:
        print(f"FAIL: only {prompts} prompt(s) seen — Ctrl-C likely killed "
              f"the input loop or the prompt redraw didn't fire")
        print(text[-400:])
        sys.exit(1)

    print("PASS: zsh survives SIGINT at the prompt and exits cleanly")


if __name__ == "__main__":
    main()
