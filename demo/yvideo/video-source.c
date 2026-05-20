/*
 * video-source — synthetic / file-fed H.264 (+optional Opus audio) emitter
 * for yvideo scene-canvas demos. Two-phase shape mirrors
 * demo/yplot/audio-source.c:
 *
 *   1) ONE create envelope (YDRAW_SCENE_BIN). CMD_GROUP(stream_id)
 *      wrapping a yvideo prim that carries the first chunk of the
 *      H.264 byte stream (must include SPS+PPS+IDR so decode can
 *      start immediately) plus the first chunk of audio packets
 *      when --audio-file is supplied. The receiving scene-canvas
 *      creates an addressable entity; the yvideo factory allocates
 *      the H.264 decoder, the optional Opus decoder, and a platform
 *      audio device for playback.
 *
 *   2) STREAM of CMD_UPDATE envelopes carrying further bytes. Each
 *      update payload is the v2 typed shape: [u8 op][u8 reserved[3]]
 *      [bytes...]. Two ops are used:
 *         APPEND_NAL    (0x00) — H.264 Annex-B bytes
 *         APPEND_AUDIO  (0x01) — length-prefixed audio packets
 *
 * Audio file format (--audio-file): raw length-prefixed packets, the
 * same layout the audio_stream wire buffer uses:
 *     [u32 length][length bytes][padding to u32 alignment][next packet...]
 *
 * To prepare an audio file from a WAV with opusenc + a small wrapper,
 * see docs/ (not yet documented; opus-tools is a starting point).
 *
 * Usage:
 *   yetty-yvideo-source <file.h264> --video-w=W --video-h=H [...]
 *
 * Run inside a yetty session:
 *   ./build-desktop-ytrace-release/yetty \
 *     -e './build-desktop-ytrace-release/demo/yvideo/demo-yvideo-source \
 *         tmp/sample.h264 --video-w=320 --video-h=240 \
 *         --audio-file=tmp/sample.opus.raw --audio-sample-rate=48000 --audio-channels=2'
 *
 * Options:
 *   --video-w=W              H.264 source width  (required, must be even)
 *   --video-h=H              H.264 source height (required, must be even)
 *   --fps=F                  playback fps        (default 30)
 *   --bounds-w=W             display width  in pane px (default = video-w)
 *   --bounds-h=H             display height in pane px (default = video-h)
 *   --chunk=N                bytes per video update envelope (default 65536)
 *   --period-ms=M            sleep between updates    (default 33 ≈ 30 Hz)
 *   --no-sleep               emit as fast as possible
 *   --stream-id=I            addressable id           (default 1)
 *   --no-loop                drop the LOOP flag
 *   --no-autoplay            drop the AUTOPLAY flag
 *   --audio-file=PATH        raw length-prefixed Opus packet file
 *   --audio-sample-rate=Hz   48000 (default; Opus accepts 8/12/16/24/48 kHz)
 *   --audio-channels=N       1 or 2 (default 2)
 *   --audio-chunk=N          bytes per audio update envelope (default 16384)
 *
 *   Playback-control ops (#198 item 3) — all sent as typed CMD_UPDATE
 *   envelopes alongside the byte stream:
 *
 *   --seek-ms=N              emit SEEK_PTS_MS right after the INIT
 *                            envelope (test the seek+resume path)
 *   --speed=F                emit SET_SPEED right after INIT
 *   --start-paused           emit SET_PLAYING(0) right after INIT
 *   --pause-after-ms=N       emit SET_PLAYING(0) N wall-clock ms into
 *                            the stream-pump loop
 *   --resume-after-ms=M      emit SET_PLAYING(1) M ms into the loop
 *                            (must be > --pause-after-ms to be useful)
 *   --loop-off               emit SET_LOOP(0) right after INIT
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yvideo/yvideo.h>
#include <yetty/yvideo/yvideo-gen.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <unistd.h>
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}
#endif

/* Op codes come from the public yvideo.h header — same symbols on both
 * sides of the wire. */

/* Audio codec id — must match yetty_yacodec_codec in
 * include/yetty/yacodec/types.h. v1 demo only ships Opus. */
#define YACODEC_CODEC_OPUS 1u

struct opts {
    const char *path;
    uint32_t video_w;
    uint32_t video_h;
    float fps;
    float bounds_w;
    float bounds_h;
    int chunk;
    int period_ms;
    int no_sleep;
    int stream_id;
    int no_loop;
    int no_autoplay;

    const char *audio_path;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    int audio_chunk;

