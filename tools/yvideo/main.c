/*
 * yvideo — emit a yvideo OSC envelope stream for a raw H.264 Annex-B
 * file. Inside a yetty terminal the OSC envelopes route to the ydraw
 * scene-canvas layer, which decodes via openh264 and renders frames as
 * they arrive. Outside a yetty terminal the bytes are still printed
 * (mostly garbage on a vt100), so typical usage is:
 *
 *   yetty -e 'yvideo path/to/clip.h264'
 *
 * or invocation from a script running inside a yetty session.
 *
 * File format: raw H.264 Annex-B byte stream (.h264 / .264). For MP4
 * input, transcode first with:
 *
 *   ffmpeg -i in.mp4 -c:v copy -an -f h264 out.h264
 *
 * The tool parses the first SPS NAL to auto-detect width and height,
 * so usually only the path is needed:
 *
 *   yvideo clip.h264
 *
 * Override with --width / --height / --fps if the SPS lies (some
 * captures elide cropping info) or the desired playback rate differs
 * from the source. The wire-format playhead is wall-clock driven by
 * fps on the receiving terminal.
 */

#include <yetty/yface/yface.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yvideo/yvideo-gen.h>
#include <yetty/yvideo/yvideo.h>

#include <stdbool.h>
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
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

/*---------------------------------------------------------------------------
 * Minimal H.264 SPS parser — extract pic_width / pic_height from the
 * first SPS NAL found in the stream. Just enough Exp-Golomb to walk to
 * the cropping fields; no caching, no full SPS struct.
 *-------------------------------------------------------------------------*/

struct bit_reader {
    const uint8_t *buf;
    size_t size;
    size_t byte_pos;
    int bit_pos; /* 0..7, MSB-first */
};

static uint32_t br_read_bits(struct bit_reader *br, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        if (br->byte_pos >= br->size) {
            return 0;
        }
        uint32_t b = (br->buf[br->byte_pos] >> (7 - br->bit_pos)) & 1u;
        v = (v << 1) | b;
        br->bit_pos++;
        if (br->bit_pos == 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return v;
}

static uint32_t br_read_ue(struct bit_reader *br)
{
    int zero_bits = 0;
    while (zero_bits < 32 && br->byte_pos < br->size && br_read_bits(br, 1) == 0) {
        zero_bits++;
    }
    if (zero_bits == 0) {
        return 0;
    }
    uint32_t tail = br_read_bits(br, zero_bits);
    return (1u << zero_bits) - 1u + tail;
}

static int32_t br_read_se(struct bit_reader *br)
{
    uint32_t v = br_read_ue(br);
    if (v & 1u) {
        return (int32_t)((v + 1u) >> 1);
    }
    return -(int32_t)(v >> 1);
}

/* Strip H.264 emulation prevention bytes (0x000003 → 0x0000) from a NAL
 * RBSP. `out_max` is the caller-supplied buffer size; returns the number
 * of bytes written. */
static size_t h264_rbsp_strip_emulation(const uint8_t *in, size_t in_len, uint8_t *out,
                                        size_t out_max)
{
    size_t oi = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (i + 2 < in_len && in[i] == 0 && in[i + 1] == 0 && in[i + 2] == 0x03) {
            if (oi + 2 > out_max) {
                break;
            }
            out[oi++] = 0;
            out[oi++] = 0;
            i += 2; /* skip the 0x03 */
            continue;
        }
        if (oi >= out_max) {
            break;
        }
        out[oi++] = in[i];
    }
    return oi;
}

/* Scan `buf` for the first SPS NAL (nal_unit_type=7) and extract
 * `width`/`height` from it. Returns 1 on success, 0 if no SPS found
 * or parsing failed. */
