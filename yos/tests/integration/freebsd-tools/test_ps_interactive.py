"""ps(1) under an interactive zsh: child output must flush at exit.

This test exists because the user hit exactly this regression:
inside an interactive `./tools/yos.sh` shell, running `ps`
produced *no output* before the prompt returned. The output was
sitting in host glibc's stdout buffer (impl/file.c aliases the
guest's FILE *2 to the host's stdout FILE *) and only reached the
terminal when the next command flushed.

Root cause: yos_exit() in src/yos/impl/proc.c didn't fflush(NULL)
before pthread_exit in the forked-child branch. The fix is one
line; this test guards against regression.

We drive zsh through a real pty and look for ps's header in the
output BETWEEN typing `ps\n` and typing any subsequent command.
"""
import os
import pty
import re
import select
import sys
import time
from run_tool import get_paths


def main():
    yos, libexec = get_paths()
    ps_abs = os.path.join(libexec, "ps")
    zsh_abs = os.path.join(libexec, "zsh")
    if not os.path.exists(ps_abs) or not os.path.exists(zsh_abs):
        print(f"SKIP: ps/zsh not in libexec")
        sys.exit(0)

    # Use ZDOTDIR=/tmp + zsh -f so the user's ~/.zshenv/.zshrc don't
    # influence PATH or aliases. Also drop YTRACE_DEFAULT_ON to keep
    # stderr quiet.
    env = dict(os.environ)
    env["ZDOTDIR"] = "/tmp"
    env.pop("YTRACE_DEFAULT_ON", None)
    env["PATH"] = libexec

    pid, fd = pty.fork()
    if pid == 0:
        os.environ.clear()
        os.environ.update(env)
        os.execvp(yos, [yos, zsh_abs, "-f"])

    def drain(seconds):
        end = time.time() + seconds
        buf = bytearray()
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
            buf.extend(d)
        return bytes(buf)

    # Let zsh settle, then issue ps and wait for output BEFORE issuing
    # anything else.
    time.sleep(1.5)
    drain(0.5)
    os.write(fd, ps_abs.encode() + b"\n")
    out = drain(2.5)
    os.write(fd, b"exit\n")
    drain(0.5)
    os.close(fd)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass

    clean = re.sub(rb"\x1b\[[?0-9;]*[a-zA-Z]", b"", out).decode(errors="replace")

    if "PID" not in clean or "COMMAND" not in clean:
        print(f"FAIL: ps header missing — child stdio not flushed at exit?")
        print(f"  output: {clean!r}")
        sys.exit(1)
    if " 1 " not in clean and " 1\t" not in clean:
        print(f"FAIL: ps did not report PID 1 (zsh) — output: {clean!r}")
        sys.exit(1)
    # The forked-and-exec'd ps process must appear with COMMAND='ps',
    # not the parent's name. yos_execve updates yos_proc->comm so
    # /proc/<pid>/stat picks up the new program's basename — guard
    # the regression where this wasn't wired.
    lines = clean.splitlines()
    ps_row = None
    for ln in lines:
        cols = ln.split(None, 3)
        if len(cols) >= 4 and cols[0].isdigit() and cols[3].strip() == "ps":
            ps_row = ln
            break
    if not ps_row:
        print(f"FAIL: no ps row with COMMAND='ps' — execve did not update "
              f"comm. output: {clean!r}")
        sys.exit(1)
    print(f"PASS: interactive ps shows itself as 'ps' ({ps_row.strip()!r})")
    sys.exit(0)


if __name__ == "__main__":
    main()
