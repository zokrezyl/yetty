#!/usr/bin/env bash
# skel.sh — set up the wasm sysroot directory layout.
#
# Args: $1 = path to FreeBSD i386 headers tree (.../usr/include lives under it)
#       $2 = path to the stamp file we touch on success
#
# Output: <build>/sysroot/usr/include -> symlink to FreeBSD's curated
# usr/include tree. Meson invokes us; <build> is meson.project_build_root().
set -euo pipefail

# Apple's /usr/bin/clang has no wasm32 backend; use the wasm-clang
# shim (honours $YOS_WASM_CLANG) so this works outside nix-develop too.
WASM_CLANG="${YOS_WASM_CLANG:-$(dirname "$0")/../wasm-clang}"

freebsd_in="$(readlink -f "$1")"   # absolutise — the symlink we make below
                                    # would otherwise be relative to the
                                    # sysroot dir and dangle.
stamp="$2"
sysroot_out="$3"
mkdir -p "$sysroot_out/usr"
rm -rf "$sysroot_out/usr/include"
ln -s "$freebsd_in/usr/include" "$sysroot_out/usr/include"

# Empty stub libraries for the libc components FreeBSD splits out of
# its main libc.a but every Linux build system asks for via -ldl /
# -lm / -lutil / -lpthread / -lrt. yos resolves all libc fns at
# wasm load time as `env.<name>` imports — there is no .a to link
# against. wasm-ld still wants the file to EXIST when it sees -lX,
# so we provide an empty archive: 0 objects, no symbols, links
# successfully and contributes nothing.
mkdir -p "$sysroot_out/usr/lib"
empty_o="$sysroot_out/usr/lib/.empty.o"
empty_c="$sysroot_out/usr/lib/.empty.c"
: > "$empty_c"
"$WASM_CLANG" -target wasm32-unknown-unknown -nostdlib -c "$empty_c" -o "$empty_o" \
    || { echo "skel.sh: clang failed compiling empty stub" >&2; exit 1; }

# yos_libc_init.o — FreeBSD's _DefaultRuneLocale ctype table, compiled
# from build-tools/wasm-pkg/configs/nvim/yos_libc_init.c. Every wasm
# binary built against this sysroot pulls it in via libc.a so isalpha,
# isspace, iscntrl, isdigit, … return the right answers for ASCII.
# Without it ssh's valid_hostname("yetty") rejects every char because
# the runetype table reads as 0xff (uninit memory) → isspace('y')→true.
yos_libc_init_o="$sysroot_out/usr/lib/.yos_libc_init.o"
yos_libc_init_c="$(dirname "$0")/../wasm-pkg/configs/nvim/yos_libc_init.c"
"$WASM_CLANG" -target wasm32-unknown-unknown -nostdlib -nostdinc \
      --sysroot="$sysroot_out" \
      -isystem "$sysroot_out/usr/include" \
      -O2 -c "$yos_libc_init_c" -o "$yos_libc_init_o" \
    || { echo "skel.sh: clang failed compiling yos_libc_init.c" >&2; exit 1; }

for lib in dl m util pthread rt c++ cxx anl crypt resolv; do
    out="$sysroot_out/usr/lib/lib${lib}.a"
    rm -f "$out"
    llvm-ar rcs "$out" "$empty_o"
done
# libc.a additionally carries _DefaultRuneLocale / _CurrentRuneLocale.
rm -f "$sysroot_out/usr/lib/libc.a"
llvm-ar rcs "$sysroot_out/usr/lib/libc.a" "$empty_o" "$yos_libc_init_o"
rm -f "$empty_c"


# Minimal crt1 — provides `_start` calling user's `main(argc, argv)`.
# yos's host runtime invokes `_start` after binding imports. We pull
# argc/argv via the env.__yos_argc / __yos_argv_setup helpers main.c
# already binds, then call user's main(), and call env.exit() with
# its return.
crt1_c="$sysroot_out/usr/lib/.crt1.c"
crt1_o="$sysroot_out/usr/lib/crt1.o"
cat > "$crt1_c" <<'EOF'
/* crt1 — yos-flavoured. Called by yos at module load.
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

/* wasm-ld synthesises __wasm_call_ctors that runs every function with
 * __attribute__((constructor)) — and any C++ static-init code. crt1
 * is the only thing that calls it; without this dispatch the FreeBSD
 * ctype init in yos_libc_init.c never runs and isalpha() is always 0,
 * which makes Lua's tokenizer reject every letter as an unknown
 * symbol. Weak so binaries that don't have any ctors still link. */
extern void __wasm_call_ctors(void) __attribute__((weak));

/* Process-wide environment vector. zsh, FreeBSD coreutils, and most
 * libc-using programs reference `extern char **environ` and walk it
 * directly during init to import inherited env vars (PATH, HOME, …).
 * Without a definition the link uses --allow-undefined and zsh sees
 * environ=NULL → walks 0 entries → falls back to a hard-coded default
 * PATH (`/bin:/usr/bin:/usr/ucb:/usr/local/bin`) instead of the
 * caller's. The storage is sized for typical shells; __yos_envp_setup
 * fills it. zsh has been observed with up to ~40 inherited env vars,
 * 256 covers everything we've seen. */
#define YOS_ENVP_MAX 256
static char *yos_envp_storage[YOS_ENVP_MAX + 1];
char **environ = yos_envp_storage;

void _start(void) {
    if (__wasm_call_ctors) __wasm_call_ctors();
    int argc = __yos_argc();
    char *argv[65] = {0};
    if (argc > 64) argc = 64;
    __yos_argv_setup(argv);

    /* Pull host env into the wasm guest BEFORE main runs — zsh's
     * createparamtable() walks `environ` directly during early init.
     * envp_setup writes wasm-offset pointers into yos_envp_storage and
     * NULL-terminates at index envc. */
    int envc = __yos_envc();
    if (envc > YOS_ENVP_MAX) envc = YOS_ENVP_MAX;
    if (envc > 0) __yos_envp_setup(yos_envp_storage);
    yos_envp_storage[envc] = 0;

    int rc = main(argc, argv);
    exit(rc);
}
EOF
"$WASM_CLANG" -target wasm32-unknown-unknown -nostdlib -O2 -c "$crt1_c" -o "$crt1_o" \
    || { echo "skel.sh: clang failed compiling crt1" >&2; exit 1; }
rm -f "$crt1_c"

date > "$stamp"
