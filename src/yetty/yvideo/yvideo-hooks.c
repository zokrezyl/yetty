/*
 * yvideo-hooks.c — prim-specific lifecycle for yvideo, plugged into
 * the generated factory (yvideo-gen.c) via the hook surface enabled by
 * `hooks: true` in yvideo.yaml. The generator owns all CPU/GPU sync
 * (uniform names/types/offsets, buffer layouts, texture descriptors).
 * This file owns the prim-specific state and lifecycle:
 *
 *   - openh264 video decoder (yvcodec) per instance
 *   - libopus audio decoder (yacodec) per instance — v2, optional
 *     (audio_codec=0 in the wire payload disables it)
 *   - platform audio device (miniaudio) per instance — see note about
 *     sharing further down
 *   - append-only Annex-B NAL ring + start-code splitter (openh264
 *     consumes ONE access unit per DecodeFrameNoDelay; multi-AU chunks
 *     silently drop everything after the first)
 *   - length-prefixed audio packet ring (Opus packets, one per element)
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
 *   scratch, marks the binder's texture dirty, calls request_render,
 *   and (when audio is active) pumps Opus packets → platform device.
 *
 * Master clock for A/V sync:
 *   When audio is present, the platform device's played_out_sec is the
 *   master clock: the chosen video frame is the one whose PTS bracket
 *   contains the current audio playback position. When audio is absent
 *   the legacy wall-clock-vs-start_monotonic_sec path is used (v1 shape).
 *
 * CMD_UPDATE wire format (v2 / #198 items 2+3):
 *   [u8 op][u8 reserved[3]][op-specific bytes...]
 *   Op 0x00 APPEND_NAL    — bytes are H.264 Annex-B
 *   Op 0x01 APPEND_AUDIO  — bytes are length-prefixed audio packets
 *                            (u32 length, length bytes, repeat)
 *   Future: SEEK_PTS_MS / SET_PLAYING / SET_SPEED / SET_LOOP (#198 item 3).
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
#include <yetty/yacodec/decoder.h>
#include <yetty/yplatform/audio.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YVIDEO_NAL_RING_INIT_CAP   (256u * 1024u)
#define YVIDEO_AUDIO_RING_INIT_CAP ( 64u * 1024u)
#define YVIDEO_PCM_PULL_MAX_FRAMES 1024u

/* CMD_UPDATE op codes — v2 typed payload. v1 senders that ship raw NAL
 * bytes are interpreted as op=first_byte, which yields garbage — fail
 * loud rather than silently corrupting the decoder. */
enum yvideo_update_op {
    YVIDEO_UPDATE_OP_APPEND_NAL   = 0x00,
    YVIDEO_UPDATE_OP_APPEND_AUDIO = 0x01,
};

/* Per-instance state — one per yvideo figure_instance. */
struct yvideo_instance_data {
    /* Video side. */
    struct yetty_yvcodec_decoder *decoder;

    uint32_t video_w;
    uint32_t video_h;
    float fps;
    uint32_t flags;

    uint8_t *frame_rgba;
    size_t frame_rgba_size;
    bool have_frame;
    bool tex_pinned;
    bool size_mismatch_warned;

    double start_monotonic_sec;
    uint32_t frames_displayed;

    uint8_t *nal_ring;
    size_t nal_ring_size;
    size_t nal_ring_cap;
    size_t nal_ring_consumed;

    /* Audio side (NULL when audio_codec=0). */
    struct yetty_yacodec_decoder *audio_decoder;
    struct yetty_yplatform_audio_device *audio_device;
    uint32_t audio_codec_id;        /* yetty_yacodec_codec value */
    uint32_t audio_sample_rate;
    uint32_t audio_channels;

    /* Audio packet ring — concatenation of (u32 length, length bytes)
     * tuples. Read cursor walks one packet per drain. */
    uint8_t *audio_ring;
    size_t audio_ring_size;
    size_t audio_ring_cap;
    size_t audio_ring_consumed;

    /* PCM scratch handed off to the platform device. Sized for the
     * worst-case Opus pull (120 ms @ 48 kHz × 2 ch = 11.5 KiB). */
    float pcm_scratch[YVIDEO_PCM_PULL_MAX_FRAMES * 2];

