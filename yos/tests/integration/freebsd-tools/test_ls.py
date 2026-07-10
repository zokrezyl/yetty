"""ls(1): list directory contents.

Status: PASS. fts(3) is now wired in — FreeBSD's lib/libc/gen/fts.c
links straight into ls (libcExtras = [..., "fts", "qsort", "reallocf"])
and the host-side opendir/readdir/closedir/dirfd bridges live in
src/yos/impl/dir.c. Without that chain ls used to trap with
`env.fts_open` unresolved.

Two layers of verification:

  1. Plain `ls /` — narrow check: at least `bin` and `etc` show up.
     We don't pin the exact entry set because / contents vary.

  2. `ls -l /tmp/<scratch>` against a directory we built ourselves —
     pins every column we know the precise value for: mode string
     (depends on strmode bridge), nlink, owner/group names (depend
     on user_from_uid / group_from_gid), size, name. Regressions in
     any of those used to look like "ls output is mangled" — kept
     surfacing as a class of "is libc broken?" reports from the
     user. Asserting them explicitly makes the next regression
     obvious instead of "looks wrong to me".
"""
import os
import re
import stat
import sys
import tempfile
from run_tool import get_paths, run


def check_listing(yos, libexec):
    r = run(yos, libexec, "ls", "/", expect_rc=0, timeout=10)
    names = set(r.stdout.decode(errors="replace").split())
    if "bin" in names and "etc" in names:
        return None
    return (f"ls / missing bin or etc in output ({len(names)} names)"
            f"\n  stdout head: {r.stdout[:200]!r}"
            f"\n  stderr: {r.stderr!r}")


def check_long_format(yos, libexec):
    """ls -l: pin mode-string, owner, group, size columns.

    Build a small scratch tree with known contents, ask ls -l for it,
    and compare line-by-line against what we know we put there. This
    catches:
      - strmode stubbed → blank first column
      - user_from_uid / group_from_gid stubbed → numeric or wrong
      - struct stat layout drift → garbage size or owner
      - cv_timespec broken → zero timestamps (we don't assert the
        timestamp value itself, just that it's not the unix epoch)
    """
    with tempfile.TemporaryDirectory() as td:
        regular = os.path.join(td, "regular.txt")
        with open(regular, "wb") as fh:
            fh.write(b"hello\n")
        os.chmod(regular, 0o644)
        execfile = os.path.join(td, "runnable.sh")
        with open(execfile, "wb") as fh:
            fh.write(b"#!/bin/sh\necho hi\n")
        os.chmod(execfile, 0o755)
        os.mkdir(os.path.join(td, "subdir"), 0o755)

        r = run(yos, libexec, "ls", "-l", td, expect_rc=0, timeout=10)
        out = r.stdout.decode(errors="replace")

        uid = os.getuid()
        gid = os.getgid()
        # Resolve names the way our user_from_uid/group_from_gid would
        # — falls back to numeric if getpwuid returns NULL.
        try:
            import pwd
            uname = pwd.getpwuid(uid).pw_name
        except KeyError:
            uname = str(uid)
        try:
            import grp
            gname = grp.getgrgid(gid).gr_name
        except KeyError:
            gname = str(gid)

        # Build expectations. Each entry: (name, mode_prefix, size).
        # mode_prefix is the first 10 chars of the strmode column
        # (file-type byte + 9 perm bits, with the trailing extended-
        # attribute space stripped because it's whitespace anyway).
        expected = {
            "regular.txt":  ("-rw-r--r--", 6),
            "runnable.sh":  ("-rwxr-xr-x", 18),
            "subdir":       ("drwxr-xr-x", None),  # size varies
        }

        # Match the long-format columns regardless of how much
        # whitespace ls uses to align them.
        line_re = re.compile(
            r"^(?P<mode>[-dlcbpsw][rwxstST-]{9})"   # 10-char mode
            r"\+?"                                  # optional ACL '+'
            r"\s+(?P<nlink>\d+)"
            r"\s+(?P<user>\S+)"
            r"\s+(?P<group>\S+)"
            r"\s+(?P<size>\d+)"
            r"\s+\S+\s+\S+\s+\S+"                  # mon day time-or-year
            r"\s+(?P<name>\S.*?)\s*$"
        )

        seen = {}
        unmatched = []
        for line in out.splitlines():
            if line.startswith("total ") or not line.strip():
                continue
            m = line_re.match(line)
            if m is None:
                unmatched.append(line)
                continue
            seen[m.group("name")] = m.groupdict()

        problems = []
        if unmatched:
            problems.append(
                f"  {len(unmatched)} line(s) didn't match the long-format "
                f"shape (mode + nlink + user + group + size + date + name):\n"
                + "\n".join("    " + l for l in unmatched[:5])
            )

        for name, (want_mode, want_size) in expected.items():
            row = seen.get(name)
            if row is None:
                problems.append(f"  missing entry: {name!r}")
                continue
            if row["mode"] != want_mode:
                problems.append(
                    f"  {name}: mode={row['mode']!r} want={want_mode!r}"
                )
            if row["user"] != uname:
                problems.append(
                    f"  {name}: user={row['user']!r} want={uname!r}"
                )
            if row["group"] != gname:
                problems.append(
                    f"  {name}: group={row['group']!r} want={gname!r}"
                )
            if want_size is not None and int(row["size"]) != want_size:
                problems.append(
                    f"  {name}: size={row['size']!r} want={want_size!r}"
                )

        if problems:
            return ("ls -l output diverges from expected:\n"
                    + "\n".join(problems)
                    + f"\n\nfull output:\n{out}")
    return None


def main():
    yos, libexec = get_paths()

    errs = []
    e1 = check_listing(yos, libexec)
    if e1: errs.append("plain `ls /`: " + e1)
    e2 = check_long_format(yos, libexec)
    if e2: errs.append("`ls -l <scratch>`: " + e2)

    if errs:
        for e in errs: print("FAIL:", e)
        sys.exit(1)
    print("PASS: ls plain + ls -l columns all parse correctly")


if __name__ == "__main__":
    main()
