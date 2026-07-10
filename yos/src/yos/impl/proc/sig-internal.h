#ifndef YOS_SIG_INTERNAL_H
#define YOS_SIG_INTERNAL_H

/* impl/proc/sig-internal.h — helpers shared between sig.c and the
 * per-platform sig-<os>.c slices. Not part of the yos public API; not
 * visible outside impl/proc/. Each slice (sig-linux.c, sig-darwin.c,
 * etc.) implements the libc bridges whose body diverges per platform
 * and reaches into sig.c for the layout/numbering helpers via these
 * declarations. */

#include "yos/types.h"
#include <signal.h>
#include <stdint.h>

/* Bounds-check that `off` + sizeof(FreeBSD sigset_t = 16 B) fits in
 * wasm linear memory. Returns 1 if safe, 0 if the wasm offset would
 * walk off the end of memory. */
int yos_sigset_bound(struct yos_exec_ctx *ctx, uint32_t off);

/* FreeBSD signal number <-> host signal number. FreeBSD numbers match
 * Linux for the historical 1..31 set but diverge for SIGRTMIN+ etc.;
 * mapping table is in sig.c. Return 0 when the input doesn't map. */
int fbsd_to_host_signo(int fb);
int host_to_fbsd_signo(int h);

/* FreeBSD-shaped 16-byte sigset bitmap <-> host sigset_t (opaque, may
 * be much larger). The renderer iterates fbsd signums and sets the
 * corresponding host bits via sigaddset(). */
void fbsd_sigset_to_host(const uint8_t *fb, sigset_t *host);
void host_sigset_to_fbsd(const sigset_t *host, uint8_t *fb);

#endif /* YOS_SIG_INTERNAL_H */
