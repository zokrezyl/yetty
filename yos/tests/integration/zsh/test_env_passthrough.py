"""Pinned regression: host env values do not reach the wasm guest.

Setting `HOME=/some/path` (or any variable) in the host environment
when invoking `yos zsh.wasm -c …` should leave that exact value
visible to the guest as `$HOME`. Today it doesn't:

  - `$HOME` ends up as "/" inside zsh — yos seeds a fallback
    (probably from a stubbed getpwuid) instead of forwarding the
    host's value.
  - Arbitrary names like `YOS_TEST_VAR=...` are dropped entirely;
    `${YOS_TEST_VAR}` is empty in the guest.

This blocks anything that needs to wire data from the host into the
wasm guest via env (test fixtures, build configs, locale settings,
TERM-overrides for nvim, …). xfail until yos's argv_setup is taught
to forward the full environ verbatim.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    zsh = find_zsh_wasm(repo)
    if not zsh:
        print("SKIP: zsh.wasm not found (run `nix build .#zsh`)")
        sys.exit(0)
    if not os.path.exists(yos):
        print(f"FAIL: yos binary not found: {yos}")
        sys.exit(1)

    env = dict(os.environ)
    env["HOME"] = "/yos-test-home-marker"
    env["YOS_PASSTHROUGH_VAR"] = "value-from-host-42"

    def rc(code, expect_rc=0):
        r = subprocess.run([yos, zsh, "-c", code],
                           capture_output=True, timeout=10, env=env)
        if r.returncode != expect_rc:
            print(f"FAIL: {code!r} → rc={r.returncode}, expected {expect_rc}")
            print(f"  stdout: {r.stdout!r}")
            print(f"  stderr: {r.stderr!r}")
            sys.exit(1)

    # Standard name passthrough — HOST sets HOME=/yos-test-home-marker,
    # guest must see exactly that. Today guest sees "/".
    rc('[ "$HOME" = /yos-test-home-marker ]')

    # Arbitrary-name passthrough — HOST sets YOS_PASSTHROUGH_VAR,
    # guest must see the exact value. Today guest sees empty string.
    rc('[ "$YOS_PASSTHROUGH_VAR" = value-from-host-42 ]')

    print("PASS: yos env passthrough is intact (host → guest)")


if __name__ == "__main__":
    main()
