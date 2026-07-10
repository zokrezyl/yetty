#!/usr/bin/env bash
# python-driver — tiny wasm guest that drives a HOST-linked libpython
# through yos's env.Py* bridges. PoC for the "library-in-host" architecture
# (vs the "compile-CPython-to-wasm" approach we abandoned because the
# whole interpreter cross-compile is huge and slow; here we link real
# libpython3.12 into the yos host process and ship a ~10KB wasm shim).
#
# What the wasm guest does:
#   int main(int argc, char **argv) {
#       Py_Initialize();
#       PyRun_SimpleString(argv[1] ?: "print('hello from libpython via yos')");
#       Py_Finalize();
#       return 0;
#   }
#
# All three Py_* are wasm imports resolved by yos at module-load time —
# the host bridge calls into libpython.so directly.

set -euo pipefail

NAME=python-driver
VERSION=0.1

: "${ROOT:?}"; : "${PREFIX:?}"; : "${WASM_CC:?}"; : "${WASM_SYSROOT:?}"
: "${WORK:=$ROOT/build-linux/wasm-pkgs/${NAME}-${VERSION}}"

mkdir -p "$WORK" "$PREFIX/bin"

# Inline guest source. Single .c file, no external deps.
SRC_C="$WORK/python_driver.c"
cat > "$SRC_C" <<'GUEST_EOF'
/* python_driver.c — wasm guest that drives HOST libpython via yos bridges.
 *
 * Each Py_* function is a wasm import. yos resolves it at module load
 * time by registering host-side m3ApiRawFunction wrappers under the
 * `env.Py_*` import names. The wrappers call into the real libpython.so
 * the yos binary is linked against, so this guest IS just a thin
 * driver around a native interpreter.
 */
#include <stddef.h>
#include <stdint.h>

/* Wasm guest-side declarations — clang -target wasm32 emits these
 * as env imports because of the import_module/import_name attrs. */
__attribute__((import_module("env"), import_name("Py_Initialize")))
void Py_Initialize(void);

__attribute__((import_module("env"), import_name("Py_Finalize")))
void Py_Finalize(void);

__attribute__((import_module("env"), import_name("PyRun_SimpleString")))
int PyRun_SimpleString(const char *command);

/* Tiny write to stderr/stdout fallback (so the driver itself can print
 * a banner before calling into Python). Reuses yos's standard env.write. */
__attribute__((import_module("env"), import_name("write")))
long write(int fd, const void *buf, unsigned long n);

static unsigned long _strlen(const char *s) {
    const char *p = s; while (*p) ++p; return (unsigned long)(p - s);
}

int main(int argc, char **argv) {
    const char *cmd = "print('hello from libpython via yos')";
    if (argc > 1 && argv[1] && argv[1][0]) cmd = argv[1];

    const char banner[] = "python-driver: starting libpython...\n";
    write(2, banner, sizeof(banner) - 1);

    Py_Initialize();
    int rc = PyRun_SimpleString(cmd);
    Py_Finalize();

    return rc;
}
GUEST_EOF

# Build the wasm. -nostdlib because there's no libc on this side — we
# only use the 3 Py_* imports + env.write. crt1.o handles _start/argc.
"$WASM_CC" \
    -target wasm32-unknown-unknown \
    -nostdlib \
    --sysroot="$WASM_SYSROOT" \
    -Wl,--no-entry \
    -Wl,--export=_start \
    -Wl,--export-all \
    -Wl,--allow-undefined \
    "$WASM_SYSROOT/usr/lib/crt1.o" \
    -O2 \
    -o "$WORK/python-driver.wasm" \
    "$SRC_C"

# Asyncify pass — same as zsh/nvim. Lets any future Py_* bridge that
# yields (network I/O, etc.) cooperate with yos's fork/setjmp pump.
wasm-opt --asyncify -O2 "$WORK/python-driver.wasm" -o "$PREFIX/bin/python-driver.wasm"

cat > "$PREFIX/manifest.txt" <<EOF
name=python-driver
version=${VERSION}
prefix=${PREFIX}
EOF

echo "[$NAME] built → $PREFIX/bin/python-driver.wasm"
