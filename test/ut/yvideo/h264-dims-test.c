/* yetty_yvideo_h264_dimensions() boundary contract — headless, deterministic.
 *
 * Pins the hardened SPS parser's accept/reject envelope with synthesized
 * Annex-B payloads (no assets, no I/O):
 *   - a baseline 4:2:0 SPS parses to its coded size
 *   - a High-profile Level 6.2 frame (1025x128 macroblocks, area within
 *     MaxFS = 139264) is ACCEPTED — the bounds are the standard's, not an
 *     arbitrary power of two
 *   - a hostile pic_width Exp-Golomb value that wraps 32-bit arithmetic is
 *     rejected
 *   - a hostile num_ref_frames_in_pic_order_cnt_cycle (~2^32) returns
 *     rejection promptly instead of spinning for billions of iterations
 *   - start code + SPS header byte + all-zero payload is rejected
 *   - 4:2:2 cropping uses the chroma-derived crop units (16x16 coded,
 *     bottom crop 2 -> 16x14, not the 4:2:0-assumed 16x12)
 *   - a frame whose per-dimension counts pass but whose area exceeds
 *     MaxFS is rejected
 *   - a crop consuming the whole coded frame is rejected
 */

#include <yetty/yvideo/yvideo.h>

#include "ytest.h"

#include <stdint.h>
#include <string.h>

/* Minimal MSB-first bit writer for synthesizing SPS RBSP payloads. */
struct sps_writer {
    uint8_t bytes[256];
    size_t bit_count;
};

static void sps_put_bit(struct sps_writer *writer, uint32_t bit)
{
    size_t byte_index = writer->bit_count / 8u;
    size_t bit_index = 7u - (writer->bit_count % 8u);
    if (bit) {
        writer->bytes[byte_index] |= (uint8_t)(1u << bit_index);
    }
    writer->bit_count++;
}

static void sps_put_flag(struct sps_writer *writer, uint32_t value)
{
    sps_put_bit(writer, value ? 1u : 0u);
}

/* Exp-Golomb ue(v): (leading zeros) + binary(value + 1). */
static void sps_put_ue(struct sps_writer *writer, uint32_t value)
{
    uint64_t code = (uint64_t)value + 1u;
    int length = 0;
    for (uint64_t probe = code; probe > 0; probe >>= 1) {
        length++;
    }
    for (int i = 0; i < length - 1; i++) {
        sps_put_bit(writer, 0u);
    }
    for (int i = length - 1; i >= 0; i--) {
        sps_put_bit(writer, (uint32_t)((code >> i) & 1u));
    }
}

/* Assemble [start code][0x67][profile, constraints, level][rbsp + stop bit]. */
static size_t sps_payload(const struct sps_writer *writer, uint8_t profile_idc, uint8_t level_idc,
                          uint8_t *out, size_t out_max)
{
    struct sps_writer stopped = *writer;
    sps_put_bit(&stopped, 1u); /* rbsp_stop_one_bit */
    size_t rbsp_bytes = (stopped.bit_count + 7u) / 8u;
    size_t total = 8u + rbsp_bytes;
    if (total > out_max) {
        return 0;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 1;
    out[4] = 0x67; /* nal_unit_type 7 (SPS) */
    out[5] = profile_idc;
    out[6] = 0x00; /* constraint flags + reserved */
    out[7] = level_idc;
    memcpy(out + 8, stopped.bytes, rbsp_bytes);
    return total;
}

/* The common SPS tail: pic_order_cnt_type 0, no gaps, dimensions, flags. */
static void sps_put_tail(struct sps_writer *writer, uint32_t width_in_mbs_minus1,
                         uint32_t height_in_map_units_minus1, uint32_t crop_bottom)
{
    sps_put_ue(writer, 0);   /* log2_max_frame_num_minus4 */
    sps_put_ue(writer, 0);   /* pic_order_cnt_type */
    sps_put_ue(writer, 0);   /* log2_max_pic_order_cnt_lsb_minus4 */
    sps_put_ue(writer, 0);   /* max_num_ref_frames */
    sps_put_flag(writer, 0); /* gaps_in_frame_num_value_allowed_flag */
    sps_put_ue(writer, width_in_mbs_minus1);
    sps_put_ue(writer, height_in_map_units_minus1);
    sps_put_flag(writer, 1); /* frame_mbs_only_flag */
    sps_put_flag(writer, 0); /* direct_8x8_inference_flag */
    if (crop_bottom > 0) {
        sps_put_flag(writer, 1); /* frame_cropping_flag */
        sps_put_ue(writer, 0);   /* left */
        sps_put_ue(writer, 0);   /* right */
        sps_put_ue(writer, 0);   /* top */
        sps_put_ue(writer, crop_bottom);
    } else {
        sps_put_flag(writer, 0);
    }
    sps_put_flag(writer, 0); /* vui_parameters_present_flag */
}

/* The High-profile prefix carried before the tail (chroma + depths). */
static void sps_put_high_profile_prefix(struct sps_writer *writer, uint32_t chroma_format_idc)
{
    sps_put_ue(writer, 0); /* seq_parameter_set_id */
    sps_put_ue(writer, chroma_format_idc);
    sps_put_ue(writer, 0);   /* bit_depth_luma_minus8 */
    sps_put_ue(writer, 0);   /* bit_depth_chroma_minus8 */
    sps_put_flag(writer, 0); /* qpprime_y_zero_transform_bypass_flag */
    sps_put_flag(writer, 0); /* seq_scaling_matrix_present_flag */
}

static void test_baseline_accepts(struct ytest *test)
{
    struct sps_writer writer = {0};
    sps_put_ue(&writer, 0);         /* seq_parameter_set_id */
    sps_put_tail(&writer, 0, 0, 0); /* 1x1 macroblocks -> 16x16 */
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 66, 30, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 1);
    YTEST_REQUIRE(test, width == 16u && height == 16u);
}

