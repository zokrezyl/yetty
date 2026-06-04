/*
 * yplot-time.c — animates yplot's `time` uniform.
 *
 * Subscribes the figure_instance's embedded event listener to a
 * factory-shared, refcounted libuv timer. First instance with
 * USES_TIME starts the timer; last detach destroys it. Each tick:
 * write monotonic seconds to the time uniform slot and call
 * request_render. Same pattern as yvideo's animation listener —
 * duplicated for now; a future "shared animation service on the
 * canvas / event_loop" would replace both copies.
 *
 * No header for this file — the two entry points (attach / detach)
 * are forward-declared in yplot-gen.c (the only caller). Keeping the
 * generated file untouched apart from those two forward decls + the
 * call sites it already had to add for instance lifecycle.
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/time.h>
#include <yetty/yplot/yplot.h> /* YETTY_YPLOT_FLAG_USES_TIME */
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/ytrace/ytrace.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define YPLOT_ANIMATION_PERIOD_MS 16 /* ~60 Hz */

/* Index of the `time` uniform inside yplot's per-instance RS. Must
 * match the slot populated in yplot-gen.c's populate_rs (currently
 * the last slot, after the zoom/viewport extras). Hardcoded on both
 * sides — easier to keep in sync than via an extern header. */
#define YETTY_YPLOT_TIME_UNIFORM_SLOT 26u

struct yplot_time_factory_state {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id; /* -1 when no timer exists */
    uint32_t subscribers;
};

struct yplot_time_instance_state {
    bool subscribed;
    double start_monotonic_sec; /* set lazily on first tick */
};

/*---------------------------------------------------------------------------
 * Factory-shared timer (refcounted), stashed in factory->hook_data.
 *-------------------------------------------------------------------------*/

static struct yplot_time_factory_state *fs_get_or_init(struct yetty_ydraw_concrete_factory *factory)
{
    if (!factory) {
        return NULL;
    }
    if (factory->hook_data) {
        return (struct yplot_time_factory_state *)factory->hook_data;
    }
    struct yplot_time_factory_state *fs = calloc(1, sizeof(*fs));
    if (!fs) {
        return NULL;
    }
    fs->event_loop = factory->event_loop;
    fs->timer_id = -1;
    factory->hook_data = fs;
    return fs;
}

static struct yetty_ycore_void_result subscribe(struct yetty_ydraw_concrete_factory *factory,
                                                struct yetty_yevent_event_listener *listener)
{
    if (!factory || !factory->event_loop) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: no event_loop on factory");
    }
    if (!listener || !listener->handler) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: listener has no handler");
    }
    struct yplot_time_factory_state *fs = fs_get_or_init(factory);
    if (!fs) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: factory_state alloc failed");
    }
    struct yetty_yevent_event_loop *loop = fs->event_loop;

    if (fs->subscribers == 0) {
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yplot-time: create_timer");
        fs->timer_id = tr.value;
        loop->ops->config_timer(loop, fs->timer_id, YPLOT_ANIMATION_PERIOD_MS);
        loop->ops->start_timer(loop, fs->timer_id);
        yinfo("yplot-time: shared timer started (period=%d ms)", YPLOT_ANIMATION_PERIOD_MS);
    }
    struct yetty_ycore_void_result rr =
        loop->ops->register_timer_listener(loop, fs->timer_id, listener);
    if (YETTY_IS_ERR(rr)) {
        if (fs->subscribers == 0) {
            loop->ops->stop_timer(loop, fs->timer_id);
            loop->ops->destroy_timer(loop, fs->timer_id);
            fs->timer_id = -1;
        }
        return YETTY_ERR(yetty_ycore_void, "yplot-time: register_timer_listener", rr);
    }
    fs->subscribers++;
    return YETTY_OK_VOID();
}

