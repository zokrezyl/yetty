#!/usr/bin/env python3
"""Curate a FreeBSD UAPI header tree from an unpacked src.txz.

We deliberately do NOT run FreeBSD's `make includes` here — that would
require bmake + a working bsd.*.mk environment on the host (Linux),
which is fragile. For abi.yaml extraction we only need the canonical
`.h` files that hold struct definitions, typedefs, macros, and
syscall numbers; FreeBSD's `make includes` mostly does install + a
few generated headers (syscall.h, etc.) that we either reconstruct
ourselves from `sys/syscalls.master` or live without.

What this script produces, for one arch:

    headers-<arch>/usr/include/
    ├── sys/               (from src/sys/sys/)
    ├── vm/                (from src/sys/vm/)
    ├── machine/           (from src/sys/<arch>/include/)
    ├── x86/               (from src/sys/x86/include/, only on i386/amd64)
    ├── net/, netinet/,
    │   netinet6/, arpa/,  (POSIX network headers, from src/include/)
    │   protocols/, rpc/,
    │   rpcsvc/, …
    └── *.h                (flat POSIX headers from src/include/)

Plus the syscalls.master file copied as-is so extract.py can parse the
syscall table (analogue of Linux's syscall_32.tbl).
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

# Top-level directories under src/include/ that should be copied
# verbatim into usr/include/. This list mirrors what FreeBSD's
# `make includes` installs minus the build-time generated bits.
INCLUDE_SUBDIRS = (
    'arpa', 'gssapi', 'protocols', 'rpc', 'rpcsvc',
    'security', 'ssp', 'xlocale',
)

# Network headers live under src/sys/<dir>/ in FreeBSD — they are
# kernel-side sources but the .h files exposed are UAPI. `make
# includes` flattens them into usr/include/<dir>/. We do the same.
SYS_NETWORK_SUBDIRS = (
    'net', 'netinet', 'netinet6', 'netipsec',
    'netgraph', 'nfs', 'nfsclient', 'nfsserver',
)

# Other UAPI subdirs from src/sys/<dir>/ — bsm/audit.h is needed by
# anything pulling sys/ucred.h (which libuv does indirectly).
EXTRA_SYS_SUBDIRS = (
    'bsm',
)


def copy_tree(src: Path, dst: Path, *, symlinks: bool = False) -> int:
    """Copy `src` into `dst`. Returns count of .h files copied.

    The post-copy step patches a few specific headers in place
    (cdefs.h shim etc.); when the source tree lives in a read-only
    location (e.g. /nix/store under the freebsd-src derivation),
    `shutil.copy2` preserves the source mode bits and the patching
    step trips on PermissionError. Chmod every copy to user-writable
    so subsequent edits work regardless of source perms.
    """
    if not src.is_dir():
        return 0
    dst.mkdir(parents=True, exist_ok=True)
    n = 0
    for entry in src.rglob('*'):
        if entry.is_dir():
            continue
        # Skip non-header noise (Makefiles, .c, .S, .conf, …) — we
        # only extract from headers and we keep the tree small.
        if entry.suffix not in ('.h', '.hpp'):
            continue
        rel = entry.relative_to(src)
        out = dst / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry, out, follow_symlinks=not symlinks)
        out.chmod(0o644)
        n += 1
    return n


def install_arch_headers(src_root: Path, out_root: Path, arch: str) -> None:
    """Build the per-arch curated UAPI tree under out_root."""
    src_sys     = src_root / 'sys'
    src_include = src_root / 'include'
    usr_include = out_root / 'usr' / 'include'

    # Wipe the per-arch output dir (idempotent rebuild). meson custom_target
    # owns the dir so we control its contents fully.
    if out_root.exists():
        shutil.rmtree(out_root)
    usr_include.mkdir(parents=True)

    counts = {}

    # 1. Flat POSIX headers from src/include/<*.h>
    n = 0
    for hdr in src_include.glob('*.h'):
        out = usr_include / hdr.name
        shutil.copy2(hdr, out, follow_symlinks=True)
        out.chmod(0o644)
        n += 1
    counts['include/*.h'] = n

    # 2. Subdirs under src/include/ that are pure UAPI (arpa, net, …).
    for sub in INCLUDE_SUBDIRS:
        n = copy_tree(src_include / sub, usr_include / sub)
        if n:
            counts[f'include/{sub}/'] = n

    # 3. src/sys/sys/* — kernel UAPI exposed as <sys/X.h>
    counts['sys/sys/'] = copy_tree(src_sys / 'sys', usr_include / 'sys')

    # 4. src/sys/vm/* — VM UAPI (mman, etc. headers refer to it)
    counts['sys/vm/']  = copy_tree(src_sys / 'vm',  usr_include / 'vm')

    # 4b. Network UAPI from src/sys/<net*>/ — net.h, netinet/in.h, etc.
    #     These are kernel-side sources but expose UAPI to userland.
    for sub in SYS_NETWORK_SUBDIRS:
        n = copy_tree(src_sys / sub, usr_include / sub)
        if n:
            counts[f'sys/{sub}/'] = n

    # 4c. Other src/sys/<dir>/ subtrees — bsm for audit headers.
    for sub in EXTRA_SYS_SUBDIRS:
        n = copy_tree(src_sys / sub, usr_include / sub)
        if n:
            counts[f'sys/{sub}/'] = n

    # 5. Arch-specific machine/ headers. For i386/amd64 also pull
    #    sys/x86/include into x86/ — many sys/<arch> headers
    #    `#include <x86/X.h>`.
    counts[f'sys/{arch}/include/'] = copy_tree(
        src_sys / arch / 'include', usr_include / 'machine')
    if arch in ('i386', 'amd64'):
        counts['sys/x86/include/'] = copy_tree(
            src_sys / 'x86' / 'include', usr_include / 'x86')

    # 6. src/sys/dev/<*>.h — driver headers, mostly internal. Skipped
    #    for now; the abi.yaml extraction doesn't reach into <dev/X.h>
    #    UAPI today, and pulling them in drags ~3000 .h files most of
    #    which are PCI-ID tables and similar noise. Re-enable
    #    selectively if extract.py reports a missing struct.

    # 7. syscalls.master — not a header, but extract.py needs it to
    #    build the syscall-number table. Copy it next to usr/include
    #    under share/syscalls/ so the extractor has a stable path.
    syscalls_master = src_sys / 'kern' / 'syscalls.master'
    if syscalls_master.is_file():
        share = out_root / 'usr' / 'share' / 'syscalls'
        share.mkdir(parents=True, exist_ok=True)
        out = share / 'syscalls.master'
        shutil.copy2(syscalls_master, out)
        out.chmod(0o644)
        counts['kern/syscalls.master'] = 1

    # 7a. Library-shipped headers FreeBSD installs to /usr/include
    #     during `make includes` but live under lib/<lib>/ in src.
    LIB_HEADERS = (
        ('lib/msun/src',     'math.h'),
        ('lib/msun/src',     'complex.h'),
        ('lib/libutil',      'libutil.h'),  # has openpty / forkpty
    )
    for lib_dir, hdr in LIB_HEADERS:
        src_hdr = src_root / lib_dir / hdr
        if src_hdr.is_file():
            out = usr_include / hdr
            shutil.copy2(src_hdr, out, follow_symlinks=True)
            out.chmod(0o644)
            counts[f'{lib_dir}/{hdr}'] = 1

    # 7a-2. <pty.h> is a Linux convention. FreeBSD puts openpty /
    #       forkpty in <libutil.h>. Provide a shim header so source
    #       written for Linux (nvim, libuv) compiles.
    pty_top = usr_include / 'pty.h'
    if not pty_top.is_file():
        pty_top.write_text(
            '/* Linux-style pty.h shim — FreeBSD ships openpty/forkpty\n'
            ' * in <libutil.h>; this header lets Linux-targeted source\n'
            ' * compile against the FreeBSD libc surface. */\n'
            '#ifndef _PTY_H_\n'
            '#define _PTY_H_\n'
            '#include <libutil.h>\n'
            '#endif /* _PTY_H_ */\n'
        )
        counts['shim pty.h'] = 1

    # 7b. Generated/installed top-level shims. FreeBSD's source tree
    #     keeps several "should be at usr/include/" headers under
    #     sys/ or relies on a `make includes` step we don't run (which
    #     would normally synthesise them). Provide minimal shims that
    #     chain to the underlying FreeBSD headers, so user code that
    #     does `#include <X.h>` resolves cleanly.

    # errno.h chains to <sys/errno.h>. Mirrors FreeBSD's /usr/include/
    # errno.h shape.
    errno_top = usr_include / 'errno.h'
    if not errno_top.is_file():
        errno_top.write_text(
            '/* Auto-generated by build-tools/freebsd/install_includes.py.\n'
            ' * Mirrors FreeBSD\'s installed /usr/include/errno.h shape. */\n'
            '#ifndef _ERRNO_H_\n'
            '#define _ERRNO_H_\n'
            '#include <sys/cdefs.h>\n'
            '#include <sys/errno.h>\n'
            '__BEGIN_DECLS\n'
            'extern int *__error(void);\n'
            '#define errno (*__error())\n'
            '__END_DECLS\n'
            '#endif /* _ERRNO_H_ */\n'
        )
        counts['shim errno.h'] = 1

    # Trivial top-level shims for POSIX-standard headers FreeBSD's
    # `make includes` step would normally synthesise. Each just
    # chains to <sys/<name>.h>. We don't run `make includes` — these
    # shims fill the gap.
    TRIVIAL_TOPLEVEL_SHIMS = (
        'fcntl', 'poll', 'syslog', 'termios', 'mqueue',
    )
    n_shims = 0
    for name in TRIVIAL_TOPLEVEL_SHIMS:
        top = usr_include / f'{name}.h'
        if top.is_file():
            continue
        sub = usr_include / 'sys' / f'{name}.h'
        if not sub.is_file():
            continue
        guard = f'_{name.upper()}_H_'
        top.write_text(
            f'/* Auto-generated by build-tools/freebsd/install_includes.py.\n'
            f' * Top-level shim chaining <{name}.h> to FreeBSD\'s sys/{name}.h. */\n'
            f'#ifndef {guard}\n#define {guard}\n'
            f'#include <sys/{name}.h>\n'
            f'#endif /* {guard} */\n'
        )
        n_shims += 1
    if n_shims:
        counts[f'shims (sys/X -> X.h)'] = n_shims

    # 7c. Provide a top-level <stdint.h> that chains to FreeBSD's
    #     <sys/stdint.h>. FreeBSD's source tree doesn't ship a
    #     stdint.h at usr/include level (relies on the compiler's
    #     resource-dir version); we want FreeBSD's typedefs to win
    #     on wasm32, so route everything through sys/stdint.h.
    stdint_top = usr_include / 'stdint.h'
    if not stdint_top.is_file():
        stdint_top.write_text(
            '/* Auto-generated by build-tools/freebsd/install_includes.py.\n'
            ' * Routes <stdint.h> requests to FreeBSD\'s sys/stdint.h\n'
            ' * (consistent with what <inttypes.h> uses) so we don\'t\n'
            ' * conflict with clang\'s resource-dir stdint.h.\n'
            ' */\n'
            '#ifndef _STDINT_H_\n'
            '#define _STDINT_H_\n'
            '#include <sys/stdint.h>\n'
            '#endif /* _STDINT_H_ */\n'
        )
        counts['shim stdint.h'] = 1

    # 8. Patch sys/cdefs.h: neutralise the macros that emit inline asm
    #    directives FreeBSD's libc uses for ELF symbol versioning and
    #    weak references (.symver, .weak, .equ). wasm32 has no symbol
    #    versioning and clang's wasm assembler rejects these
    #    directives. Make the macros expand to nothing — every
    #    subsequent FreeBSD header that uses them then compiles
    #    cleanly. Targeted patch: only the asm-emitting macros, leave
    #    the rest of cdefs.h untouched.
    cdefs = usr_include / 'sys' / 'cdefs.h'
    if cdefs.is_file():
        text = cdefs.read_text()
        marker = '/* yos: wasm32-friendly asm-macro neutralisation */'
        if marker not in text:
            i386_shim = ''
            if arch == 'i386':
                # Force the guest arch macro at the cdefs.h level.
                # The whole FreeBSD header tree gates struct layouts,
                # typedefs and decls on __i386__; clang -target wasm32
                # never sets it, and meson's `--define __i386__=1`
                # arg reaches extract.py on Linux but for unclear
                # reasons (libclang version skew? cindex argument
                # passing?) doesn't take effect under macOS' python
                # libclang binding. Forcing it in the header itself
                # is invariant across hosts and across cflags drift.
                i386_shim = (
                    '#ifndef __i386__\n'
                    '#define __i386__ 1\n'
                    '#endif\n'
                )
            shim = (
                '\n' + marker + '\n'
                + i386_shim +
                '#undef  __weak_reference\n'
                '#undef  __warn_references\n'
                '#undef  __sym_compat\n'
                '#undef  __sym_default\n'
                '#define __weak_reference(sym, alias)\n'
                '#define __warn_references(sym, msg)\n'
                '#define __sym_compat(sym, impl, verid)\n'
                '#define __sym_default(sym, impl, verid)\n'
                '/* end yos patch */\n'
            )
            # Insert before the trailing `#endif /* !_SYS_CDEFS_H_ */`
            # so the redefinitions take effect for everyone who later
            # includes <sys/cdefs.h>. Find the LAST #endif and inject.
            idx = text.rfind('\n#endif')
            if idx < 0:
                # Fallback: just append (will likely break the
                # include-guard but at least leaves a diagnostic).
                cdefs.write_text(text + shim)
            else:
                cdefs.write_text(text[:idx] + shim + text[idx:])
            counts['patch sys/cdefs.h'] = 1

    # Stamp file: meson uses it to detect re-runs (timestamp on dir
    # alone isn't reliable when sub-tree differs).
    (out_root / '.installed').write_text(arch + '\n')

    # Summary
    total = sum(counts.values())
    print(f'[freebsd-headers] arch={arch}  total .h files={total}', file=sys.stderr)
    for k, v in sorted(counts.items()):
        print(f'  {v:6d}  {k}', file=sys.stderr)


def main() -> int:
    p = argparse.ArgumentParser(description='Curate a FreeBSD UAPI tree per arch.')
    p.add_argument('--src-root', required=True,
                   help='path to extracted FreeBSD source tree (the dir that contains sys/, include/, lib/, …)')
    p.add_argument('--out-root', required=True,
                   help='output directory; will be wiped + populated')
    p.add_argument('--arch', required=True, choices=('i386', 'amd64', 'arm', 'arm64', 'riscv'),
                   help='FreeBSD MACHINE_ARCH selector')
    args = p.parse_args()

    src_root = Path(args.src_root)
    out_root = Path(args.out_root)
    if not (src_root / 'sys').is_dir() or not (src_root / 'include').is_dir():
        print(f'[freebsd-headers] {src_root} does not look like a FreeBSD source tree '
              f'(missing sys/ or include/)', file=sys.stderr)
        return 2

    install_arch_headers(src_root, out_root, args.arch)
    return 0


if __name__ == '__main__':
    sys.exit(main())