    bool subscribed;
};

/* Per-FACTORY state — one for the whole yvideo type, not per instance. */
struct yvideo_factory_state {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    uint32_t subscribers;
    int period_ms;
};

static struct yvideo_instance_data *yvideo_state(struct yetty_ydraw_figure_instance *self)
{
    return (struct yvideo_instance_data *)self->instance_data;
}

/*---------------------------------------------------------------------------
 * Refcounted shared timer (factory-level state).
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
    }
}

/*---------------------------------------------------------------------------
 * NAL ring helpers.
 *-------------------------------------------------------------------------*/

static int yvideo_byte_ring_append(uint8_t **ring, size_t *size, size_t *cap,
                                   size_t init_cap, const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return 1;
    }
    size_t need = *size + len;
    if (need > *cap) {
        size_t new_cap = *cap ? *cap : init_cap;
        while (new_cap < need) {
            new_cap *= 2;
        }
        uint8_t *nb = realloc(*ring, new_cap);
        if (!nb) {
            return 0;
        }
        *ring = nb;
        *cap = new_cap;
    }
    memcpy(*ring + *size, bytes, len);
    *size += len;
    return 1;
}

static int yvideo_nal_ring_append(struct yvideo_instance_data *st,
                                  const uint8_t *bytes, size_t len)
{
    return yvideo_byte_ring_append(&st->nal_ring, &st->nal_ring_size,
                                   &st->nal_ring_cap,
                                   YVIDEO_NAL_RING_INIT_CAP, bytes, len);
}

