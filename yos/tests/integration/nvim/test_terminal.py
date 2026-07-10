""":terminal inside nvim on desktop yos — the embedded-shell round trip.

Drives the exact user flow: start nvim in a pty, `:terminal`, wait for
the embedded shell's prompt, run a command, watch its output paint in
the terminal buffer, `exit` the shell, `:q!` nvim, expect a clean exit.

Unlike the sibling tests this one drives the pty ADAPTIVELY (wait for a
byte pattern with a deadline, not fixed sleeps): it boots a SECOND
interpreted guest (the embedded zsh) inside the first, and that cold
start ranges from ~4s idle to >90s on a loaded host.

This pins the whole native pty-spawn chain, each link a real regression
fixed in July 2026:
  - env.forkpty (impl/libc/syslog_extras.c): openpty + asyncify-fork +
    login_tty composed in one bridge — was an unresolved import;
  - execve BASENAME resolution (impl/proc/proc.c): the terminal shell
    is $SHELL, typically "/bin/zsh" — a HOST path that passes nvim's
    os_can_exe() pre-check against the real host fs and then is an
    ELF, not wasm; without the guest-$PATH basename walk the child
    died ENOEXEC and the buffer stayed empty;
  - hand-bridged iconv (impl/libc/iconv.c): the codegen passthrough
    passed guest offsets behind iconv()'s char** — host glibc
    SIGSEGV'd on the first typed byte's encoding conversion;
  - empty-path = ENOENT (yos_path_resolve): stat("") reported the cwd,
    so netrw's isdirectory(expand("<amatch>")) hijacked the startup
    buffer into a directory listing that swallowed the session;
  - SIGCHLD for forkpty children (deliver_to_proc no longer drops 20):
    how nvim learns the shell exited.

SHELL is pinned to /bin/zsh in the test env to reproduce the host-path
condition regardless of the runner's own shell.
"""

import os
import pty
import fcntl
import termios
import struct
import select
import signal
import time
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _nvim_path import find_nvim


class PtySession:
    def __init__(self, argv, env, rows=32, cols=100):
        self.buf = b""
        pid, fd = pty.fork()
        if pid == 0:
            os.environ.clear()
            os.environ.update(env)
            try:
                os.execvp(argv[0], argv)
            except Exception as exc:  # noqa: BLE001 — report into the pty
                os.write(2, f"exec failed: {exc}\n".encode())
            os._exit(127)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))

    def pump(self, seconds):
        """Read output for `seconds`; False once the pty hits EOF."""
        end = time.time() + seconds
        alive = True
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.25)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                return False
            if not data:
                return False
            self.buf += data
        return alive

    def wait_for(self, patterns, deadline_s, label):
        """Pump until any of `patterns` appears in the stream."""
        deadline = time.time() + deadline_s
        while time.time() < deadline:
            if any(p in self.buf for p in patterns):
                return True
            if not self.pump(1.0):
                break
        return any(p in self.buf for p in patterns)

    def send(self, data):
        os.write(self.fd, data)

    def finish(self, grace_s):
        """Wait for child exit up to grace_s; SIGKILL stragglers."""
        deadline = time.time() + grace_s
        while time.time() < deadline:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                return status
            self.pump(0.5)
        os.kill(self.pid, signal.SIGKILL)
        _, status = os.waitpid(self.pid, 0)
        return status


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    paths = find_nvim(repo)
    if not paths:
        print("SKIP: nvim wasm not found (run `nix build .#all`)")
        sys.exit(0)
    if not paths["has_runtime"]:
        print("SKIP: nvim runtime tree missing — need `.#all` umbrella")
        sys.exit(0)
    libexec = os.path.dirname(paths["nvim"])
    if not os.path.exists(os.path.join(libexec, "zsh")):
        print("SKIP: no zsh next to nvim in libexec — need `.#all` umbrella")
        sys.exit(0)

    env = {
        "TERM": "xterm-256color",
        # Clean HOME: the user's init.lua/zshrc must not taint the run.
        "HOME": "/tmp/yos-nvim-test-no-such-home",
        # The user-reported shape: $SHELL is a HOST path. nvim execs it;
        # yos must resolve the basename onto the wasm zsh.
        "SHELL": "/bin/zsh",
        # Guest PATH — where the basename resolution looks.
        "PATH": libexec,
    }

    session = PtySession([paths["yos"], paths["nvim"], "--clean"], env)

    failed = []

    def check(name, ok):
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        if not ok:
            failed.append(name)

    # nvim boot, then :terminal. Type the command IMMEDIATELY after
    # entering terminal-mode: the pty input buffer holds it until the
    # embedded shell finishes booting (measured anywhere from ~4s idle
    # to >2min on a loaded host — a prompt-wait would just flake), and
    # the single wait below covers boot + execution + paint at once.
    session.pump(3.0)
    session.send(b":terminal\r")
    check(":terminal opened (term:// statusline)",
          session.wait_for([b"term://"], 60, "term://"))
    check("nvim did not refuse the shell (no E475)", b"E475" not in session.buf)

    session.send(b"i")
    session.pump(1.0)
    session.send(b"echo TERM_OK_$((6*7))\r")
    check("embedded shell ran the command (output painted)",
          session.wait_for([b"TERM_OK_42"], 300, "TERM_OK_42"))

    session.send(b"exit\r")
    session.pump(4.0)
    session.send(b"\r")          # dismiss [Process exited]
    session.pump(1.0)
    session.send(b":q!\r")
    status = session.finish(30)
    check("nvim exited cleanly after :q!",
          os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0)

    if failed:
        sys.stdout.buffer.write(b"--- last 600 bytes ---\n")
        sys.stdout.buffer.write(session.buf[-600:])
        sys.stdout.buffer.write(b"\n")
        sys.exit(1)
    print("PASS: :terminal runs a live shell inside nvim and tears down cleanly")


if __name__ == "__main__":
    main()