static void test_level_6_2_accepts(struct ytest *test)
{
    /* 1025x128 macroblocks: per-dimension over the old 1024 cap, frame
     * area 131200 <= MaxFS 139264 — valid at Level 6.2, must parse. */
    struct sps_writer writer = {0};
    sps_put_high_profile_prefix(&writer, 1);
    sps_put_tail(&writer, 1024, 127, 0);
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 100, 62, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 1);
    YTEST_REQUIRE(test, width == 16400u && height == 2048u);
}

static void test_wrapping_width_rejected(struct ytest *test)
{
    /* (0x10000000 + 1) * 16 wraps to 16 in uint32 — the parser must see
     * the true value and reject, not the wrapped one. */
    struct sps_writer writer = {0};
    sps_put_ue(&writer, 0);
    sps_put_tail(&writer, 0x10000000u, 0, 0);
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 66, 30, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 0);
}

static void test_hostile_cycle_count_rejected(struct ytest *test)
{
    /* pic_order_cnt_type 1 with num_ref_frames_in_pic_order_cnt_cycle of
     * ~2^32: formerly pinned the parser for billions of iterations. The
     * ctest TIMEOUT doubles as the promptness assertion. */
    struct sps_writer writer = {0};
    sps_put_ue(&writer, 0);           /* seq_parameter_set_id */
    sps_put_ue(&writer, 0);           /* log2_max_frame_num_minus4 */
    sps_put_ue(&writer, 1);           /* pic_order_cnt_type = 1 */
    sps_put_flag(&writer, 0);         /* delta_pic_order_always_zero_flag */
    sps_put_ue(&writer, 0);           /* offset_for_non_ref_pic (se 0) */
    sps_put_ue(&writer, 0);           /* offset_for_top_to_bottom_field (se 0) */
    sps_put_ue(&writer, 0xfffffffeu); /* the hostile cycle count */
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 66, 30, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 0);
}

static void test_zero_garbage_rejected(struct ytest *test)
{
    /* Start code, SPS header byte, then zeros: formerly "parsed" as 16x32
     * from fabricated end-of-buffer reads. */
    uint8_t payload[108] = {0, 0, 0, 1, 0x67};
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test,
                  yetty_yvideo_h264_dimensions(payload, sizeof payload, &width, &height) == 0);
}

static void test_chroma_422_crop_units(struct ytest *test)
{
    /* 4:2:2, 16x16 coded, bottom crop 2: CropUnitY is SubHeightC(1) * 1,
     * so the height is 16 - 2 = 14 (a 4:2:0-assuming parser says 12). */
    struct sps_writer writer = {0};
    sps_put_high_profile_prefix(&writer, 2);
    sps_put_tail(&writer, 0, 0, 2);
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 122, 30, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 1);
    YTEST_REQUIRE(test, width == 16u && height == 14u);
}

static void test_area_over_maxfs_rejected(struct ytest *test)
{
    /* 1055x1055 macroblocks: each dimension inside the per-dimension
     * bound, area 1113025 > MaxFS 139264 — valid at no level. */
    struct sps_writer writer = {0};
    sps_put_high_profile_prefix(&writer, 1);
    sps_put_tail(&writer, 1054, 1054, 0);
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 100, 62, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 0);
}

static void test_crop_consuming_frame_rejected(struct ytest *test)
{
    /* Bottom crop of 8 units on a 16-sample-high 4:2:0 frame: crop total
     * (8 * 2 = 16) consumes the coded height — malformed. */
    struct sps_writer writer = {0};
    sps_put_ue(&writer, 0);
    sps_put_tail(&writer, 0, 0, 8);
    uint8_t payload[280];
    size_t size = sps_payload(&writer, 66, 30, payload, sizeof payload);
    YTEST_REQUIRE(test, size > 0);
    uint32_t width = 0;
    uint32_t height = 0;
    YTEST_REQUIRE(test, yetty_yvideo_h264_dimensions(payload, size, &width, &height) == 0);
}

int main(void)
{
    struct ytest test = ytest_begin("yvideo_h264_dims");
    YTEST_RUN(&test, test_baseline_accepts);
    YTEST_RUN(&test, test_level_6_2_accepts);
    YTEST_RUN(&test, test_wrapping_width_rejected);
    YTEST_RUN(&test, test_hostile_cycle_count_rejected);
    YTEST_RUN(&test, test_zero_garbage_rejected);
    YTEST_RUN(&test, test_chroma_422_crop_units);
    YTEST_RUN(&test, test_area_over_maxfs_rejected);
    YTEST_RUN(&test, test_crop_consuming_frame_rejected);
    return ytest_end(&test);
}
