"""kill(1) -l: list signal names.

Status: XFAIL today. The kill binary runs, but every signal name
prints as `(null)`. Root cause: FreeBSD libc's sys_signame[] table
isn't compiled into the wasm tool. Fix path: pull
lib/libc/gen/siglist.c (and the generated siglist table) into the
kill build via libcExtras, the way fts.c is.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "kill", "-l", expect_rc=None, timeout=5)
    out = r.stdout.decode().lower()

    if "hup" in out and "term" in out and "kill" in out:
        print("PASS: kill -l includes HUP/TERM/KILL")
        sys.exit(0)
    if "(null)" in out and out.count("(null)") > 10:
        print("XFAIL: kill -l prints (null) — sys_signame[] not in wasm libc")
        sys.exit(1)
    print(f"FAIL: kill -l unexpected: rc={r.returncode}, "
          f"stdout head: {r.stdout[:200]!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
