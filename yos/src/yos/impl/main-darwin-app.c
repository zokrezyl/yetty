/* impl/main-darwin-app.c — sandboxed Apple-app host signal stubs.
 *
 * iOS and tvOS app bundles run in a sandbox that forbids the Mach
 * exception APIs (`task_set_exception_ports`, `thread_set_exception_
 * ports`, `mach_msg`). sigaltstack is also marked unavailable on
 * tvOS-arm64 (and iOS-arm64 in some SDK versions), so we don't call
 * it here either — the OS provides its own crash-reporting path.
 *
 * Both entry points are stubs; the BSD signal handler that main.c
 * still installs catches whatever the OS does forward.
 *
 * NO #ifdef inside this file — meson selects it when ios_unblock or
 * tvos_unblock is set. macOS uses main-macos.c.
 */

#include <stddef.h>

void yos_main_install_altstack(void *sp, size_t sz)
{
    (void)sp;
    (void)sz;
}

void yos_main_install_signal_infra(void)
{
    /* No Mach exception ports inside sandboxed Apple apps — host
     * delivers SIGSEGV/SIGBUS via the regular BSD signal path. */
}
