/*
 * test_freebsd_headers.c — pin the "user app uses ONLY FreeBSD
 *                          headers" contract.
 *
 * WHAT this verifies:
 *   The user code includes nothing yos-specific — no yos_imports.h,
 *   no __attribute__((import_module(...))) anywhere — only stock
 *   FreeBSD-shape headers (<unistd.h>, <stdlib.h>). Each call to a
 *   libc fn becomes an env import via wasm-ld's --allow-undefined,
 *   which yos resolves through its bridges.
 *
 * WHY this matters:
 *   This is the user-facing contract. A program written for
 *   FreeBSD-libc compiles on yos's wasm sysroot and runs on yos's
 *   host with zero yos awareness. If this regresses (FreeBSD
 *   header gets a new asm directive, sysroot symlink breaks, our
 *   sys/cdefs.h patch needs another macro), this test is where
 *   we'd catch it before the next nvim-build attempt.
 *
 * Expected: exit 11, stdout contains "freebsd-headers ok".
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* strlen is also resolved as env.strlen — passthrough to host
     * libc. Confirms multiple imports survive in one TU. */
    static const char msg[] = "freebsd-headers ok\n";
    write(1, msg, strlen(msg));
    return 11;
}

/* yos's host doesn't run main() automatically — we expose _start. */
void _start(void) {
    exit(main());
}