static void unsubscribe(struct yetty_ydraw_concrete_factory *factory,
                        struct yetty_yevent_event_listener *listener)
{
    if (!factory) {
        return;
    }
    struct yplot_time_factory_state *fs = (struct yplot_time_factory_state *)factory->hook_data;
    if (!fs || !fs->event_loop || fs->timer_id < 0 || fs->subscribers == 0) {
        return;
    }
    struct yetty_yevent_event_loop *loop = fs->event_loop;
    if (loop->ops->deregister_timer_listener) {
        loop->ops->deregister_timer_listener(loop, fs->timer_id, listener);
    }
    fs->subscribers--;
    if (fs->subscribers == 0) {
        loop->ops->stop_timer(loop, fs->timer_id);
        loop->ops->destroy_timer(loop, fs->timer_id);
        fs->timer_id = -1;
        yinfo("yplot-time: shared timer stopped (last subscriber gone)");
    }
}

/*---------------------------------------------------------------------------
 * Listener handler — invoked on each timer tick.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_int_result on_tick(struct yetty_yevent_event_listener *listener,
                                             const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ydraw_figure *instance =
        container_of(listener, struct yetty_ydraw_figure, listener);
    if (!instance || !instance->resource_set || !instance->factory ||
        !instance->factory->event_loop) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yplot_time_instance_state *st =
        (struct yplot_time_instance_state *)instance->instance_data;
    if (!st) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    double now = yetty_yplatform_ytime_monotonic_sec();
    if (st->start_monotonic_sec == 0.0) {
        st->start_monotonic_sec = now;
    }
    float t = (float)(now - st->start_monotonic_sec);
    instance->resource_set->uniforms[YETTY_YPLOT_TIME_UNIFORM_SLOT].f32 = t;
    /* Per-figure dirty + global render kick. ydraw_layer_render's
     * figure loop only invokes inst->render iff inst->dirty || force. */
    instance->dirty = 1;

    struct yetty_yevent_event_loop *loop = instance->factory->event_loop;
    loop->ops->request_render(loop);
    return YETTY_OK(yetty_ycore_int, 0);
}

/*---------------------------------------------------------------------------
 * Public attach / detach — called from yplot's create / destroy.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yplot_time_attach(struct yetty_ydraw_figure *instance)
{
    if (!instance || !instance->buffer_data || !instance->factory) {
        return YETTY_OK_VOID();
    }
    /* Read flags from the wire to decide whether to subscribe. */
    const uint32_t *data = (const uint32_t *)instance->buffer_data;
    const uint32_t *payload = data + 2;
    /* Layout: bounds(4 f32) + xrange(2 f32) + yrange(2 f32) → payload[8] is flags. */
    uint32_t flags = payload[8];
    if (!(flags & YETTY_YPLOT_FLAG_USES_TIME)) {
        return YETTY_OK_VOID();
    }
    if (!instance->factory->event_loop) {
        return YETTY_OK_VOID(); /* test mode — no loop */
    }

    struct yplot_time_instance_state *st = calloc(1, sizeof(*st));
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: instance state alloc failed");
    }
    instance->instance_data = st;
    instance->listener.handler = on_tick;

    struct yetty_ycore_void_result sr = subscribe(instance->factory, &instance->listener);
    if (YETTY_IS_ERR(sr)) {
        free(st);
        instance->instance_data = NULL;
        instance->listener.handler = NULL;
        return YETTY_ERR(yetty_ycore_void, "yplot-time: subscribe failed", sr);
    }
    st->subscribed = true;
    return YETTY_OK_VOID();
}

void yetty_yplot_time_detach(struct yetty_ydraw_figure *instance)
{
    if (!instance) {
        return;
    }
    struct yplot_time_instance_state *st =
        (struct yplot_time_instance_state *)instance->instance_data;
    if (st && st->subscribed && instance->factory) {
        unsubscribe(instance->factory, &instance->listener);
        st->subscribed = false;
    }
    free(st);
    instance->instance_data = NULL;
    instance->listener.handler = NULL;
}