static int h264_extract_dimensions(const uint8_t *buf, size_t size, uint32_t *out_w,
                                   uint32_t *out_h)
{
    /* Find the SPS NAL: a start code followed by a header byte whose
     * low 5 bits == 7. */
    size_t i = 0;
    while (i + 4 < size) {
        bool sc4 = (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 && buf[i + 3] == 1);
        bool sc3 = (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1);
        if (!sc3 && !sc4) {
            i++;
            continue;
        }
        size_t hdr = i + (sc4 ? 4u : 3u);
        if (hdr >= size) {
            return 0;
        }
        uint8_t nal_type = buf[hdr] & 0x1f;
        if (nal_type != 7) {
            i = hdr;
            continue;
        }
        /* Find the next start code to bound this NAL. */
        size_t nal_end = size;
        for (size_t j = hdr + 1; j + 3 <= size; j++) {
            if (buf[j] == 0 && buf[j + 1] == 0 && (buf[j + 2] == 1 ||
                (j + 4 <= size && buf[j + 2] == 0 && buf[j + 3] == 1))) {
                nal_end = j;
                break;
            }
        }
        if (nal_end <= hdr + 1) {
            return 0;
        }
        /* RBSP starts AFTER the header byte. */
        const uint8_t *rbsp_in = buf + hdr + 1;
        size_t rbsp_in_len = nal_end - (hdr + 1);
        uint8_t rbsp[4096];
        size_t rbsp_len = h264_rbsp_strip_emulation(rbsp_in, rbsp_in_len, rbsp, sizeof rbsp);
        if (rbsp_len < 4) {
            return 0;
        }

        struct bit_reader br = {.buf = rbsp, .size = rbsp_len, .byte_pos = 0, .bit_pos = 0};
        uint8_t profile_idc = rbsp[0];
        br.byte_pos = 3; /* skip profile_idc, constraints+reserved, level_idc */

        (void)br_read_ue(&br); /* seq_parameter_set_id */

        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
            profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
            profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135) {
            uint32_t chroma = br_read_ue(&br);
            if (chroma == 3) {
                br_read_bits(&br, 1); /* separate_colour_plane_flag */
            }
            (void)br_read_ue(&br); /* bit_depth_luma_minus8 */
            (void)br_read_ue(&br); /* bit_depth_chroma_minus8 */
            br_read_bits(&br, 1);  /* qpprime_y_zero_transform_bypass_flag */
            uint32_t seq_scaling_list_present = br_read_bits(&br, 1);
            if (seq_scaling_list_present) {
                int n = (chroma != 3) ? 8 : 12;
                for (int s = 0; s < n; s++) {
                    if (br_read_bits(&br, 1)) {
                        int last_scale = 8, next_scale = 8;
                        int sz = s < 6 ? 16 : 64;
                        for (int k = 0; k < sz; k++) {
                            if (next_scale != 0) {
                                int delta = br_read_se(&br);
                                next_scale = (last_scale + delta + 256) % 256;
                            }
                            last_scale = next_scale == 0 ? last_scale : next_scale;
                        }
                    }
                }
            }
        }

        (void)br_read_ue(&br); /* log2_max_frame_num_minus4 */
        uint32_t pic_order_cnt_type = br_read_ue(&br);
        if (pic_order_cnt_type == 0) {
            (void)br_read_ue(&br); /* log2_max_pic_order_cnt_lsb_minus4 */
        } else if (pic_order_cnt_type == 1) {
            br_read_bits(&br, 1);  /* delta_pic_order_always_zero_flag */
            (void)br_read_se(&br); /* offset_for_non_ref_pic */
            (void)br_read_se(&br); /* offset_for_top_to_bottom_field */
            uint32_t n = br_read_ue(&br);
            for (uint32_t k = 0; k < n; k++) {
                (void)br_read_se(&br);
            }
        }
        (void)br_read_ue(&br); /* num_ref_frames */
        br_read_bits(&br, 1);  /* gaps_in_frame_num_value_allowed_flag */

        uint32_t pic_width_in_mbs_minus1 = br_read_ue(&br);
        uint32_t pic_height_in_map_units_minus1 = br_read_ue(&br);
        uint32_t frame_mbs_only_flag = br_read_bits(&br, 1);
        if (!frame_mbs_only_flag) {
            br_read_bits(&br, 1); /* mb_adaptive_frame_field_flag */
        }
        br_read_bits(&br, 1); /* direct_8x8_inference_flag */

        uint32_t width = (pic_width_in_mbs_minus1 + 1u) * 16u;
        uint32_t height = (2u - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1u) * 16u;

        uint32_t frame_cropping_flag = br_read_bits(&br, 1);
        if (frame_cropping_flag) {
            uint32_t left = br_read_ue(&br);
            uint32_t right = br_read_ue(&br);
            uint32_t top = br_read_ue(&br);
            uint32_t bottom = br_read_ue(&br);
            /* SubWidthC/SubHeightC = 2 for the common YUV420 case. */
            uint32_t sx = 2u, sy = 2u;
            uint32_t cx = sx * (left + right);
            uint32_t cy = sy * (top + bottom) * (2u - frame_mbs_only_flag);
            if (cx < width) width -= cx;
            if (cy < height) height -= cy;
        }
        *out_w = width;
        *out_h = height;
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * OSC envelope plumbing — mirrors demo/yvideo/video-source.c (the wire
 * shape is identical; this is the polished CLI on top).
 *-------------------------------------------------------------------------*/

struct yvideo_opts {
    const char *path;
    uint32_t video_w; /* 0 = autodetect from SPS */
    uint32_t video_h; /* 0 = autodetect from SPS */
    float fps;
    float bounds_w; /* 0 = use video_w */
    float bounds_h; /* 0 = use video_h */
    int chunk;
    int period_ms;
    int stream_id;
    bool no_sleep;
    bool no_loop;
    bool no_autoplay;
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] <file.h264>\n"
        "\n"
        "Emit a yvideo OSC envelope stream for the given H.264 Annex-B\n"
        "byte stream. INIT envelope wraps the first chunk in CMD_GROUP(id);\n"
        "subsequent chunks ship as CMD_UPDATE envelopes targeting that id.\n"
        "Dimensions are auto-detected from the first SPS NAL.\n"
        "\n"
        "Options:\n"
        "  -w, --width=N       override SPS width (must be even)\n"
        "  -H, --height=N      override SPS height (must be even)\n"
        "      --fps=F         playback rate, default 30\n"
        "      --bounds-w=N    display width in pane px (default = video width)\n"
        "      --bounds-h=N    display height in pane px (default = video height)\n"
        "      --chunk=N       bytes per UPDATE envelope (default 65536)\n"
        "      --period-ms=N   sleep between UPDATEs, default 33\n"
        "      --no-sleep      emit as fast as possible (PTY backpressure paces)\n"
        "      --stream-id=N   addressable id for the figure, default 1\n"
        "      --no-loop       drop the LOOP flag\n"
        "      --no-autoplay   drop the AUTOPLAY flag\n"
        "  -h, --help          show this help\n",
        prog);
}

