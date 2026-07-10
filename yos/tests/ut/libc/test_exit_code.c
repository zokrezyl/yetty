/*
 * test_exit_code.c — exit code propagates from guest to host.
 *
 * WHAT this verifies:
 *   The guest calls _exit(N) via env.exit; the bridge calls host
 *   _exit(N); the host yos process exits with status N visible to
 *   the test runner.
 *
 * WHY this matters:
 *   Exit-code propagation is the test infrastructure's primary
 *   pass/fail signal. Without it, every test would have to parse
 *   stdout for assertion strings and we'd lose the cleanest
 *   pass/fail bit. Pin it explicitly with a non-trivial code (42)
 *   so a "always exits 0" regression jumps out.
 *
 * Expected: exit 42 (not 0).
 */

__attribute__((import_module("env"), import_name("exit")))
__attribute__((noreturn))
void _exit(int);

void _start(void) {
    _exit(42);
}
