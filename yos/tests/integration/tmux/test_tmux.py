"""tmux(1): the terminal multiplexer running on the yos libc surface.

This is the end-to-end regression guard for the chain of fixes that
took tmux from "does nothing" to actually creating sessions and panes:

  * errno was read before the host call set it (unspecified arg-eval
    order in the yos_errno_check macro) — tmux's connect()/ENOENT probe
    misclassified the error and never started a server.
  * the sigaction bridge wrote 32 bytes into the guest's 24-byte
    `struct sigaction` oldact, overrunning it and corrupting the heap
    free-list header (whole guest heap lost).
  * sendmsg/recvmsg had no real bridge — the auto-generated one passed
    the wasm fd straight to the host and cast the guest msghdr as a host
    one; tmux's imsg client/server transport needs fd + iovec + SCM_RIGHTS
    translation.
  * the glob bridge turned a NULL errfunc into ctx->memory and host glob
    called it → SIGSEGV while tmux globbed its config paths.
  * the guest C-locale rune table (_DefaultRuneLocale) was never linked,
    so isalnum/isdigit returned 0 and tmux's arg parser rejected every
    flag ("invalid flag -N") and fatal'd in key_bindings_init.

What this verifies: a single `new-session -d` succeeds (rc 0), and the
server's own debug log shows it parsed the default key bindings, created
a session, and spawned the pane — with no fatal/parse error. (Under the
native thread-fork model the detached server does not outlive the
spawning process, so this drives the full server bring-up in one
invocation rather than re-attaching from a second one.)
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _tmux_path import find_tmux_wasm


def _deoctal(text: str) -> str:
    # tmux's compat/vis.c octal-escapes every log line (\NNN).
    return re.sub(r"\\([0-7]{3})", lambda m: chr(int(m.group(1), 8)), text)


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.environ.get("YOS_BIN") or os.path.join(
        repo, build_dir, "src", "yos", "yos")
    tmux = find_tmux_wasm(repo)

    if not tmux:
        print("SKIP: tmux.wasm not found (run `nix build .#tmux` or `.#all`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"SKIP: yos binary not found: {yos}")
        sys.exit(0)

    with tempfile.TemporaryDirectory() as workdir:
        sock = os.path.join(workdir, "tmux.sock")
        # -vv makes the server write tmux-server-<pid>.log into cwd.
        env = dict(os.environ)
        env.pop("TMUX", None)  # don't let an outer tmux confuse the guest
        r = subprocess.run(
            [yos, tmux, "-vv", "-S", sock, "new-session", "-d", "sleep 1"],
            cwd=workdir, capture_output=True, timeout=30, env=env,
        )
        if r.returncode != 0:
            print(f"FAIL: new-session rc={r.returncode}")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)

        logs = [f for f in os.listdir(workdir) if f.startswith("tmux-server-")]
        if not logs:
            print("FAIL: no tmux-server log produced (server never started)")
            sys.exit(1)
        raw = open(os.path.join(workdir, logs[0]), "rb").read().decode("latin1")
        log = _deoctal(raw)

        for bad in ("fatal:", "invalid flag", "unknown flag"):
            if bad in log:
                line = next((l for l in log.splitlines() if bad in l), bad)
                print(f"FAIL: server log contains {bad!r}: {line!r}")
                sys.exit(1)

        for needle in ("new session", "spawn_pane", "spawn_window"):
            if needle not in log:
                print(f"FAIL: server log missing {needle!r} "
                      "(session/pane not created)")
                sys.exit(1)

    print("PASS: tmux new-session created a session + pane on the yos surface")
    sys.exit(0)


if __name__ == "__main__":
    main()
