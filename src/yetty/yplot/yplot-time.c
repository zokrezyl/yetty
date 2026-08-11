/*
 * yplot-time.c — animates yplot's `time` uniform.
 *
 * Every figure_instance with USES_TIME joins a factory-shared list; the
 * factory owns ONE libuv timer and ONE listener on it. First instance
 * creates the timer, last one destroys it. Each tick walks the list:
 * write monotonic seconds to each instance's time uniform slot, mark it
 * dirty, then request_render ONCE for the whole batch.
 *
 * The list exists because the event loop caps listeners per timer
 * (MAX_LISTENERS_PER_TIMER); registering one listener per animated
 * figure made the 17th animated plot on screen fail to subscribe and
 * silently render static. One listener per factory has no such ceiling,
 * and it collapses N request_render calls per tick down to one.
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
#include <stddef.h> /* offsetof */
#include <stdint.h>
#include <stdlib.h>

#define YPLOT_ANIMATION_PERIOD_MS 16 /* ~60 Hz */

/* Index of the `time` uniform inside yplot's per-instance RS: the
 * generated YETTY_YPLOT_UNIFORM_TIME_SLOT (yplot-gen.h), which tracks
 * the schema. A local copy of that number went stale the first time
 * the uniform list grew — never hardcode it again. */

struct yplot_time_factory_state {
    struct yetty_yevent_event_loop *event_loop;
    /* The single listener registered on the timer. Its handler fans the
     * tick out to every instance in `instances`. */
    struct yetty_yevent_event_listener listener;
    yetty_yevent_timer_id timer_id; /* -1 when no timer exists */
    struct yetty_ydraw_composite **instances;
    size_t instance_count;
    size_t instance_capacity;
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

static struct yetty_ycore_int_result on_tick(struct yetty_yevent_event_listener *listener,
                                             const struct yetty_yui_event *event);

/* Append `instance` to the factory's animated list, growing it as needed. */
static struct yetty_ycore_void_result instances_append(struct yplot_time_factory_state *fs,
                                                       struct yetty_ydraw_composite *instance)
{
    if (fs->instance_count == fs->instance_capacity) {
        size_t new_capacity = fs->instance_capacity ? fs->instance_capacity * 2 : 8;
        struct yetty_ydraw_composite **grown =
            realloc(fs->instances, new_capacity * sizeof(*fs->instances));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yplot-time: instance list grow failed");
        }
        fs->instances = grown;
        fs->instance_capacity = new_capacity;
    }
    fs->instances[fs->instance_count++] = instance;
    return YETTY_OK_VOID();
}

/* Remove `instance` from the list (swap-with-last: order is irrelevant,
 * every entry gets the same tick). */
static void instances_remove(struct yplot_time_factory_state *fs,
                             const struct yetty_ydraw_composite *instance)
{
    for (size_t i = 0; i < fs->instance_count; i++) {
        if (fs->instances[i] == instance) {
            fs->instances[i] = fs->instances[fs->instance_count - 1];
            fs->instance_count--;
            return;
        }
    }
}

static struct yetty_ycore_void_result subscribe(struct yetty_ydraw_concrete_factory *factory,
                                                struct yetty_ydraw_composite *instance)
{
    if (!factory || !factory->event_loop) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: no event_loop on factory");
    }
    struct yplot_time_factory_state *fs = fs_get_or_init(factory);
    if (!fs) {
        return YETTY_ERR(yetty_ycore_void, "yplot-time: factory_state alloc failed");
    }
    struct yetty_yevent_event_loop *loop = fs->event_loop;

    /* First animated instance brings up the timer and the one listener. */
    if (fs->instance_count == 0) {
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yplot-time: create_timer");
        fs->timer_id = tr.value;
        loop->ops->config_timer(loop, fs->timer_id, YPLOT_ANIMATION_PERIOD_MS);
        loop->ops->start_timer(loop, fs->timer_id);

        fs->listener.handler = on_tick;
        struct yetty_ycore_void_result rr =
            loop->ops->register_timer_listener(loop, fs->timer_id, &fs->listener);
        if (YETTY_IS_ERR(rr)) {
            loop->ops->stop_timer(loop, fs->timer_id);
            loop->ops->destroy_timer(loop, fs->timer_id);
            fs->timer_id = -1;
            fs->listener.handler = NULL;
            return YETTY_ERR(yetty_ycore_void, "yplot-time: register_timer_listener", rr);
        }
        yinfo("yplot-time: shared timer started (period=%d ms)", YPLOT_ANIMATION_PERIOD_MS);
    }

    struct yetty_ycore_void_result ar = instances_append(fs, instance);
    if (YETTY_IS_ERR(ar)) {
        if (fs->instance_count == 0) {
            loop->ops->deregister_timer_listener(loop, fs->timer_id, &fs->listener);
            loop->ops->stop_timer(loop, fs->timer_id);
            loop->ops->destroy_timer(loop, fs->timer_id);
            fs->timer_id = -1;
            fs->listener.handler = NULL;
        }
        return YETTY_ERR(yetty_ycore_void, "yplot-time: joining animated list", ar);
    }
    return YETTY_OK_VOID();
}

