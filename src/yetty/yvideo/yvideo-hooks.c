/*
 * yvideo-hooks.c — prim-specific lifecycle for yvideo, plugged into
 * the generated factory (yvideo-gen.c) via the hook surface enabled by
 * `hooks: true` in yvideo.yaml. The generator owns all CPU/GPU sync
 * (uniform names/types/offsets, buffer layouts, texture descriptors).
 * This file owns the prim-specific state and lifecycle:
 *
 *   - openh264 decoder (yvcodec) per instance
 *   - append-only Annex-B NAL ring + start-code splitter (openh264
 *     consumes ONE access unit per DecodeFrameNoDelay; multi-AU chunks
 *     silently drop everything after the first)
 *   - per-frame YUV→BGRA convert into a CPU-side scratch buffer
 *   - wall-clock-driven pump that advances the playhead on every
 *     timer tick (NOT every render — yetty only renders when input
 *     arrives, so a render-driven pump never makes progress between
 *     CMD_UPDATE envelopes)
 *
 * Subscription model:
 *   yetty_ydraw_figure_instance embeds a yetty_yevent_event_listener.
 *   instance_create registers it on a libuv timer at the source's fps;
 *   the handler advances the decoder, writes RGBA into a per-instance
 *   scratch, marks the binder's texture dirty, and calls request_render.
 *   instance_destroy stops + destroys the timer (which deregisters
 *   the listener with it).
 *
 * Same shape shader-glyph-layer uses for its 60 Hz animation timer —
 * just at the figure granularity instead of the layer.
 */

#include <yetty/yvideo/yvideo.h>
#include <yetty/yvideo/yvideo-gen.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvcodec/decoder.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YVIDEO_NAL_RING_INIT_CAP (256u * 1024u)

/* Per-instance state — one per yvideo figure_instance. */
struct yvideo_instance_data {
    struct yetty_yvcodec_decoder *decoder;

    uint32_t video_w;
    uint32_t video_h;
    float fps;
    uint32_t flags;

    /* CPU scratch the binder uploads with dirty=1. */
    uint8_t *frame_rgba;
    size_t frame_rgba_size;
    bool have_frame;
    bool tex_pinned; /* RS textures[0] pointed at frame_rgba? */
    bool size_mismatch_warned; /* throttle the size-mismatch yerror */

    /* Wall-clock head — set on first successful decode so a delayed
     * INIT doesn't snap past N frames. */
    double start_monotonic_sec;
    uint32_t frames_displayed;

    /* Append-only Annex-B NAL ring; the pump walks start codes and
     * feeds one NAL per DecodeFrameNoDelay call. */
    uint8_t *nal_ring;
    size_t nal_ring_size;
    size_t nal_ring_cap;
    size_t nal_ring_consumed;

    /* True iff this instance's listener is currently registered on the
     * factory's shared timer. Tracked so destroy + EOS unsubscribes are
     * idempotent. */
    bool subscribed;
};

/* Per-FACTORY state — one for the whole yvideo type, not per instance.
 * Lives in factory->hook_data so the generator doesn't need to know
 * about it. A single refcounted libuv timer ticks for every yvideo
 * instance that has subscribed; first subscriber starts it, last
 * unsubscriber stops + destroys it. */
struct yvideo_factory_state {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id; /* -1 when no timer exists */
    uint32_t subscribers;           /* number of active instance listeners */
    int period_ms;                  /* current period — set on first subscribe */
};

static struct yvideo_instance_data *yvideo_state(struct yetty_ydraw_figure_instance *self)
{
    return (struct yvideo_instance_data *)self->instance_data;
}

/*---------------------------------------------------------------------------
 * Refcounted shared timer (factory-level state).
 *
 * Stored in instance->factory->hook_data so it persists across
 * instance lifetimes for the lifetime of the factory. First subscriber
 * allocates + starts the timer; last unsubscriber frees + destroys it.
 *-------------------------------------------------------------------------*/

