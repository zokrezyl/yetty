"""awk(1): pattern-action language.

Status: XFAIL today.

Surface failure: awk receives `BEGIN { print 42 }` correctly via
argv (the trace shows `argv[1]="BEGIN { print 42 }"` at offset
0x50013), but its lexer reads only "B" before bailing with
"syntax error at source line 1, context >>> B <<<".

Root cause: the wasm guest libc's `<_ctype.h>` macros expand
isalnum / isalpha / isdigit into inline functions that deref
`_CurrentRuneLocale->__runetype[c]`. Without the FreeBSD locale
runtime initialised, the lookup returns 0 — so awk's gettok()
reads 'B', sees `!isalnum('B') == true` (bug), and returns the
literal byte as a token instead of a keyword identifier. yyparse
then chokes on the bare 'B'.

The freebsd-tools build links a sed-edited copy of
`lib/libc/locale/table.c` (providing _DefaultRuneLocale +
_CurrentRuneLocale + __mb_sb_limit), which puts the rune data into
the wasm module — wabt confirms the "RuneMagiNONE" magic bytes are
in the .data section. But the inline macro path in <_ctype.h>
goes through `__getCurrentRuneLocale()` which checks
`_ThreadRuneLocale` first; that's `_Thread_local` and wasm32-clang
emits it without proper TLS init, so the read returns garbage.

Fix paths to investigate:
  1. Add `-D__RUNETYPE_INTERNAL` to the awk/tr/sed/grep build —
     swaps the inline `__getCurrentRuneLocale` for an extern call,
     which we then implement returning `_CurrentRuneLocale`
     directly (no TLS).
  2. Provide an `extern _Thread_local const _RuneLocale *
     _ThreadRuneLocale = NULL;` symbol so the if-check evaluates
     against a real zero.
  3. Bridge the inline path entirely via host-side ctype helpers.
"""
import sys
from run_tool import get_paths, run


def main():
    yos, libexec = get_paths()
    r = run(yos, libexec, "awk", "BEGIN { print 42 }",
            expect_rc=None, timeout=5)
    if r.returncode == 0 and r.stdout.strip() == b"42":
        print("PASS: awk BEGIN block ran")
        sys.exit(0)
    # Pin the current failure mode so an accidental fix flips this
    # test to UNEXPECTEDPASS rather than slipping by.
    if b"syntax error" in r.stderr and b">>> B <<<" in r.stderr:
        print("XFAIL: awk lexer bails after one byte "
              "(yos rune-locale gap)")
        sys.exit(1)
    print(f"FAIL: awk unexpected: rc={r.returncode}")
    print(f"  stdout: {r.stdout!r}")
    print(f"  stderr: {r.stderr!r}")
    sys.exit(1)


if __name__ == "__main__":
    main()