static void unsubscribe(struct yetty_ydraw_concrete_factory *factory,
                        struct yetty_ydraw_composite *instance)
{
    if (!factory) {
        return;
    }
    struct yplot_time_factory_state *fs = (struct yplot_time_factory_state *)factory->hook_data;
    if (!fs || !fs->event_loop || fs->timer_id < 0 || fs->instance_count == 0) {
        return;
    }
    instances_remove(fs, instance);
    if (fs->instance_count > 0) {
        return;
    }

    struct yetty_yevent_event_loop *loop = fs->event_loop;
    if (loop->ops->deregister_timer_listener) {
        loop->ops->deregister_timer_listener(loop, fs->timer_id, &fs->listener);
    }
    loop->ops->stop_timer(loop, fs->timer_id);
    loop->ops->destroy_timer(loop, fs->timer_id);
    fs->timer_id = -1;
    fs->listener.handler = NULL;
    free(fs->instances);
    fs->instances = NULL;
    fs->instance_capacity = 0;
    yinfo("yplot-time: shared timer stopped (last subscriber gone)");
}

/*---------------------------------------------------------------------------
 * Listener handler — invoked on each timer tick.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_int_result on_tick(struct yetty_yevent_event_listener *listener,
                                             const struct yetty_yui_event *event)
{
    (void)event;
    struct yplot_time_factory_state *fs =
        container_of(listener, struct yplot_time_factory_state, listener);
    if (!fs || !fs->event_loop || fs->instance_count == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    double now = yetty_yplatform_ytime_monotonic_sec();
    for (size_t i = 0; i < fs->instance_count; i++) {
        struct yetty_ydraw_composite *instance = fs->instances[i];
        if (!instance || !instance->resource_set) {
            continue;
        }
        struct yplot_time_instance_state *st =
            (struct yplot_time_instance_state *)instance->instance_data;
        if (!st) {
            continue;
        }
        /* Each figure's clock starts at its own first tick, so a plot
         * added later doesn't jump into the middle of the animation. */
        if (st->start_monotonic_sec == 0.0) {
            st->start_monotonic_sec = now;
        }
        instance->resource_set->uniforms[YETTY_YPLOT_UNIFORM_TIME_SLOT].f32 =
            (float)(now - st->start_monotonic_sec);
        /* Per-figure dirty + global render kick. ydraw_layer_render's
         * figure loop only invokes inst->render iff inst->dirty || force. */
        instance->dirty = 1;
    }

    /* One kick for the whole batch — the render pass repaints every dirty
     * figure, so a per-instance call would just be N-1 redundant wakeups. */
    fs->event_loop->ops->request_render(fs->event_loop);
    return YETTY_OK(yetty_ycore_int, 0);
}

/*---------------------------------------------------------------------------
 * Public attach / detach — called from yplot's create / destroy.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yplot_time_attach(struct yetty_ydraw_composite *instance)
{
    if (!instance || !instance->buffer_data || !instance->factory) {
        return YETTY_OK_VOID();
    }
    /* Read flags from the wire to decide whether to subscribe.
     * Wire layout: [type_id][payload_size][uniform words...], where the
     * uniform words are a verbatim copy of struct yetty_yplot_uniforms.
     * Derive the flags word index from the struct itself — a hardcoded
     * index goes stale the moment a uniform is inserted before it (it
     * already did once, silently disabling every animated plot). */
    const uint32_t *data = (const uint32_t *)instance->buffer_data;
    const uint32_t *uniform_words = data + 2;
    uint32_t flags = uniform_words[offsetof(struct yetty_yplot_uniforms, flags) / sizeof(uint32_t)];
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

    struct yetty_ycore_void_result sr = subscribe(instance->factory, instance);
    if (YETTY_IS_ERR(sr)) {
        free(st);
        instance->instance_data = NULL;
        return YETTY_ERR(yetty_ycore_void, "yplot-time: subscribe failed", sr);
    }
    st->subscribed = true;
    return YETTY_OK_VOID();
}

void yetty_yplot_time_detach(struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return;
    }
    struct yplot_time_instance_state *st =
        (struct yplot_time_instance_state *)instance->instance_data;
    if (st && st->subscribed && instance->factory) {
        unsubscribe(instance->factory, instance);
        st->subscribed = false;
    }
    free(st);
    instance->instance_data = NULL;
}
