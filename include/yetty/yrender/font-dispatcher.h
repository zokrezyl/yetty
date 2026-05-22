#ifndef YETTY_YRENDER_FONT_DISPATCHER_H
#define YETTY_YRENDER_FONT_DISPATCHER_H

/*
 * Per-slot font dispatcher generator.
 *
 * ydraw-layer.wgsl's GLYPH branch calls three generic helpers:
 *
 *   font_base_size(slot)                       -> f32
 *   font_glyph_size(slot, glyph_index)         -> vec2<f32>
 *   font_glyph_sample(slot, glyph_index, uv, ps) -> f32
 *
 * Each font attached to the layer contributes its own namespaced
 * implementation (e.g. `<namespace>_glyph_sample(...)`) — the binder
 * substitutes `__NS__` -> `<namespace>` when merging the font's
 * resource_set shader. This module emits the slot -> namespace switch
 * dispatcher that bridges the two.
 *
 * Used by ydraw-layer (canvas-backed) and ygrid (standalone-figure);
 * both produce the same dispatcher shape so the layer shader works
 * unchanged in either embedding.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build WGSL source for the three switch-case dispatcher functions.
 *
 * `slot_namespaces` is an array of `slot_count` C-strings, indexed by
 * slot. A NULL entry means "no font in this slot" -- its switch case
 * is skipped and the default fall-through (transparent glyph) handles
 * it at render time. `slot_count == 0` is valid and emits dispatchers
 * with only the default cases (every GLYPH prim renders transparent).
 *
 * On success *out_wgsl points at a heap-allocated, NUL-terminated WGSL
 * string and *out_size is its length excluding the NUL. Caller owns
 * the buffer and frees it with free(). */
struct yetty_ycore_void_result yetty_yrender_build_font_dispatcher_wgsl(
    const char *const *slot_namespaces, size_t slot_count,
    char **out_wgsl, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_FONT_DISPATCHER_H */
