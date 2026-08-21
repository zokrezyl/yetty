/*
 * h264-dims.c — extract width/height from the first SPS NAL of a raw
 * H.264 Annex-B byte stream. Lifted from the yvideo tool so every
 * producer (the tool, the ycomplex2 video drawable) shares ONE parser.
 * CPU-only; part of yetty_yvideo_core.
 */
#include <yetty/yvideo/yvideo.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

int yetty_yvideo_h264_dimensions(const uint8_t *buf, size_t size, uint32_t *out_w, uint32_t *out_h)
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
