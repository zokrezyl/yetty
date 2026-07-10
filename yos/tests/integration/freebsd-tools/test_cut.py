"""cut(1): extract fields/columns.

Status: XFAIL today. Empty stdout for any real input. Same root
cause as test_awk.py — the wasm guest's _CurrentRuneLocale deref
returns garbage so isspace/isalpha return 0, which makes cut's
delimiter parsing path treat every byte as not-a-delimiter and
print nothing.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "cut", "-d:", "-f2",
            stdin=b"a:b:c\nd:e:f\n", expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout == b"b\ne\n":
        print("PASS: cut -d: -f2")
        sys.exit(0)
    # cut outputs a bare newline per input line because the byte at
    # the field start is treated as a delimiter (rune-locale bug
    # again). Mark this signature as the documented xfail.
    if (r.stdout in (b"", b"\n\n") or b"unresolved import" in r.stderr
        or b"trapped" in r.stderr):
        print(f"XFAIL: cut produces empty output / traps "
              f"(rc={r.returncode}, stdout={r.stdout!r}, "
              f"stderr={r.stderr!r})")
        sys.exit(1)
    print(f"FAIL: cut unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
