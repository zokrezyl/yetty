"""sleep(1): pause for N seconds.

Status: XFAIL today. FreeBSD's sleep.c parses its argument via
sscanf(3), and sscanf is variadic. yos's bridge surface routes
variadics through Tier-2 libc-pure.wasm, which isn't built yet —
so sscanf shows up as `env.sscanf unresolved import`.

Fix path: build libc-pure.wasm and wire the sidecar so variadic
fns route through Tier 2. Then this test should flip to PASS.
"""
import sys
import time
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    t0 = time.monotonic()
    r = run(yos, libexec, "sleep", "1", expect_rc=None, timeout=10)
    dt = time.monotonic() - t0

    if r.returncode == 0 and 0.9 < dt < 5.0:
        print(f"PASS: sleep 1 took {dt:.2f}s")
        sys.exit(0)
    if b"unresolved import env.sscanf" in r.stderr:
        print("XFAIL: sleep needs sscanf — Tier 2 libc-pure not wired yet")
        sys.exit(1)
    print(f"FAIL: sleep unexpected: rc={r.returncode}, dt={dt:.2f}s, "
          f"stderr={r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
