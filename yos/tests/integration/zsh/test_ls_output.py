"""End-to-end zsh+ls output formatting check.

Builds a scratch directory with known contents (regular file,
executable, dir, symlink), then runs

    zsh -fc 'cd <scratch>; ls -alrt'

under yos with PATH pointed at the wasm umbrella's libexec/ (so zsh
finds the wasm-built ls there, the same way ./tools/yos.sh does).

Verifies line-by-line:

  1. No spurious diagnostic lines on stderr or stdout (the
     'yos-tool: ./X: Success' fts info leak that surfaced as the
     user-visible "chaos still" report).
  2. The 'total N' header is present once.
  3. Every non-header line parses against the long-format regex
     (mode + nlink + user + group + size + date + name) — a
     regression in strmode / user_from_uid / group_from_gid /
     struct stat layout / ls's column code would break the shape.
  4. Each entry we put in the dir is present, with the mode, owner
     and group columns matching what we set.
  5. With -t (sort by mtime), entries appear in the order we mtimed
     them (after . / ..).

This is the test the user keeps asking for: a real zsh+ls round-trip
on a directory whose every byte is under our control, not just a
plain `ls /` smoke check.
"""

import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from _zsh_path import find_zsh_wasm  # noqa: E402


def _umbrella_libexec(repo: str) -> str | None:
    """Locate the wasm umbrella's libexec/ (where ls / cat / etc. live)."""
    import glob
    for sym in sorted(glob.glob(os.path.join(repo, "result*"))):
        libex = os.path.join(sym, "libexec")
        if os.path.isdir(libex):
            return libex
    try:
        r = subprocess.run(["nix", "path-info", ".#all"],
                           cwd=repo, capture_output=True, text=True, timeout=10)
        if r.returncode == 0 and r.stdout.strip():
            libex = os.path.join(r.stdout.strip(), "libexec")
            if os.path.isdir(libex):
                return libex
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return None


def _resolve_uid_gid_name(path):
    """Return (uname, gname) for `path` based on the file's ACTUAL
    uid/gid, not the process's. On macOS the tempdir under
    /var/folders/.../T has the sgid bit set, so files created
    there inherit the group of T (often 'wheel'), not the process's
    primary group ('staff'). Reading per-file stat picks up whatever
    the kernel actually wrote. Falls back to the numeric id when
    NSS doesn't resolve the name (rare on darwin's DirectoryServices
    where /etc/passwd often lacks regular users)."""
    st = os.stat(path)
    try:
        import pwd
        uname = pwd.getpwuid(st.st_uid).pw_name
    except KeyError:
        uname = str(st.st_uid)
    try:
        import grp
        gname = grp.getgrgid(st.st_gid).gr_name
    except KeyError:
        gname = str(st.st_gid)
    return uname, gname


# Long-format line shape. Permissive about whitespace count between
# columns (ls right-aligns numeric columns by computing the max width).
LINE_RE = re.compile(
    r"^(?P<mode>[-dlcbpsw][rwxstST-]{9})"   # 10-char mode
    r"\+?"                                  # optional ACL '+'
    r"\s+(?P<nlink>\d+)"
    r"\s+(?P<user>\S+)"
    r"\s+(?P<group>\S+)"
    r"\s+(?P<size>\d+)"
    r"\s+\S+\s+\S+\s+\S+"                  # mon day time-or-year
    r"\s+(?P<name>\S.*?)\s*$"
)


