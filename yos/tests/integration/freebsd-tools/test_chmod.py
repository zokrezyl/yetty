"""chmod(1): change file permissions.

Status: XFAIL today. chmod calls setmode(3) to parse the symbolic
mode string ('+x', 'u=rw', etc.). setmode(3) isn't bridged — yos
traps with `env.setmode unresolved import` as soon as chmod tries
to parse anything beyond the simplest octal modes.

Fix path: pull lib/libc/gen/setmode.c into the chmod build via
libcExtras (same trick as fts/qsort), then this test should flip
to PASS.
"""
import os
import stat
import sys
import tempfile
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    with tempfile.TemporaryDirectory(prefix="yos-chmodtest-") as base:
        target = os.path.join(base, "f")
        with open(target, "w") as f:
            f.write("x")
        os.chmod(target, 0o644)

        r = run(yos, libexec, "chmod", "600", target,
                expect_rc=None, timeout=5)
        if r.returncode == 0:
            mode = stat.S_IMODE(os.stat(target).st_mode)
            if mode == 0o600:
                print(f"PASS: chmod 600 → 0o{mode:o}")
                sys.exit(0)
            print(f"FAIL: chmod 600 returned rc=0 but mode is 0o{mode:o}")
            sys.exit(1)
        if b"unresolved import env.setmode" in r.stderr:
            print("XFAIL: chmod needs setmode(3) — pull libc/gen/setmode.c")
            sys.exit(1)
        print(f"FAIL: chmod unexpected: rc={r.returncode}, stderr={r.stderr!r}")
        sys.exit(1)


if __name__ == "__main__":
    main()