    /* Playback-control flags (#198 item 3). */
    int      seek_ms;             /* -1 = no seek */
    float    speed;               /* 0.0 = no SET_SPEED emit */
    int      start_paused;
    int      pause_after_ms;      /* -1 = no mid-stream pause */
    int      resume_after_ms;     /* -1 = no mid-stream resume */
    int      loop_off;
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <file.h264> --video-w=W --video-h=H [opts]\n"
            "Options: --fps=F --bounds-w=W --bounds-h=H --chunk=N\n"
            "         --period-ms=M --no-sleep --stream-id=I\n"
            "         --no-loop --no-autoplay\n"
            "         --audio-file=PATH --audio-sample-rate=Hz\n"
            "         --audio-channels=N --audio-chunk=N\n"
            "         --seek-ms=N --speed=F --start-paused\n"
            "         --pause-after-ms=N --resume-after-ms=M\n"
            "         --loop-off\n",
            prog);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}
static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (uint32_t)v;
    return 0;
}
static int parse_float(const char *s, float *out)
{
    char *end = NULL;
    float v = strtof(s, &end);
    if (!end || *end != '\0') return -1;
    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, struct opts *o)
{
    memset(o, 0, sizeof(*o));
    o->fps = 30.0f;
    o->chunk = 65536;
    o->period_ms = 33;
    o->stream_id = 1;
    o->audio_sample_rate = 48000u;
    o->audio_channels = 2u;
    o->audio_chunk = 16384;
    o->seek_ms = -1;
    o->speed = 0.0f;
    o->pause_after_ms = -1;
    o->resume_after_ms = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strncmp(a, "--video-w=", 10) == 0) {
            if (parse_u32(a + 10, &o->video_w) < 0) return -1;
        } else if (strncmp(a, "--video-h=", 10) == 0) {
            if (parse_u32(a + 10, &o->video_h) < 0) return -1;
        } else if (strncmp(a, "--fps=", 6) == 0) {
            if (parse_float(a + 6, &o->fps) < 0) return -1;
        } else if (strncmp(a, "--bounds-w=", 11) == 0) {
            if (parse_float(a + 11, &o->bounds_w) < 0) return -1;
        } else if (strncmp(a, "--bounds-h=", 11) == 0) {
            if (parse_float(a + 11, &o->bounds_h) < 0) return -1;
        } else if (strncmp(a, "--chunk=", 8) == 0) {
            if (parse_int(a + 8, &o->chunk) < 0) return -1;
        } else if (strncmp(a, "--period-ms=", 12) == 0) {
            if (parse_int(a + 12, &o->period_ms) < 0) return -1;
        } else if (strncmp(a, "--stream-id=", 12) == 0) {
            if (parse_int(a + 12, &o->stream_id) < 0) return -1;
        } else if (strcmp(a, "--no-sleep") == 0) {
            o->no_sleep = 1;
        } else if (strcmp(a, "--no-loop") == 0) {
            o->no_loop = 1;
        } else if (strcmp(a, "--no-autoplay") == 0) {
            o->no_autoplay = 1;
        } else if (strncmp(a, "--audio-file=", 13) == 0) {
            o->audio_path = a + 13;
        } else if (strncmp(a, "--audio-sample-rate=", 20) == 0) {
            if (parse_u32(a + 20, &o->audio_sample_rate) < 0) return -1;
        } else if (strncmp(a, "--audio-channels=", 17) == 0) {
            if (parse_u32(a + 17, &o->audio_channels) < 0) return -1;
        } else if (strncmp(a, "--audio-chunk=", 14) == 0) {
            if (parse_int(a + 14, &o->audio_chunk) < 0) return -1;
        } else if (strncmp(a, "--seek-ms=", 10) == 0) {
            if (parse_int(a + 10, &o->seek_ms) < 0) return -1;
        } else if (strncmp(a, "--speed=", 8) == 0) {
            if (parse_float(a + 8, &o->speed) < 0) return -1;
        } else if (strcmp(a, "--start-paused") == 0) {
            o->start_paused = 1;
        } else if (strncmp(a, "--pause-after-ms=", 17) == 0) {
            if (parse_int(a + 17, &o->pause_after_ms) < 0) return -1;
        } else if (strncmp(a, "--resume-after-ms=", 18) == 0) {
            if (parse_int(a + 18, &o->resume_after_ms) < 0) return -1;
        } else if (strcmp(a, "--loop-off") == 0) {
            o->loop_off = 1;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 1;
        } else if (a[0] != '-' && !o->path) {
            o->path = a;
        } else {
            return -1;
        }
    }
    if (!o->path || o->video_w == 0 || o->video_h == 0 || o->chunk <= 0 ||
        o->stream_id <= 0 || o->fps <= 0.0f || o->audio_chunk <= 0) {
        return -1;
    }
    if ((o->video_w & 1u) || (o->video_h & 1u)) {
        fprintf(stderr, "video-source: video_w/video_h must be even (H.264 constraint)\n");
        return -1;
    }
    if (o->audio_path && (o->audio_channels == 0u || o->audio_channels > 2u)) {
        fprintf(stderr, "video-source: --audio-channels must be 1 or 2\n");
        return -1;
    }
    return 0;
}

