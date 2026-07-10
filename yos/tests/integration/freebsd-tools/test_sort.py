"""sort(1): line sorting.

Status: XFAIL today. Exits with `yos-tool: errno=2` (ENOENT)
straight from main() — sort calls `sort_strdup("-")` first thing
to set the default outfile name. Even though strdup is now bridged,
sort then opens random files (`/etc/locale.conf`, etc.) for
locale lookup and bails on the first ENOENT. Needs locale-source
absence to be tolerated gracefully.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "sort",
            stdin=b"banana\napple\ncherry\n",
            expect_rc=None, timeout=5)
    want = b"apple\nbanana\ncherry\n"
    if r.returncode == 0 and r.stdout == want:
        print("PASS: sort produced lexicographic order")
        sys.exit(0)
    if b"errno=" in r.stderr:
        print(f"XFAIL: sort errno path "
              f"(rc={r.returncode}, stderr={r.stderr!r})")
        sys.exit(1)
    print(f"FAIL: sort unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}, stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
