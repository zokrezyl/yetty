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
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/yvideo/yvideo-gen.h>
#include <yetty/yvideo/yvideo.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* minimp4 is header-only — emit its implementation in THIS translation
 * unit. Kept after the project headers so its `#define` knobs don't
 * leak into them. */
#define MINIMP4_IMPLEMENTATION
#include <minimp4.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}
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
            if (buf[j] == 0 && buf[j + 1] == 0 &&
                (buf[j + 2] == 1 || (j + 4 <= size && buf[j + 2] == 0 && buf[j + 3] == 1))) {
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

        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
            profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
            profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
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
            if (cx < width) {
                width -= cx;
            }
            if (cy < height) {
                height -= cy;
            }
        }
        *out_w = width;
        *out_h = height;
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * MP4 → Annex-B demuxer.
 *
 * minimp4 returns each H.264 frame as a sequence of length-prefixed
 * NAL units (AVCC format: `[u32 length_be][nal_bytes]…`). We convert
 * to Annex-B byte stream (`00 00 00 01 [nal_bytes]…`) and prepend the
 * SPS + PPS NALs from the track's DSI so the receiving decoder can
 * start cold without the MP4 metadata.
 *-------------------------------------------------------------------------*/

static int mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token)
{
    struct mp4_buf {
        const uint8_t *data;
        size_t size;
    } *src = token;
    if (offset < 0 || (size_t)offset > src->size || size > src->size - (size_t)offset) {
        return -1;
    }
    memcpy(buffer, src->data + offset, size);
    return 0;
}

/* True iff `buf` looks like an MP4: ftyp box at offset 4 (the canonical
 * ISO base-media file format signature). */
static bool is_mp4(const uint8_t *buf, size_t size)
{
    return size >= 8 && buf[4] == 'f' && buf[5] == 't' && buf[6] == 'y' && buf[7] == 'p';
}

/* Append `[00 00 00 01][nal_bytes]` to *out (realloc'd as needed). */
static int annexb_append(uint8_t **out, size_t *out_len, size_t *out_cap, const uint8_t *nal,
                         size_t nal_len)
{
    size_t need = *out_len + 4u + nal_len;
    if (need > *out_cap) {
        size_t new_cap = *out_cap ? *out_cap : 4096;
        while (new_cap < need) {
            new_cap *= 2;
        }
        uint8_t *nb = realloc(*out, new_cap);
        if (!nb) {
            return -1;
        }
        *out = nb;
        *out_cap = new_cap;
    }
    uint8_t *p = *out + *out_len;
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 1;
    memcpy(p + 4, nal, nal_len);
    *out_len = need;
    return 0;
}

/* Demux the first video track of an MP4 file into a freshly-allocated
 * Annex-B byte stream. Returns 0 on success; *out / *out_len take the
 * allocated buffer + its length. Caller frees *out. */
static int demux_mp4_to_annexb(const uint8_t *mp4_data, size_t mp4_size, uint8_t **out,
                               size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    size_t out_cap = 0;

    struct {
        const uint8_t *data;
        size_t size;
    } src = {mp4_data, mp4_size};

    MP4D_demux_t mp4;
    if (!MP4D_open(&mp4, mp4_read_cb, &src, (int64_t)mp4_size)) {
        return -1;
    }

    /* Find an H.264 video track. handler_type 'vide' = video,
     * object_type_indication 0x21 = AVC. */
    int track_idx = -1;
    for (unsigned i = 0; i < mp4.track_count; i++) {
        if (mp4.track[i].handler_type == (('v' << 24) | ('i' << 16) | ('d' << 8) | 'e') &&
            mp4.track[i].object_type_indication == 0x21) {
            track_idx = (int)i;
            break;
        }
    }
    if (track_idx < 0) {
        MP4D_close(&mp4);
        return -2;
    }

    /* Emit SPS NALs from track's DSI. */
    for (int n = 0;; n++) {
        int sps_bytes = 0;
        const void *sps = MP4D_read_sps(&mp4, (unsigned)track_idx, n, &sps_bytes);
        if (!sps) {
            break;
        }
        if (annexb_append(out, out_len, &out_cap, sps, (size_t)sps_bytes) < 0) {
            free(*out);
            MP4D_close(&mp4);
            return -3;
        }
    }
    /* Emit PPS NALs. */
    for (int n = 0;; n++) {
        int pps_bytes = 0;
        const void *pps = MP4D_read_pps(&mp4, (unsigned)track_idx, n, &pps_bytes);
        if (!pps) {
            break;
        }
        if (annexb_append(out, out_len, &out_cap, pps, (size_t)pps_bytes) < 0) {
            free(*out);
            MP4D_close(&mp4);
            return -3;
        }
    }

    /* Per-sample (frame) NAL conversion. MP4 stores each frame as one
     * or more length-prefixed NAL units; convert each to start-code
     * Annex-B. */
    MP4D_track_t *tr = &mp4.track[track_idx];
    for (unsigned s = 0; s < tr->sample_count; s++) {
        unsigned frame_bytes = 0;
        MP4D_file_offset_t off =
            MP4D_frame_offset(&mp4, (unsigned)track_idx, s, &frame_bytes, NULL, NULL);
        if (off + frame_bytes > mp4_size || frame_bytes < 4) {
            continue;
        }
        const uint8_t *p = mp4_data + off;
        const uint8_t *end = p + frame_bytes;
        while (p + 4 <= end) {
            uint32_t nal_len = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            p += 4;
            if (nal_len == 0 || p + nal_len > end) {
                break;
            }
            if (annexb_append(out, out_len, &out_cap, p, nal_len) < 0) {
                free(*out);
                MP4D_close(&mp4);
                return -3;
            }
            p += nal_len;
        }
    }

    MP4D_close(&mp4);
    return 0;
}

