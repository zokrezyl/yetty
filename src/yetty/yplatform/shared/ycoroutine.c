/*
 * yplatform/shared/ycoroutine.c - Desktop coroutine implementation.
 *
 * Backed by libco via the yetty_yco_* wrappers. Single-threaded model:
 * coroutines only ever execute on the event-loop thread; cross-thread
 * wakeups (e.g. the GPU poll thread) post a request via the event loop and
 * the loop-thread handler calls yplatform_coro_resume.
 */

#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yco/co.h>
#include <yetty/ytrace.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Default coroutine stack. Must be large enough for the deepest call chain
 * we run inside a coro plus one ydebug/spdlog format buffer (those eat a
 * few KB on first use). 64KB was too small and caused heap corruption when
 * the entry function overflowed into adjacent malloc chunks. */
#define YPLATFORM_CORO_DEFAULT_STACK (1024 * 1024)

struct yplatform_coro {
    yetty_yco_thread thread;
    yetty_yco_thread caller;
    yplatform_coro_entry entry;
    void *arg;
    char *name;
    unsigned int id;
    int status;
    int finished;
};

/* The currently-running coroutine on this (loop) thread. NULL on the main
 * stack. We don't run coroutines on other threads, so a plain global is
 * sufficient. */
static struct yplatform_coro *g_current = NULL;
static atomic_uint g_next_id = 1;

/* libco entry has no argument. We read the about-to-run coro from g_current,
 * which yplatform_coro_resume sets just before yco_switch. */
static void coro_trampoline(void)
{
    struct yplatform_coro *self = g_current;
    ydebug("coro %u (%s) entry", self->id, self->name ? self->name : "(anon)");
    self->entry(self->arg);
    ydebug("coro %u (%s) returned from entry", self->id, self->name ? self->name : "(anon)");
    self->finished = 1;
    /* Return to whoever last resumed us; they'll see finished and may
     * destroy us. The yco_switch call must NOT return. */
    yetty_yco_switch(self->caller);
}

struct yplatform_coro_ptr_result yplatform_coro_spawn(yplatform_coro_entry entry, void *arg,
                                                      size_t stack_hint, const char *name)
{
    if (!entry) {
        return YETTY_ERR(yplatform_coro_ptr, "entry is NULL");
    }

    struct yplatform_coro *coro = calloc(1, sizeof(struct yplatform_coro));
    if (!coro) {
        return YETTY_ERR(yplatform_coro_ptr, "calloc failed");
    }

    unsigned int stack = stack_hint ? (unsigned int)stack_hint : YPLATFORM_CORO_DEFAULT_STACK;

    coro->thread = yetty_yco_create(stack, coro_trampoline);
    if (!coro->thread) {
        free(coro);
        return YETTY_ERR(yplatform_coro_ptr, "yco_create failed");
    }

    coro->entry = entry;
    coro->arg = arg;
    coro->id = atomic_fetch_add(&g_next_id, 1);
    if (name) {
        coro->name = strdup(name);
        if (!coro->name) {
            yetty_yco_delete(coro->thread);
            free(coro);
            return YETTY_ERR(yplatform_coro_ptr, "strdup failed");
        }
    }

    ydebug("coro spawn id=%u name=%s stack=%u", coro->id, coro->name ? coro->name : "(anon)",
           stack);
    return YETTY_OK(yplatform_coro_ptr, coro);
}

void yplatform_coro_yield(void)
{
    struct yplatform_coro *self = g_current;
    if (!self) {
        ywarn("yplatform_coro_yield called from main stack");
        return;
    }
    ydebug("coro %u (%s) yield", self->id, self->name ? self->name : "(anon)");
    yetty_yco_switch(self->caller);
    ydebug("coro %u (%s) resumed", self->id, self->name ? self->name : "(anon)");
}

void yplatform_coro_resume(struct yplatform_coro *coro)
{
    if (!coro || coro->finished) {
        return;
    }

    struct yplatform_coro *prev_current = g_current;
    coro->caller = yetty_yco_active();
    g_current = coro;
    ydebug("resume coro %u (%s)", coro->id, coro->name ? coro->name : "(anon)");
    yetty_yco_switch(coro->thread);
    g_current = prev_current;
    ydebug("back from coro %u finished=%d", coro->id, coro->finished);
}

void yplatform_coro_destroy(struct yplatform_coro *coro)
{
    if (!coro) {
        return;
    }
    ydebug("coro destroy id=%u name=%s finished=%d", coro->id, coro->name ? coro->name : "(anon)",
           coro->finished);
    if (coro->thread) {
        yetty_yco_delete(coro->thread);
    }
    free(coro->name);
    free(coro);
}

struct yplatform_coro *yplatform_coro_current(void)
{
    return g_current;
}

int yplatform_coro_is_finished(const struct yplatform_coro *coro)
{
    return coro ? coro->finished : 1;
}

unsigned int yplatform_coro_id(const struct yplatform_coro *coro)
{
    return coro ? coro->id : 0;
}

const char *yplatform_coro_name(const struct yplatform_coro *coro)
{
    return coro && coro->name ? coro->name : "(anon)";
}

void yplatform_coro_set_status(struct yplatform_coro *coro, int status)
{
    if (coro) {
        coro->status = status;
    }
}

int yplatform_coro_get_status(const struct yplatform_coro *coro)
{
    return coro ? coro->status : 0;
}
