/*
 * font-dispatcher.c -- per-slot font dispatcher WGSL generator.
 *
 * See include/yetty/yrender/font-dispatcher.h for the contract. This
 * file is the single source of truth for the dispatcher shape; both
 * ydraw-layer and ygrid call into it instead of carrying their own
 * copy of the snprintf chain that used to live in ydraw-layer.c.
 */

#include <yetty/yrender/font-dispatcher.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Heap-grown char buffer. Doubling growth; capacity tracked separately
 * from used size so we can reserve once and append without checking
 * after every snprintf. */
struct wgsl_string_builder {
    char *data;
    size_t size;
    size_t capacity;
    int oom;
};

static int builder_reserve(struct wgsl_string_builder *builder, size_t additional)
{
    if (builder->oom) {
        return -1;
    }
    /* +1 for the trailing NUL the caller relies on. */
    size_t needed = builder->size + additional + 1u;
    if (needed <= builder->capacity) {
        return 0;
    }
    size_t new_capacity = builder->capacity ? builder->capacity * 2u : 256u;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }
    char *grown = (char *)realloc(builder->data, new_capacity);
    if (!grown) {
        builder->oom = 1;
        return -1;
    }
    builder->data = grown;
    builder->capacity = new_capacity;
    return 0;
}

/* printf-style append. Sets builder->oom on allocation failure so the
 * caller can check once at the end instead of after every line. */
static void builder_appendf(struct wgsl_string_builder *builder, const char *fmt, ...)
{
    if (builder->oom) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    /* First pass: measure required length. vsnprintf needs a separate
     * va_list each call (it consumes the args), so copy before reuse. */
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0) {
        builder->oom = 1;
        va_end(args);
        return;
    }
    if (builder_reserve(builder, (size_t)needed) < 0) {
        va_end(args);
        return;
    }
    int written =
        vsnprintf(builder->data + builder->size, builder->capacity - builder->size, fmt, args);
    va_end(args);
    if (written < 0) {
        builder->oom = 1;
        return;
    }
    builder->size += (size_t)written;
}

struct yetty_ycore_void_result yetty_yrender_build_font_dispatcher_wgsl(
    const char *const *slot_namespaces, size_t slot_count, char **out_wgsl, size_t *out_size)
{
    if (!out_wgsl || !out_size) {
        return YETTY_ERR(yetty_ycore_void, "build_font_dispatcher_wgsl: NULL out args");
    }
    if (slot_count > 0 && !slot_namespaces) {
        return YETTY_ERR(yetty_ycore_void,
                         "build_font_dispatcher_wgsl: NULL namespaces with slot_count > 0");
    }

    struct wgsl_string_builder builder = {0};

    builder_appendf(&builder, "// auto-generated font dispatcher (slots 0..%zu)\n", slot_count);

    /* font_base_size(slot) -> f32 */
    builder_appendf(&builder, "fn font_base_size(slot: u32) -> f32 {\n"
                              "    switch slot {\n");
    for (size_t slot = 0; slot < slot_count; slot++) {
        const char *namespace_name = slot_namespaces[slot];
        if (!namespace_name) {
            continue;
        }
        builder_appendf(&builder, "        case %zuu: { return uniforms.%s_base_size; }\n", slot,
                        namespace_name);
    }
    builder_appendf(&builder, "        default: { return 1.0; }\n"
                              "    }\n"
                              "}\n");

    /* font_glyph_size(slot, glyph_index) -> vec2<f32> */
    builder_appendf(&builder, "fn font_glyph_size(slot: u32, glyph_index: u32) -> vec2<f32> {\n"
                              "    switch slot {\n");
    for (size_t slot = 0; slot < slot_count; slot++) {
        const char *namespace_name = slot_namespaces[slot];
        if (!namespace_name) {
            continue;
        }
        builder_appendf(&builder, "        case %zuu: { return %s_glyph_size(glyph_index); }\n",
                        slot, namespace_name);
    }
    builder_appendf(&builder, "        default: { return vec2<f32>(0.0, 0.0); }\n"
                              "    }\n"
                              "}\n");

    /* font_glyph_sample(slot, glyph_index, uv, ps) -> f32 */
    builder_appendf(
        &builder,
        "fn font_glyph_sample(slot: u32, glyph_index: u32, uv: vec2<f32>, ps: f32) -> f32 {\n"
        "    switch slot {\n");
    for (size_t slot = 0; slot < slot_count; slot++) {
        const char *namespace_name = slot_namespaces[slot];
        if (!namespace_name) {
            continue;
        }
        builder_appendf(&builder,
                        "        case %zuu: { return %s_glyph_sample(glyph_index, uv, ps); }\n",
                        slot, namespace_name);
    }
    builder_appendf(&builder, "        default: { return 0.0; }\n"
                              "    }\n"
                              "}\n\n");

    if (builder.oom) {
        free(builder.data);
        return YETTY_ERR(yetty_ycore_void, "build_font_dispatcher_wgsl: out of memory");
    }
    /* builder_reserve always allocates one byte beyond builder.size for
     * the NUL, so this write is in bounds. */
    builder.data[builder.size] = '\0';
    *out_wgsl = builder.data;
    *out_size = builder.size;
    return YETTY_OK_VOID();
}