static struct yvideo_factory_state *yvideo_factory_state_get_or_init(
    struct yetty_ydraw_concrete_factory *factory, int period_ms)
{
    if (!factory) return NULL;
    if (factory->hook_data) {
        return (struct yvideo_factory_state *)factory->hook_data;
    }
    struct yvideo_factory_state *fs = calloc(1, sizeof(*fs));
    if (!fs) return NULL;
    fs->event_loop = factory->event_loop;
    fs->timer_id = -1;
    fs->subscribers = 0;
    fs->period_ms = period_ms;
    factory->hook_data = fs;
    return fs;
}

/* Subscribe `listener` to the factory's shared animation timer. Creates
 * the timer if this is the first subscriber. Returns OK_VOID iff
 * registration succeeded; the caller can then trust the listener will
 * be invoked on every tick until yvideo_animation_unsubscribe(). */
static struct yetty_ycore_void_result yvideo_animation_subscribe(
    struct yetty_ydraw_concrete_factory *factory,
    struct yetty_yevent_event_listener *listener, int period_ms_hint)
{
    if (!factory || !factory->event_loop) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: no event_loop on factory");
    }
    if (!listener || !listener->handler) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: listener has no handler");
    }
    struct yvideo_factory_state *fs =
        yvideo_factory_state_get_or_init(factory, period_ms_hint);
    if (!fs) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: factory_state alloc failed");
    }
    struct yetty_yevent_event_loop *loop = fs->event_loop;

    if (fs->subscribers == 0) {
        /* First subscriber: build the timer. */
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yvideo: create_timer");
        fs->timer_id = tr.value;
        if (period_ms_hint < 1) period_ms_hint = 33;
        fs->period_ms = period_ms_hint;
        loop->ops->config_timer(loop, fs->timer_id, fs->period_ms);
        loop->ops->start_timer(loop, fs->timer_id);
        yinfo("yvideo: shared timer started (period=%d ms)", fs->period_ms);
    }

    struct yetty_ycore_void_result rr =
        loop->ops->register_timer_listener(loop, fs->timer_id, listener);
    if (YETTY_IS_ERR(rr)) {
        /* If we just created the timer for THIS subscriber, tear it
         * back down — otherwise we'd leak the slot. */
        if (fs->subscribers == 0) {
            loop->ops->stop_timer(loop, fs->timer_id);
            loop->ops->destroy_timer(loop, fs->timer_id);
            fs->timer_id = -1;
        }
        return YETTY_ERR(yetty_ycore_void, "yvideo: register_timer_listener", rr);
    }
    fs->subscribers++;
    return YETTY_OK_VOID();
}

/* Unsubscribe `listener` from the factory's shared timer. Idempotent
 * w.r.t. listeners not currently registered. Destroys the timer when
 * the last subscriber leaves. */
static void yvideo_animation_unsubscribe(struct yetty_ydraw_concrete_factory *factory,
                                         struct yetty_yevent_event_listener *listener)
{
    if (!factory) return;
    struct yvideo_factory_state *fs = (struct yvideo_factory_state *)factory->hook_data;
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
        yinfo("yvideo: shared timer stopped (last subscriber gone)");
        /* Keep `fs` allocated — next subscribe re-arms a fresh timer.
         * Freed in yvideo_factory_destroy (extends the generated path
         * via factory teardown — for now it leaks 1 struct/process). */
    }
}

/*---------------------------------------------------------------------------
 * NAL ring helpers.
 *-------------------------------------------------------------------------*/

static int yvideo_nal_ring_append(struct yvideo_instance_data *st, const uint8_t *bytes,
                                  size_t len)
{
    if (len == 0) {
        return 1;
    }
    size_t need = st->nal_ring_size + len;
    if (need > st->nal_ring_cap) {
        size_t new_cap = st->nal_ring_cap ? st->nal_ring_cap : YVIDEO_NAL_RING_INIT_CAP;
        while (new_cap < need) {
            new_cap *= 2;
        }
        uint8_t *nb = realloc(st->nal_ring, new_cap);
        if (!nb) {
            return 0;
        }
        st->nal_ring = nb;
        st->nal_ring_cap = new_cap;
    }
    memcpy(st->nal_ring + st->nal_ring_size, bytes, len);
    st->nal_ring_size += len;
    return 1;
}

