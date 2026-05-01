/*
 * yplatform/webasm/ycoroutine.c - Degenerate coroutine stub for emscripten.
 *
 * Why this exists:
 *   Callers (e.g. src/yrender-utils/tile-diff.c) wrap a GPU readback in
 *   yplatform_coro_spawn + yplatform_coro_resume. On desktop those are
 *   real stackful coroutines (yplatform/shared/ycoroutine.c, libco-backed)
 *   so the _await wrapper can yield back to the event loop while a wgpu
 *   callback is pending.
 *
 *   On webasm we don't need a stack switch at all: the wgpu _await
 *   wrappers (yplatform/webasm/ywebgpu.c) suspend the entire C call stack
 *   via Asyncify (emscripten_sleep). The browser's JS event loop pumps
 *   wgpu callbacks for free during that suspension. So "spawn + resume"
 *   collapses into "run the entry function inline"; the asyncify-suspend
 *   inside the await is what gives the JS loop air to breathe.
 *
 * Effect at the call site:
 *   yplatform_coro_resume blocks (asyncify-suspended) until the entry
 *   function returns. yplatform_coro_is_finished() is therefore always
 *   true after resume returns, and the caller's "if finished, destroy"
 *   branch always runs. Functionally identical to desktop for the GPU
 *   readback case, no fibers, no separate stack.
 *
 *   yplatform_coro_yield() is unused on this platform — the _await
 *   wrappers suspend via emscripten_sleep directly, not via yield. Kept
 *   as a warn-only no-op so a misuse (a non-wgpu caller wiring up yield)
 *   shows up in traces instead of silently breaking.
 */

#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct yetty_yplatform_coro {
    yplatform_coro_entry entry;
    void *arg;
    char *name;
    unsigned int id;
    int status;
    int finished;
};

static struct yetty_yplatform_coro *g_current = NULL;
static atomic_uint g_next_id = 1;

struct yplatform_coro_ptr_result yetty_yplatform_coro_spawn(yplatform_coro_entry entry, void *arg,
                                                      size_t stack_hint, const char *name)
{
    (void)stack_hint;

    if (!entry) {
        return YETTY_ERR(yplatform_coro_ptr, "entry is NULL");
    }

    struct yetty_yplatform_coro *coro = calloc(1, sizeof(struct yetty_yplatform_coro));
    if (!coro) {
        return YETTY_ERR(yplatform_coro_ptr, "calloc failed");
    }

    coro->entry = entry;
    coro->arg = arg;
    coro->id = atomic_fetch_add(&g_next_id, 1);
    if (name) {
        coro->name = strdup(name);
        if (!coro->name) {
            free(coro);
            return YETTY_ERR(yplatform_coro_ptr, "strdup failed");
        }
    }

    ydebug("coro spawn id=%u name=%s (webasm stub)", coro->id,
           coro->name ? coro->name : "(anon)");
    return YETTY_OK(yplatform_coro_ptr, coro);
}

void yetty_yplatform_coro_resume(struct yetty_yplatform_coro *coro)
{
    if (!coro || coro->finished) {
        return;
    }

    struct yetty_yplatform_coro *prev = g_current;
    g_current = coro;
    ydebug("resume coro %u (%s) — runs inline on webasm", coro->id,
           coro->name ? coro->name : "(anon)");
    coro->entry(coro->arg);
    coro->finished = 1;
    g_current = prev;
    ydebug("coro %u finished (webasm)", coro->id);
}

void yetty_yplatform_coro_yield(void)
{
    /* On webasm the _await wrappers suspend via emscripten_sleep. yield
     * shouldn't be reachable from any code path that's expected to work
     * on web. */
    ywarn("yplatform_coro_yield called on webasm — no-op");
}

void yetty_yplatform_coro_destroy(struct yetty_yplatform_coro *coro)
{
    if (!coro) {
        return;
    }
    ydebug("coro destroy id=%u name=%s finished=%d", coro->id,
           coro->name ? coro->name : "(anon)", coro->finished);
    free(coro->name);
    free(coro);
}

struct yetty_yplatform_coro *yetty_yplatform_coro_current(void)
{
    return g_current;
}

int yetty_yplatform_coro_is_finished(const struct yetty_yplatform_coro *coro)
{
    return coro ? coro->finished : 1;
}

unsigned int yetty_yplatform_coro_id(const struct yetty_yplatform_coro *coro)
{
    return coro ? coro->id : 0;
}

const char *yetty_yplatform_coro_name(const struct yetty_yplatform_coro *coro)
{
    return coro && coro->name ? coro->name : "(anon)";
}

void yetty_yplatform_coro_set_status(struct yetty_yplatform_coro *coro, int status)
{
    if (coro) {
        coro->status = status;
    }
}

int yetty_yplatform_coro_get_status(const struct yetty_yplatform_coro *coro)
{
    return coro ? coro->status : 0;
}
