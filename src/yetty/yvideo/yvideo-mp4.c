/*
 * yvideo-mp4.c — MP4 demux + render_from_mp4 sender helpers.
 *
 * The MP4 container is parsed with minimp4 (header-only). H.264 frames
 * land as length-prefixed AVCC NAL units; we convert them to Annex-B
 * (00 00 00 01 prefix) and prepend the SPS / PPS NALs from the
 * track's DSI so the receiving decoder can start cold. video_w /
 * video_h are extracted from the first SPS NAL with a minimal
 * Exp-Golomb bit reader — just enough to reach the cropping fields.
 *
 * Single source of truth for MP4 ingestion: tools/yvideo,
 * ygui_yvideo, and any future sender call yetty_yvideo_render_from_mp4_*
 * here rather than carrying their own demuxer.
 */

#include <yetty/yvideo/yvideo-mp4.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yvideo/yvideo.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef YETTY_HAVE_MINIMP4
/* Implementation is compiled exactly once into the minimp4 static lib
 * (see build-tools/yetty/minimp4.cmake). Don't `#define
 * MINIMP4_IMPLEMENTATION` here — it would emit MP4E_* / MP4D_*
 * duplicates whenever this TU and yvnc/vnc-server.c end up in the
 * same link. */
#include <minimp4.h>
#endif

#ifdef YETTY_HAVE_MINIMP4

/*---------------------------------------------------------------------------
 * H.264 SPS bit reader — extracts pic_width / pic_height from the first
 * SPS NAL. Just enough Exp-Golomb to walk to the cropping fields.
 *-------------------------------------------------------------------------*/

struct bit_reader {
    const uint8_t *buf;
    size_t size;
    size_t byte_pos;
    int bit_pos;
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

static size_t rbsp_strip_emulation(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    size_t oi = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (i + 2 < in_len && in[i] == 0 && in[i + 1] == 0 && in[i + 2] == 0x03) {
            if (oi + 2 > out_max) {
                break;
            }
            out[oi++] = 0;
            out[oi++] = 0;
            i += 2;
            continue;
        }
        if (oi >= out_max) {
            break;
        }
        out[oi++] = in[i];
    }
    return oi;
}

