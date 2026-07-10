"""Helper: run a freebsd-tools wasm module under yos with capture.

Tests use `run(tool_name, *args, stdin=..., timeout=...)` — same
shape across every test in this dir so the failure mode is uniform
when a tool hits an unbridged libc fn (yos prints "unresolved import
env.<name>" and traps with non-zero exit).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _tools_path import find_yos_and_tools


def get_paths():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    yos, libexec = find_yos_and_tools(repo)
    if not (yos and libexec):
        print("SKIP: yos+libexec not found (run `nix build .#all`)")
        sys.exit(0)
    return yos, libexec


def run(yos, libexec, name, *args, stdin=None, timeout=15, expect_rc=0):
    """Run yos result/libexec/<name> <args> with input stdin.

    Returns the CompletedProcess. If expect_rc is set and rc differs,
    prints diagnostics and exits 1 (FAIL). Stdout/stderr are captured.
    """
    wasm = os.path.join(libexec, name)
    if not os.path.exists(wasm):
        print(f"FAIL: {name}.wasm missing at {wasm}")
        sys.exit(1)
    r = subprocess.run([yos, wasm, *args],
                       input=stdin, capture_output=True, timeout=timeout)
    if expect_rc is not None and r.returncode != expect_rc:
        print(f"FAIL: {name} {args} → rc={r.returncode}, expected {expect_rc}")
        print(f"  stdout: {r.stdout!r}")
        print(f"  stderr: {r.stderr!r}")
        sys.exit(1)
    return r