def _find_yos(repo: str, build_dir: str) -> str | None:
    """Prefer the meson-built yos so iteration is fast; fall back to
    nix-built yos when meson hasn't run yet (or its output is the
    stale 0-byte placeholder from a previous reconfigure)."""
    meson_yos = os.path.join(repo, build_dir, "src", "yos", "yos")
    if os.path.exists(meson_yos) and os.access(meson_yos, os.X_OK):
        return meson_yos
    try:
        r = subprocess.run(["nix", "path-info", ".#yos"],
                           cwd=repo, capture_output=True, text=True, timeout=10)
        if r.returncode == 0 and r.stdout.strip():
            cand = os.path.join(r.stdout.strip(), "bin", "yos")
            if os.path.exists(cand):
                return cand
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return None


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    build_dir = os.environ.get("YOS_BUILD_DIR") or "build-linux"
    yos = _find_yos(repo, build_dir)
    zsh = find_zsh_wasm(repo)
    libexec = _umbrella_libexec(repo)
    if not zsh or not libexec or not yos:
        print("SKIP: yos / zsh.wasm / umbrella libexec/ not found "
              "(run `nix build .#all`)")
        sys.exit(0)
    if not os.path.exists(os.path.join(libexec, "ls")):
        print(f"SKIP: ls not in libexec ({libexec})")
        sys.exit(0)

    with tempfile.TemporaryDirectory() as td:
        # Lay down a small known tree. mtimes are set explicitly so
        # we can assert the -t (sort by mtime, oldest last unless -r)
        # ordering reliably.
        layout = [
            # (relpath, mode, content, mtime)
            ("alpha.txt",  0o644, b"alpha\n",     1_700_000_000),
            ("bravo.sh",   0o755, b"#!/bin/sh\n", 1_700_001_000),
            ("subdir",     None,  None,           1_700_002_000),
            ("link2alpha", None,  "alpha.txt",    1_700_003_000),  # symlink
        ]
        for name, mode, content, _ in layout:
            full = os.path.join(td, name)
            if isinstance(content, bytes):
                with open(full, "wb") as fh:
                    fh.write(content)
                os.chmod(full, mode)
            elif content is None:
                os.mkdir(full, 0o755)
            else:  # symlink
                os.symlink(content, full)
        for name, _, _, mtime in layout:
            full = os.path.join(td, name)
            os.utime(full, (mtime, mtime), follow_symlinks=False)

        env = os.environ.copy()
        env["PATH"] = libexec
        # Don't leak the host's locale into ls — keep numeric/date
        # rendering deterministic.
        env["LANG"] = "C"
        env["LC_ALL"] = "C"
        cmd = [yos, zsh, "-fc", f"cd '{td}' && ls -alrt"]
        # Capture per-file uid/gid BEFORE the subprocess starts (which
        # would close `td`'s context if we waited). See
        # _resolve_uid_gid_name's docstring for why we can't use
        # os.getgid() instead.
        expected_ug = {
            name: _resolve_uid_gid_name(os.path.join(td, name))
            for name in ("alpha.txt", "bravo.sh", "subdir", "link2alpha")
        }
        r = subprocess.run(cmd, env=env, capture_output=True, timeout=30)

    if r.returncode != 0:
        print(f"FAIL: rc={r.returncode}")
        print(f"  stdout: {r.stdout!r}")
        print(f"  stderr: {r.stderr!r}")
        sys.exit(1)

    out = r.stdout.decode(errors="replace")

    problems = []
    lines = out.splitlines()

    # (1) No 'yos-tool: …' diagnostic noise on stdout OR stderr. This
    #     is the "chaos still" thing the user keeps surfacing as broken
    #     — fts gives one entry fts_info=FTS_ERR/FTS_NS with errno=0
    #     and ls then prints 'yos-tool: <name>: Success' to stderr.
    #     A working ls run has zero such lines for a directory we know
    #     is healthy. Most ls error output goes to stderr so check
    #     both streams.
    stderr_text = r.stderr.decode(errors="replace")
    noise = [l for l in (lines + stderr_text.splitlines())
             if l.startswith("yos-tool:")]
    if noise:
        problems.append("spurious yos-tool: lines (fts info leak):\n  "
                        + "\n  ".join(noise))

    # (2) Exactly one 'total NNN' header.
    totals = [l for l in lines if re.match(r"^total \d+\s*$", l)]
    if len(totals) != 1:
        problems.append(f"expected 1 'total N' header, got {len(totals)}: {totals!r}")

    # (3+4) Parse every non-header line and check our entries are
    #       present with the expected mode/owner/group.
    rows_by_name = {}
    unparseable = []
    for l in lines:
        if not l.strip() or l.startswith("total ") or l.startswith("yos-tool:"):
            continue
        m = LINE_RE.match(l)
        if m is None:
            unparseable.append(l)
            continue
        # symlink lines have "name -> target"; strip the arrow tail
        # so the key is just the entry name.
        name = m.group("name").split(" -> ", 1)[0]
        rows_by_name[name] = m.groupdict()
    if unparseable:
        problems.append("lines that didn't parse as long-format:\n  "
                        + "\n  ".join(unparseable[:5]))

    want_mode = {
        "alpha.txt":   "-rw-r--r--",
        "bravo.sh":    "-rwxr-xr-x",
        "subdir":      "drwxr-xr-x",
        "link2alpha":  "lrwxrwxrwx",
    }
    want_size = {
        "alpha.txt":   6,    # "alpha\n"
        "bravo.sh":   10,    # "#!/bin/sh\n" (10 bytes incl. trailing \n)
        "link2alpha":  9,    # strlen("alpha.txt") for symlink target
        # subdir size varies (4096 on most fs, 64 on darwin tmpfs);
        # not asserted.
    }
    for name, mode in want_mode.items():
        row = rows_by_name.get(name)
        if row is None:
            problems.append(f"missing entry: {name!r}")
            continue
        if row["mode"] != mode:
            problems.append(f"{name}: mode={row['mode']!r} want={mode!r}")
        want_u, want_g = expected_ug.get(name, (None, None))
        if want_u and row["user"] != want_u:
            problems.append(f"{name}: user={row['user']!r} want={want_u!r}")
        if want_g and row["group"] != want_g:
            problems.append(f"{name}: group={row['group']!r} want={want_g!r}")
        want_sz = want_size.get(name)
        if want_sz is not None and int(row["size"]) != want_sz:
            problems.append(f"{name}: size={row['size']!r} want={want_sz!r}")

    # (5) -t with -r: newest LAST in the file region (oldest first).
    #     The four entries we control should appear in mtime-ascending
    #     order among themselves.
    file_names = ["alpha.txt", "bravo.sh", "subdir", "link2alpha"]
    seen_order = [n for n in (
        re.match(LINE_RE, l).group("name").split(" -> ", 1)[0]
        for l in lines
        if l and not l.startswith(("total ", "yos-tool:"))
        and re.match(LINE_RE, l)
    ) if n in file_names]
    if seen_order != file_names:
        problems.append(
            f"-rt order wrong: got {seen_order!r} want {file_names!r}")

    if problems:
        print("FAIL: ls -alrt under zsh diverges:")
        for p in problems:
            print("  -", p)
        print("\nfull stdout:")
        print(out)
        if r.stderr:
            print("\nstderr:")
            print(r.stderr.decode(errors="replace"))
        sys.exit(1)

    print("PASS: zsh + ls -alrt formats correctly "
          f"({len(rows_by_name)} entries, no yos-tool noise)")


if __name__ == "__main__":
    main()
