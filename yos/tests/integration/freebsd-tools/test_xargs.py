"""xargs(1): build command lines from stdin and run them.

Status: XFAIL today. Traps with `internal error: no free pid slot`
— xargs's first call after parsing args is `pipe()` + `fork()` +
`execvp()`. yos's proc table runs out of slots. Ports that fork
need a deeper look at how the proc table is sized + recycled in
the wasm runtime.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "xargs", "-n", "1", "echo",
            stdin=b"one\ntwo\n", expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout == b"one\ntwo\n":
        print("PASS: xargs -n 1 echo")
        sys.exit(0)
    if b"no free pid slot" in r.stderr or b"unresolved import" in r.stderr:
        print(f"XFAIL: xargs proc/exec gap "
              f"(rc={r.returncode}, stderr={r.stderr!r})")
        sys.exit(1)
    print(f"FAIL: xargs unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
