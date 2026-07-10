/* impl/libc_globals_link.c — bind per-ctx versions of libc functions
 * whose backing globals are shared across guests. This file is the
 * single place where m3_LinkRawFunction's race-to-register matters:
 * we win against the auto-bridge by linking BEFORE
 * yos_brg_link_imports runs.
 *
 * Adding a function here = corresponding entry in
 * build-tools/libbridge/policies/libc.yaml must be marked
 * `bridged_per_ctx via: impl/<file>.c`. The extractor's
 * --fail-on-leak catches the mismatch in CI.
 */

#include <stdint.h>
#include <stddef.h>

#include "wasm3.h"
#include "m3_env.h"
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

extern int32_t  yos_getopt(struct yos_exec_ctx *ctx,
                           int32_t argc, uint32_t argv_off,
                           uint32_t optstring_off);
extern void     yos_tzset (struct yos_exec_ctx *ctx);
extern uint32_t yos_getopt_optarg_get(struct yos_exec_ctx *);
extern int32_t  yos_getopt_optind_get(struct yos_exec_ctx *);
extern int32_t  yos_getopt_optopt_get(struct yos_exec_ctx *);
extern int32_t  yos_getopt_opterr_get(struct yos_exec_ctx *);
extern void     yos_getopt_optind_set(struct yos_exec_ctx *, int32_t);
extern void     yos_getopt_opterr_set(struct yos_exec_ctx *, int32_t);

/* env.getopt — int getopt(int argc, char *const argv[], const char *optstring).
 * Wasm ABI: i(i,i,i) → returns next option char or -1.
 * argv_off is the wasm-memory offset of the char* array; impl/getopt.c
 * walks per-element offsets out of the linear memory. */
static const void *m3_yos_getopt(IM3Runtime runtime, IM3ImportContext _ctx,
                                 uint64_t *_sp, void *_mem)
{
    (void)_ctx; (void)_mem;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    int32_t  argc           = (int32_t)_sp[1];
    uint32_t argv_off       = (uint32_t)_sp[2];
    uint32_t optstring_off  = (uint32_t)_sp[3];
    _sp[0] = (uint64_t)(uint32_t)yos_getopt(ctx, argc, argv_off, optstring_off);
    return NULL;
}

/* env.tzset — void tzset(void).
 * Wasm ABI: v() → no args, no return. Snapshots the ctx's TZ-induced
 * timezone state into ctx->tz_state instead of leaving host tzname/
 * timezone/daylight mutated for the next guest to see. */
static const void *m3_yos_tzset(IM3Runtime runtime, IM3ImportContext _ctx,
                                uint64_t *_sp, void *_mem)
{
    (void)_ctx; (void)_sp; (void)_mem;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    yos_tzset(ctx);
    return NULL;
}

/* Per-ctx accessors that getopt-compat shims in the guest call to
 * mirror yos's per-ctx optarg/optind/optopt/opterr into the guest's
 * own char* / int storage. yos's libc.a has no getopt body so the
 * guest can't read these globals directly — these imports are the
 * bridge. */
static const void *m3_yos_optarg_get(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    _sp[0] = (uint64_t)(uint32_t)yos_getopt_optarg_get(ctx);
    return NULL;
}
static const void *m3_yos_optind_get(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    _sp[0] = (uint64_t)(int64_t)yos_getopt_optind_get(ctx);
    return NULL;
}
static const void *m3_yos_optopt_get(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    _sp[0] = (uint64_t)(int64_t)yos_getopt_optopt_get(ctx);
    return NULL;
}
static const void *m3_yos_opterr_get(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    _sp[0] = (uint64_t)(int64_t)yos_getopt_opterr_get(ctx);
    return NULL;
}
static const void *m3_yos_optind_set(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    yos_getopt_optind_set(ctx, (int32_t)_sp[1]);
    return NULL;
}
static const void *m3_yos_opterr_set(IM3Runtime runtime, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    yos_getopt_opterr_set(ctx, (int32_t)_sp[1]);
    return NULL;
}

void yos_libc_globals_link(IM3Module mod)
{
    if (!mod) return;
    m3_LinkRawFunction(mod, "env", "getopt", "i(iii)", m3_yos_getopt);
    m3_LinkRawFunction(mod, "env", "tzset",  "v()",    m3_yos_tzset);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_optarg_get", "i()",  m3_yos_optarg_get);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_optind_get", "i()",  m3_yos_optind_get);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_optopt_get", "i()",  m3_yos_optopt_get);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_opterr_get", "i()",  m3_yos_opterr_get);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_optind_set", "v(i)", m3_yos_optind_set);
    m3_LinkRawFunction(mod, "env", "__yos_getopt_opterr_set", "v(i)", m3_yos_opterr_set);
}