/*---------------------------------------------------------------------------
 * OSC envelope plumbing — mirrors demo/yvideo/video-source.c (the wire
 * shape is identical; this is the polished CLI on top).
 *-------------------------------------------------------------------------*/

struct yvideo_opts {
    const char *path;
    uint32_t video_w; /* parsed from SPS, not user-overridable */
    uint32_t video_h; /* parsed from SPS, not user-overridable */
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
            "Video dimensions are taken from the first SPS NAL — they MUST\n"
            "match what the decoder will produce, so they're not user\n"
            "overridable. Use --bounds-w / --bounds-h for display size.\n"
            "\n"
            "Options:\n"
            "      --fps=F         playback rate, default 30\n"
            "      --bounds-w=N    display width in pane px (default = video width)\n"
            "      --bounds-h=N    display height in pane px (default = video height)\n"
            "      --chunk=N       split the stream into N-byte CMD_UPDATE envelopes\n"
            "                      (default 0 = send the whole stream in one INIT envelope;\n"
            "                       only use --chunk for clips too large to buffer on\n"
            "                       the receiver — chunking adds per-envelope render kicks)\n"
            "      --period-ms=N   sleep between UPDATEs, default 33\n"
            "      --no-sleep      emit as fast as possible (PTY backpressure paces)\n"
            "      --stream-id=N   addressable id for the figure, default 1\n"
            "      --no-loop       drop the LOOP flag\n"
            "      --no-autoplay   drop the AUTOPLAY flag\n"
            "  -h, --help          show this help\n",
            prog);
}

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') {
        return -1;
    }
    *out = (int)v;
    return 0;
}
static int parse_float(const char *s, float *out)
{
    char *end = NULL;
    float v = strtof(s, &end);
    if (!end || *end != '\0') {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_args(int argc, char **argv, struct yvideo_opts *o)
{
    memset(o, 0, sizeof(*o));
    o->fps = 30.0f;
    /* chunk == 0 → send the entire byte stream in ONE INIT envelope.
     * The receiving wire handles multi-MB prims fine. Splitting into
     * CMD_UPDATE chunks was a v1 workaround that made each envelope
     * kick a render (via scene-canvas's cursor_set_callback piggy-
     * back), producing un-paced render storms that look like flicker.
     * Streaming with --chunk=N stays available for very long clips
     * where the whole demuxed buffer would be too big to keep in
     * memory on the receiver. */
    o->chunk = 0;
    o->period_ms = 33;
    o->stream_id = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout, argv[0]);
            return 1;
        } else if (strncmp(a, "--width=", 8) == 0 || strcmp(a, "-w") == 0 ||
                   strcmp(a, "--width") == 0 || strncmp(a, "--height=", 9) == 0 ||
                   strcmp(a, "-H") == 0 || strcmp(a, "--height") == 0) {
            fprintf(stderr,
                    "%s: --width / --height removed — dimensions come "
                    "from the SPS; use --bounds-w / --bounds-h for display size.\n",
                    argv[0]);
            return -1;
        } else if (strncmp(a, "--fps=", 6) == 0) {
            if (parse_float(a + 6, &o->fps) < 0) {
                return -1;
            }
        } else if (strncmp(a, "--bounds-w=", 11) == 0) {
            if (parse_float(a + 11, &o->bounds_w) < 0) {
                return -1;
            }
        } else if (strncmp(a, "--bounds-h=", 11) == 0) {
            if (parse_float(a + 11, &o->bounds_h) < 0) {
                return -1;
            }
        } else if (strncmp(a, "--chunk=", 8) == 0) {
            if (parse_int(a + 8, &o->chunk) < 0) {
                return -1;
            }
        } else if (strncmp(a, "--period-ms=", 12) == 0) {
            if (parse_int(a + 12, &o->period_ms) < 0) {
                return -1;
            }
        } else if (strncmp(a, "--stream-id=", 12) == 0) {
            if (parse_int(a + 12, &o->stream_id) < 0) {
                return -1;
            }
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
    if (!o->path) {
        return -1;
    }
    if (o->chunk < 0 || o->stream_id <= 0 || o->fps <= 0.0f) {
        return -1;
    }
    if (o->video_w && (o->video_w & 1u)) {
        return -1;
    }
    if (o->video_h && (o->video_h & 1u)) {
        return -1;
    }
    return 0;
}

static struct yetty_ycore_void_result emit_scene_bin(const struct yetty_ydraw_drawable_list *dl)
{
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)dl, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "drawable_list_serialize: empty");
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
        YETTY_DCS_YCOMPOSITOR_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
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

