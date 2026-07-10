/*
 * test_generated_imports.c — pin the auto-generated yos_imports.h.
 *
 * WHAT this verifies:
 *   The bridge generator emits a C header full of import-decorated
 *   function redeclarations (one per "compatible" passthrough libc
 *   function). Including yos_imports.h alone — no hand-written
 *   import-attribute decls, no FreeBSD `unistd.h` — must give us
 *   `write` and `_exit` as wasm imports the program can call. If
 *   the generated declarations have bad types or the macro
 *   expansions go wrong, this test won't compile or won't run.
 *
 * WHY this matters:
 *   This is the codegen's user-facing artefact. The whole point of
 *   the pipeline is that an app `#include`s ONE header and calls
 *   libc functions normally; clang turns each call into an env
 *   import. If anything in the emitter regresses (wrong type
 *   spelling, missing function, attribute formatting bug), the
 *   header stops being usable and we fall back to hand-rolled
 *   declarations. That's the failure mode this test pins.
 *
 * Expected: exit 0, stdout contains "generated imports work".
 */

#include "yos_imports.h"

/* yos_imports.h gives us `int write(int, const void const *, unsigned int)`
 * (canonical-typed, redeclaration-merge-friendly) and `void _exit(int)`. */

void _start(void) {
    static const char m[] = "generated imports work\n";
    write(1, m, sizeof(m) - 1);
    _exit(0);
}
