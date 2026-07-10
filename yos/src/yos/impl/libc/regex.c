/* impl/regex.c — POSIX regex bridge using a host-side handle table.
 *
 * Problem: the FreeBSD wasm32 regex_t and host glibc regex_t are
 * different layouts. The auto-bridge's "cast wasm offset to host
 * struct pointer" trick (which yos_bridge.c does for the rest of the
 * regex family) writes host pointers into wasm memory, then regexec
 * reads them back from the wrong layout and either crashes or
 * silently fails to match.
 *
 * Strategy: don't expose the host regex_t layout to the wasm guest at
 * all. regcomp allocates a slot in a host-side table, calls host
 * regcomp into the host regex_t there, and writes a (slot_index + 1)
 * handle into the first 4 bytes of the guest's regex_t buffer. The
 * +1 makes 0 mean "uninitialised" so a guest that zeros the struct
 * before passing it gets predictable REG_NOMATCH instead of stumbling
 * into a stale slot. regexec / regfree / regerror read the handle
 * back and resolve to the host regex_t for the real call.
 *
 * regmatch_t: FreeBSD = { int32_t rm_so; int32_t rm_eo; } = 8 bytes.
 * Glibc x86_64 = { regoff_t rm_so; regoff_t rm_eo; } where regoff_t
 * is `int` in the default config = also 8 bytes. We still copy field-
 * by-field rather than memcpy in case someone builds glibc with
 * REGOFF_BIG (regoff_t = int64) — that'd silently truncate.
 *
 * Thread-safety: the slot table is mutex-guarded so concurrent
 * pthread guests don't race on slot allocation / freeing.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <errno.h>
#include <pthread.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/errno_helpers.h"

#define YOS_REGEX_MAX 64

static struct {
    regex_t host;
    int     in_use;
} g_regex_tab[YOS_REGEX_MAX];
static pthread_mutex_t g_regex_lock = PTHREAD_MUTEX_INITIALIZER;

static int regex_slot_alloc(void)
{
    pthread_mutex_lock(&g_regex_lock);
    for (int i = 0; i < YOS_REGEX_MAX; i++) {
        if (!g_regex_tab[i].in_use) {
            g_regex_tab[i].in_use = 1;
            pthread_mutex_unlock(&g_regex_lock);
            return i;
        }
    }
    pthread_mutex_unlock(&g_regex_lock);
    return -1;
}

static void regex_slot_free(int idx)
{
    if (idx < 0 || idx >= YOS_REGEX_MAX) return;
    pthread_mutex_lock(&g_regex_lock);
    if (g_regex_tab[idx].in_use) {
        regfree(&g_regex_tab[idx].host);
        g_regex_tab[idx].in_use = 0;
    }
    pthread_mutex_unlock(&g_regex_lock);
}

/* Read the wasm-side handle out of preg's first 4 bytes. Returns -1
 * if out-of-bounds or the slot has been freed. */
static int regex_handle_to_slot(struct yos_exec_ctx *ctx, uint32_t preg)
{
    if (!preg || preg + 4 > ctx->memory_size) return -1;
    uint32_t handle = *(uint32_t *)(ctx->memory + preg);
    if (handle == 0 || handle > YOS_REGEX_MAX) return -1;
    int idx = (int)handle - 1;
    if (!g_regex_tab[idx].in_use) return -1;
    return idx;
}

int32_t yos_regcomp(struct yos_exec_ctx *ctx, uint32_t preg,
                    uint32_t pattern, int32_t cflags)
{
    if (!preg || preg + 4 > ctx->memory_size) return REG_BADPAT;
    if (!pattern || pattern >= ctx->memory_size) return REG_BADPAT;

    int idx = regex_slot_alloc();
    if (idx < 0) return REG_ESPACE;

    const char *pat = (const char *)(ctx->memory + pattern);
    int rc = regcomp(&g_regex_tab[idx].host, pat, cflags);
    if (rc != 0) {
        /* host regcomp failed before any allocation it'd have to undo,
         * so plain slot release is safe (no regfree call needed). */
        pthread_mutex_lock(&g_regex_lock);
        g_regex_tab[idx].in_use = 0;
        pthread_mutex_unlock(&g_regex_lock);
        return rc;
    }
    *(uint32_t *)(ctx->memory + preg) = (uint32_t)(idx + 1);
    ydebug("regcomp(\"%s\", cflags=%d) -> slot %d\n", pat, cflags, idx);
    return 0;
}

int32_t yos_regexec(struct yos_exec_ctx *ctx, uint32_t preg, uint32_t str,
                    uint32_t nmatch, uint32_t pmatch_off, int32_t eflags)
{
    int idx = regex_handle_to_slot(ctx, preg);
    if (idx < 0) return REG_NOMATCH;
    if (!str || str >= ctx->memory_size) return REG_NOMATCH;

    const char *s = (const char *)(ctx->memory + str);

    regmatch_t *host_matches = NULL;
    if (nmatch > 0 && pmatch_off) {
        if ((uint64_t)pmatch_off + (uint64_t)nmatch * 8u > ctx->memory_size)
            return REG_ESPACE;
        host_matches = calloc(nmatch, sizeof(regmatch_t));
        if (!host_matches) return REG_ESPACE;
    }

    int rc = regexec(&g_regex_tab[idx].host, s, nmatch, host_matches, eflags);

    if (rc == 0 && host_matches && pmatch_off) {
        uint8_t *w = ctx->memory + pmatch_off;
        for (uint32_t i = 0; i < nmatch; i++) {
            int32_t so = (int32_t)host_matches[i].rm_so;
            int32_t eo = (int32_t)host_matches[i].rm_eo;
            memcpy(w + i*8 + 0, &so, 4);
            memcpy(w + i*8 + 4, &eo, 4);
        }
    }
    free(host_matches);
    return rc;
}

uint32_t yos_regerror(struct yos_exec_ctx *ctx, int32_t errcode,
                      uint32_t preg, uint32_t errbuf_off,
                      uint32_t errbuf_size)
{
    /* preg can resolve to a slot, or be NULL — POSIX says regerror
     * tolerates a NULL preg (the error string is still useful even
     * without the compiled regex's context). */
    int idx = regex_handle_to_slot(ctx, preg);
    regex_t *host_preg = (idx >= 0) ? &g_regex_tab[idx].host : NULL;

    /* Compute the full message length first using a NULL/0 buffer —
     * POSIX regerror returns the size needed including the trailing
     * NUL, regardless of errbuf size. */
    size_t needed = regerror((int)errcode, host_preg, NULL, 0);

    if (errbuf_off && errbuf_size > 0) {
        if ((uint64_t)errbuf_off + (uint64_t)errbuf_size > ctx->memory_size)
            return (uint32_t)needed;
        regerror((int)errcode, host_preg,
                 (char *)(ctx->memory + errbuf_off), errbuf_size);
    }
    return (uint32_t)needed;
}

void yos_regfree(struct yos_exec_ctx *ctx, uint32_t preg)
{
    int idx = regex_handle_to_slot(ctx, preg);
    if (idx < 0) return;
    regex_slot_free(idx);
    /* Clear the handle word so the guest doesn't accidentally reuse
     * a stale slot pointer. */
    *(uint32_t *)(ctx->memory + preg) = 0;
}