static int yvideo_audio_ring_append(struct yvideo_instance_data *st,
                                    const uint8_t *bytes, size_t len)
{
    return yvideo_byte_ring_append(&st->audio_ring, &st->audio_ring_size,
                                   &st->audio_ring_cap,
                                   YVIDEO_AUDIO_RING_INIT_CAP, bytes, len);
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

/*---------------------------------------------------------------------------
 * Audio pump — drain length-prefixed packets from the audio_ring, feed
 * them to the Opus decoder, and push decoded PCM into the platform
 * device's ring.
 *-------------------------------------------------------------------------*/

static void yvideo_pump_audio(struct yvideo_instance_data *st)
{
    if (!st->audio_decoder || !st->audio_device) {
        return;
    }
    /* Walk packets while at least the u32 length prefix is present. */
    while (st->audio_ring_consumed + sizeof(uint32_t) <= st->audio_ring_size) {
        uint32_t packet_len;
        memcpy(&packet_len, st->audio_ring + st->audio_ring_consumed,
               sizeof(packet_len));
        size_t payload_off = st->audio_ring_consumed + sizeof(uint32_t);
        /* Packet bytes are followed by 0..3 pad bytes for u32 alignment;
         * the next packet's length starts at the next u32 boundary. */
        size_t padded_len = (packet_len + 3u) & ~3u;
        if (payload_off + padded_len > st->audio_ring_size) {
            /* Incomplete packet — wait for more bytes. */
            return;
        }
        struct yetty_ycore_void_result fr = yetty_yacodec_decoder_feed(
            st->audio_decoder, st->audio_ring + payload_off, (size_t)packet_len);
        st->audio_ring_consumed = payload_off + padded_len;
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
            continue;
        }
        /* Drain PCM in chunks until the codec has nothing left. */
        for (;;) {
            size_t got = 0u;
            struct yetty_ycore_void_result pr = yetty_yacodec_decoder_pull_pcm(
                st->audio_decoder, st->pcm_scratch,
                YVIDEO_PCM_PULL_MAX_FRAMES, &got);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
                break;
            }
            if (got == 0u) {
                break;
            }
            struct yetty_ycore_size_result wr =
                yetty_yplatform_audio_device_write_pcm(st->audio_device,
                                                       st->pcm_scratch, got);
            if (YETTY_IS_ERR(wr)) {
                yetty_ycore_error_destroy(wr.error);
                break;
            }
            if (wr.value < got) {
                /* Device ring is full — pause feeding for this tick.
                 * The platform consumer will catch up, and the next
                 * tick re-tries from the same decoder buffer. */
                break;
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * Video pump — same as v1 but split into pump_until / target_frame.
 *-------------------------------------------------------------------------*/

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

/* Wall-clock or audio-clock target frame for the current tick. When an
 * audio device is active, use its played_out_sec as the master clock —
 * the video frame chosen is the one whose PTS bracket contains that
 * timestamp (frame_n = floor(audio_t * fps)). */
static uint32_t yvideo_target_frame(struct yvideo_instance_data *st)
{
    if (st->fps <= 0.0f) {
        return st->frames_displayed; /* paused */
    }
    if (!st->have_frame) {
        return 0;
    }
    double t;
    if (st->audio_device) {
        t = yetty_yplatform_audio_device_played_out_sec(st->audio_device);
    } else {
        extern double yetty_yplatform_ytime_monotonic_sec(void);
        double now = yetty_yplatform_ytime_monotonic_sec();
        t = now - st->start_monotonic_sec;
    }
    if (t < 0.0) {
        return 0;
    }
    return (uint32_t)(t * (double)st->fps);
}

/*---------------------------------------------------------------------------
 * Timer listener — pumps video + audio every tick.
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

    /* Pump audio FIRST so the master clock advances before we pick the
     * matching video frame. Cheap when no audio is configured (early
     * return inside the pump). */
    yvideo_pump_audio(st);

    uint32_t target = yvideo_target_frame(st);
    uint32_t produced = yvideo_pump_until(st, target);

    if (st->have_frame && st->start_monotonic_sec == 0.0) {
        extern double yetty_yplatform_ytime_monotonic_sec(void);
        st->start_monotonic_sec = yetty_yplatform_ytime_monotonic_sec();
        /* Lazy device start — only kick it once we actually have a
         * frame to show. Removes startup-time A/V skew (audio would
         * otherwise start playing before the first decoded video frame
         * appears). */
        if (st->audio_device) {
            struct yetty_ycore_void_result sr =
                yetty_yplatform_audio_device_start(st->audio_device);
            if (YETTY_IS_ERR(sr)) {
                ywarn("yvideo: audio device start failed; playback will be silent");
                yetty_ycore_error_destroy(sr.error);
            }
        }
    }

    if (produced == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

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
    instance->dirty = 1;

    struct yetty_yevent_event_loop *loop = instance->factory->event_loop;
    loop->ops->request_render(loop);
    return YETTY_OK(yetty_ycore_int, 0);
}

/*---------------------------------------------------------------------------
 * Instance lifecycle teardown.
 *-------------------------------------------------------------------------*/

static void yvideo_state_destroy(struct yvideo_instance_data *st)
{
    if (!st) {
        return;
    }
    if (st->decoder) {
        yetty_yvcodec_decoder_destroy(st->decoder);
    }
    if (st->audio_decoder) {
        yetty_yacodec_decoder_destroy(st->audio_decoder);
    }
    if (st->audio_device) {
        /* Stop before destroy so the backend callback unwinds first —
         * destroy frees the ring the callback drains from. */
        struct yetty_ycore_void_result spr =
            yetty_yplatform_audio_device_stop(st->audio_device);
        if (YETTY_IS_ERR(spr)) {
            yetty_ycore_error_destroy(spr.error);
        }
        yetty_yplatform_audio_device_destroy(st->audio_device);
    }
    free(st->frame_rgba);
    free(st->nal_ring);
    free(st->audio_ring);
    free(st);
}

/*---------------------------------------------------------------------------
 * Hook implementations.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yvideo_hook_instance_create(
    struct yetty_ydraw_figure_instance *instance, const void *buffer_data, size_t size)
{
    (void)size;
    /* v2 wire layout: 2 header words (type_id, payload_size), 12 uniform
     * words, 2 buffer-length words, then payloads in declaration order
     * (nal_stream, audio_stream). */
    const uint32_t *data = (const uint32_t *)buffer_data;
    const uint32_t *payload = data + 2;
    uint32_t video_w           = payload[4];
    uint32_t video_h           = payload[5];
    float    fps               = ((const float *)payload)[6];
    uint32_t flags             = payload[8];
    uint32_t audio_codec       = payload[9];
    uint32_t audio_sample_rate = payload[10];
    uint32_t audio_channels    = payload[11];
    uint32_t nal_words         = payload[12];
    uint32_t audio_words       = payload[13];
    const uint8_t *nal_bytes   = (const uint8_t *)(payload + 14);
    const uint8_t *audio_bytes = nal_bytes + (size_t)nal_words * 4u;
    size_t   nal_byte_count    = (size_t)nal_words   * 4u;
    size_t   audio_byte_count  = (size_t)audio_words * 4u;

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

    /* Audio path — opt-in. Failure here is non-fatal: the video plays
     * silent. The user gets one ywarn and the rest of the figure
     * lifecycle continues normally. */
    if (audio_codec != 0u) {
        st->audio_codec_id    = audio_codec;
        st->audio_sample_rate = audio_sample_rate;
        st->audio_channels    = audio_channels;

        struct yetty_yacodec_decoder_ptr_result adr =
            yetty_yacodec_decoder_create((enum yetty_yacodec_codec)audio_codec,
                                         audio_sample_rate, audio_channels);
        if (YETTY_IS_ERR(adr)) {
            ywarn("yvideo: audio decoder create failed; video will play silent");
            yetty_ycore_error_destroy(adr.error);
        } else {
            st->audio_decoder = adr.value;
            struct yetty_yplatform_audio_device_ptr_result devr =
                yetty_yplatform_audio_device_create(audio_sample_rate,
                                                    audio_channels);
            if (YETTY_IS_ERR(devr)) {
                ywarn("yvideo: audio device create failed; video will play silent");
                yetty_ycore_error_destroy(devr.error);
                yetty_yacodec_decoder_destroy(st->audio_decoder);
                st->audio_decoder = NULL;
            } else {
                st->audio_device = devr.value;
            }
        }

        if (audio_byte_count > 0u && st->audio_decoder) {
            if (!yvideo_audio_ring_append(st, audio_bytes, audio_byte_count)) {
                ywarn("yvideo: audio ring grow failed; dropping initial audio");
            }
        }
    }

    instance->instance_data = st;

    instance->resource_set->textures[0].data = NULL;
    instance->resource_set->textures[0].width = video_w;
    instance->resource_set->textures[0].height = video_h;
    instance->resource_set->textures[0].dirty = 0;

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

    yinfo("yvideo: instance created %ux%u @ %.1f fps (NAL=%zu B, audio_codec=%u %u Hz × %u ch, audio=%zu B, flags=0x%x)",
          video_w, video_h, (double)fps, nal_byte_count,
          audio_codec, audio_sample_rate, audio_channels, audio_byte_count, flags);
    return YETTY_OK_VOID();
}

void yvideo_hook_instance_destroy(struct yetty_ydraw_figure_instance *instance)
{
    struct yvideo_instance_data *st = yvideo_state(instance);
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

    /* v2 typed payload: [u8 op][u8 reserved[3]][bytes...] — at least 4
     * leading bytes are required for the header. Reject shorter
     * payloads loudly rather than treating them as v1 raw NAL bytes,
     * which would only ever match by accident. */
    if (size < 4u) {
        return YETTY_ERR(yetty_ycore_void,
                         "yvideo update: payload < 4 bytes (no op header)");
    }
    const uint8_t *p = (const uint8_t *)payload;
    uint8_t op = p[0];
    const uint8_t *body = p + 4;
    size_t body_size = size - 4u;

    switch (op) {
    case YVIDEO_UPDATE_OP_APPEND_NAL:
        if (body_size > 0u) {
            if (!yvideo_nal_ring_append(st, body, body_size)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yvideo update: NAL ring grow failed");
            }
        }
        return YETTY_OK_VOID();

    case YVIDEO_UPDATE_OP_APPEND_AUDIO:
        if (!st->audio_decoder || !st->audio_device) {
            /* Sender supplied audio bytes but the figure was created
             * without audio_codec set — drop silently rather than
             * holding bytes that will never decode. */
            return YETTY_OK_VOID();
        }
        if (body_size > 0u) {
            if (!yvideo_audio_ring_append(st, body, body_size)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yvideo update: audio ring grow failed");
            }
        }
        return YETTY_OK_VOID();

    default:
        return YETTY_ERR(yetty_ycore_void, "yvideo update: unknown op");
    }
}

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