static int sps_extract_dims(const uint8_t *buf, size_t size, uint32_t *out_w, uint32_t *out_h)
{
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
        uint8_t rbsp[4096];
        size_t rbsp_len =
            rbsp_strip_emulation(buf + hdr + 1, nal_end - (hdr + 1), rbsp, sizeof rbsp);
        if (rbsp_len < 4) {
            return 0;
        }

        struct bit_reader br = {.buf = rbsp, .size = rbsp_len, .byte_pos = 0, .bit_pos = 0};
        uint8_t profile_idc = rbsp[0];
        br.byte_pos = 3;
        (void)br_read_ue(&br);
        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
            profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
            profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135) {
            uint32_t chroma = br_read_ue(&br);
            if (chroma == 3) {
                br_read_bits(&br, 1);
            }
            (void)br_read_ue(&br);
            (void)br_read_ue(&br);
            br_read_bits(&br, 1);
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
        (void)br_read_ue(&br);
        uint32_t pic_order_cnt_type = br_read_ue(&br);
        if (pic_order_cnt_type == 0) {
            (void)br_read_ue(&br);
        } else if (pic_order_cnt_type == 1) {
            br_read_bits(&br, 1);
            (void)br_read_se(&br);
            (void)br_read_se(&br);
            uint32_t n = br_read_ue(&br);
            for (uint32_t k = 0; k < n; k++) {
                (void)br_read_se(&br);
            }
        }
        (void)br_read_ue(&br);
        br_read_bits(&br, 1);
        uint32_t pic_width_in_mbs_minus1 = br_read_ue(&br);
        uint32_t pic_height_in_map_units_minus1 = br_read_ue(&br);
        uint32_t frame_mbs_only_flag = br_read_bits(&br, 1);
        if (!frame_mbs_only_flag) {
            br_read_bits(&br, 1);
        }
        br_read_bits(&br, 1);
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
 *-------------------------------------------------------------------------*/

struct mp4_src {
    const uint8_t *data;
    size_t size;
};

static int mp4_read_cb(int64_t offset, void *buffer, size_t size, void *token)
{
    struct mp4_src *src = (struct mp4_src *)token;
    if (offset < 0 || (size_t)offset > src->size || size > src->size - (size_t)offset) {
        return -1;
    }
    memcpy(buffer, src->data + offset, size);
    return 0;
}

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

/* On success: *out is malloc'd Annex-B byte stream; caller frees.
 * Return codes: 0 ok; -1 file open / read failure; -2 no video track;
 * -3 allocation failure. */
static int demux_to_annexb(const uint8_t *mp4_data, size_t mp4_size, uint8_t **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    size_t out_cap = 0;
    struct mp4_src src = {mp4_data, mp4_size};

    MP4D_demux_t mp4;
    if (!MP4D_open(&mp4, mp4_read_cb, &src, (int64_t)mp4_size)) {
        return -1;
    }
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

static int slurp_file(const char *path, uint8_t **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = (size_t)sz;
    return 0;
}

/*---------------------------------------------------------------------------
 * Public render_from_mp4_* helpers — real implementations.
 *-------------------------------------------------------------------------*/

struct yetty_ydraw_draw_list_result yetty_yvideo_render_from_mp4_bytes(
    const uint8_t *mp4_bytes, size_t mp4_len, const struct yetty_yvideo_render_config *overrides)
{
    if (!mp4_bytes || mp4_len == 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yvideo_mp4: empty input");
    }
    uint8_t *annexb = NULL;
    size_t annexb_len = 0;
    int dr = demux_to_annexb(mp4_bytes, mp4_len, &annexb, &annexb_len);
    if (dr != 0 || !annexb || annexb_len == 0) {
        free(annexb);
        return YETTY_ERR(yetty_ydraw_draw_list,
                         "yvideo_mp4: demux failed (no video track or unreadable container)");
    }
    uint32_t video_w = 0, video_h = 0;
    if (!sps_extract_dims(annexb, annexb_len, &video_w, &video_h) || video_w == 0 || video_h == 0) {
        free(annexb);
        return YETTY_ERR(yetty_ydraw_draw_list, "yvideo_mp4: SPS dimensions extraction failed");
    }
    struct yetty_yvideo_render_config cfg = {0};
    if (overrides) {
        cfg = *overrides;
    }
    cfg.video_w = video_w; /* SPS is authoritative */
    cfg.video_h = video_h;
    if (cfg.fps <= 0.0f) {
        cfg.fps = 30.0f;
    }
    if (cfg.color_matrix == 0u) {
        cfg.color_matrix = 1u; /* BT.709 */
    }
    if (cfg.flags == 0u) {
        cfg.flags = YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY;
    }

    struct yetty_ydraw_draw_list_result r = yetty_yvideo_render(annexb, annexb_len, NULL, 0, &cfg);
    free(annexb);
    return r;
}

struct yetty_ydraw_draw_list_result yetty_yvideo_render_from_mp4_file(
    const char *path, const struct yetty_yvideo_render_config *overrides)
{
    if (!path) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yvideo_mp4: NULL path");
    }
    uint8_t *buf = NULL;
    size_t len = 0;
    if (slurp_file(path, &buf, &len) != 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yvideo_mp4: file slurp failed");
    }
    struct yetty_ydraw_draw_list_result r = yetty_yvideo_render_from_mp4_bytes(buf, len, overrides);
    free(buf);
    return r;
}

#else /* !YETTY_HAVE_MINIMP4 */

/*---------------------------------------------------------------------------
 * Stub implementations — keep the public ABI stable on builds without
 * minimp4 (so callers don't need their own ifdef). Every entry point
 * returns ERR.
 *-------------------------------------------------------------------------*/

struct yetty_ydraw_draw_list_result yetty_yvideo_render_from_mp4_bytes(
    const uint8_t *mp4_bytes, size_t mp4_len, const struct yetty_yvideo_render_config *overrides)
{
    (void)mp4_bytes;
    (void)mp4_len;
    (void)overrides;
    return YETTY_ERR(yetty_ydraw_draw_list,
                     "yvideo_mp4: build without minimp4 — MP4 ingestion unavailable");
}

struct yetty_ydraw_draw_list_result yetty_yvideo_render_from_mp4_file(
    const char *path, const struct yetty_yvideo_render_config *overrides)
{
    (void)path;
    (void)overrides;
    return YETTY_ERR(yetty_ydraw_draw_list,
                     "yvideo_mp4: build without minimp4 — MP4 ingestion unavailable");
}

#endif /* YETTY_HAVE_MINIMP4 */
