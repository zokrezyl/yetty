/*
 * test_redecl_merge.c — pin the codegen's foundational mechanism.
 *
 * WHAT this verifies:
 *   When the FreeBSD-shape header (or any plain declaration) declares
 *   `ssize_t write(int, const void *, size_t);` and a SECOND
 *   declaration with import attributes follows:
 *     __attribute__((import_module("env"), import_name("write")))
 *     ssize_t write(int, const void *, size_t);
 *   clang must merge the two and apply the import attributes to all
 *   call sites. The resulting .wasm must contain
 *   `(import "env" "write" ...)` and call into it.
 *
 * WHY this matters:
 *   yos's guest libc strategy relies on exactly this. The codegen
 *   does NOT rewrite FreeBSD's headers; it emits a SUPPLEMENTAL
 *   header (yos_imports.h) with import-decorated redeclarations of
 *   each function. The two declarations share a translation unit;
 *   clang must merge them. If this ever stops working — clang
 *   tightening redeclaration rules, or wasm-ld dropping decorated
 *   second decls — the entire sysroot strategy collapses. This test
 *   is the canary.
 *
 * Expected: exit 0, stdout contains "redecl merged ok".
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

/* (1) FreeBSD-shape plain declaration. */
ssize_t write(int fd, const void *buf, size_t n);

/* (2) Codegen-emitted redeclaration with import attributes. */
__attribute__((import_module("env"), import_name("write")))
ssize_t write(int fd, const void *buf, size_t n);

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn))
void _exit(int);

void _start(void) {
    static const char m[] = "redecl merged ok\n";
    write(1, m, sizeof(m) - 1);
    _exit(0);
}
