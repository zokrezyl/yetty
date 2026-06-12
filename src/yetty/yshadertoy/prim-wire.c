/*
 * prim-wire.c — yshadertoy prim wire serializer (CPU-only).
 *
 * Packs a bounds rect + the user's WGSL mainImage text into the complex-prim
 * record layout documented in prim.h, and offers a one-call drawable-list
 * builder for senders (ycat). No WebGPU — lives in yetty_yshadertoy_core so
 * client tools link it without Dawn.
 */

#include <yetty/yshadertoy/prim.h>

#include <stdlib.h>
#include <string.h>

/* Payload words ahead of the WGSL text: 4×f32 bounds + flags + wgsl_len. */
#define YSHADERTOY_PRIM_FIXED_WORDS 6u

size_t yetty_yshadertoy_prim_serialized_size(size_t wgsl_len)
{
    size_t wgsl_words = (wgsl_len + 3u) / 4u;
    return (2u + YSHADERTOY_PRIM_FIXED_WORDS + wgsl_words) * sizeof(uint32_t);
}

struct yetty_ycore_size_result yetty_yshadertoy_prim_serialize(
    const struct yetty_yshadertoy_prim_config *config, const char *wgsl_source, size_t wgsl_len,
    uint8_t *out, size_t out_capacity)
{
    if (!config) {
        return YETTY_ERR(yetty_ycore_size, "yshadertoy prim serialize: config is NULL");
    }
    if (!out) {
        return YETTY_ERR(yetty_ycore_size, "yshadertoy prim serialize: out is NULL");
    }
    if (wgsl_len > 0u && !wgsl_source) {
        return YETTY_ERR(yetty_ycore_size, "yshadertoy prim serialize: wgsl_len > 0 but source NULL");
    }
    if (wgsl_len > UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_size, "yshadertoy prim serialize: shader text exceeds u32");
    }

    size_t required = yetty_yshadertoy_prim_serialized_size(wgsl_len);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "yshadertoy prim serialize: buffer too small");
    }

    uint32_t *words = (uint32_t *)out;
    words[0] = YETTY_YSHADERTOY_PRIM_TYPE_ID;
    words[1] = (uint32_t)(required - 2u * sizeof(uint32_t));
    memcpy(&words[2], &config->bounds_x, sizeof(float));
    memcpy(&words[3], &config->bounds_y, sizeof(float));
    memcpy(&words[4], &config->bounds_w, sizeof(float));
    memcpy(&words[5], &config->bounds_h, sizeof(float));
    words[6] = 0u; /* flags — reserved */
    words[7] = (uint32_t)wgsl_len;
    if (wgsl_len > 0u) {
        /* Zero the trailing pad bytes of the last word, then copy. */
        size_t wgsl_words = (wgsl_len + 3u) / 4u;
        words[8u + wgsl_words - 1u] = 0u;
        memcpy(&words[8], wgsl_source, wgsl_len);
    }
    return YETTY_OK(yetty_ycore_size, required);
}

struct yetty_ydraw_drawable_list_result yetty_yshadertoy_prim_render(
    const char *wgsl_source, size_t wgsl_len, const struct yetty_yshadertoy_prim_config *config)
{
    if (!config || config->bounds_w <= 0.0f || config->bounds_h <= 0.0f) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "yshadertoy prim render: config NULL or bounds empty");
    }

    size_t required = yetty_yshadertoy_prim_serialized_size(wgsl_len);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yshadertoy prim render: alloc failed");
    }
    struct yetty_ycore_size_result serialize_res =
        yetty_yshadertoy_prim_serialize(config, wgsl_source, wgsl_len, prim_buf, required);
    if (YETTY_IS_ERR(serialize_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yshadertoy prim render: serialize",
                         serialize_res);
    }

    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = config->bounds_x + config->bounds_w,
        .scene_max_y = config->bounds_y + config->bounds_h,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    if (YETTY_IS_ERR(list_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yshadertoy prim render: list create",
                         list_res);
    }

    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(list_res.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yshadertoy prim render: add_prim", add_res);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list_res.value);
}