static size_t yvideo_find_start_code(const uint8_t *buf, size_t size, size_t from)
{
    if (size < 3 || from >= size) {
        return (size_t)-1;
    }
    for (size_t i = from; i + 3 <= size; i++) {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
            return i;
        }
        if (i + 4 <= size && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 &&
            buf[i + 3] == 1) {
            return i;
        }
    }
    return (size_t)-1;
}

/* Pump the decoder forward until either target_frame is reached or no
 * complete NAL is available. Returns the number of newly produced
 * frames so the caller can decide whether to mark the RS dirty. */
static uint32_t yvideo_pump_until(struct yvideo_instance_data *st, uint32_t target_frame)
{
    uint32_t produced = 0;
    if (!st->decoder || !st->nal_ring) {
        return 0;
    }
    while (st->frames_displayed <= target_frame) {
        size_t curr = yvideo_find_start_code(st->nal_ring, st->nal_ring_size,
                                             st->nal_ring_consumed);
        if (curr == (size_t)-1) {
            return produced;
        }
        size_t next = yvideo_find_start_code(st->nal_ring, st->nal_ring_size, curr + 3);
        if (next == (size_t)-1) {
            return produced;
        }
        size_t nal_size = next - curr;
        struct yetty_ycore_void_result fr =
            yetty_yvcodec_decoder_feed(st->decoder, st->nal_ring + curr, nal_size);
        st->nal_ring_consumed = next;
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
            continue;
        }
        struct yetty_yvcodec_yuv_frame yuv = {0};
        bool got = false;
        struct yetty_ycore_void_result gr =
            yetty_yvcodec_decoder_get_frame(st->decoder, &yuv, &got);
        if (YETTY_IS_ERR(gr)) {
            yetty_ycore_error_destroy(gr.error);
            continue;
        }
        if (!got) {
            continue;
        }
        /* Hard safety check: the SPS-declared frame dimensions MUST
         * match what we allocated based on wire's video_w/video_h. If
         * a sender lied (e.g. user passed --width/--height overrides
         * that don't match the source), refuse to write — otherwise
         * yuv_frame_yuv420_to_bgra would scribble past frame_rgba and
         * corrupt the heap. Also: the binder's atlas slot was packed
         * for the wire dimensions, so even a successful write would
         * truncate at upload time. Drop the frame, log once. */
        size_t need = (size_t)yuv.width * (size_t)yuv.height * 4u;
        if (need > st->frame_rgba_size) {
            if (!st->size_mismatch_warned) {
                yerror("yvideo: decoded frame %ux%u exceeds declared %ux%u; "
                       "dropping frames (wire video_w/h must match source SPS)",
                       yuv.width, yuv.height, st->video_w, st->video_h);
                st->size_mismatch_warned = true;
            }
            continue;
        }
        yetty_yvcodec_yuv_frame_yuv420_to_bgra(&yuv, st->frame_rgba);
        st->have_frame = true;
        st->frames_displayed++;
        produced++;
    }
    return produced;
}

/* Wall-clock target frame for the current tick. */
static uint32_t yvideo_target_frame(struct yvideo_instance_data *st)
{
    if (st->fps <= 0.0f) {
        return st->frames_displayed; /* paused */
    }
    if (!st->have_frame) {
        return 0; /* try to surface frame 0 */
    }
    double now;
    {
        /* Use the same clock yetty uses elsewhere; declared in
         * yetty/yplatform/time.h. */
        extern double yetty_yplatform_ytime_monotonic_sec(void);
        now = yetty_yplatform_ytime_monotonic_sec();
    }
    double t = now - st->start_monotonic_sec;
    if (t < 0.0) {
        return 0;
    }
    return (uint32_t)(t * (double)st->fps);
}

