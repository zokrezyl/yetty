#!/usr/bin/env python3
"""Sysroot skeleton emitter — cross-platform replacement for skel.sh.

Args (positional):
  1) freebsd_headers_root   path containing usr/include/ (and friends)
  2) stamp                  file to touch on success
  3) sysroot_out            target sysroot directory

Output layout under sysroot_out/:
  usr/include/       -> symlink or directory junction to freebsd's usr/include
  usr/lib/lib*.a     empty archives for -ldl / -lm / -lutil / -lpthread / ...
                     plus libc.a carrying yos_libc_init.o
  usr/lib/crt1.o     wasm32 crt1 (calls main(argc, argv), then env.exit)

The wasm toolchain (clang + llvm-ar) is resolved from:
  $YOS_WASM_CLANG           (defaults to ../wasm-clang on POSIX,
                             ../wasm-clang.cmd on Windows)
  $YOS_LLVM_AR              (defaults to llvm-ar on PATH)
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path


def _make_link(target: Path, link: Path) -> None:
    """Best-effort link target → at link. Symlink on POSIX or
    dev-mode-enabled Windows; falls back to mklink /J (directory
    junction, no admin) on stock Windows."""
    if link.exists() or link.is_symlink():
        if link.is_symlink() or link.is_dir():
            try:
                link.unlink()
            except (OSError, IsADirectoryError, PermissionError):
                # On Windows a directory junction looks like a dir; rmdir it.
                shutil.rmtree(link, ignore_errors=True)
    try:
        os.symlink(str(target), str(link), target_is_directory=True)
        return
    except (OSError, NotImplementedError):
        pass
    if os.name == 'nt':
        rc = subprocess.run(
            ['cmd', '/c', 'mklink', '/J', str(link), str(target)],
            capture_output=True, text=True,
        ).returncode
        if rc == 0:
            return
    raise SystemExit(f'[skel] could not link {link} -> {target}')


CRT1_C = r"""/* crt1 — yos-flavoured. Called by yos at module load.
 *
 * `main(int, char**)` is the user's entry point. clang -target wasm32
 * with -mexec-model=command would normally turn it into
 * `__main_argc_argv` and emit a wrapper, but we use -nostdlib and
 * explicit -mexec-model=reactor (or none) — main stays as `main`,
 * gets exported, and our call here resolves within the same module.
 * We declare it as a regular extern (NO import attributes) so wasm-ld
 * resolves it locally rather than auto-importing as
 * `env.main` / `env.__main_argc_argv`. */
__attribute__((import_module("env"), import_name("__yos_argc")))
int   __yos_argc(void);
__attribute__((import_module("env"), import_name("__yos_argv_setup")))
void  __yos_argv_setup(char **argv);
__attribute__((import_module("env"), import_name("__yos_envc")))
int   __yos_envc(void);
__attribute__((import_module("env"), import_name("__yos_envp_setup")))
void  __yos_envp_setup(char **envp);
__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn))
void  exit(int);

extern int main(int argc, char **argv);

extern void __wasm_call_ctors(void) __attribute__((weak));

#define YOS_ENVP_MAX 256
static char *yos_envp_storage[YOS_ENVP_MAX + 1];
char **environ = yos_envp_storage;

void _start(void) {
    if (__wasm_call_ctors) __wasm_call_ctors();
    int argc = __yos_argc();
    char *argv[65] = {0};
    if (argc > 64) argc = 64;
    __yos_argv_setup(argv);

    int envc = __yos_envc();
    if (envc > YOS_ENVP_MAX) envc = YOS_ENVP_MAX;
    if (envc > 0) __yos_envp_setup(yos_envp_storage);
    yos_envp_storage[envc] = 0;

    int rc = main(argc, argv);
    exit(rc);
}
"""


def _default_wasm_clang() -> str:
    here = Path(__file__).resolve().parent.parent
    if os.name == 'nt':
        cand = here / 'wasm-clang.cmd'
    else:
        cand = here / 'wasm-clang'
    return os.environ.get('YOS_WASM_CLANG', str(cand))


def _run(cmd: list[str], desc: str) -> None:
    rc = subprocess.run(cmd).returncode
    if rc != 0:
        raise SystemExit(f'[skel] {desc} failed (rc={rc}): {cmd}')


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(f'usage: {argv[0]} <freebsd_root> <stamp> <sysroot_out>', file=sys.stderr)
        return 2

    freebsd_in   = Path(argv[1]).resolve()
    stamp_path   = Path(argv[2])
    sysroot_out  = Path(argv[3]).resolve()

    wasm_clang = _default_wasm_clang()
    llvm_ar    = os.environ.get('YOS_LLVM_AR', 'llvm-ar')

    (sysroot_out / 'usr').mkdir(parents=True, exist_ok=True)
    _make_link(freebsd_in / 'usr' / 'include',
               sysroot_out / 'usr' / 'include')

    lib_dir = sysroot_out / 'usr' / 'lib'
    lib_dir.mkdir(parents=True, exist_ok=True)

    empty_c = lib_dir / '.empty.c'
    empty_o = lib_dir / '.empty.o'
    empty_c.write_text('')
    _run(
        [wasm_clang, '-target', 'wasm32-unknown-unknown', '-nostdlib',
         '-c', str(empty_c), '-o', str(empty_o)],
        'compiling .empty.c',
    )

    yos_libc_init_c = (Path(__file__).resolve().parent.parent
                       / 'wasm-pkg' / 'configs' / 'nvim' / 'yos_libc_init.c')
    yos_libc_init_o = lib_dir / '.yos_libc_init.o'
    _run(
        [wasm_clang, '-target', 'wasm32-unknown-unknown', '-nostdlib', '-nostdinc',
         f'--sysroot={sysroot_out}',
         '-isystem', str(sysroot_out / 'usr' / 'include'),
         '-O2', '-c', str(yos_libc_init_c), '-o', str(yos_libc_init_o)],
        'compiling yos_libc_init.c',
    )

    for lib in ('dl', 'm', 'util', 'pthread', 'rt',
                'c++', 'cxx', 'anl', 'crypt', 'resolv'):
        out = lib_dir / f'lib{lib}.a'
        if out.exists():
            out.unlink()
        _run([llvm_ar, 'rcs', str(out), str(empty_o)],
             f'creating lib{lib}.a')

    libc_a = lib_dir / 'libc.a'
    if libc_a.exists():
        libc_a.unlink()
    _run([llvm_ar, 'rcs', str(libc_a), str(empty_o), str(yos_libc_init_o)],
         'creating libc.a')

    empty_c.unlink(missing_ok=True)

    crt1_c = lib_dir / '.crt1.c'
    crt1_o = lib_dir / 'crt1.o'
    crt1_c.write_text(CRT1_C)
    _run(
        [wasm_clang, '-target', 'wasm32-unknown-unknown', '-nostdlib', '-O2',
         '-c', str(crt1_c), '-o', str(crt1_o)],
        'compiling crt1',
    )
    crt1_c.unlink(missing_ok=True)

    stamp_path.parent.mkdir(parents=True, exist_ok=True)
    stamp_path.write_text('ok\n')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
