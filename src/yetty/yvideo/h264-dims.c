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
    int bit_pos;  /* 0..7, MSB-first */
    bool overrun; /* a read walked past the end, or a field was malformed;
                   * once set every later read returns 0 and the whole
                   * parse must be rejected */
};

static uint32_t br_read_bits(struct bit_reader *reader, int count)
{
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        if (reader->byte_pos >= reader->size) {
            reader->overrun = true;
            return 0;
        }
        uint32_t bit = (reader->buf[reader->byte_pos] >> (7 - reader->bit_pos)) & 1u;
        value = (value << 1) | bit;
        reader->bit_pos++;
        if (reader->bit_pos == 8) {
            reader->bit_pos = 0;
            reader->byte_pos++;
        }
    }
    return value;
}

static uint32_t br_read_ue(struct bit_reader *reader)
{
    int zero_bits = 0;
    while (!reader->overrun && br_read_bits(reader, 1) == 0) {
        zero_bits++;
        if (zero_bits > 31) {
            /* No valid Exp-Golomb code carries 32+ leading zeros; this is
             * garbage (an all-zero buffer lands here), not a real SPS. */
            reader->overrun = true;
            return 0;
        }
    }
    if (reader->overrun || zero_bits == 0) {
        return 0;
    }
    uint32_t tail = br_read_bits(reader, zero_bits);
    return (1u << zero_bits) - 1u + tail;
}

static int32_t br_read_se(struct bit_reader *reader)
{
    uint32_t coded = br_read_ue(reader);
    if (coded & 1u) {
        return (int32_t)((coded + 1u) >> 1);
    }
    return -(int32_t)(coded >> 1);
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

        struct bit_reader br = {
            .buf = rbsp, .size = rbsp_len, .byte_pos = 0, .bit_pos = 0, .overrun = false};
        uint8_t profile_idc = rbsp[0];
        br.byte_pos = 3; /* skip profile_idc, constraints+reserved, level_idc */

        (void)br_read_ue(&br); /* seq_parameter_set_id */

        /* Defaults for profiles that carry no chroma_format_idc: 4:2:0. */
        uint32_t chroma_format_idc = 1;
        uint32_t separate_colour_plane = 0;

        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
            profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
            profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135) {
            chroma_format_idc = br_read_ue(&br);
            if (chroma_format_idc > 3) {
                return 0;
            }
            if (chroma_format_idc == 3) {
                separate_colour_plane = br_read_bits(&br, 1);
            }
            (void)br_read_ue(&br); /* bit_depth_luma_minus8 */
            (void)br_read_ue(&br); /* bit_depth_chroma_minus8 */
            br_read_bits(&br, 1);  /* qpprime_y_zero_transform_bypass_flag */
            uint32_t seq_scaling_list_present = br_read_bits(&br, 1);
            if (seq_scaling_list_present) {
                int n = (chroma_format_idc != 3) ? 8 : 12;
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
            /* num_ref_frames_in_pic_order_cnt_cycle: the spec bounds it to
             * [0, 255]. A hostile Exp-Golomb value can otherwise encode
             * ~2^32 and pin this loop for billions of iterations — reject
             * oversized counts and stop on reader error immediately. */
            uint32_t cycle_count = br_read_ue(&br);
            if (br.overrun || cycle_count > 255u) {
                return 0;
            }
            for (uint32_t k = 0; k < cycle_count && !br.overrun; k++) {
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

        /* All dimension arithmetic is 64-bit: the counts are hostile-
         * controlled 32-bit Exp-Golomb values, and 32-bit products would
         * wrap into small "valid" numbers that defeat the range checks
         * below (the worst case here is ~2^37, comfortably inside u64). */
        uint64_t width_in_mbs = (uint64_t)pic_width_in_mbs_minus1 + 1u;
        uint64_t frame_height_in_mbs =
            (2u - (uint64_t)frame_mbs_only_flag) * ((uint64_t)pic_height_in_map_units_minus1 + 1u);
        /* Annex A bounds, taken at the largest defined level (6.2,
         * MaxFS = 139264 macroblocks): each dimension is limited to
         * floor(sqrt(8 * MaxFS)) = 1055 macroblocks (16880 samples), and
         * the frame area to MaxFS. Values beyond these are not valid in
         * ANY H.264 stream. */
        if (width_in_mbs > 1055u || frame_height_in_mbs > 1055u ||
            width_in_mbs * frame_height_in_mbs > 139264u) {
            return 0;
        }
        uint64_t width = width_in_mbs * 16u;
        uint64_t height = frame_height_in_mbs * 16u;

        uint32_t frame_cropping_flag = br_read_bits(&br, 1);
        if (frame_cropping_flag) {
            uint32_t left = br_read_ue(&br);
            uint32_t right = br_read_ue(&br);
            uint32_t top = br_read_ue(&br);
            uint32_t bottom = br_read_ue(&br);
            /* Crop units per 7.4.2.1.1: ChromaArrayType 0 (monochrome or
             * separate planes) crops in luma samples; otherwise in chroma
             * units — SubWidthC/SubHeightC depend on the sampling
             * (4:2:0 -> 2x2, 4:2:2 -> 2x1, 4:4:4 -> 1x1). */
            uint32_t chroma_array_type = separate_colour_plane ? 0u : chroma_format_idc;
            uint64_t crop_unit_x;
            uint64_t crop_unit_y;
            if (chroma_array_type == 0) {
                crop_unit_x = 1u;
                crop_unit_y = 2u - frame_mbs_only_flag;
            } else {
                uint64_t sub_width_c = (chroma_array_type == 3) ? 1u : 2u;
                uint64_t sub_height_c = (chroma_array_type == 1) ? 2u : 1u;
                crop_unit_x = sub_width_c;
                crop_unit_y = sub_height_c * (2u - frame_mbs_only_flag);
            }
            /* 64-bit throughout: offsets are hostile-controlled 32-bit
             * values, so their sums/products must not wrap before the
             * comparison below. */
            uint64_t cx = crop_unit_x * ((uint64_t)left + right);
            uint64_t cy = crop_unit_y * ((uint64_t)top + bottom);
            /* A crop consuming the whole coded frame is malformed. */
            if (cx >= width || cy >= height) {
                return 0;
            }
            width -= cx;
            height -= cy;
        }
        /* A parse that ran past the RBSP (or hit a malformed field) read
         * fabricated zeros — its numbers are meaningless. Re-check the
         * cropped result against the Level 6.2 per-dimension maximum
         * (1055 macroblocks = 16880 samples). */
        if (br.overrun || width == 0 || height == 0 || width > 16880u || height > 16880u) {
            return 0;
        }
        *out_w = (uint32_t)width;
        *out_h = (uint32_t)height;
        return 1;
    }
    return 0;
}
