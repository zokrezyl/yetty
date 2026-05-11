/*
 * osc.c - OSC envelope writer for ypaint binary buffers.
 *
 * Wire format (consumed by yterm/ypaint-layer.c):
 *   ESC ] 600001 ; <b64(yetty_yface_bin_meta)> ; <base64(LZ4F(payload))> ESC \
 *
 * The payload is the magic-tagged blob produced by
 * yetty_ypaint_core_buffer_serialize() — prims + text_spans + scene_bounds.
 * yetty_yface owns the LZ4F + base64 + envelope construction.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ycore/types.h>
#include <yetty/yterm/osc-codes.h> /* YETTY_OSC_YPAINT_BIN */

#include <stdio.h>
#include <stdlib.h>

struct yetty_ycore_size_result yetty_ycat_osc_bin_emit(
    const struct yetty_ypaint_core_buffer *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ycat_osc_bin_emit: NULL buffer or out");
    }

    const uint8_t *raw_bytes = NULL;
    size_t raw_size =
        yetty_ypaint_core_buffer_serialize((struct yetty_ypaint_core_buffer *)buffer, &raw_bytes);
    if (raw_size == 0 || !raw_bytes) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ycat_osc_bin_emit: serialize empty");
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
    struct yetty_ycore_void_result r =
        yetty_yface_emit(YETTY_OSC_YPAINT_BIN, /*compressed=*/1, &meta, sizeof(meta), raw_bytes,
                         raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yetty_ycat_osc_bin_emit: yface_emit failed", r);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
