"""nvim launched from an interactive zsh: must not crash on Enter.

This test drives `./tools/yos.sh` (zsh) through a pty, sends
`nvim<Enter>`, lets nvim settle, then sends a second `<Enter>` and
asserts the yos process is still alive.

Why the second Enter matters: user reports that after typing `nvim`
the screen shows just a 1×1 SIXEL probe (nvim asking the terminal
to confirm graphics support) in the upper-left corner, and hitting
Enter at that point CRASHES yos. The pre-crash trace + post-crash
exit status from this test is the single signal that catches that
specific regression.

Trace + raw pty output go to:
  tmp/test_nvim_from_zsh.stderr   (full yos ydebug stream)
  tmp/test_nvim_from_zsh.pty.raw  (raw bytes from pty master)

so a failure can be inspected without re-running.

Pass criteria, in order:
  - nvim renders "NVIM" (full welcome screen — only on a terminal
    emulator that answers DA1/kitty/2026 probes).
  - nvim emits its terminal-cap queries AND the post-Enter yos
    process is still alive (proof-of-life + no crash).

Fail criteria:
  - yos exits with a fatal signal after the second Enter.
  - "no such option: embed" / "Remote address changed" / other
    known regression signatures appear in the pty stream.
"""
import os
import pty
import re
import select
import signal
import sys
import time
from run_tool import get_paths


# Where to dump captured streams for human inspection.
TMP = os.path.join(os.environ.get("YOS_REPO_ROOT", os.getcwd()), "tmp")
PTY_LOG = os.path.join(TMP, "test_nvim_from_zsh.pty.raw")
ERR_LOG = os.path.join(TMP, "test_nvim_from_zsh.stderr")


