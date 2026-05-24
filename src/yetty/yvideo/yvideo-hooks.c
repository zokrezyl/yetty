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
 *   yetty_ydraw_figure embeds a yetty_yevent_event_listener.
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
#include <yetty/yplatform/ycoroutine.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YVIDEO_NAL_RING_INIT_CAP (256u * 1024u)
#define YVIDEO_AUDIO_RING_INIT_CAP (64u * 1024u)
#define YVIDEO_PCM_PULL_MAX_FRAMES 1024u

/* Retention caps (#198 item 5). Senders push bytes append-only and the
 * wire is one-way — without retention, a 1-hour 2 Mbps H.264 stream
 * pins ~900 MB in RAM. We cap each ring at a fixed budget and slide
 * the read+consumed cursors back to 0 when growth exceeds the cap, but
 * ONLY drop bytes that the decoder has already consumed (preserving the
 * un-decoded tail). Seeking before lowest_retained_pts_ms is undefined
 * — the sender can replay from an earlier IDR, but the figure can't
 * re-request because the wire is one-way. */
#define YVIDEO_NAL_RING_MAX_BYTES (64u * 1024u * 1024u)   /* 64 MB */
#define YVIDEO_AUDIO_RING_MAX_BYTES (16u * 1024u * 1024u) /* 16 MB */

/* CMD_UPDATE op codes live in the public yvideo.h header — both sender
 * and receiver reference YETTY_YVIDEO_UPDATE_OP_* directly. */

/* Per-instance state — one per yvideo figure_instance. */
struct yvideo_instance_data {
    /* Video side. */
    struct yetty_yvcodec_decoder *decoder;

    uint32_t video_w;
    uint32_t video_h;
    uint32_t chroma_w; /* video_w / 2 for 4:2:0 */
    uint32_t chroma_h; /* video_h / 2 for 4:2:0 */
    float fps;
    uint32_t flags;

    /* Three R8 plane buffers — tightly packed (no stride padding). The
     * decoder's YUV output has its own stride per plane, so per-frame
     * we memcpy row-by-row into these stride-tight buffers, then the
     * binder uploads each one as a separate R8 texture. v1's
     * frame_rgba (single W×H×4 buffer + CPU convert) is gone. */
    uint8_t *y_buf; /* size = video_w * video_h */
    uint8_t *u_buf; /* size = chroma_w * chroma_h */
    uint8_t *v_buf; /* size = chroma_w * chroma_h */
    size_t y_buf_size;
    size_t uv_buf_size;
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
    uint32_t audio_codec_id; /* yetty_yacodec_codec value */
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

    /* Decode coroutine (#198 item 4). Spawned at instance_create, runs
     * the decode loop, yields between frames so big catch-ups don't
     * stall the event loop. Resumed from the timer tick AND from
     * CMD_UPDATE handlers (the latter so newly-arrived bytes start
     * decoding immediately, not on the next tick). should_exit is set
     * by destroy_hook so the coro entry can return cleanly before
     * coro_destroy. */
    struct yetty_yplatform_coro *decode_coro;
    bool coro_should_exit;
    /* Target the coro chases on each wake — set by the tick or update
     * handler before resuming. The coro decodes until frames_displayed
     * exceeds this, then yields. Bumping the target == "decode more
     * now". */
    uint32_t coro_target_frame;

    /* Playback control state — driven by the v2 typed CMD_UPDATE ops
     * (#198 item 3). */
    bool paused;
    float speed;            /* 1.0 = normal */
    double seek_offset_sec; /* PTS at the start of the current
                                    * playback segment — reported for
                                    * display; does not affect decode. */
    /* When using the audio device as master clock, played_out_sec is
     * monotonic across the lifetime of the device — to "reset" the
     * effective clock at seek we subtract this offset. Captured at
     * seek time. */
    double audio_clock_offset_sec;
    /* Wall-clock pause bookkeeping (used when there is no audio).
     * paused_at_sec holds the wall-clock time of the pause; on resume
     * we shift start_monotonic_sec by the elapsed pause duration so
     * the playhead picks up where it left off. */
    double paused_at_sec;
};

/* Per-FACTORY state — one for the whole yvideo type, not per instance. */
struct yvideo_factory_state {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    uint32_t subscribers;
    int period_ms;
};

