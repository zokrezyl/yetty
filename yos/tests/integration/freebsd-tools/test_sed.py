"""sed(1): stream editor.

Status: XFAIL today. Aborts with `mimalloc: error: corrupted free
list entry`. sed's compile.c does heavy realloc/free of regex
intermediates; our impl/freebsd_userland.c provides asprintf via
yos_malloc but the malloc/realloc/free path the guest's libc
follows must be drifting from the mimalloc bookkeeping. Likely
yos_malloc/yos_free pairing isn't symmetric for the realloc case.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "sed", "s/foo/BAR/g",
            stdin=b"foo bar foo\n", expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout == b"BAR bar BAR\n":
        print("PASS: sed s/foo/BAR/g")
        sys.exit(0)
    if b"mimalloc: error" in r.stderr:
        print("XFAIL: sed trips mimalloc free-list corruption")
        sys.exit(1)
    print(f"FAIL: sed unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
