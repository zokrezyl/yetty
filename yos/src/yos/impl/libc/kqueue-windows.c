/* impl/libc/kqueue-windows.c — kqueue/kevent stubs on Windows.
 *
 * Windows has neither kqueue (BSD) nor epoll (Linux). A faithful
 * emulation would require IOCP (or the wepoll shim) plus quite a bit
 * of per-fd HANDLE bookkeeping; for the initial Windows port we ship
 * stubs that simply fail with ENOSYS so wasm guests fall back to
 * select-/poll-based loops. yos_kqueue_notify_exit is wired so
 * impl/proc.c can still call it unconditionally.
 *
 * NO #ifdef in this file — meson selects it only on windows hosts.
 */

#include <stdint.h>
#include <errno.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern int yos_remap_errno_h2g(int);

static inline void write_errno(struct yos_exec_ctx *ctx, int e)
{
    if (ctx && ctx->memory && ctx->errno_off) {
        *(int *)(ctx->memory + ctx->errno_off) = yos_remap_errno_h2g(e);
    }
}

void yos_kqueue_notify_exit(uint32_t pid)
{
    (void)pid;
}

static m3ApiRawFunction(m3_yos_kqueue)
{
    m3ApiReturnType(int32_t);
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    write_errno(ctx, ENOSYS);
    m3ApiReturn(-1);
}

static m3ApiRawFunction(m3_yos_kqueue1)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, flags); (void)flags;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    write_errno(ctx, ENOSYS);
    m3ApiReturn(-1);
}

static m3ApiRawFunction(m3_yos_kqueuex)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, flags); (void)flags;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    write_errno(ctx, ENOSYS);
    m3ApiReturn(-1);
}

static m3ApiRawFunction(m3_yos_kevent)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t,  kq_wfd);
    m3ApiGetArg(uint32_t, changelist);
    m3ApiGetArg(int32_t,  nchanges);
    m3ApiGetArg(uint32_t, eventlist);
    m3ApiGetArg(int32_t,  nevents);
    m3ApiGetArg(uint32_t, timeout_off);
    (void)kq_wfd; (void)changelist; (void)nchanges;
    (void)eventlist; (void)nevents; (void)timeout_off;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    write_errno(ctx, ENOSYS);
    m3ApiReturn(-1);
}

void yos_kqueue_link(IM3Module mod)
{
    m3_LinkRawFunction(mod, "env", "kqueue",  "i()",       m3_yos_kqueue);
    m3_LinkRawFunction(mod, "env", "kqueue1", "i(i)",      m3_yos_kqueue1);
    m3_LinkRawFunction(mod, "env", "kqueuex", "i(i)",      m3_yos_kqueuex);
    m3_LinkRawFunction(mod, "env", "kevent",  "i(iiiiii)", m3_yos_kevent);
}
