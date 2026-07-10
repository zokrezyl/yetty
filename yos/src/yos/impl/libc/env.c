/* impl/env.c — getenv / setenv / unsetenv / putenv / clearenv backed
 * by a per-ctx environment store kept in the wasm guest's linear
 * memory.
 *
 * The catch: getenv returns `char *`, which the guest must be able
 * to dereference. Returning a HOST pointer (to glibc's internal
 * environ table) doesn't work — guest's `ctx->memory + offset` model
 * has no idea where the host's heap lives. So we keep the env strings
 * INSIDE the wasm linear memory: an array of (name=value) strings
 * malloc'd via yos_malloc (which lives in the guest's mimalloc
 * arena). yos_setenv copies the new strings there, yos_getenv looks
 * up by name and returns the cached offset.
 *
 * Start state: at first getenv/setenv we walk the host `environ` and
 * copy every entry into the wasm side, so the guest sees the same
 * environment yos was started with.
 *
 * Layout: a parallel array of {name_off, value_off} entries; both
 * offsets point into the wasm linear memory and are stable until
 * the entry is unset / clearenv is called. This is enough for the
 * FreeBSD clearenv test, the simple plus-pop pattern nvim uses, and
 * most other programs.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/errno_helpers.h"

extern uint32_t yos_malloc(struct yos_exec_ctx *ctx, uint32_t size);
extern void     yos_free  (struct yos_exec_ctx *ctx, uint32_t off);
extern char   **environ;

/* The env store + YOS_ENV_MAX live on `struct yos_exec_ctx` (see
 * types.h::env_store). Fork copies it into the child ctx; per-ctx
 * storage is what lets parent and child have independent setenv
 * results. */

/* Copy a host string into the wasm linear memory via yos_malloc.
 * Returns the wasm offset, or 0 on failure. */
static uint32_t copy_to_wasm(struct yos_exec_ctx *ctx, const char *s)
{
    if (!s) return 0;
    size_t n = strlen(s) + 1;
    uint32_t off = yos_malloc(ctx, (uint32_t)n);
    if (!off) return 0;
    memcpy(ctx->memory + off, s, n);
    return off;
}

static int find_entry(struct yos_exec_ctx *ctx, const char *name)
{
    size_t nlen = strlen(name);
    for (int i = 0; i < ctx->env_store.count; i++) {
        if (ctx->env_store.e[i].name_off == 0) continue;
        if (ctx->env_store.e[i].name_len != nlen) continue;
        if (memcmp(ctx->memory + ctx->env_store.e[i].name_off, name, nlen) == 0)
            return i;
    }
    return -1;
}

/* Walk an environ-style char** vector and copy entries into wasm.
 * Used by env_init_once with EITHER the per-ctx exec envp (preferred
 * when an exec'd wasm process inherited a curated env) OR the host's
 * environ (for the first/root process). */
static void env_load_from_vec(struct yos_exec_ctx *ctx, char **vec)
{
    if (!vec) return;
    for (char **p = vec; *p && ctx->env_store.count < YOS_ENV_MAX; p++) {
        const char *eq = strchr(*p, '=');
        if (!eq) continue;
        size_t nlen = (size_t)(eq - *p);
        char nbuf[256];
        if (nlen >= sizeof nbuf) continue;
        memcpy(nbuf, *p, nlen);
        nbuf[nlen] = 0;
        uint32_t name_off  = copy_to_wasm(ctx, nbuf);
        uint32_t value_off = copy_to_wasm(ctx, eq + 1);
        if (!name_off || !value_off) continue;
        ctx->env_store.e[ctx->env_store.count].name_off  = name_off;
        ctx->env_store.e[ctx->env_store.count].value_off = value_off;
        ctx->env_store.e[ctx->env_store.count].name_len  = (uint32_t)nlen;
        ctx->env_store.count++;
    }
}

static void env_load_from_host(struct yos_exec_ctx *ctx)
{
    env_load_from_vec(ctx, environ);
}

/* Pick the right source. Per-ctx envp (set by execve / fork) wins
 * over host environ — after a wasm-side execve, host environ still
 * reflects the yos host process's env, but the new wasm program
 * inherited a curated envp from its parent that we need to honour
 * (PATH, PWD, USER, TERM, etc. that telnetd and friends set). */
static void env_init_once(struct yos_exec_ctx *ctx)
{
    if (ctx->env_store.initialised) return;
    ctx->env_store.initialised = 1;
    if (ctx && ctx->envp && ctx->envc > 0) {
        env_load_from_vec(ctx, ctx->envp);
    } else {
        env_load_from_host(ctx);
    }
}

/* Called from the execve flow when the wasm guest replaces its
 * module: the wasm linear memory is fresh, so every wasm offset we
 * cached in ctx->env_store (name_off / value_off) is now stale.
 * Wipe the cache; the next getenv/setenv re-loads from the new
 * ctx's envp. Without this, zsh inheriting a parent telnetd's env
 * reads back garbage and traps deep inside its param-table init. */