static struct yetty_ycore_void_result emit_scene_bin(const struct yetty_ydraw_draw_list *dl)
{
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_draw_list_serialize((struct yetty_ydraw_draw_list *)dl, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "draw_list_serialize: empty");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer out = {0};
    struct yetty_ycore_void_result er = yetty_yface_emit(
        YETTY_OSC_YDRAW_SCENE_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &out);
    if (YETTY_IS_ERR(er)) {
        yetty_ycore_buffer_destroy(&out);
        return YETTY_ERR(yetty_ycore_void, "yface_emit failed", er);
    }
    if (out.size > 0) {
        fwrite(out.data, 1, out.size, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    yetty_ycore_buffer_destroy(&out);
    return YETTY_OK_VOID();
}

/* INIT envelope: GROUP(stream_id) wrapping a yvideo prim with the
 * first chunks of NAL bytes + audio packets. */
static struct yetty_ycore_void_result emit_create_envelope(
    const struct opts *o,
    const uint8_t *first_nals,  size_t first_nal_len,
    const uint8_t *first_audio, size_t first_audio_len)
{
    uint32_t flags = 0u;
    if (!o->no_loop)     flags |= YETTY_YVIDEO_FLAG_LOOP;
    if (!o->no_autoplay) flags |= YETTY_YVIDEO_FLAG_AUTOPLAY;

    struct yetty_yvideo_uniforms u = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = o->bounds_w > 0.0f ? o->bounds_w : (float)o->video_w,
        .bounds_h = o->bounds_h > 0.0f ? o->bounds_h : (float)o->video_h,
        .video_w = o->video_w,
        .video_h = o->video_h,
        .chroma_w = o->video_w / 2u,
        .chroma_h = o->video_h / 2u,
        .fps = o->fps,
        .color_matrix = 1u, /* BT.709 */
        .flags = flags,
        .audio_codec       = o->audio_path ? YACODEC_CODEC_OPUS : 0u,
        .audio_sample_rate = o->audio_path ? o->audio_sample_rate : 0u,
        .audio_channels    = o->audio_path ? o->audio_channels    : 0u,
    };

    size_t nal_words = (first_nal_len + 3u) / 4u;
    uint32_t *nal_words_buf = NULL;
    if (nal_words > 0u) {
        nal_words_buf = calloc(nal_words, sizeof(uint32_t));
        if (!nal_words_buf) {
            return YETTY_ERR(yetty_ycore_void, "create envelope: nal-pack alloc failed");
        }
        memcpy(nal_words_buf, first_nals, first_nal_len);
    }
    size_t audio_words = (first_audio_len + 3u) / 4u;
    uint32_t *audio_words_buf = NULL;
    if (audio_words > 0u) {
        audio_words_buf = calloc(audio_words, sizeof(uint32_t));
        if (!audio_words_buf) {
            free(nal_words_buf);
            return YETTY_ERR(yetty_ycore_void, "create envelope: audio-pack alloc failed");
        }
        memcpy(audio_words_buf, first_audio, first_audio_len);
    }
    struct yetty_yvideo_buffers bufs = {
        .nal_stream       = nal_words_buf,
        .nal_stream_len   = nal_words,
        .audio_stream     = audio_words_buf,
        .audio_stream_len = audio_words,
    };

    size_t prim_size = yetty_yvideo_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_bytes = malloc(prim_size);
    if (!prim_bytes) {
        free(nal_words_buf);
        free(audio_words_buf);
        return YETTY_ERR(yetty_ycore_void, "create envelope: prim alloc failed");
    }
    struct yetty_ycore_size_result sr =
        yetty_yvideo_uniforms_serialize(&u, &bufs, prim_bytes, prim_size);
    free(nal_words_buf);
    free(audio_words_buf);
    if (YETTY_IS_ERR(sr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: serialize", sr);
    }

    struct yetty_ydraw_draw_list_config dlcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_w,
        .scene_max_y = u.bounds_h,
    };
    struct yetty_ydraw_draw_list_result outr =
        yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(outr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: draw_list create", outr);
    }
    struct yetty_ydraw_draw_list *out = outr.value;

    struct yetty_ydraw_id_result gr =
        yetty_ydraw_draw_list_begin_group(out, (uint32_t)o->stream_id);
    if (YETTY_IS_ERR(gr)) {
        yetty_ydraw_draw_list_destroy(out);
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "create envelope: begin_group", gr);
    }
    struct yetty_ydraw_id_result ar =
        yetty_ydraw_draw_list_add_prim(out, prim_bytes, prim_size);
    free(prim_bytes);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_draw_list_destroy(out);
        return YETTY_ERR(yetty_ycore_void, "create envelope: add_prim", ar);
    }
    struct yetty_ycore_void_result er =
        yetty_ydraw_draw_list_end_group(out, (uint32_t)gr.value);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_draw_list_destroy(out);
        return YETTY_ERR(yetty_ycore_void, "create envelope: end_group", er);
    }

    struct yetty_ycore_void_result emr = emit_scene_bin(out);
    yetty_ydraw_draw_list_destroy(out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emr, "create envelope: emit");
    return YETTY_OK_VOID();
}

