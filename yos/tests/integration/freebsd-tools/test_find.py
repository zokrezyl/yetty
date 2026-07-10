"""find(1): walk filesystem tree.

Status: XFAIL today. fts(3) is now wired in (find links
lib/libc/gen/fts.c via libcExtras and the host opendir/readdir/
closedir bridges live in src/yos/impl/dir.c), so the
`env.fts_open` trap is gone. The remaining gap is the AT_* flag
space — yos_fstatat passes the guest's FreeBSD
AT_SYMLINK_NOFOLLOW (0x200) straight through to host Linux
fstatat which expects 0x100, so the host returns EINVAL and find
aborts with "Invalid argument" before printing any entries.

Same root cause hits `ls -l`. Fix path: translate AT_* flags in
the bridges (yos_fstatat, yos_openat, yos_unlinkat, …).
"""
import os
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-findtest-") as base:
        with open(os.path.join(base, "hit"), "w") as f:
            f.write("")

        r = run(yos, libexec, "find", base, "-name", "hit",
                expect_rc=None, timeout=10)
        if r.returncode == 0 and b"hit" in r.stdout:
            print("PASS: find found 'hit'")
            sys.exit(0)
        if b"unresolved import env.fts_open" in r.stderr:
            print("XFAIL: find needs fts_* bridge family")
            sys.exit(1)
        if b"Invalid argument" in r.stderr:
            print("XFAIL: find: yos_fstatat AT_* flag translation gap")
            sys.exit(1)
        print(f"FAIL: find unexpected: rc={r.returncode}, "
              f"stdout={r.stdout!r}, stderr={r.stderr!r}")
        sys.exit(1)


if __name__ == "__main__":
    main()
