#ifndef YETTY_YFONT_FONT_CACHE_H
#define YETTY_YFONT_FONT_CACHE_H

/*
 * yetty_yfont_cache - Per-canvas, refcounted pool of MSDF font instances.
 *
 * The cache is a proxy for MSDF font construction. It does not produce the
 * on-disk CDB — that pipeline (PDF blob -> hashed TTF on disk -> generated
 * CDB) is upstream and lives outside this module. The caller hands the
 * cache a `key` (stable identity, also used as the WGSL shader namespace)
 * and a `cdb_path`; on miss the cache calls yetty_yfont_msdf_font_create
 * and stores the resulting font under `key`. Subsequent get_font calls for
 * the same key return the same font with refcount bumped.
 *
 * Lifetime / refcount:
 *   get_font     — refcount += 1 (hit) or = 1 (miss + construct)
 *   retain       — refcount += 1
 *   release_font — refcount -= 1; at 0 the font is destroyed and the slot
 *                  becomes reusable
 *   destroy      — destroys every entry regardless of refcount
 *
 * Slots / handles:
 *   A handle is the integer slot index. Stable while the entry lives.
 *   When refcount hits 0 the slot is freed and may be reused by a later
 *   install. Old handles to a freed slot must not be reused — by the time
 *   refcount reached 0, every legitimate holder had already released.
 *
 * Per-canvas, not shared across canvases — each canvas's atlas grows
 * progressively with the glyphs it actually renders, and one canvas's
 * accumulated glyph state is dead weight for another.
 */

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_font;
struct yetty_yfont_cache;

typedef uint32_t yetty_yfont_cache_handle;
#define YETTY_YFONT_CACHE_HANDLE_INVALID UINT32_MAX

/* Pair returned by get_font. `font` is borrowed — valid only while the
 * caller still holds at least one ref to `handle`. */
struct yetty_yfont_cache_ref {
    struct yetty_ydraw_font *font;
    yetty_yfont_cache_handle  handle;
};

YETTY_YRESULT_DECLARE(yetty_yfont_cache_ptr, struct yetty_yfont_cache *);
YETTY_YRESULT_DECLARE(yetty_yfont_cache_ref, struct yetty_yfont_cache_ref);

/* Create a cache. `shaders_dir` is the directory path where per-font WGSL
 * shaders live (resolved by the caller from `paths/shaders` config). The
 * cache copies it for its lifetime. Taking the bare string instead of a
 * yetty_context keeps yfont GPU-less — yetty_context embeds a WGPU app
 * context and would drag <webgpu/webgpu.h> into every yfont consumer. */
struct yetty_yfont_cache_ptr_result yetty_yfont_cache_create(const char *shaders_dir);

/* Destroys every entry regardless of refcount. */
void yetty_yfont_cache_destroy(struct yetty_yfont_cache *cache);

/* Hit:  retain + return ref.
 * Miss: build font via yetty_yfont_msdf_font_create(cdb_path,
 *                                                   <shaders_dir>/msdf-font.wgsl,
 *                                                   key),
 *       store under `key`, refcount = 1, return ref.
 * Construct failure surfaces as an error result. */
struct yetty_yfont_cache_ref_result yetty_yfont_cache_get_font(struct yetty_yfont_cache *cache,
                                                               const char *key,
                                                               const char *cdb_path);

/* Bump refcount. NULL/INVALID-safe. */
void yetty_yfont_cache_retain(struct yetty_yfont_cache *cache, yetty_yfont_cache_handle handle);

/* Drop one refcount. At 0, destroys font and frees the slot. NULL/INVALID-safe. */
void yetty_yfont_cache_release_font(struct yetty_yfont_cache *cache,
                                    yetty_yfont_cache_handle handle);

/* Read access. Returns NULL if handle is INVALID, out of range, or freed. */
struct yetty_ydraw_font *yetty_yfont_cache_font_at(const struct yetty_yfont_cache *cache,
                                                    yetty_yfont_cache_handle handle);

/* Total slots ever allocated (including freed ones). For binder / debug
 * iteration; combine with font_at() which returns NULL on freed slots. */
uint32_t yetty_yfont_cache_count(const struct yetty_yfont_cache *cache);

/* Monotonic counter that bumps on every slot allocation AND every slot
 * release. Consumers that key cached state (shader dispatchers, gpu
 * resource sets) on the cache's slot membership should compare this
 * against the value seen at last rebuild — `count` alone is a high
 * watermark and stays unchanged when a slot drops, hiding the change. */
uint32_t yetty_yfont_cache_generation(const struct yetty_yfont_cache *cache);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFONT_FONT_CACHE_H */
