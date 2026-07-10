/* impl/tz.c — per-ctx timezone state.
 *
 * Cross-guest leak fix for tzname/__tzname/timezone/__timezone/
 * daylight/__daylight listed in policies/libc.yaml `leaks:`. The
 * auto-generated yos_tzset calls host tzset() directly, which
 * mutates host-process-wide globals — guest A's `TZ=America/NY;
 * tzset()` corrupts guest B's `localtime()` output.
 *
 * Strategy: yos's per-ctx environ (impl/env.c) already isolates
 * the TZ value seen by each guest. This file owns the bridge for
 * tzset itself: instead of letting it propagate to host tzset, we
 * read the guest's TZ from ctx->envp, call host tzset() WITH host
 * TZ env temporarily set to the guest's value, snapshot the
 * resulting tzname/timezone/daylight into ctx->tz_state, and
 * restore host TZ. From then on the guest's "current timezone" is
 * the snapshot in its ctx slot; localtime/mktime/strftime bridges
 * swap the four host globals in from ctx around each call.
 *
 * This is the cheapest correct approach short of writing a full
 * timezone parser ourselves.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* Host's tzname/timezone/daylight access via the libc-provided
 * declarations in <time.h>. */
extern char *tzname[];
extern long  timezone;
extern int   daylight;

/* tzset() reads/writes shared host globals — serialize across all
 * yos worker threads so two guests' tzset() calls don't interleave
 * during the brief snapshot window. Fine-grained, only held for the
 * snapshot, no nested locks. */
static pthread_mutex_t _tz_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *_ctx_getenv(struct yos_exec_ctx *ctx, const char *name)
{
    extern uint32_t yos_getenv_offset(struct yos_exec_ctx *ctx, const char *name);
    /* Most yos builds expose per-ctx getenv via impl/env.c. If the
     * helper isn't available, fall back to NULL (uses host TZ which
     * is the worst-case but no-worse-than-status-quo). */
    /* We avoid linking to yos_getenv_offset directly — different
     * builds may name it differently. Walk the envp ourselves: it's
     * a NULL-terminated array of char* in ctx->envp. */
    if (!ctx) return NULL;
    char **envp = (char **)ctx->envp;
    if (!envp) return NULL;
    size_t nlen = strlen(name);
    for (int i = 0; envp[i]; i++) {
        const char *e = envp[i];
        if (strncmp(e, name, nlen) == 0 && e[nlen] == '=')
            return e + nlen + 1;
    }
    return NULL;
}

/* env.tzset — () → void.
 *
 * Snapshot the timezone induced by ctx's TZ into ctx->tz_state. The
 * tz state is then authoritative for subsequent localtime/mktime
 * calls bridged via tz_swap_in_for_ctx() (TODO: wire those bridges
 * to call us; for now this fixes the loud leak — yos_tzset stops
 * stomping on the host globals across guests). */
void yos_tzset(struct yos_exec_ctx *ctx)
{
    if (!ctx) return;

    const char *guest_tz = _ctx_getenv(ctx, "TZ");
    ydebug("tzset: ctx_tz='%s'\n", guest_tz ? guest_tz : "(unset)");

    pthread_mutex_lock(&_tz_lock);

    /* Snapshot host TZ so we can restore after. */
    const char *host_tz_saved = getenv("TZ");
    char *host_tz_saved_copy = host_tz_saved ? strdup(host_tz_saved) : NULL;

    if (guest_tz)
        setenv("TZ", guest_tz, /*overwrite=*/1);
    else
        unsetenv("TZ");

    tzset();   /* host call — only place we let it run */

    /* Snapshot effect into ctx. tzname[] entries are static
     * libc-internal strings, safe to strncpy. */
    if (tzname[0]) strncpy(ctx->tz_state.tzname0, tzname[0],
                           sizeof(ctx->tz_state.tzname0) - 1);
    else ctx->tz_state.tzname0[0] = '\0';
    if (tzname[1]) strncpy(ctx->tz_state.tzname1, tzname[1],
                           sizeof(ctx->tz_state.tzname1) - 1);
    else ctx->tz_state.tzname1[0] = '\0';
    ctx->tz_state.timezone    = timezone;
    ctx->tz_state.daylight    = daylight;
    ctx->tz_state.initialized = 1;

    /* Restore host TZ + re-run tzset to put the host back. */
    if (host_tz_saved_copy) {
        setenv("TZ", host_tz_saved_copy, 1);
        free(host_tz_saved_copy);
    } else {
        unsetenv("TZ");
    }
    tzset();

    pthread_mutex_unlock(&_tz_lock);

    ydebug("tzset: ctx snapshot: tz0='%s' tz1='%s' offset=%ld dst=%d\n",
           ctx->tz_state.tzname0, ctx->tz_state.tzname1,
           ctx->tz_state.timezone, ctx->tz_state.daylight);
}

/* env.__yos_tz_apply — host-internal helper bridges call before
 * localtime/mktime/etc. so the host tzname/timezone/daylight reflect
 * THIS guest's snapshot for the duration of one call. Caller invokes
 * yos_tz_apply_for_ctx(ctx) on entry, yos_tz_release(prev) on exit.
 *
 * For correctness across concurrent guests, we hold _tz_lock for
 * the full enter→host-call→exit window. Bridges that aren't yet
 * wired (most of <time.h>) continue to see host tzname — leaks
 * unchanged but at least tzset itself no longer thrashes. */
void *yos_tz_apply_for_ctx(struct yos_exec_ctx *ctx)
{
    if (!ctx || !ctx->tz_state.initialized) return NULL;
    pthread_mutex_lock(&_tz_lock);
    /* Save current host tz so we can restore — capture in a small
     * heap struct returned to the caller. */
    struct saved_tz {
        char  tzname0[64], tzname1[64];
        long  timezone;
        int   daylight;
        char  tz_env_saved[128];
        int   tz_env_was_set;
    };
    struct saved_tz *s = calloc(1, sizeof(*s));
    if (!s) { pthread_mutex_unlock(&_tz_lock); return NULL; }
    if (tzname[0]) strncpy(s->tzname0, tzname[0], sizeof(s->tzname0) - 1);
    if (tzname[1]) strncpy(s->tzname1, tzname[1], sizeof(s->tzname1) - 1);
    s->timezone = timezone;
    s->daylight = daylight;
    const char *cur = getenv("TZ");
    if (cur) {
        s->tz_env_was_set = 1;
        strncpy(s->tz_env_saved, cur, sizeof(s->tz_env_saved) - 1);
    }
    /* Apply ctx's tz: set TZ env to the guest's value, retzset. The
     * setenv path is forced because tzname[]/timezone/daylight are
     * derived from TZ env at tzset() time. */
    const char *guest_tz = _ctx_getenv(ctx, "TZ");
    if (guest_tz) setenv("TZ", guest_tz, 1); else unsetenv("TZ");
    tzset();
    return s;
}

void yos_tz_release(void *saved)
{
    struct saved_tz {
        char  tzname0[64], tzname1[64];
        long  timezone;
        int   daylight;
        char  tz_env_saved[128];
        int   tz_env_was_set;
    } *s = saved;
    if (!s) { pthread_mutex_unlock(&_tz_lock); return; }
    if (s->tz_env_was_set) setenv("TZ", s->tz_env_saved, 1);
    else unsetenv("TZ");
    tzset();
    free(s);
    pthread_mutex_unlock(&_tz_lock);
}
