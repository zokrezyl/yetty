"""test(1): the POSIX condition tester.

Status: PASS. Pure parsing + scalar ops, no extras. We check the
exit-rc contract (0 for true, 1 for false) which is the entire
purpose of test(1).
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()

    cases = [
        # args, expected rc
        (("1", "=", "1"), 0),
        (("1", "=", "2"), 1),
        (("hello", "!=", "world"), 0),
        (("-z", ""), 0),
        (("-n", "x"), 0),
    ]
    for args, want in cases:
        r = run(yos, libexec, "test", *args, expect_rc=None, timeout=5)
        if r.returncode != want:
            print(f"FAIL: test {' '.join(args)} -> rc={r.returncode}, want {want}")
            print(f"  stderr={r.stderr!r}")
            sys.exit(1)
    print(f"PASS: test ({len(cases)} cases)")
    sys.exit(0)


if __name__ == "__main__":
    main()
