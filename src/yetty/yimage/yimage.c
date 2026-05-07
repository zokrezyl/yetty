/*
 * yimage.c — high-level convenience wrappers around the auto-generated
 * yimage-gen.c API. See include/yetty/yimage/yimage.h for the contract.
 *
 * Compute-shader resampling is deferred (see scale-image.wgsl in this
 * directory). Pixels go to the atlas at source resolution; the atlas
 * sampler does the runtime bilinear at the displayed size.
 */

#include <yetty/yimage/yimage.h>

#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ycore/types.h>
#include <yetty/yterm/osc-codes.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_image.h>

struct yetty_ypaint_core_buffer_result yetty_yimage_render(
    const uint8_t *image_bytes, size_t len,
    const struct yetty_yimage_render_config *config)
{
    if (!image_bytes || len == 0) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage: image_bytes is NULL/empty");
    }

    /* Decode via stb_image — force 4-channel RGBA8. */
    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(image_bytes, (int)len, &w, &h, &channels, 4);
    if (!pixels) {
        return YETTY_ERR(yetty_ypaint_core_buffer, stbi_failure_reason());
    }

    /* Resolve config defaults — fall back to source dimensions when caller
     * didn't pin a display size. */
    struct yetty_yimage_uniforms u = {0};
    u.bounds_x = config ? config->bounds_x : 0.0f;
    u.bounds_y = config ? config->bounds_y : 0.0f;
    u.bounds_w = (config && config->bounds_w > 0.0f) ? config->bounds_w : (float)w;
    u.bounds_h = (config && config->bounds_h > 0.0f) ? config->bounds_h : (float)h;
    u.image_w = (uint32_t)w;
    u.image_h = (uint32_t)h;

    /* Pixels treated as packed RGBA8 u32 — one word per pixel, row-major. */
    struct yetty_yimage_buffers bufs = {
        .pixels = (const uint32_t *)pixels,
        .pixels_len = (size_t)w * (size_t)h,
    };

    size_t required = yetty_yimage_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        stbi_image_free(pixels);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yimage_uniforms_serialize(&u, &bufs, prim_buf, required);
    /* The serialize call has copied pixels into prim_buf; stbi data is no
     * longer referenced. */
    stbi_image_free(pixels);
    if (YETTY_IS_ERR(ser)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage: serialize failed", ser);
    }

    struct yetty_ypaint_core_buffer_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_x + u.bounds_w,
        .scene_max_y = u.bounds_y + u.bounds_h,
    };
    struct yetty_ypaint_core_buffer_result br =
        yetty_ypaint_core_buffer_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage: ypaint buffer create failed", br);
    }

    struct yetty_ypaint_core_id_result idr =
        yetty_ypaint_core_buffer_add_prim(br.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ypaint_core_buffer_destroy(br.value);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage: ypaint add_prim failed", idr);
    }

    return YETTY_OK(yetty_ypaint_core_buffer, br.value);
}

struct yetty_ypaint_core_buffer_result yetty_yimage_render_path(
    const char *path, const struct yetty_yimage_render_config *config)
{
    if (!path) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: path is NULL");
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: fopen failed");
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: fseek failed");
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: empty or unreadable");
    }
    rewind(f);
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: malloc failed");
    }
    size_t nread = fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(bytes);
        return YETTY_ERR(yetty_ypaint_core_buffer, "yimage_render_path: short read");
    }

    struct yetty_ypaint_core_buffer_result r =
        yetty_yimage_render(bytes, (size_t)size, config);
    free(bytes);
    return r;
}

struct yetty_ycore_size_result yetty_yimage_osc_bin_emit(
    const struct yetty_ypaint_core_buffer *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yimage_osc_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ypaint_core_buffer_serialize((struct yetty_ypaint_core_buffer *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yimage_osc_bin_emit: empty serialize");
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
    struct yetty_ycore_void_result r = yetty_yface_emit(YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
                                                        &meta, sizeof(meta), raw, raw_size,
                                                        &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yimage_osc_bin_emit: yface_emit failed", r);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