/*---------------------------------------------------------------------------
 * Timer listener — the figure_instance IS the listener (the struct is
 * embedded in the base), so the handler recovers the instance via
 * container_of and pulls the prim-specific state from instance_data.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_int_result yvideo_on_tick(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ydraw_figure_instance *instance =
        container_of(listener, struct yetty_ydraw_figure_instance, listener);
    struct yvideo_instance_data *st = yvideo_state(instance);
    if (!st || !instance->factory || !instance->factory->event_loop) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    uint32_t target = yvideo_target_frame(st);
    uint32_t produced = yvideo_pump_until(st, target);

    if (st->have_frame && st->start_monotonic_sec == 0.0) {
        extern double yetty_yplatform_ytime_monotonic_sec(void);
        st->start_monotonic_sec = yetty_yplatform_ytime_monotonic_sec();
    }

    if (produced == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Pin the RS texture to our scratch on the first frame. After
     * that the same pointer stays valid; we just toggle dirty=1 so
     * the binder re-uploads the latest decode. */
    if (instance->resource_set && st->frame_rgba) {
        struct yetty_ydraw_gpu_resource_set *rs = instance->resource_set;
        if (!st->tex_pinned) {
            rs->textures[0].data = st->frame_rgba;
            rs->textures[0].width = st->video_w;
            rs->textures[0].height = st->video_h;
            st->tex_pinned = true;
        }
        rs->textures[0].dirty = 1;
    }
    /* Per-figure dirty so ydraw_layer_render iterates and invokes
     * inst->render even when the layer's own dirty bit is clean. */
    instance->dirty = 1;

    struct yetty_yevent_event_loop *loop = instance->factory->event_loop;
    loop->ops->request_render(loop);
    return YETTY_OK(yetty_ycore_int, 0);
}

/*---------------------------------------------------------------------------
 * Instance lifecycle teardown (used in both error paths and destroy hook).
 *-------------------------------------------------------------------------*/

/* yvideo_state_destroy — free only the per-instance state. The
 * subscription is dropped earlier in the destroy hook (so we can still
 * pass the factory pointer); this function never touches the timer. */
static void yvideo_state_destroy(struct yvideo_instance_data *st)
{
    if (!st) {
        return;
    }
    if (st->decoder) {
        yetty_yvcodec_decoder_destroy(st->decoder);
    }
    free(st->frame_rgba);
    free(st->nal_ring);
    free(st);
}

