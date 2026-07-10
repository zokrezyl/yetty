#ifndef YOS_MAIN_INTERNAL_H
#define YOS_MAIN_INTERNAL_H

/* impl/main-internal.h — shared declarations between main.c and the
 * per-platform main-<os>.c slices. Not part of the yos public API. */

#include <stdint.h>
#include <stdatomic.h>
#include <sys/types.h>

#define YOS_BRG_RING 1024
struct yos_brg_rec {
    const char *name;
    pid_t       tid;
    uint64_t    seq;
    uint64_t    args[4];
};
extern struct yos_brg_rec yos_brg_ring[YOS_BRG_RING];
extern _Atomic uint64_t   yos_brg_ring_seq;
extern const char        *yos_brg_last_call;

/* Per-platform host-signal infrastructure. Slices live in
 * impl/main-{macos,linux,darwin-app,windows}.c. The "signal infra"
 * call covers whatever per-OS startup wiring is needed to make
 * synchronous fault delivery + the host CRT well-behaved:
 *   - linux:   no-op (kernel delivers to BSD handlers directly)
 *   - macos:   install the Mach exception port
 *   - windows: calm the debug CRT (invalid-parameter + abort dialog)
 *              and WSAStartup
 *   - darwin-app: same Mach setup as macos */
void yos_main_install_altstack(void *sp, size_t sz);
void yos_main_install_signal_infra(void);

#endif /* YOS_MAIN_INTERNAL_H */