void yos_env_post_execve_reset(struct yos_exec_ctx *ctx)
{
    /* DON'T yos_free here: the offsets are into the OLD wasm memory
     * which has already been freed by m3_FreeRuntime. The allocator
     * lives inside guest memory; once that memory blob is gone, so
     * is the allocator state. Just zero the table. */
    memset(&ctx->env_store, 0, sizeof ctx->env_store);
}

/* Test-only: drop all known entries and re-pull from host environ.
 * Bound as `env.__yos_env_reload` — atf-runner-style forks would
 * give each test case a fresh env; we approximate by reloading. */
void yos_env_reload(struct yos_exec_ctx *ctx)
{
    /* Free the wasm-side string buffers we allocated. */
    for (int i = 0; i < ctx->env_store.count; i++) {
        if (ctx->env_store.e[i].name_off)  yos_free(ctx, ctx->env_store.e[i].name_off);
        if (ctx->env_store.e[i].value_off) yos_free(ctx, ctx->env_store.e[i].value_off);
    }
    memset(&ctx->env_store, 0, sizeof ctx->env_store);
    ctx->env_store.initialised = 1;
    env_load_from_host(ctx);
}

uint32_t yos_getenv(struct yos_exec_ctx *ctx, uint32_t name_off)
{
    env_init_once(ctx);
    if (!name_off || name_off >= ctx->memory_size) return 0;
    const char *name = (const char *)(ctx->memory + name_off);
    int idx = find_entry(ctx, name);
    ydebug("getenv(\"%s\") -> %s (idx=%d)\n", name,
           idx >= 0 ? "found" : "NULL", idx);
    if (idx < 0) return 0;
    return ctx->env_store.e[idx].value_off;
}

int32_t yos_setenv(struct yos_exec_ctx *ctx, uint32_t name_off,
                   uint32_t value_off, int32_t overwrite)
{
    env_init_once(ctx);
    if (!name_off || !value_off ||
        name_off >= ctx->memory_size || value_off >= ctx->memory_size)
        return yos_errno_neg(ctx, EINVAL);

    const char *name  = (const char *)(ctx->memory + name_off);
    const char *value = (const char *)(ctx->memory + value_off);
    if (!*name || strchr(name, '=') != NULL)
        return yos_errno_neg(ctx, EINVAL);

    /* YTRACE_* env vars take effect immediately on the host trace
     * system, so the guest-side `ytrace` wrapper (a tiny setenv +
     * execvp wasm program) can flip tracing on for the child it's
     * about to exec. Without this, setenv() only updates the
     * per-process g_env store and the next exec'd child inherits
     * it for its own getenv() lookups — but the HOST'S ytrace
     * registry (process-wide, not per-ctx) never learns about the
     * change because that lives outside the guest's env model.
     * Per-ctx env is otherwise normal; only these tracing knobs
     * also poke host state. */
    if (strcmp(name, "YTRACE_DEFAULT_ON") == 0) {
        extern void ytrace_set_all_enabled(bool);
        bool on = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0 ||
                   strcmp(value, "true") == 0);
        ytrace_set_all_enabled(on);
    } else if (strcmp(name, "YTRACE_FILE_PREFIX") == 0) {
        /* Propagate to host setenv so ytrace.c's per-thread file
         * re-open (driven by yos_ytrace_set_comm on each new comm)
         * picks up the new prefix. */
        setenv("YTRACE_FILE_PREFIX", value, 1);
    } else if (strcmp(name, "YPERF") == 0) {
        extern void yperf_set_enabled(bool);
        extern void yperf_dump_and_reset(void);
        if (strcmp(value, "stop") == 0) {
            /* "stop" → flush captured profile to YPERF_FILE and
             * disable recording. Used by the guest-side `yperf`
             * wrapper after wait()-ing for the child it was
             * profiling, so the dump is bounded to that child's
             * lifetime. The next setenv("YPERF","yes") begins a
             * fresh capture. */
            yperf_dump_and_reset();
        } else {
            bool on = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0 ||
                       strcmp(value, "true") == 0);
            yperf_set_enabled(on);
        }
    } else if (strcmp(name, "YPERF_FILE") == 0) {
        /* yperf reads YPERF_FILE at dump time via getenv, so push
         * to the host env so the eventual atexit-driven dump finds
         * it. Mirrors the YTRACE_FILE_PREFIX path above. */
        setenv("YPERF_FILE", value, 1);
    } else if (strcmp(name, "YPERF_RING_SIZE") == 0) {
        /* Read by yperf_init when each host thread allocates its
         * per-thread ring. Push to host env so the alloc picks the
         * caller's chosen capacity. Threads that have already
         * allocated keep their existing size — only NEW threads
         * (e.g. the fork'd child the wrapper is about to exec)
         * see the new value, which is exactly the per-app scope
         * we want. */
        setenv("YPERF_RING_SIZE", value, 1);
    }

    int idx = find_entry(ctx, name);
    if (idx >= 0) {
        if (!overwrite) return 0;
        /* Replace the value — keep the name slot, reallocate value. */
        uint32_t new_val = copy_to_wasm(ctx, value);
        if (!new_val) return yos_errno_neg(ctx, ENOMEM);
        if (ctx->env_store.e[idx].value_off)
            yos_free(ctx, ctx->env_store.e[idx].value_off);
        ctx->env_store.e[idx].value_off = new_val;
        return 0;
    }

    if (ctx->env_store.count >= YOS_ENV_MAX)
        return yos_errno_neg(ctx, ENOMEM);
    uint32_t nm = copy_to_wasm(ctx, name);
    uint32_t vl = copy_to_wasm(ctx, value);
    if (!nm || !vl) {
        if (nm) yos_free(ctx, nm);
        if (vl) yos_free(ctx, vl);
        return yos_errno_neg(ctx, ENOMEM);
    }
    ctx->env_store.e[ctx->env_store.count].name_off  = nm;
    ctx->env_store.e[ctx->env_store.count].value_off = vl;
    ctx->env_store.e[ctx->env_store.count].name_len  = (uint32_t)strlen(name);
    ctx->env_store.count++;
    return 0;
}

