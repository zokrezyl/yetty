"""du(1): disk usage.

Status: XFAIL today. Traps with `unresolved import env.fts_open`,
same family as find(1).
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "du", ".", expect_rc=None, timeout=5)
    if r.returncode == 0 and len(r.stdout) > 0:
        print("PASS: du produced output")
        sys.exit(0)
    if b"unresolved import env.fts_open" in r.stderr:
        print("XFAIL: du needs fts_* bridge family (same as find)")
        sys.exit(1)
    print(f"FAIL: du unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
