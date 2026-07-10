"""bsdcat(1): FreeBSD's libarchive `cat` front-end, on the yos
libarchive surface.

Status: PASS. bsdcat is built from contrib/libarchive source verbatim
and imports env.archive_* — resolved by the host libarchive bridge
(src/yos/impl/libc/libarchive.c). This guards two paths:

  1. raw: `bsdcat <plain file>` reproduces the file byte-for-byte
     (archive_read_support_format_raw), exercising open_filename +
     read_data_into_fd through the bridge.
  2. filter: `bsdcat <file.gz>` decompresses to the original content,
     exercising libarchive's gzip filter end-to-end (host zlib) over
     the same surface.
"""
import gzip
import os
import sys
import tempfile

from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    payload = b"hello from bsdcat\nsecond line\n"

    # 1. plain file -> raw passthrough
    with tempfile.NamedTemporaryFile(suffix=".txt", delete=False) as f:
        f.write(payload)
        plain = f.name
    # 2. gzip -> filter decompress
    gz = plain + ".gz"
    with gzip.open(gz, "wb") as g:
        g.write(payload)

    try:
        r = run(yos, libexec, "bsdcat", plain, expect_rc=0, timeout=10)
        if r.stdout != payload:
            print(f"FAIL: bsdcat raw mismatch: {r.stdout!r} != {payload!r}")
            sys.exit(1)

        r = run(yos, libexec, "bsdcat", gz, expect_rc=0, timeout=10)
        if r.stdout != payload:
            print(f"FAIL: bsdcat gzip-decompress mismatch: {r.stdout!r}")
            sys.exit(1)
    finally:
        for p in (plain, gz):
            try:
                os.unlink(p)
            except OSError:
                pass

    print("PASS: bsdcat raw + gzip-decompress both reproduce the input")
    sys.exit(0)


if __name__ == "__main__":
    main()