static struct yvideo_instance_data *yvideo_state(struct yetty_ydraw_figure *self)
{
    return (struct yvideo_instance_data *)self->instance_data;
}

/*---------------------------------------------------------------------------
 * Refcounted shared timer (factory-level state).
 *-------------------------------------------------------------------------*/

static struct yvideo_factory_state *yvideo_factory_state_get_or_init(
    struct yetty_ydraw_concrete_factory *factory, int period_ms)
{
    if (!factory) {
        return NULL;
    }
    if (factory->hook_data) {
        return (struct yvideo_factory_state *)factory->hook_data;
    }
    struct yvideo_factory_state *fs = calloc(1, sizeof(*fs));
    if (!fs) {
        return NULL;
    }
    fs->event_loop = factory->event_loop;
    fs->timer_id = -1;
    fs->subscribers = 0;
    fs->period_ms = period_ms;
    factory->hook_data = fs;
    return fs;
}

static struct yetty_ycore_void_result yvideo_animation_subscribe(
    struct yetty_ydraw_concrete_factory *factory, struct yetty_yevent_event_listener *listener,
    int period_ms_hint)
{
    if (!factory || !factory->event_loop) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: no event_loop on factory");
    }
    if (!listener || !listener->handler) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: listener has no handler");
    }
    struct yvideo_factory_state *fs = yvideo_factory_state_get_or_init(factory, period_ms_hint);
    if (!fs) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: factory_state alloc failed");
    }
    struct yetty_yevent_event_loop *loop = fs->event_loop;

    if (fs->subscribers == 0) {
        struct yetty_yevent_timer_id_result tr = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yvideo: create_timer");
        fs->timer_id = tr.value;
        if (period_ms_hint < 1) {
            period_ms_hint = 33;
        }
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
    if (!factory) {
        return;
    }
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

/* Drop `drop_bytes` from the front of the ring — slide remaining bytes
 * down and adjust the consumed cursor. The caller passes a count that
 * never exceeds *consumed (we only drop bytes the decoder has already
 * eaten). */
static void yvideo_byte_ring_drop_front(uint8_t *ring, size_t *size, size_t *consumed,
                                        size_t drop_bytes)
{
    if (drop_bytes == 0u || drop_bytes > *consumed) {
        return;
    }
    size_t remaining = *size - drop_bytes;
    if (remaining > 0u) {
        memmove(ring, ring + drop_bytes, remaining);
    }
    *size -= drop_bytes;
    *consumed -= drop_bytes;
}

/* Append with retention. When the post-append size would exceed
 * max_bytes AND some bytes have been consumed, slide consumed bytes
 * out of the front before appending. The capacity itself still grows
 * (geometric realloc) until we're below max_bytes again; the steady
 * state is roughly max_bytes regardless of stream duration. */
static int yvideo_byte_ring_append_capped(uint8_t **ring, size_t *size, size_t *cap,
                                          size_t *consumed, size_t init_cap, size_t max_bytes,
                                          const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return 1;
    }
    /* If we'd overflow the retention cap, drop as many already-consumed
     * bytes as we can to make room. We don't try to be fancy here:
     * always drop ALL consumed bytes when growth would exceed the cap.
     * This keeps the un-decoded tail intact. */
    if (*size + len > max_bytes && *consumed > 0u) {
        yvideo_byte_ring_drop_front(*ring, size, consumed, *consumed);
    }
    size_t need = *size + len;
    if (need > *cap) {
        size_t new_cap = *cap ? *cap : init_cap;
        while (new_cap < need) {
            new_cap *= 2u;
        }
        /* Don't grow beyond the cap-rounded-up-to-power-of-two — once
         * we hit the retention ceiling, the drop_front above is the
         * only way to make room. If the un-decoded tail itself exceeds
         * max_bytes (decoder is starved), we still grow — better to
         * overshoot the budget than to lose bytes the decoder will
         * eventually need. */
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

static int yvideo_nal_ring_append(struct yvideo_instance_data *st, const uint8_t *bytes, size_t len)
{
    return yvideo_byte_ring_append_capped(&st->nal_ring, &st->nal_ring_size, &st->nal_ring_cap,
                                          &st->nal_ring_consumed, YVIDEO_NAL_RING_INIT_CAP,
                                          YVIDEO_NAL_RING_MAX_BYTES, bytes, len);
}

static int yvideo_audio_ring_append(struct yvideo_instance_data *st, const uint8_t *bytes,
                                    size_t len)
{
    return yvideo_byte_ring_append_capped(
        &st->audio_ring, &st->audio_ring_size, &st->audio_ring_cap, &st->audio_ring_consumed,
        YVIDEO_AUDIO_RING_INIT_CAP, YVIDEO_AUDIO_RING_MAX_BYTES, bytes, len);
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
        if (i + 4 <= size && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 && buf[i + 3] == 1) {
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
    if (st->paused || !st->audio_decoder || !st->audio_device) {
        return;
    }
    /* Walk packets while at least the u32 length prefix is present. */
    while (st->audio_ring_consumed + sizeof(uint32_t) <= st->audio_ring_size) {
        uint32_t packet_len;
        memcpy(&packet_len, st->audio_ring + st->audio_ring_consumed, sizeof(packet_len));
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
                st->audio_decoder, st->pcm_scratch, YVIDEO_PCM_PULL_MAX_FRAMES, &got);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
                break;
            }
            if (got == 0u) {
                break;
            }
            struct yetty_ycore_size_result wr =
                yetty_yplatform_audio_device_write_pcm(st->audio_device, st->pcm_scratch, got);
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
 * Video pump — single-step, drives the decode coroutine (#198 item 4).
 *-------------------------------------------------------------------------*/

enum yvideo_decode_step {
    YVIDEO_DECODE_STEP_FRAME,    /* produced a frame — caller yields  */
    YVIDEO_DECODE_STEP_PROGRESS, /* fed a NAL but no frame yet (SPS/  */
                                 /* PPS/non-IDR slice header) — loop */
    YVIDEO_DECODE_STEP_NO_INPUT, /* no complete NAL available — yield */
};

/* One decoder step. Either feeds at most one NAL or reports no input.
 * Three-valued return lets the coroutine decide whether to yield. */
static enum yvideo_decode_step yvideo_decode_one_step(struct yvideo_instance_data *st)
{
    if (!st->decoder || !st->nal_ring) {
        return YVIDEO_DECODE_STEP_NO_INPUT;
    }
    size_t curr = yvideo_find_start_code(st->nal_ring, st->nal_ring_size, st->nal_ring_consumed);
    if (curr == (size_t)-1) {
        return YVIDEO_DECODE_STEP_NO_INPUT;
    }
    size_t next = yvideo_find_start_code(st->nal_ring, st->nal_ring_size, curr + 3);
    if (next == (size_t)-1) {
        return YVIDEO_DECODE_STEP_NO_INPUT;
    }
    size_t nal_size = next - curr;
    struct yetty_ycore_void_result fr =
        yetty_yvcodec_decoder_feed(st->decoder, st->nal_ring + curr, nal_size);
    st->nal_ring_consumed = next;
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return YVIDEO_DECODE_STEP_PROGRESS;
    }
    struct yetty_yvcodec_yuv_frame yuv = {0};
    bool got = false;
    struct yetty_ycore_void_result gr = yetty_yvcodec_decoder_get_frame(st->decoder, &yuv, &got);
    if (YETTY_IS_ERR(gr)) {
        yetty_ycore_error_destroy(gr.error);
        return YVIDEO_DECODE_STEP_PROGRESS;
    }
    if (!got) {
        return YVIDEO_DECODE_STEP_PROGRESS;
    }
    if (yuv.width != st->video_w || yuv.height != st->video_h) {
        if (!st->size_mismatch_warned) {
            yerror("yvideo: decoded frame %ux%u != declared %ux%u; "
                   "dropping frames (wire video_w/h must match source SPS)",
                   yuv.width, yuv.height, st->video_w, st->video_h);
            st->size_mismatch_warned = true;
        }
        return YVIDEO_DECODE_STEP_PROGRESS;
    }
    /* Stride-strip copy — see comment block in the previous v2 version. */
    for (uint32_t row = 0u; row < st->video_h; row++) {
        memcpy(st->y_buf + (size_t)row * st->video_w, yuv.y_plane + (size_t)row * yuv.y_stride,
               st->video_w);
    }
    for (uint32_t row = 0u; row < st->chroma_h; row++) {
        memcpy(st->u_buf + (size_t)row * st->chroma_w, yuv.u_plane + (size_t)row * yuv.u_stride,
               st->chroma_w);
        memcpy(st->v_buf + (size_t)row * st->chroma_w, yuv.v_plane + (size_t)row * yuv.v_stride,
               st->chroma_w);
    }
    st->have_frame = true;
    st->frames_displayed++;
    return YVIDEO_DECODE_STEP_FRAME;
}

/* Decode coroutine entry. Pumps audio + video to completion, then
 * yields once at the end. We don't yield between individual frames —
 * a 20-frame catch-up would otherwise pay 20 context-switch round-
 * trips through the tick loop. The cost we pay instead is that a big
 * burst can stall the event loop briefly; in exchange CMD_UPDATE can
 * still wake us up between bursts to start decoding immediately.
 *
 * The predicate is `<=` not `<`: a fresh instance starts with
 * frames_displayed=0 AND coro_target_frame=0 (target is 0 until
 * `have_frame` flips true — see yvideo_target_frame). We need the
 * first decode attempt to bootstrap that flip, so the loop must run
 * the body at least once when both are 0. */
static void yvideo_decode_coro_entry(void *arg)
{
    struct yvideo_instance_data *st = (struct yvideo_instance_data *)arg;
    while (!st->coro_should_exit) {
        if (!st->paused) {
            yvideo_pump_audio(st);

            while (st->frames_displayed <= st->coro_target_frame && !st->coro_should_exit) {
                enum yvideo_decode_step step = yvideo_decode_one_step(st);
                if (step == YVIDEO_DECODE_STEP_NO_INPUT) {
                    break;
                }
            }
        }
        yetty_yplatform_coro_yield();
    }
}

/* External wakeup — resumes the decode coro if it's alive and yielded.
 * Called from the timer tick AND from CMD_UPDATE handlers. Cheap to
 * call when nothing has changed: the coro just yields back immediately. */
static void yvideo_wake_decoder(struct yvideo_instance_data *st)
{
    if (st->decode_coro && !yetty_yplatform_coro_is_finished(st->decode_coro)) {
        yetty_yplatform_coro_resume(st->decode_coro);
    }
}

/* Wall-clock or audio-clock target frame for the current tick. When an
 * audio device is active, use its played_out_sec as the master clock —
 * the video frame chosen is the one whose PTS bracket contains that
 * timestamp (frame_n = floor(audio_t * fps * speed)).
 *
 * speed != 1.0 desyncs from audio (audio still plays at 1×); that's
 * a documented v1 limitation — Opus pitch-preserving time-stretch
 * needs a resampler we don't ship yet. */
static uint32_t yvideo_target_frame(struct yvideo_instance_data *st)
{
    if (st->paused || st->fps <= 0.0f) {
        return st->frames_displayed;
    }
    if (!st->have_frame) {
        return 0;
    }
    double t;
    if (st->audio_device) {
        double played = yetty_yplatform_audio_device_played_out_sec(st->audio_device);
        t = played - st->audio_clock_offset_sec;
    } else {
        extern double yetty_yplatform_ytime_monotonic_sec(void);
        double now = yetty_yplatform_ytime_monotonic_sec();
        t = now - st->start_monotonic_sec;
    }
    if (t < 0.0) {
        return 0;
    }
    double speed = (st->speed > 0.0f) ? (double)st->speed : 1.0;
    return (uint32_t)(t * (double)st->fps * speed);
}

/*---------------------------------------------------------------------------
 * Timer listener — pumps video + audio every tick.
 *-------------------------------------------------------------------------*/

static struct yetty_ycore_int_result yvideo_on_tick(struct yetty_yevent_event_listener *listener,
                                                    const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ydraw_figure *instance =
        container_of(listener, struct yetty_ydraw_figure, listener);
    struct yvideo_instance_data *st = yvideo_state(instance);
    if (!st || !instance->factory || !instance->factory->event_loop) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Update the coroutine's target frame from the master clock and
     * resume it. The coro pumps audio + video one step at a time,
     * yielding between frames so a big catch-up doesn't stall this
     * thread. After it yields back here, st->frames_displayed reflects
     * the new state. */
    uint32_t prev_displayed = st->frames_displayed;
    st->coro_target_frame = yvideo_target_frame(st);
    yvideo_wake_decoder(st);
    uint32_t produced = st->frames_displayed - prev_displayed;

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

    if (instance->resource_set && st->y_buf && st->u_buf && st->v_buf) {
        struct yetty_ydraw_gpu_resource_set *rs = instance->resource_set;
        if (!st->tex_pinned) {
            rs->textures[0].data = st->y_buf;
            rs->textures[0].width = st->video_w;
            rs->textures[0].height = st->video_h;
            rs->textures[1].data = st->u_buf;
            rs->textures[1].width = st->chroma_w;
            rs->textures[1].height = st->chroma_h;
            rs->textures[2].data = st->v_buf;
            rs->textures[2].width = st->chroma_w;
            rs->textures[2].height = st->chroma_h;
            st->tex_pinned = true;
        }
        rs->textures[0].dirty = 1;
        rs->textures[1].dirty = 1;
        rs->textures[2].dirty = 1;
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
    /* Drain + destroy the decode coro BEFORE freeing the decoder /
     * ring buffers it walks. The coro is suspended (yielded) by
     * invariant — we only ever reach here from the event-loop thread,
     * and the coro never holds control across event-loop work. We
     * flip the exit flag and resume; the entry function sees it,
     * returns, and the coro is finished — only then is destroy safe. */
    if (st->decode_coro) {
        st->coro_should_exit = true;
        if (!yetty_yplatform_coro_is_finished(st->decode_coro)) {
            yetty_yplatform_coro_resume(st->decode_coro);
        }
        yetty_yplatform_coro_destroy(st->decode_coro);
        st->decode_coro = NULL;
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
        struct yetty_ycore_void_result spr = yetty_yplatform_audio_device_stop(st->audio_device);
        if (YETTY_IS_ERR(spr)) {
            yetty_ycore_error_destroy(spr.error);
        }
        yetty_yplatform_audio_device_destroy(st->audio_device);
    }
    free(st->y_buf);
    free(st->u_buf);
    free(st->v_buf);
    free(st->nal_ring);
    free(st->audio_ring);
    free(st);
}

/*---------------------------------------------------------------------------
 * Playback control — SEEK / pause / speed / loop.
 *
 * These are called from yvideo_hook_instance_update on the event-loop
 * thread (same thread as the timer-driven pumps), so mutating instance
 * state is race-free w.r.t. the pump. The audio device's backend
 * callback runs on a separate thread — we stop the device around any
 * operation that mutates the ring (seek's flush) so the consumer is
 * quiescent during the reset.
 *-------------------------------------------------------------------------*/

static void yvideo_do_seek(struct yvideo_instance_data *st, uint32_t pts_ms)
{
    /* Stop audio first so the backend thread isn't reading the ring
     * while we flush it. Restart after flush so playback resumes when
     * the sender feeds new bytes. */
    if (st->audio_device) {
        struct yetty_ycore_void_result sr = yetty_yplatform_audio_device_stop(st->audio_device);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
        yetty_yplatform_audio_device_flush(st->audio_device);
        /* Capture the device's lifetime played_out so we can subtract
         * it to derive a fresh effective clock. The counter itself is
         * monotonic — we shift, not reset. */
        st->audio_clock_offset_sec = yetty_yplatform_audio_device_played_out_sec(st->audio_device);
    }
    if (st->decoder) {
        yetty_yvcodec_decoder_reset(st->decoder);
    }
    if (st->audio_decoder) {
        yetty_yacodec_decoder_reset(st->audio_decoder);
    }
    /* Clear both rings — sender re-feeds from the seek point. */
    st->nal_ring_size = 0u;
    st->nal_ring_consumed = 0u;
    st->audio_ring_size = 0u;
    st->audio_ring_consumed = 0u;
    /* Frame counter restarts at 0; first decoded frame after the seek
     * is treated as the new t=0 of the playback segment. */
    st->frames_displayed = 0u;
    st->have_frame = false;
    st->start_monotonic_sec = 0.0;
    st->paused_at_sec = 0.0;
    /* Stored only for reporting — the playhead math doesn't use it. */
    st->seek_offset_sec = (double)pts_ms / 1000.0;

    if (st->audio_device && !st->paused) {
        /* Lazy device start in the on-tick path waits for the first
         * decoded frame; restart immediately here so the next decoded
         * audio packet can flow into the device without an extra
         * round-trip through the lazy-start condition. */
        struct yetty_ycore_void_result sr = yetty_yplatform_audio_device_start(st->audio_device);
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
    }

    yinfo("yvideo: seek to %u ms", pts_ms);
}

static void yvideo_do_set_playing(struct yvideo_instance_data *st, bool playing)
{
    if (playing == !st->paused) {
        return; /* no-op */
    }
    extern double yetty_yplatform_ytime_monotonic_sec(void);
    double now = yetty_yplatform_ytime_monotonic_sec();

    if (playing) {
        /* Resume. */
        if (st->audio_device) {
            struct yetty_ycore_void_result sr =
                yetty_yplatform_audio_device_start(st->audio_device);
            if (YETTY_IS_ERR(sr)) {
                yetty_ycore_error_destroy(sr.error);
            }
        } else if (st->paused_at_sec > 0.0) {
            /* Wall-clock master: shift start_monotonic_sec by the pause
             * duration so the playhead picks up where it was. */
            st->start_monotonic_sec += now - st->paused_at_sec;
            st->paused_at_sec = 0.0;
        }
        st->paused = false;
    } else {
        /* Pause. */
        if (st->audio_device) {
            /* Stopping the device freezes its played_out counter — no
             * extra offset bookkeeping needed for the audio path. */
            struct yetty_ycore_void_result sr = yetty_yplatform_audio_device_stop(st->audio_device);
            if (YETTY_IS_ERR(sr)) {
                yetty_ycore_error_destroy(sr.error);
            }
        } else {
            st->paused_at_sec = now;
        }
        st->paused = true;
    }
    yinfo("yvideo: %s", playing ? "play" : "pause");
}

static void yvideo_do_set_speed(struct yvideo_instance_data *st, float speed)
{
    if (speed <= 0.0f) {
        ywarn("yvideo: rejected non-positive speed %.3f", (double)speed);
        return;
    }
    st->speed = speed;
    yinfo("yvideo: speed %.3f", (double)speed);
}

static void yvideo_do_set_loop(struct yvideo_instance_data *st, bool loop)
{
    if (loop) {
        st->flags |= YETTY_YVIDEO_FLAG_LOOP;
    } else {
        st->flags &= ~YETTY_YVIDEO_FLAG_LOOP;
    }
    yinfo("yvideo: loop %s", loop ? "on" : "off");
}

/*---------------------------------------------------------------------------
 * Hook implementations.
 *-------------------------------------------------------------------------*/

struct yetty_ycore_void_result yvideo_hook_instance_create(struct yetty_ydraw_figure *instance,
                                                           const void *buffer_data, size_t size)
{
    (void)size;
    /* v2 wire layout: 2 header words (type_id, payload_size), 14 uniform
     * words (post-#198 item 1 — chroma_w/h added), 2 buffer-length
     * words, then payloads in declaration order (nal_stream,
     * audio_stream). */
    const uint32_t *data = (const uint32_t *)buffer_data;
    const uint32_t *payload = data + 2;
    uint32_t video_w = payload[4];
    uint32_t video_h = payload[5];
    uint32_t chroma_w = payload[6];
    uint32_t chroma_h = payload[7];
    float fps = ((const float *)payload)[8];
    uint32_t flags = payload[10];
    uint32_t audio_codec = payload[11];
    uint32_t audio_sample_rate = payload[12];
    uint32_t audio_channels = payload[13];
    uint32_t nal_words = payload[14];
    uint32_t audio_words = payload[15];
    const uint8_t *nal_bytes = (const uint8_t *)(payload + 16);
    const uint8_t *audio_bytes = nal_bytes + (size_t)nal_words * 4u;
    size_t nal_byte_count = (size_t)nal_words * 4u;
    size_t audio_byte_count = (size_t)audio_words * 4u;

    if (video_w == 0 || video_h == 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "yvideo: video_w/video_h must be > 0 (set in INIT envelope)");
    }
    /* Receiver-side default if the sender left chroma_w/h at 0 — 4:2:0
     * is the only mode openh264 emits, so divide by 2 is safe. */
    if (chroma_w == 0u) {
        chroma_w = video_w / 2u;
    }
    if (chroma_h == 0u) {
        chroma_h = video_h / 2u;
    }

    struct yvideo_instance_data *st = calloc(1, sizeof(*st));
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: state alloc failed");
    }
    st->video_w = video_w;
    st->video_h = video_h;
    st->chroma_w = chroma_w;
    st->chroma_h = chroma_h;
    st->fps = fps;
    st->flags = flags;
    st->paused = false;
    st->speed = 1.0f;
    st->seek_offset_sec = 0.0;
    st->audio_clock_offset_sec = 0.0;
    st->paused_at_sec = 0.0;
    st->y_buf_size = (size_t)video_w * (size_t)video_h;
    st->uv_buf_size = (size_t)chroma_w * (size_t)chroma_h;
    st->y_buf = calloc(1u, st->y_buf_size);
    st->u_buf = calloc(1u, st->uv_buf_size);
    st->v_buf = calloc(1u, st->uv_buf_size);
    if (!st->y_buf || !st->u_buf || !st->v_buf) {
        yvideo_state_destroy(st);
        return YETTY_ERR(yetty_ycore_void, "yvideo: plane buf alloc failed");
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
        st->audio_codec_id = audio_codec;
        st->audio_sample_rate = audio_sample_rate;
        st->audio_channels = audio_channels;

        struct yetty_yacodec_decoder_ptr_result adr = yetty_yacodec_decoder_create(
            (enum yetty_yacodec_codec)audio_codec, audio_sample_rate, audio_channels);
        if (YETTY_IS_ERR(adr)) {
            ywarn("yvideo: audio decoder create failed; video will play silent");
            yetty_ycore_error_destroy(adr.error);
        } else {
            st->audio_decoder = adr.value;
            struct yetty_yplatform_audio_device_ptr_result devr =
                yetty_yplatform_audio_device_create(audio_sample_rate, audio_channels);
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

    /* Spawn the decode coroutine (#198 item 4). Defaults to 0 target
     * frame; the first tick sets it from the master clock. Stack hint
     * 0 → platform default (1 MiB on desktop) — generous, but openh264
     * has been observed to need a few hundred KB of stack during macroblock
     * deblocking. */
    {
        struct yplatform_coro_ptr_result cr = yetty_yplatform_coro_spawn(
            yvideo_decode_coro_entry, st, /*stack_hint=*/0u, "yvideo-decode");
        if (YETTY_IS_OK(cr)) {
            st->decode_coro = cr.value;
        } else {
            ywarn("yvideo: decode coro spawn failed; falling back to silent video");
            yetty_ycore_error_destroy(cr.error);
            /* Without the coro the figure won't decode anything — the
             * tick handler's wake call is a no-op when decode_coro is
             * NULL. Better than hard-failing the create. */
        }
    }

    /* Pre-pin all three plane textures' dimensions BEFORE binder->submit
     * — atlas pack uses these. data stays NULL until the first decoded
     * frame; the binder skips uploads on NULL data, so the initial
     * finalize doesn't fault. */
    instance->resource_set->textures[0].data = NULL;
    instance->resource_set->textures[0].width = video_w;
    instance->resource_set->textures[0].height = video_h;
    instance->resource_set->textures[0].dirty = 0;
    instance->resource_set->textures[1].data = NULL;
    instance->resource_set->textures[1].width = chroma_w;
    instance->resource_set->textures[1].height = chroma_h;
    instance->resource_set->textures[1].dirty = 0;
    instance->resource_set->textures[2].data = NULL;
    instance->resource_set->textures[2].width = chroma_w;
    instance->resource_set->textures[2].height = chroma_h;
    instance->resource_set->textures[2].dirty = 0;

    if (instance->factory && instance->factory->event_loop) {
        instance->listener.handler = yvideo_on_tick;
        int period_ms = (fps > 0.0f) ? (int)(1000.0f / fps) : 33;
        struct yetty_ycore_void_result sr =
            yvideo_animation_subscribe(instance->factory, &instance->listener, period_ms);
        if (YETTY_IS_OK(sr)) {
            st->subscribed = true;
        } else {
            ywarn("yvideo: animation_subscribe failed; playback will be static");
            yetty_ycore_error_destroy(sr.error);
        }
    }

    yinfo("yvideo: instance created %ux%u @ %.1f fps (NAL=%zu B, audio_codec=%u %u Hz × %u ch, "
          "audio=%zu B, flags=0x%x)",
          video_w, video_h, (double)fps, nal_byte_count, audio_codec, audio_sample_rate,
          audio_channels, audio_byte_count, flags);
    return YETTY_OK_VOID();
}

void yvideo_hook_instance_destroy(struct yetty_ydraw_figure *instance)
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

struct yetty_ycore_void_result yvideo_hook_instance_update(struct yetty_ydraw_figure *instance,
                                                           const void *payload, size_t size)
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
        return YETTY_ERR(yetty_ycore_void, "yvideo update: payload < 4 bytes (no op header)");
    }
    const uint8_t *p = (const uint8_t *)payload;
    uint8_t op = p[0];
    const uint8_t *body = p + 4;
    size_t body_size = size - 4u;

    switch (op) {
    case YETTY_YVIDEO_UPDATE_OP_APPEND_NAL:
        if (body_size > 0u) {
            if (!yvideo_nal_ring_append(st, body, body_size)) {
                return YETTY_ERR(yetty_ycore_void, "yvideo update: NAL ring grow failed");
            }
            /* Wake the decode coro immediately — newly-arrived NAL
             * bytes can produce a frame before the next tick fires. */
            yvideo_wake_decoder(st);
        }
        return YETTY_OK_VOID();

    case YETTY_YVIDEO_UPDATE_OP_APPEND_AUDIO:
        if (!st->audio_decoder || !st->audio_device) {
            /* Sender supplied audio bytes but the figure was created
             * without audio_codec set — drop silently rather than
             * holding bytes that will never decode. */
            return YETTY_OK_VOID();
        }
        if (body_size > 0u) {
            if (!yvideo_audio_ring_append(st, body, body_size)) {
                return YETTY_ERR(yetty_ycore_void, "yvideo update: audio ring grow failed");
            }
            yvideo_wake_decoder(st);
        }
        return YETTY_OK_VOID();

    case YETTY_YVIDEO_UPDATE_OP_SEEK_PTS_MS: {
        if (body_size < sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "yvideo update: SEEK body < 4 bytes");
        }
        uint32_t pts_ms;
        memcpy(&pts_ms, body, sizeof(pts_ms));
        yvideo_do_seek(st, pts_ms);
        return YETTY_OK_VOID();
    }

    case YETTY_YVIDEO_UPDATE_OP_SET_PLAYING: {
        if (body_size < 1u) {
            return YETTY_ERR(yetty_ycore_void, "yvideo update: SET_PLAYING body < 1 byte");
        }
        yvideo_do_set_playing(st, body[0] != 0u);
        return YETTY_OK_VOID();
    }

    case YETTY_YVIDEO_UPDATE_OP_SET_SPEED: {
        if (body_size < sizeof(float)) {
            return YETTY_ERR(yetty_ycore_void, "yvideo update: SET_SPEED body < 4 bytes");
        }
        float speed;
        memcpy(&speed, body, sizeof(speed));
        yvideo_do_set_speed(st, speed);
        return YETTY_OK_VOID();
    }

    case YETTY_YVIDEO_UPDATE_OP_SET_LOOP: {
        if (body_size < 1u) {
            return YETTY_ERR(yetty_ycore_void, "yvideo update: SET_LOOP body < 1 byte");
        }
        yvideo_do_set_loop(st, body[0] != 0u);
        return YETTY_OK_VOID();
    }

    default:
        return YETTY_ERR(yetty_ycore_void, "yvideo update: unknown op");
    }
}

struct yetty_ycore_void_result yvideo_hook_instance_render_pre(struct yetty_ydraw_figure *instance,
                                                               struct yetty_ydraw_target *target,
                                                               float x, float y)
{
    (void)instance;
    (void)target;
    (void)x;
    (void)y;
    return YETTY_OK_VOID();
}