int32_t yos_unsetenv(struct yos_exec_ctx *ctx, uint32_t name_off)
{
    env_init_once(ctx);
    if (!name_off || name_off >= ctx->memory_size)
        return yos_errno_neg(ctx, EINVAL);
    const char *name = (const char *)(ctx->memory + name_off);
    if (!*name || strchr(name, '=') != NULL)
        return yos_errno_neg(ctx, EINVAL);
    int idx = find_entry(ctx, name);
    if (idx < 0) return 0;
    if (ctx->env_store.e[idx].name_off)  yos_free(ctx, ctx->env_store.e[idx].name_off);
    if (ctx->env_store.e[idx].value_off) yos_free(ctx, ctx->env_store.e[idx].value_off);
    /* Compact: copy last entry into this slot. */
    ctx->env_store.e[idx] = ctx->env_store.e[--ctx->env_store.count];
    ctx->env_store.e[ctx->env_store.count].name_off  = 0;
    ctx->env_store.e[ctx->env_store.count].value_off = 0;
    ctx->env_store.e[ctx->env_store.count].name_len  = 0;
    return 0;
}

int32_t yos_clearenv(struct yos_exec_ctx *ctx)
{
    env_init_once(ctx);
    for (int i = 0; i < ctx->env_store.count; i++) {
        if (ctx->env_store.e[i].name_off)  yos_free(ctx, ctx->env_store.e[i].name_off);
        if (ctx->env_store.e[i].value_off) yos_free(ctx, ctx->env_store.e[i].value_off);
        ctx->env_store.e[i].name_off  = 0;
        ctx->env_store.e[i].value_off = 0;
        ctx->env_store.e[i].name_len  = 0;
    }
    ctx->env_store.count = 0;
    return 0;
}

/* putenv("NAME=VALUE") — writes the input string into the env table.
 * POSIX warns the caller that putenv'd strings become part of the
 * environment (no copy); we copy because the wasm-side buffer
 * lifetime is unpredictable. */
int32_t yos_putenv(struct yos_exec_ctx *ctx, uint32_t s_off)
{
    env_init_once(ctx);
    if (!s_off || s_off >= ctx->memory_size)
        return yos_errno_neg(ctx, EINVAL);
    const char *s = (const char *)(ctx->memory + s_off);
    const char *eq = strchr(s, '=');
    if (!eq || eq == s)
        return yos_errno_neg(ctx, EINVAL);

    /* Re-do setenv with split name/value. */
    size_t nlen = (size_t)(eq - s);
    char nbuf[256];
    if (nlen >= sizeof nbuf)
        return yos_errno_neg(ctx, EINVAL);
    memcpy(nbuf, s, nlen);
    nbuf[nlen] = 0;
    /* Allocate a wasm-side copy of the name and the value to feed
     * setenv via the same offset path. */
    uint32_t name_off = copy_to_wasm(ctx, nbuf);
    uint32_t val_off  = copy_to_wasm(ctx, eq + 1);
    if (!name_off || !val_off) {
        if (name_off) yos_free(ctx, name_off);
        if (val_off)  yos_free(ctx, val_off);
        return yos_errno_neg(ctx, ENOMEM);
    }
    int32_t r = yos_setenv(ctx, name_off, val_off, 1);
    /* setenv copied the strings — free our temp duplicates. */
    yos_free(ctx, name_off);
    yos_free(ctx, val_off);
    return r;
}