/* UPDATE envelope: CMD_UPDATE carrying a typed payload — 4-byte
 * [op][reserved×3] header followed by the op's body bytes. */
static struct yetty_ycore_void_result emit_update_envelope(
    int stream_id, uint8_t op, const uint8_t *bytes, size_t len)
{
    /* Stage the typed payload: [op][0][0][0][bytes...]. */
    size_t total = 4u + len;
    uint8_t *typed = malloc(total);
    if (!typed) {
        return YETTY_ERR(yetty_ycore_void, "update envelope: typed alloc failed");
    }
    typed[0] = op;
    typed[1] = 0u;
    typed[2] = 0u;
    typed[3] = 0u;
    if (len > 0u) {
        memcpy(typed + 4u, bytes, len);
    }

    struct yetty_ydraw_draw_list_config dlcfg = {0};
    struct yetty_ydraw_draw_list_result outr =
        yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(outr)) {
        free(typed);
        return YETTY_ERR(yetty_ycore_void, "update envelope: draw_list create", outr);
    }
    struct yetty_ydraw_draw_list *out = outr.value;

    struct yetty_ycore_void_result cu =
        yetty_ydraw_draw_list_add_cmd_update(out, (uint32_t)stream_id, typed, total);
    free(typed);
    if (YETTY_IS_ERR(cu)) {
        yetty_ydraw_draw_list_destroy(out);
        return YETTY_ERR(yetty_ycore_void, "update envelope: add_cmd_update", cu);
    }
    struct yetty_ycore_void_result er = emit_scene_bin(out);
    yetty_ydraw_draw_list_destroy(out);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "update envelope: emit");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct opts o;
    int r = parse_args(argc, argv, &o);
    if (r == 1) return 0;
    if (r < 0) {
        usage(argv[0]);
        return 2;
    }

    FILE *f = fopen(o.path, "rb");
    if (!f) {
        fprintf(stderr, "video-source: open '%s' failed\n", o.path);
        return 1;
    }
    FILE *af = NULL;
    if (o.audio_path) {
        af = fopen(o.audio_path, "rb");
        if (!af) {
            fprintf(stderr, "video-source: open audio '%s' failed\n", o.audio_path);
            fclose(f);
            return 1;
        }
    }

    uint8_t *chunk = malloc((size_t)o.chunk);
    uint8_t *achunk = af ? malloc((size_t)o.audio_chunk) : NULL;
    if (!chunk || (af && !achunk)) {
        free(chunk);
        free(achunk);
        fclose(f);
        if (af) fclose(af);
        fprintf(stderr, "video-source: chunk alloc failed\n");
        return 1;
    }

    /* Read first chunks from both streams. */
    size_t first_nal   = fread(chunk,  1, (size_t)o.chunk,       f);
    size_t first_audio = af ? fread(achunk, 1, (size_t)o.audio_chunk, af) : 0u;
    if (first_nal == 0) {
        free(chunk); free(achunk);
        fclose(f); if (af) fclose(af);
        fprintf(stderr, "video-source: empty input\n");
        return 1;
    }
    struct yetty_ycore_void_result cr =
        emit_create_envelope(&o, chunk, first_nal, achunk, first_audio);
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "video-source: init envelope failed: %s\n", cr.error.msg);
        yetty_ycore_error_destroy(cr.error);
        free(chunk); free(achunk);
        fclose(f); if (af) fclose(af);
        return 1;
    }

    /* Post-INIT control envelopes (#198 item 3). Order matters: SPEED
     * and LOOP are state-only and can go first; SEEK comes next so the
     * stream-loop bytes that follow land in the post-seek decoder
     * state; SET_PLAYING(0) is last so the figure is fully configured
     * before being paused. Each ctrl envelope is its own CMD_UPDATE. */