static int parse_uint(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (uint32_t)v;
    return 0;
}
static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
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

static int parse_args(int argc, char **argv, struct yvideo_opts *o)
{
    memset(o, 0, sizeof(*o));
    o->fps = 30.0f;
    o->chunk = 65536;
    o->period_ms = 33;
    o->stream_id = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout, argv[0]);
            return 1;
        } else if (strncmp(a, "--width=", 8) == 0) {
            if (parse_uint(a + 8, &o->video_w) < 0) return -1;
        } else if ((strcmp(a, "-w") == 0 || strcmp(a, "--width") == 0) && i + 1 < argc) {
            if (parse_uint(argv[++i], &o->video_w) < 0) return -1;
        } else if (strncmp(a, "--height=", 9) == 0) {
            if (parse_uint(a + 9, &o->video_h) < 0) return -1;
        } else if ((strcmp(a, "-H") == 0 || strcmp(a, "--height") == 0) && i + 1 < argc) {
            if (parse_uint(argv[++i], &o->video_h) < 0) return -1;
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
            o->no_sleep = true;
        } else if (strcmp(a, "--no-loop") == 0) {
            o->no_loop = true;
        } else if (strcmp(a, "--no-autoplay") == 0) {
            o->no_autoplay = true;
        } else if (a[0] == '-') {
            return -1;
        } else if (!o->path) {
            o->path = a;
        } else {
            return -1;
        }
    }
    if (!o->path) return -1;
    if (o->chunk <= 0 || o->stream_id <= 0 || o->fps <= 0.0f) return -1;
    if (o->video_w && (o->video_w & 1u)) return -1;
    if (o->video_h && (o->video_h & 1u)) return -1;
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
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        YETTY_OSC_YDRAW_SCENE_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_void, "yface_emit failed", r);
    }
    if (envelope.size > 0) {
        fwrite(envelope.data, 1, envelope.size, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK_VOID();
}

/* INIT envelope: GROUP(stream_id) wrapping a yvideo prim with the first
 * NAL chunk. Mirrors what demo/yvideo/video-source.c does. */
static struct yetty_ycore_void_result emit_init(const struct yvideo_opts *o,
                                                const uint8_t *first, size_t first_len)
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

    /* The generator-emitted serializer takes a u32 buffer; pack the
     * NAL bytes (0..3 trailing zero pad bytes are valid Annex-B
     * padding). */
    size_t nal_words = (first_len + 3u) / 4u;
    uint32_t *nal_words_buf = nal_words ? calloc(nal_words, sizeof(uint32_t)) : NULL;
    if (nal_words && !nal_words_buf) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: nal-pack alloc failed");
    }
    if (nal_words_buf) {
        memcpy(nal_words_buf, first, first_len);
    }
    struct yetty_yvideo_buffers bufs = {
        .nal_stream = nal_words_buf,
        .nal_stream_len = nal_words,
    };
    size_t prim_size = yetty_yvideo_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_bytes = malloc(prim_size);
    if (!prim_bytes) {
        free(nal_words_buf);
        return YETTY_ERR(yetty_ycore_void, "yvideo: prim alloc failed");
    }
    struct yetty_ycore_size_result sr =
        yetty_yvideo_uniforms_serialize(&u, &bufs, prim_bytes, prim_size);
    free(nal_words_buf);
    if (YETTY_IS_ERR(sr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "yvideo: serialize failed", sr);
    }

    struct yetty_ydraw_draw_list_config dlcfg = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_w, .scene_max_y = u.bounds_h,
    };
    struct yetty_ydraw_draw_list_result dlr =
        yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(dlr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "yvideo: draw_list create", dlr);
    }
    struct yetty_ydraw_draw_list *dl = dlr.value;

    struct yetty_ydraw_id_result gr =
        yetty_ydraw_draw_list_begin_group(dl, (uint32_t)o->stream_id);
    if (YETTY_IS_ERR(gr)) {
        yetty_ydraw_draw_list_destroy(dl);
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "yvideo: begin_group", gr);
    }
    struct yetty_ydraw_id_result ar =
        yetty_ydraw_draw_list_add_prim(dl, prim_bytes, prim_size);
    free(prim_bytes);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_draw_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: add_prim", ar);
    }
    struct yetty_ycore_void_result er =
        yetty_ydraw_draw_list_end_group(dl, (uint32_t)gr.value);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_draw_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: end_group", er);
    }

    struct yetty_ycore_void_result em = emit_scene_bin(dl);
    yetty_ydraw_draw_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, em, "yvideo: emit init");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_update(int stream_id, const uint8_t *bytes,
                                                  size_t len)
{
    struct yetty_ydraw_draw_list_config dlcfg = {0};
    struct yetty_ydraw_draw_list_result dlr =
        yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dlr, "yvideo: update draw_list create");
    struct yetty_ydraw_draw_list *dl = dlr.value;
    struct yetty_ycore_void_result cu =
        yetty_ydraw_draw_list_add_cmd_update(dl, (uint32_t)stream_id, bytes, len);
    if (YETTY_IS_ERR(cu)) {
        yetty_ydraw_draw_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: add_cmd_update", cu);
    }
    struct yetty_ycore_void_result em = emit_scene_bin(dl);
    yetty_ydraw_draw_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, em, "yvideo: emit update");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct yvideo_opts o;
    int pa = parse_args(argc, argv, &o);
    if (pa == 1) return 0;
    if (pa < 0) {
        usage(stderr, argv[0]);
        return 2;
    }

    FILE *f = fopen(o.path, "rb");
    if (!f) {
        fprintf(stderr, "yvideo: cannot open '%s'\n", o.path);
        return 1;
    }
    uint8_t *chunk = malloc((size_t)o.chunk);
    if (!chunk) {
        fclose(f);
        fprintf(stderr, "yvideo: chunk alloc failed\n");
        return 1;
    }
    size_t first = fread(chunk, 1, (size_t)o.chunk, f);
    if (first == 0) {
        free(chunk);
        fclose(f);
        fprintf(stderr, "yvideo: empty input\n");
        return 1;
    }

    /* Auto-detect dimensions from the SPS unless overridden. */
    if (o.video_w == 0 || o.video_h == 0) {
        uint32_t w = 0, h = 0;
        if (!h264_extract_dimensions(chunk, first, &w, &h)) {
            free(chunk);
            fclose(f);
            fprintf(stderr,
                    "yvideo: no SPS NAL in first %zu bytes — pass --width and --height "
                    "explicitly or grow --chunk.\n",
                    first);
            return 1;
        }
        if (o.video_w == 0) o.video_w = w;
        if (o.video_h == 0) o.video_h = h;
    }
    if ((o.video_w & 1u) || (o.video_h & 1u)) {
        free(chunk);
        fclose(f);
        fprintf(stderr, "yvideo: video dimensions must be even (got %ux%u)\n",
                o.video_w, o.video_h);
        return 1;
    }

    struct yetty_ycore_void_result ir = emit_init(&o, chunk, first);
    if (YETTY_IS_ERR(ir)) {
        fprintf(stderr, "yvideo: init envelope failed: %s\n", ir.error.msg);
        yetty_ycore_error_destroy(ir.error);
        free(chunk);
        fclose(f);
        return 1;
    }

    for (;;) {
        size_t n = fread(chunk, 1, (size_t)o.chunk, f);
        if (n == 0) break;
        struct yetty_ycore_void_result ur = emit_update(o.stream_id, chunk, n);
        if (YETTY_IS_ERR(ur)) {
            fprintf(stderr, "yvideo: update envelope failed: %s\n", ur.error.msg);
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
