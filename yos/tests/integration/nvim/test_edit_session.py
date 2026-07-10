"""nvim realistic-edit-session test: open empty buffer, enter insert mode,
type text, escape back to normal, save, quit-all-discard.

Mirrors the typical interactive flow that exposed the user-reported
'crashes after typing and quitting' regression — the basic :q!  test
exercises only the no-edit path. This drives the editor through:

  i      enter insert mode
  ...    type some text
  <esc>  back to normal
  :w …   write to a temp file (exercises file open + write through yos vfs)
  :qa!   quit all without confirming further edits

Pass criteria: nvim exits with status 0, the temp file contains exactly
what we typed, and no error/trap appears in nvim's stderr stream.
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_in_pty import run_in_pty
from _nvim_path import find_nvim


def main():
    repo = os.environ.get("YOS_REPO_ROOT") or os.getcwd()
    paths = find_nvim(repo)
    if not paths:
        print("SKIP: nvim wasm not found (run `nix build .#all`)")
        sys.exit(0)
    if not paths["has_runtime"]:
        print("SKIP: nvim runtime tree missing — need `.#all` umbrella")
        sys.exit(0)

    fd, tmp_path = tempfile.mkstemp(prefix="nvim_edit_", suffix=".txt")
    os.close(fd)
    os.unlink(tmp_path)

    text_line1 = "the quick brown fox"
    text_line2 = "jumps over the lazy dog"

    env = dict(os.environ)
    env["TERM"] = "xterm-256color"
    env["HOME"] = "/tmp/yos-nvim-test-no-such-home"

    write_cmd = f":w {tmp_path}\r".encode()
    out, status = run_in_pty(
        [paths["yos"], paths["nvim"], "--clean"],
        rows=40, cols=120,
        driver=[
            (3.0, b""),
            (0.4, b"i"),
            (0.4, text_line1.encode()),
            (0.4, b"\r"),
            (0.4, text_line2.encode()),
            (0.4, b"\x1b"),
            (0.5, write_cmd),
            (1.0, b":qa!\r"),
            (3.0, b""),
        ],
        kill_after=2.0,
        env=env,
    )

    failed = False
    if status != 0:
        print(f"FAIL: nvim exit status={status} (expected 0)")
        failed = True

    if not os.path.exists(tmp_path):
        print(f"FAIL: nvim did not write {tmp_path}")
        failed = True
    else:
        with open(tmp_path, "rb") as f:
            written = f.read()
        expected = (text_line1 + "\n" + text_line2 + "\n").encode()
        if written != expected:
            print(f"FAIL: file content mismatch")
            print(f"  expected ({len(expected)}B): {expected!r}")
            print(f"  got      ({len(written)}B): {written!r}")
            failed = True
        os.unlink(tmp_path)

    if failed:
        sys.stdout.buffer.write(b"--- last 600 bytes of pty output ---\n")
        sys.stdout.buffer.write(out[-600:])
        sys.exit(1)
    print(f"PASS: typed {len(text_line1)+len(text_line2)+1}B, file matches, clean exit")


if __name__ == "__main__":
    main()