#define CTRL_EMIT(op, body, len, desc)                                                              \
    do {                                                                                            \
        struct yetty_ycore_void_result _ur =                                                        \
            emit_update_envelope(o.stream_id, (op), (body), (len));                                 \
        if (YETTY_IS_ERR(_ur)) {                                                                    \
            fprintf(stderr, "video-source: " desc " failed: %s\n", _ur.error.msg);                  \
            yetty_ycore_error_destroy(_ur.error);                                                   \
            free(chunk); free(achunk);                                                              \
            fclose(f); if (af) fclose(af);                                                          \
            return 1;                                                                               \
        }                                                                                           \
    } while (0)

    if (o.speed > 0.0f) {
        uint8_t body[sizeof(float)];
        memcpy(body, &o.speed, sizeof(float));
        CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SET_SPEED, body, sizeof(body), "SET_SPEED");
    }
    if (o.loop_off) {
        uint8_t body[1] = {0u};
        CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SET_LOOP, body, sizeof(body), "SET_LOOP");
    }
    if (o.seek_ms >= 0) {
        uint32_t pts = (uint32_t)o.seek_ms;
        uint8_t body[sizeof(uint32_t)];
        memcpy(body, &pts, sizeof(pts));
        CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SEEK_PTS_MS, body, sizeof(body), "SEEK_PTS_MS");
    }
    if (o.start_paused) {
        uint8_t body[1] = {0u};
        CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SET_PLAYING, body, sizeof(body), "SET_PLAYING(0)");
    }

    /* Stream the rest. Each tick: try to emit a NAL update + (if audio)
     * an audio update. Either side can hit EOF independently; we keep
     * going until BOTH are done. While streaming, watch the elapsed
     * wall-clock and fire one-shot pause/resume control envelopes when
     * the configured thresholds are crossed. */
    int nal_eof = 0;
    int audio_eof = (af == NULL);
    int pause_emitted  = 0;
    int resume_emitted = 0;
    int elapsed_ms = 0;
    for (;;) {
        if (!nal_eof) {
            size_t n = fread(chunk, 1, (size_t)o.chunk, f);
            if (n == 0) {
                nal_eof = 1;
            } else {
                struct yetty_ycore_void_result ur = emit_update_envelope(
                    o.stream_id, YETTY_YVIDEO_UPDATE_OP_APPEND_NAL, chunk, n);
                if (YETTY_IS_ERR(ur)) {
                    fprintf(stderr, "video-source: NAL update failed: %s\n", ur.error.msg);
                    yetty_ycore_error_destroy(ur.error);
                    free(chunk); free(achunk);
                    fclose(f); if (af) fclose(af);
                    return 1;
                }
            }
        }
        if (!audio_eof) {
            size_t n = fread(achunk, 1, (size_t)o.audio_chunk, af);
            if (n == 0) {
                audio_eof = 1;
            } else {
                struct yetty_ycore_void_result ur = emit_update_envelope(
                    o.stream_id, YETTY_YVIDEO_UPDATE_OP_APPEND_AUDIO, achunk, n);
                if (YETTY_IS_ERR(ur)) {
                    fprintf(stderr, "video-source: audio update failed: %s\n", ur.error.msg);
                    yetty_ycore_error_destroy(ur.error);
                    free(chunk); free(achunk);
                    fclose(f); if (af) fclose(af);
                    return 1;
                }
            }
        }
        if (nal_eof && audio_eof) break;

        /* Mid-stream control: fire pause / resume when the elapsed
         * wall-clock crosses the configured ms threshold. */
        if (!pause_emitted && o.pause_after_ms >= 0 && elapsed_ms >= o.pause_after_ms) {
            uint8_t body[1] = {0u};
            CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SET_PLAYING, body, sizeof(body), "SET_PLAYING(0)");
            pause_emitted = 1;
        }
        if (!resume_emitted && o.resume_after_ms >= 0 && elapsed_ms >= o.resume_after_ms) {
            uint8_t body[1] = {1u};
            CTRL_EMIT(YETTY_YVIDEO_UPDATE_OP_SET_PLAYING, body, sizeof(body), "SET_PLAYING(1)");
            resume_emitted = 1;
        }

        if (!o.no_sleep) sleep_ms(o.period_ms);
        elapsed_ms += o.period_ms;
    }
#undef CTRL_EMIT

    free(chunk); free(achunk);
    fclose(f); if (af) fclose(af);
    return 0;
}
