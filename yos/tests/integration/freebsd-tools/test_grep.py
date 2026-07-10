"""grep(1): pattern matching.

Status: XFAIL today. Empty output / silent exit. With strdup now
bridged via impl/freebsd_userland.c grep gets past its arg-parse
phase, but pattern matching uses regex.h and ctype macros — same
rune-locale gap as awk's lexer hits. Regex character class lookups
return 0 for every byte, so no line ever matches.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "grep", "apple",
            stdin=b"apple\nbanana\napple\n",
            expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout.count(b"apple\n") == 2:
        print("PASS: grep apple")
        sys.exit(0)
    if r.stdout == b"":
        print(f"XFAIL: grep matched nothing "
              f"(rc={r.returncode}, stderr={r.stderr!r})")
        sys.exit(1)
    print(f"FAIL: grep unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