def main():
    yos, libexec = get_paths()
    zsh_abs  = os.path.join(libexec, "zsh")
    nvim_abs = os.path.join(libexec, "nvim")
    if not os.path.exists(zsh_abs) or not os.path.exists(nvim_abs):
        print("SKIP: zsh/nvim not in libexec")
        sys.exit(0)
    os.makedirs(TMP, exist_ok=True)

    pid, fd = pty.fork()
    if pid == 0:
        os.environ.clear()
        os.environ["ZDOTDIR"] = "/tmp"
        os.environ["PATH"] = libexec
        os.environ["TERM"] = "xterm"
        os.environ["YTRACE_DEFAULT_ON"] = "yes"
        # Per-process trace files: each yos guest's host thread
        # writes ydebug output to tmp/trace_nvim-<comm>-<tid>. Lets
        # the test (or anyone debugging) separate the pid 1 (zsh)
        # stream from pid 2 (nvim TUI) and pid 3 (nvim --embed),
        # which would otherwise interleave line-by-line on stderr.
        os.environ["YTRACE_FILE_PREFIX"] = os.path.join(TMP, "trace_nvim")
        # Redirect yos's stderr (the ydebug stream) to a file too
        # for the lines that fire before YTRACE_FILE_PREFIX kicks in
        # (e.g. tier2 init at startup).
        err = os.open(ERR_LOG, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        os.dup2(err, 2)
        os.close(err)
        os.execvp(yos, [yos, zsh_abs, "-f"])

    pty_log = open(PTY_LOG, "wb")

    def answer_term_queries(chunk):
        """Reply to the terminal-capability probes nvim sends on
        startup. Without these, nvim either hangs forever on the
        DA1 / DECRQM 2026 read or decides the terminal is too dumb
        and exits before our test gets to type the second Enter.
        We answer with the minimal xterm-ish set."""
        reply = b""
        # Primary Device Attributes: nvim sends ESC [ c. Answer "VT220
        # with sixel + selective erase" — generic xterm reply.
        if b"\x1b[c" in chunk:
            reply += b"\x1b[?62;1;2;6;9;22c"
        # DECRQM mode 2026 (synchronised output). Answer "mode supported,
        # currently reset": ESC [ ? 2026 ; 2 $ y.
        if b"\x1b[?2026$p" in chunk:
            reply += b"\x1b[?2026;2$y"
        # Cursor position report (ESC [ 6 n): answer row 1 col 1.
        if b"\x1b[6n" in chunk:
            reply += b"\x1b[1;1R"
        # DECRQSS SGR (ESC P $ q m ESC \): answer "ESC P 1 $ r 0 m ESC \".
        if b"\x1bP$qm\x1b\\" in chunk:
            reply += b"\x1bP1$r0m\x1b\\"
        if reply:
            try:
                os.write(fd, reply)
            except OSError:
                pass

    def drain(seconds):
        out = bytearray(); end = time.time() + seconds
        while time.time() < end:
            rs, _, _ = select.select([fd], [], [], 0.2)
            if not rs:
                continue
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            if not d:
                break
            out.extend(d)
            pty_log.write(d); pty_log.flush()
            answer_term_queries(d)
        return bytes(out)

    def yos_alive():
        try:
            wpid, status = os.waitpid(pid, os.WNOHANG)
            if wpid == 0:
                return True
            # yos exited — record how.
            if os.WIFEXITED(status):
                main.exit_code = os.WEXITSTATUS(status)
                main.exit_signal = 0
            elif os.WIFSIGNALED(status):
                main.exit_code = -1
                main.exit_signal = os.WTERMSIG(status)
            else:
                main.exit_code = -1
                main.exit_signal = -1
            return False
        except ChildProcessError:
            main.exit_code = -1
            main.exit_signal = -1
            return False

    time.sleep(1.5)
    drain(0.5)
    os.write(fd, b"nvim\n")
    out_nvim = drain(3.0)
    nvim_alive_after_typing_nvim = yos_alive()

    os.write(fd, b"\n")
    out_enter = drain(2.0)
    nvim_alive_after_enter = yos_alive()

    # Try to shut down cleanly if still alive.
    if nvim_alive_after_enter:
        os.write(fd, b"\x1b:q!\n")
        drain(0.5)
        os.write(fd, b"exit\n")
        drain(0.3)
    try:
        os.close(fd)
    except OSError:
        pass
    pty_log.close()
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    try:
        os.kill(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass

    all_out = out_nvim + out_enter

    # ── Fail signatures: explicit regressions we recognise. ──────────
    if not nvim_alive_after_typing_nvim:
        sig = getattr(main, "exit_signal", -1)
        print(f"FAIL: yos exited right after `nvim<Enter>` "
              f"(signal={sig}, rc={getattr(main, 'exit_code', -1)}). "
              f"See {ERR_LOG} + {PTY_LOG}.")
        sys.exit(1)
    if not nvim_alive_after_enter:
        sig = getattr(main, "exit_signal", -1)
        print(f"FAIL: yos crashed when nvim received Enter "
              f"(signal={sig}, rc={getattr(main, 'exit_code', -1)}). "
              f"This is the user-reported nvim+enter crash. "
              f"See {ERR_LOG} + {PTY_LOG}.")
        sys.exit(1)
    if b"no such option: embed" in all_out:
        print(f"FAIL: nvim still hits the wrong-binary spawn path — "
              f"sysctl(KERN_PROC_PATHNAME) regression?")
        sys.exit(1)
    if b"mimalloc: assertion failed" in open(ERR_LOG, "rb").read():
        print(f"FAIL: mimalloc assertion fired during nvim startup — "
              f"see {ERR_LOG}.")
        sys.exit(1)

    # ── Pass signatures. ─────────────────────────────────────────────
    if b"NVIM" in all_out or b"VIM " in all_out:
        print(f"PASS: nvim rendered welcome screen and survived Enter")
        sys.exit(0)
    if b"\x1b[c" in all_out or b"\x1b[?2026$p" in all_out:
        print(f"PASS-PARTIAL: nvim reached terminal-cap negotiation "
              f"and survived Enter (welcome screen needs a real "
              f"terminal emulator that answers DA1/2026)")
        sys.exit(0)

    clean = re.sub(rb"\x1b[\[\]][?0-9;]*[a-zA-Z]|\x1b[PX^_].*?\x1b\\",
                   b"", all_out, flags=re.DOTALL).decode(errors="replace")
    print(f"FAIL: nvim output unexpected: {clean[:300]!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
