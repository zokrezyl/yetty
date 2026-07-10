"""tr(1): translate characters.

Status: XFAIL today. Traps with `unresolved import env.mergesort`.
tr's cset.c (character set builder) sorts internally with mergesort
which isn't a glibc-canonical name (it's BSD libc's qsort variant).
Bridge gap — fixable by binding env.mergesort to the host's `qsort`
(BSD-shape and stable enough for tr's use).
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "tr", "a-z", "A-Z",
            stdin=b"hello\n", expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout == b"HELLO\n":
        print("PASS: tr a-z A-Z")
        sys.exit(0)
    if b"unresolved import env.mergesort" in r.stderr:
        print("XFAIL: tr needs env.mergesort bridge")
        sys.exit(1)
    print(f"FAIL: tr unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
