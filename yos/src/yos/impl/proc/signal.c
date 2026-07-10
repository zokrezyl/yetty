/* impl/signal.c — sigset_t-aware FreeBSD signal helpers.
 *
 * FreeBSD wasm32 sigset_t is `struct __sigset { __uint32_t __bits[4]; }`
 * — 16 bytes total, bit i = (__bits[i/32] & (1u << (i%32))).
 *
 * Linux sigset_t is `unsigned long [16]` — 128 bytes. Auto-generated
 * bridges that pass the FreeBSD-shape mask to host libc would read the
 * extra 112 bytes of nonsense and corrupt the host's masking state.
 *
 * We bypass that by implementing the API host-side directly on the
 * FreeBSD layout. pthread_sigmask / sigprocmask are no-ops (return 0
 * success) — nvim doesn't depend on real masking for startup. The
 * sig{empty,fill,add,del,ismember}set helpers manipulate the wasm-side
 * mask directly.
 *
 * Bound by main.c via yos_signal_link(). Listed in hooks.yaml's stub:
 * bucket so bridge.py doesn't emit conflicting passthroughs.
 */

#include <stdint.h>
#include <string.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"

#define FREEBSD_SIGSET_BYTES 16

static inline uint32_t *sigset_at(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (off == 0) return NULL;
    if (off + FREEBSD_SIGSET_BYTES > ctx->memory_size) return NULL;
    return (uint32_t *)(ctx->memory + off);
}

static m3ApiRawFunction(m3_yos_sigemptyset)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, set_off);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    uint32_t *s = sigset_at(ctx, set_off);
    if (!s) m3ApiReturn(-1);
    s[0] = s[1] = s[2] = s[3] = 0;
    m3ApiReturn(0);
}

static m3ApiRawFunction(m3_yos_sigfillset)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, set_off);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    uint32_t *s = sigset_at(ctx, set_off);
    if (!s) m3ApiReturn(-1);
    s[0] = s[1] = s[2] = s[3] = 0xFFFFFFFFu;
    m3ApiReturn(0);
}

static m3ApiRawFunction(m3_yos_sigaddset)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, set_off);
    m3ApiGetArg(int32_t,  signo);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    uint32_t *s = sigset_at(ctx, set_off);
    if (!s || signo < 1 || signo > 128) m3ApiReturn(-1);
    int bit = signo - 1;
    s[bit >> 5] |= (1u << (bit & 31));
    m3ApiReturn(0);
}

static m3ApiRawFunction(m3_yos_sigdelset)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, set_off);
    m3ApiGetArg(int32_t,  signo);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    uint32_t *s = sigset_at(ctx, set_off);
    if (!s || signo < 1 || signo > 128) m3ApiReturn(-1);
    int bit = signo - 1;
    s[bit >> 5] &= ~(1u << (bit & 31));
    m3ApiReturn(0);
}

static m3ApiRawFunction(m3_yos_sigismember)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, set_off);
    m3ApiGetArg(int32_t,  signo);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    uint32_t mem_size = 0;
    ctx->memory = m3_GetMemory(runtime, &mem_size, 0);
    ctx->memory_size = mem_size;

    uint32_t *s = sigset_at(ctx, set_off);
    if (!s || signo < 1 || signo > 128) m3ApiReturn(-1);
    int bit = signo - 1;
    m3ApiReturn((s[bit >> 5] >> (bit & 31)) & 1u);
}

void yos_signal_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "sigemptyset",   "i(i)",   m3_yos_sigemptyset);
    m3_LinkRawFunction(mod, "env", "sigfillset",    "i(i)",   m3_yos_sigfillset);
    m3_LinkRawFunction(mod, "env", "sigaddset",     "i(ii)",  m3_yos_sigaddset);
    m3_LinkRawFunction(mod, "env", "sigdelset",     "i(ii)",  m3_yos_sigdelset);
    m3_LinkRawFunction(mod, "env", "sigismember",   "i(ii)",  m3_yos_sigismember);
    /* pthread_sigmask, sigprocmask, sigpending, signal are bound via
     * the auto-generated bridges pointing at impl/sig.c. The old no-op
     * raw-function stubs that used to live here would otherwise win the
     * binding race and silently mask away the layout-aware impls. */
}