/*---------------------------------------------------------------------------
 * Hook implementations.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yvideo_hook_instance_create(
    struct yetty_ydraw_figure_instance *instance, const void *buffer_data, size_t size)
{
    (void)size;
    const uint32_t *data = (const uint32_t *)buffer_data;
    const uint32_t *payload = data + 2;
    uint32_t video_w = payload[4];
    uint32_t video_h = payload[5];
    float fps = ((const float *)payload)[6];
    uint32_t flags = payload[8];
    uint32_t nal_words = payload[9];
    const uint8_t *nal_bytes = (const uint8_t *)(payload + 10);
    size_t nal_byte_count = (size_t)nal_words * 4u;

    if (video_w == 0 || video_h == 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "yvideo: video_w/video_h must be > 0 (set in INIT envelope)");
    }

    struct yvideo_instance_data *st = calloc(1, sizeof(*st));
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: state alloc failed");
    }
    st->video_w = video_w;
    st->video_h = video_h;
    st->fps = fps;
    st->flags = flags;
    st->frame_rgba_size = (size_t)video_w * (size_t)video_h * 4u;
    st->frame_rgba = calloc(1u, st->frame_rgba_size);
    if (!st->frame_rgba) {
        yvideo_state_destroy(st);
        return YETTY_ERR(yetty_ycore_void, "yvideo: frame buf alloc failed");
    }
    struct yetty_yvcodec_decoder_ptr_result dr = yetty_yvcodec_decoder_create_h264();
    if (YETTY_IS_ERR(dr)) {
        yvideo_state_destroy(st);
        return YETTY_ERR(yetty_ycore_void, "yvideo: decoder create failed", dr);
    }
    st->decoder = dr.value;

    if (nal_byte_count > 0u) {
        if (!yvideo_nal_ring_append(st, nal_bytes, nal_byte_count)) {
            yvideo_state_destroy(st);
            return YETTY_ERR(yetty_ycore_void, "yvideo: NAL ring grow failed");
        }
    }
    instance->instance_data = st;

    /* Pin the frame texture's dimensions BEFORE the generated factory
     * calls binder->submit (atlas pack uses these). `data` stays NULL
     * until the first decoded frame — the binder skips uploads on
     * NULL data, so the initial finalize doesn't fault. */
    instance->resource_set->textures[0].data = NULL;
    instance->resource_set->textures[0].width = video_w;
    instance->resource_set->textures[0].height = video_h;
    instance->resource_set->textures[0].dirty = 0;

    /* Subscribe this figure's embedded listener to the factory's
     * SHARED, refcounted animation timer. First subscriber creates
     * the timer; last unsubscribe destroys it. EOS / pause should
     * unsubscribe early to drop the refcount without killing the
     * instance — destroy_hook handles the final unsub. */
    if (instance->factory && instance->factory->event_loop) {
        instance->listener.handler = yvideo_on_tick;
        int period_ms = (fps > 0.0f) ? (int)(1000.0f / fps) : 33;
        struct yetty_ycore_void_result sr = yvideo_animation_subscribe(
            instance->factory, &instance->listener, period_ms);
        if (YETTY_IS_OK(sr)) {
            st->subscribed = true;
        } else {
            ywarn("yvideo: animation_subscribe failed; playback will be static");
            yetty_ycore_error_destroy(sr.error);
        }
    }

    yinfo("yvideo: instance created %ux%u @ %.1f fps (init NAL=%zu bytes, flags=0x%x)", video_w,
          video_h, (double)fps, nal_byte_count, flags);
    return YETTY_OK_VOID();
}

void yvideo_hook_instance_destroy(struct yetty_ydraw_figure_instance *instance)
{
    struct yvideo_instance_data *st = yvideo_state(instance);
    /* Drop our reference on the shared timer BEFORE freeing the
     * instance — otherwise the listener pointer (which lives inside
     * the instance) becomes a dangling entry in the timer's listener
     * list and the next tick scribbles freed memory. */
    if (st && st->subscribed && instance->factory) {
        yvideo_animation_unsubscribe(instance->factory, &instance->listener);
        st->subscribed = false;
    }
    yvideo_state_destroy(st);
    instance->instance_data = NULL;
    instance->listener.handler = NULL;
}

struct yetty_ycore_void_result yvideo_hook_instance_update(
    struct yetty_ydraw_figure_instance *instance, const void *payload, size_t size)
{
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: null instance");
    }
    if (size == 0u) {
        return YETTY_OK_VOID();
    }
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: null payload, size > 0");
    }
    struct yvideo_instance_data *st = yvideo_state(instance);
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: no instance state");
    }
    if (!yvideo_nal_ring_append(st, (const uint8_t *)payload, size)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo update: NAL ring grow failed");
    }
    return YETTY_OK_VOID();
}

/* render_pre is empty — all the work happens on timer ticks. Keeping
 * the hook stub since the generator unconditionally calls it. */
struct yetty_ycore_void_result yvideo_hook_instance_render_pre(
    struct yetty_ydraw_figure_instance *instance, struct yetty_ydraw_target *target, float x,
    float y)
{
    (void)instance;
    (void)target;
    (void)x;
    (void)y;
    return YETTY_OK_VOID();
}
