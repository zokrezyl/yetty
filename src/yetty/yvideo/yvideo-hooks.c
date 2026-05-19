/*
 * yvideo-hooks.c — prim-specific lifecycle for yvideo, plugged into
 * the generated factory (yvideo-gen.c) via the hook surface enabled by
 * `hooks: true` in yvideo.yaml. The generator owns ALL CPU/GPU sync
 * (uniform names/types/offsets, buffer layouts, texture descriptors)
 * via the schema; this file owns only the prim-specific state and
 * lifecycle:
 *
 *   - openh264 decoder (yvcodec) per instance
 *   - append-only Annex-B NAL ring + start-code splitter (openh264
 *     consumes ONE access unit per DecodeFrameNoDelay; multi-AU
 *     chunks silently drop everything after the first)
 *   - per-frame YUV→BGRA convert into a CPU-side scratch buffer
 *   - wall-clock-driven pump that advances the playhead each render
 *
 * The hook entry points (signatures fixed by the generator):
 *
 *   yvideo_hook_instance_create     — runs after RS clone + wire wiring
 *                                     but BEFORE binder->submit; sets
 *                                     instance_data + per-instance
 *                                     texture dimensions for atlas pack
 *   yvideo_hook_instance_destroy    — runs BEFORE binder destroy
 *   yvideo_hook_instance_update     — CMD_UPDATE payload = raw H.264
 *                                     bytes, append to ring
 *   yvideo_hook_instance_render_pre — every render, AFTER uniform
 *                                     refresh, BEFORE binder->update;
 *                                     pump decoder + write texture
 */

#include <yetty/yvideo/yvideo.h>
#include <yetty/yvideo/yvideo-gen.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yplatform/time.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvcodec/decoder.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YVIDEO_NAL_RING_INIT_CAP (256u * 1024u)

struct yvideo_instance_data {
    struct yetty_yvcodec_decoder *decoder;

    uint32_t video_w;
    uint32_t video_h;
    float fps;
    uint32_t flags;

    /* CPU scratch the binder uploads each render with dirty=1. */
    uint8_t *frame_rgba;
    size_t frame_rgba_size;
    bool have_frame;

    /* Wall-clock head; start_monotonic_sec is set lazily on first
     * successful decode so a delayed INIT doesn't snap past N frames. */
    double start_monotonic_sec;
    uint32_t frames_displayed;

    /* Append-only Annex-B NAL ring; the pump walks start codes and
     * feeds one NAL per DecodeFrameNoDelay call. */
    uint8_t *nal_ring;
    size_t nal_ring_size;
    size_t nal_ring_cap;
    size_t nal_ring_consumed;
};

static struct yvideo_instance_data *yvideo_state(struct yetty_ydraw_figure_instance *self)
{
    return (struct yvideo_instance_data *)self->instance_data;
}

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

/* Annex-B start code search: 00 00 01 or 00 00 00 01. Returns the
 * index of the leading 00 or (size_t)-1 when none is found. */
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

/* Pump forward to target_frame or until the ring runs out of complete
 * NALs. One DecodeFrameNoDelay call per NAL — openh264's contract. */
static void yvideo_pump_until(struct yvideo_instance_data *st, uint32_t target_frame)
{
    if (!st->decoder || !st->nal_ring) {
        return;
    }
    while (st->frames_displayed <= target_frame) {
        size_t curr = yvideo_find_start_code(st->nal_ring, st->nal_ring_size,
                                             st->nal_ring_consumed);
        if (curr == (size_t)-1) {
            return;
        }
        size_t next = yvideo_find_start_code(st->nal_ring, st->nal_ring_size, curr + 3);
        if (next == (size_t)-1) {
            return; /* current NAL not yet complete */
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
            continue; /* decoder buffering — feed the next NAL */
        }
        yetty_yvcodec_yuv_frame_yuv420_to_bgra(&yuv, st->frame_rgba);
        if (!st->have_frame) {
            st->start_monotonic_sec = yetty_yplatform_ytime_monotonic_sec();
            st->have_frame = true;
        }
        st->frames_displayed++;
    }
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

    /* Pin frame texture dimensions to video_w × video_h BEFORE the
     * generator-emitted code calls binder->submit (atlas pack uses
     * these). The actual `data` pointer stays NULL until the first
     * decoded frame — the binder skips uploads on NULL data, so the
     * initial finalize won't fault. */
    instance->resource_set->textures[0].data = NULL;
    instance->resource_set->textures[0].width = video_w;
    instance->resource_set->textures[0].height = video_h;
    instance->resource_set->textures[0].dirty = 0;

    yinfo("yvideo: instance created %ux%u @ %.1f fps (init NAL=%zu bytes, flags=0x%x)", video_w,
          video_h, (double)fps, nal_byte_count, flags);
    return YETTY_OK_VOID();
}

void yvideo_hook_instance_destroy(struct yetty_ydraw_figure_instance *instance)
{
    yvideo_state_destroy(yvideo_state(instance));
    instance->instance_data = NULL;
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

struct yetty_ycore_void_result yvideo_hook_instance_render_pre(
    struct yetty_ydraw_figure_instance *instance, struct yetty_ydraw_target *target, float x,
    float y)
{
    (void)target;
    (void)x;
    (void)y;
    struct yvideo_instance_data *st = yvideo_state(instance);
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "yvideo render_pre: no instance state");
    }
    /* Pump up to the wall-clock playhead, then point the frame texture
     * at the latest decoded RGBA with dirty=1. */
    if (st->fps > 0.0f && st->have_frame) {
        double now = yetty_yplatform_ytime_monotonic_sec();
        double t = now - st->start_monotonic_sec;
        uint32_t target_frame = (uint32_t)(t * (double)st->fps);
        if (target_frame > st->frames_displayed) {
            yvideo_pump_until(st, target_frame);
        }
    } else if (!st->have_frame) {
        yvideo_pump_until(st, 0u);
    }
    if (st->have_frame && st->frame_rgba && st->video_w > 0 && st->video_h > 0) {
        struct yetty_ydraw_gpu_resource_set *rs = instance->resource_set;
        rs->textures[0].data = st->frame_rgba;
        rs->textures[0].width = st->video_w;
        rs->textures[0].height = st->video_h;
        rs->textures[0].dirty = 1;
    }
    return YETTY_OK_VOID();
}
