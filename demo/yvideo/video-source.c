/*
 * video-source — synthetic / file-fed H.264 emitter for yvideo
 * scene-canvas demos. Mirrors demo/yplot/audio-source.c's two-phase
 * shape:
 *
 *   1) ONE create envelope (YDRAW_SCENE_BIN). CMD_GROUP(stream_id)
 *      wrapping a yvideo prim that carries the first chunk of the
 *      H.264 byte stream (must include SPS+PPS+IDR so decode can
 *      start immediately). The receiving scene-canvas creates an
 *      addressable entity, the yvideo factory allocates a decoder and
 *      a frame texture.
 *
 *   2) STREAM of CMD_UPDATE envelopes carrying further NAL bytes.
 *      Each update payload is raw H.264 Annex-B bytes. The factory
 *      feeds them straight to openh264; render() advances the
 *      playhead by wall-clock and uploads decoded frames.
 *
 * Usage:
 *   yetty-yvideo-source <file.h264> --video-w=W --video-h=H [...]
 *
 * Run inside a yetty session:
 *   ./build-desktop-ytrace-release/yetty \
 *     -e './build-desktop-ytrace-release/demo/yvideo/demo-yvideo-source \
 *         tmp/sample.h264 --video-w=320 --video-h=240'
 *
 * Options:
 *   --video-w=W        H.264 source width  (required, must be even)
 *   --video-h=H        H.264 source height (required, must be even)
 *   --fps=F            playback fps        (default 30)
 *   --bounds-w=W       display width  in pane px (default = video-w)
 *   --bounds-h=H       display height in pane px (default = video-h)
 *   --chunk=N          bytes per update envelope (default 65536)
 *   --period-ms=M      sleep between updates    (default 33 ≈ 30 Hz)
 *   --no-sleep         emit as fast as possible
 *   --stream-id=I      addressable id           (default 1)
 *   --no-loop          drop the LOOP flag
 *   --no-autoplay      drop the AUTOPLAY flag
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
};

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <file.h264> --video-w=W --video-h=H [opts]\n"
            "Options: --fps=F --bounds-w=W --bounds-h=H --chunk=N\n"
            "         --period-ms=M --no-sleep --stream-id=I\n"
            "         --no-loop --no-autoplay\n",
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
        o->stream_id <= 0 || o->fps <= 0.0f) {
        return -1;
    }
    if ((o->video_w & 1u) || (o->video_h & 1u)) {
        fprintf(stderr, "video-source: video_w/video_h must be even (H.264 constraint)\n");
        return -1;
    }
    return 0;
}

/* Same yface envelope shape as demo/yplot/audio-source.c. */
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
 * first NAL chunk. Serialises the prim directly with
 * yetty_yvideo_uniforms_serialize (mirrors yplot audio-source.c). */
static struct yetty_ycore_void_result emit_create_envelope(
    const struct opts *o, const uint8_t *first_nals, size_t first_len)
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
        .fps = o->fps,
        .color_matrix = 1u, /* BT.709 */
        .flags = flags,
    };
    /* Generator-emitted serializer expects u32 words. Pad NAL bytes
     * up to a 4-byte boundary; trailing 0..3 zero bytes are valid
     * Annex-B padding. */
    size_t nal_words = (first_len + 3u) / 4u;
    uint32_t *nal_words_buf = NULL;
    if (nal_words > 0u) {
        nal_words_buf = calloc(nal_words, sizeof(uint32_t));
        if (!nal_words_buf) {
            return YETTY_ERR(yetty_ycore_void, "create envelope: nal-pack alloc failed");
        }
        memcpy(nal_words_buf, first_nals, first_len);
    }
    struct yetty_yvideo_buffers bufs = {
        .nal_stream = nal_words_buf,
        .nal_stream_len = nal_words,
    };

    size_t prim_size = yetty_yvideo_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_bytes = malloc(prim_size);
    if (!prim_bytes) {
        free(nal_words_buf);
        return YETTY_ERR(yetty_ycore_void, "create envelope: prim alloc failed");
    }
    struct yetty_ycore_size_result sr =
        yetty_yvideo_uniforms_serialize(&u, &bufs, prim_bytes, prim_size);
    free(nal_words_buf);
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

/* UPDATE envelope: one CMD_UPDATE(stream_id, raw_nal_bytes) record. */
static struct yetty_ycore_void_result emit_update_envelope(int stream_id, const uint8_t *bytes,
                                                            size_t len)
{
    struct yetty_ydraw_draw_list_config dlcfg = {0};
    struct yetty_ydraw_draw_list_result outr =
        yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, outr, "update envelope: draw_list create");
    struct yetty_ydraw_draw_list *out = outr.value;

    struct yetty_ycore_void_result cu =
        yetty_ydraw_draw_list_add_cmd_update(out, (uint32_t)stream_id, bytes, len);
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

    uint8_t *chunk = malloc((size_t)o.chunk);
    if (!chunk) {
        fclose(f);
        fprintf(stderr, "video-source: chunk alloc failed\n");
        return 1;
    }

    /* Read the first chunk and ship it via the INIT envelope. */
    size_t first = fread(chunk, 1, (size_t)o.chunk, f);
    if (first == 0) {
        free(chunk);
        fclose(f);
        fprintf(stderr, "video-source: empty input\n");
        return 1;
    }
    struct yetty_ycore_void_result cr = emit_create_envelope(&o, chunk, first);
    if (YETTY_IS_ERR(cr)) {
        fprintf(stderr, "video-source: init envelope failed: %s\n", cr.error.msg);
        yetty_ycore_error_destroy(cr.error);
        free(chunk);
        fclose(f);
        return 1;
    }

    /* Stream the rest. */
    for (;;) {
        size_t n = fread(chunk, 1, (size_t)o.chunk, f);
        if (n == 0) break;
        struct yetty_ycore_void_result ur = emit_update_envelope(o.stream_id, chunk, n);
        if (YETTY_IS_ERR(ur)) {
            fprintf(stderr, "video-source: update envelope failed: %s\n", ur.error.msg);
            yetty_ycore_error_destroy(ur.error);
            free(chunk);
            fclose(f);
            return 1;
        }
        if (!o.no_sleep) sleep_ms(o.period_ms);
    }

    free(chunk);
    fclose(f);
    return 0;
}
