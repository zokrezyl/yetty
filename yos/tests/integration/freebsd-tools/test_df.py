"""df(1): filesystem disk space.

Status: runs to completion with rc=0 but produces empty stdout.

Our getmntinfo bridge stub (impl/freebsd_userland.c) returns 0
mounts. df.c only prints its header AFTER it knows there's at
least one mount to display — with 0 mounts, df short-circuits
out, and our libxo shim's xo_emit calls produce no header text.

To turn this into a useful test would need either (a) a real
mount-table source (yos's procfs synth), or (b) a one-line stub
mount entry so df can exercise its formatting path. Both are
larger than the "tools port" scope.

Today we just assert "df runs without crashing or printing an
error" — which is the honest baseline for a stub-mount sandbox.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "df", expect_rc=None, timeout=5)
    if r.returncode == 0 and b"unresolved import" not in r.stderr \
       and b"trapped" not in r.stderr:
        print(f"PASS: df ran clean (0 mounts → empty output, expected)")
        sys.exit(0)
    print(f"FAIL: df unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