/* Defensive cleanup: emit one CMD_ZERO envelope BEFORE the INIT GROUP
 * so any prior scene-canvas state (e.g. a yvideo entity left over from
 * a previous run in the same terminal session) is wiped. scene_clear
 * walks every in-use entity and tears it down through the figure
 * destroy chain, which reaches our destroy hook → stops & frees the
 * timer subscription. Empty canvas is a no-op. */
static struct yetty_ycore_void_result emit_zero(void)
{
    struct yetty_ydraw_drawable_list_config dlcfg = {0};
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(&dlcfg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dlr, "yvideo: zero drawable_list create");
    struct yetty_ydraw_drawable_list *dl = dlr.value;
    struct yetty_ycore_void_result zr = yetty_ydraw_drawable_list_add_cmd_zero(dl);
    if (YETTY_IS_ERR(zr)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: add_cmd_zero", zr);
    }
    struct yetty_ycore_void_result em = emit_scene_bin(dl);
    yetty_ydraw_drawable_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, em, "yvideo: emit zero");
    return YETTY_OK_VOID();
}

/* INIT envelope: GROUP(stream_id) wrapping a yvideo prim with the first
 * NAL chunk. Mirrors what demo/yvideo/video-source.c does. */
static struct yetty_ycore_void_result emit_init(const struct yvideo_opts *o, const uint8_t *first,
                                                size_t first_len)
{
    uint32_t flags = 0u;
    if (!o->no_loop) {
        flags |= YETTY_YVIDEO_FLAG_LOOP;
    }
    if (!o->no_autoplay) {
        flags |= YETTY_YVIDEO_FLAG_AUTOPLAY;
    }

    struct yetty_yvideo_uniforms u = {
        .bounds_x = 0.0f,
        .bounds_y = 0.0f,
        .bounds_w = o->bounds_w > 0.0f ? o->bounds_w : (float)o->video_w,
        .bounds_h = o->bounds_h > 0.0f ? o->bounds_h : (float)o->video_h,
        .video_w = o->video_w,
        .video_h = o->video_h,
        /* v2: YUV 4:2:0 chroma planes are half-res in each axis. */
        .chroma_w = o->video_w / 2u,
        .chroma_h = o->video_h / 2u,
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

    struct yetty_ydraw_drawable_list_config dlcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_w,
        .scene_max_y = u.bounds_h,
    };
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(dlr)) {
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "yvideo: drawable_list create", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;

    struct yetty_ydraw_id_result gr =
        yetty_ydraw_drawable_list_begin_group(dl, (uint32_t)o->stream_id);
    if (YETTY_IS_ERR(gr)) {
        yetty_ydraw_drawable_list_destroy(dl);
        free(prim_bytes);
        return YETTY_ERR(yetty_ycore_void, "yvideo: begin_group", gr);
    }
    struct yetty_ydraw_id_result ar = yetty_ydraw_drawable_list_add_prim(dl, prim_bytes, prim_size);
    free(prim_bytes);
    if (YETTY_IS_ERR(ar)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: add_prim", ar);
    }
    struct yetty_ycore_void_result er = yetty_ydraw_drawable_list_end_group(dl, (uint32_t)gr.value);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: end_group", er);
    }

    struct yetty_ycore_void_result em = emit_scene_bin(dl);
    yetty_ydraw_drawable_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, em, "yvideo: emit init");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_update(int stream_id, const uint8_t *bytes, size_t len)
{
    /* v2 typed payload (#198 item 3): [u8 op][u8 reserved[3]][body...].
     * Op 0x00 = APPEND_NAL — body is raw H.264 Annex-B bytes. The
     * receiver REJECTS payloads < 4 bytes and treats the first byte as
     * the op code, so the bare-bytes v1 path no longer works. */
    size_t total = 4u + len;
    uint8_t *typed = malloc(total);
    if (!typed) {
        return YETTY_ERR(yetty_ycore_void, "yvideo: update typed alloc failed");
    }
    typed[0] = YETTY_YVIDEO_UPDATE_OP_APPEND_NAL;
    typed[1] = 0u;
    typed[2] = 0u;
    typed[3] = 0u;
    if (len > 0u) {
        memcpy(typed + 4u, bytes, len);
    }

    struct yetty_ydraw_drawable_list_config dlcfg = {0};
    struct yetty_ydraw_drawable_list_result dlr =
        yetty_ydraw_drawable_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(dlr)) {
        free(typed);
        return YETTY_ERR(yetty_ycore_void, "yvideo: update drawable_list create", dlr);
    }
    struct yetty_ydraw_drawable_list *dl = dlr.value;
    struct yetty_ycore_void_result cu =
        yetty_ydraw_drawable_list_add_cmd_update(dl, (uint32_t)stream_id, typed, total);
    free(typed);
    if (YETTY_IS_ERR(cu)) {
        yetty_ydraw_drawable_list_destroy(dl);
        return YETTY_ERR(yetty_ycore_void, "yvideo: add_cmd_update", cu);
    }
    struct yetty_ycore_void_result em = emit_scene_bin(dl);
    yetty_ydraw_drawable_list_destroy(dl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, em, "yvideo: emit update");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct yvideo_opts o;
    int pa = parse_args(argc, argv, &o);
    if (pa == 1) {
        return 0;
    }
    if (pa < 0) {
        usage(stderr, argv[0]);
        return 2;
    }

    /* Load the whole file into memory. Keeps the demux path simple
     * (minimp4 needs random access) and lets us treat .h264 and .mp4
     * inputs uniformly downstream. */
    FILE *f = fopen(o.path, "rb");
    if (!f) {
        fprintf(stderr, "yvideo: cannot open '%s'\n", o.path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "yvideo: fseek failed\n");
        return 1;
    }
    long flen_signed = ftell(f);
    if (flen_signed <= 0) {
        fclose(f);
        fprintf(stderr, "yvideo: empty input\n");
        return 1;
    }
    rewind(f);
    size_t flen = (size_t)flen_signed;
    uint8_t *file_buf = malloc(flen);
    if (!file_buf) {
        fclose(f);
        fprintf(stderr, "yvideo: file buf alloc failed (%zu bytes)\n", flen);
        return 1;
    }
    if (fread(file_buf, 1, flen, f) != flen) {
        free(file_buf);
        fclose(f);
        fprintf(stderr, "yvideo: short read\n");
        return 1;
    }
    fclose(f);

    /* If the input is MP4, demux to Annex-B in a new buffer and swap. */
    const uint8_t *stream = file_buf;
    size_t stream_len = flen;
    uint8_t *demuxed = NULL;
    if (is_mp4(file_buf, flen)) {
        size_t demuxed_len = 0;
        int dr = demux_mp4_to_annexb(file_buf, flen, &demuxed, &demuxed_len);
        if (dr != 0) {
            free(file_buf);
            fprintf(stderr,
                    "yvideo: MP4 demux failed (%d) — only single-track AVC/H.264 "
                    "MP4 files are supported.\n",
                    dr);
            return 1;
        }
        stream = demuxed;
        stream_len = demuxed_len;
        fprintf(stderr, "yvideo: demuxed MP4 → %zu bytes of Annex-B\n", demuxed_len);
    }

    /* Dimensions come from the SPS — they must match what the decoder
     * will produce on the receiving side or its scratch buffer would
     * overflow. */
    {
        uint32_t w = 0, h = 0;
        if (!h264_extract_dimensions(stream, stream_len, &w, &h)) {
            free(demuxed);
            free(file_buf);
            fprintf(stderr, "yvideo: no SPS NAL found in stream (after demux=%s).\n",
                    demuxed ? "yes" : "no");
            return 1;
        }
        o.video_w = w;
        o.video_h = h;
    }
    if ((o.video_w & 1u) || (o.video_h & 1u)) {
        free(demuxed);
        free(file_buf);
        fprintf(stderr, "yvideo: video dimensions must be even (got %ux%u)\n", o.video_w,
                o.video_h);
        return 1;
    }

    /* Wipe any prior scene-canvas state before INIT (re-runs in the
     * same terminal would otherwise hit GROUP id collisions). Errors
     * are logged but not fatal. */
    {
        struct yetty_ycore_void_result zr = emit_zero();
        if (YETTY_IS_ERR(zr)) {
            fprintf(stderr, "yvideo: pre-zero envelope failed: %s\n", zr.error.msg);
            yetty_ycore_error_destroy(zr.error);
        }
    }

    /* chunk == 0 → send the WHOLE Annex-B stream in one INIT envelope.
     * Otherwise: first chunk → INIT, the rest → CMD_UPDATE envelopes.
     * Each CMD_UPDATE arrival on the receiver triggers a render via
     * the cursor_set_callback piggyback, so chunking trades smooth
     * playback for incremental delivery — only worth it on very large
     * clips. */
    size_t first_n = stream_len;
    if (o.chunk > 0 && (size_t)o.chunk < first_n) {
        first_n = (size_t)o.chunk;
    }
    struct yetty_ycore_void_result ir = emit_init(&o, stream, first_n);
    if (YETTY_IS_ERR(ir)) {
        fprintf(stderr, "yvideo: init envelope failed: %s\n", ir.error.msg);
        yetty_ycore_error_destroy(ir.error);
        free(demuxed);
        free(file_buf);
        return 1;
    }
    size_t cursor = first_n;

    while (cursor < stream_len) {
        size_t n = stream_len - cursor;
        if ((size_t)o.chunk > 0 && n > (size_t)o.chunk) {
            n = (size_t)o.chunk;
        }
        struct yetty_ycore_void_result ur = emit_update(o.stream_id, stream + cursor, n);
        if (YETTY_IS_ERR(ur)) {
            fprintf(stderr, "yvideo: update envelope failed: %s\n", ur.error.msg);
            yetty_ycore_error_destroy(ur.error);
            free(demuxed);
            free(file_buf);
            return 1;
        }
        cursor += n;
        if (!o.no_sleep && cursor < stream_len) {
            sleep_ms(o.period_ms);
        }
    }

    /* Keep the PTY child alive until the receiving terminal has had
     * time to play the clip. Under `yetty -e 'yvideo …'` the tool IS
     * the PTY child — exiting closes the PTY which tears the terminal
     * down (and the figure with it). Estimate playout time from slice
     * NAL count ÷ fps; fall back to file size when no slices are
     * countable. The user can Ctrl-C to break out early. */
    if (!o.no_sleep) {
        uint32_t slices = 0;
        for (size_t i = 0; i + 4 < stream_len;) {
            bool sc4 =
                (stream[i] == 0 && stream[i + 1] == 0 && stream[i + 2] == 0 && stream[i + 3] == 1);
            bool sc3 = (stream[i] == 0 && stream[i + 1] == 0 && stream[i + 2] == 1);
            if (!sc3 && !sc4) {
                i++;
                continue;
            }
            size_t hdr = i + (sc4 ? 4u : 3u);
            if (hdr >= stream_len) {
                break;
            }
            uint8_t t = stream[hdr] & 0x1fu;
            if (t == 1u || t == 5u) {
                slices++;
            }
            i = hdr + 1u;
        }
        double secs = (slices > 0 && o.fps > 0.0f) ? (double)slices / (double)o.fps : 0.0;
        if (secs > 0.0) {
            int ms = (int)(secs * 1000.0) + 500; /* +500ms slack for decode/upload */
            sleep_ms(ms);
        }
    }

    free(demuxed);
    free(file_buf);
    return 0;
}
