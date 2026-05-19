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
 *   On webasm we don't have separate stacks, but we still need yield to
 *   *unwind* — the wire-statemachine and ydraw layers use yield as the
 *   "stop here, come back next envelope" signal. A pure no-op turns those
 *   yields into infinite spin loops (scene_process_input loops until
 *   yield, then yield does nothing, then loop again forever).
 *
 *   The trick: use setjmp in resume() and longjmp in yield(). The longjmp
 *   abandons whatever frames are between yield and resume, so resume()
 *   returns to its caller as if the entry had finished — except we leave
 *   `finished` unset, so the next resume re-enters entry from the top.
 *   Layer process_input loops are written so each iteration of their outer
 *   for(;;) is a self-contained envelope (iter_init … iter_destroy, all
 *   per-envelope locals cleaned up before yield), so re-entering from the
 *   top is functionally equivalent to picking up after yield.
 *
 *   Asyncify (emscripten_sleep) is not used here — wgpu _await wrappers
 *   handle their own suspension via emscripten_sleep; the coro layer
 *   never needs to release the JS event loop itself.
 */

#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>

#include <setjmp.h>
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
    /* setjmp target inside the current resume() call. Valid only while
     * the coro is on the stack between resume() entering and entry()
     * returning (or yield() longjmping out). */
    jmp_buf yield_target;
    int yield_target_valid;
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

    ydebug("coro spawn id=%u name=%s (webasm stub)", coro->id, coro->name ? coro->name : "(anon)");
    return YETTY_OK(yplatform_coro_ptr, coro);
}

void yetty_yplatform_coro_resume(struct yetty_yplatform_coro *coro)
{
    if (!coro || coro->finished) {
        return;
    }

    struct yetty_yplatform_coro *prev = g_current;
    g_current = coro;
    coro->yield_target_valid = 1;
    if (setjmp(coro->yield_target) == 0) {
        ydebug("resume coro %u (%s) — runs inline on webasm", coro->id,
               coro->name ? coro->name : "(anon)");
        coro->entry(coro->arg);
        coro->finished = 1;
        ydebug("coro %u finished (webasm)", coro->id);
    } else {
        ydebug("coro %u yielded (longjmp) — returning to resume caller", coro->id);
    }
    coro->yield_target_valid = 0;
    g_current = prev;
}

void yetty_yplatform_coro_yield(void)
{
    struct yetty_yplatform_coro *self = g_current;
    if (!self || !self->yield_target_valid) {
        ywarn("yplatform_coro_yield called outside coro context — no-op");
        return;
    }
    longjmp(self->yield_target, 1);
}

void yetty_yplatform_coro_destroy(struct yetty_yplatform_coro *coro)
{
    if (!coro) {
        return;
    }
    ydebug("coro destroy id=%u name=%s finished=%d", coro->id, coro->name ? coro->name : "(anon)",
           coro->finished);
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
