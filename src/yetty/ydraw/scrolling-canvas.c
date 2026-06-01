/* scrolling-canvas.c — terminal text/ydraw canvas with scrollback.
 *
 * Self-contained implementation. Owns:
 *   - font cache, flyweight registry, figure factory, default font,
 *     font dirs/family/render-method, MSDF generator
 *   - cursor + rolling-row state, viewport override
 *   - GPU staging buffers (grid + prim)
 *   - scroll/cursor callbacks
 *   - per-envelope streaming state (iter, font map, attach list)
 *   - the opaque scrolling-grid (line storage + scrollbuffer eviction)
 *
 * Fills in the polymorphic vtable from <yetty/ydraw/canvas.h>. Shares
 * NOTHING with scene-canvas — the vtable is the only contact point.
 */

#include "scrolling-grid.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/ydraw/canvas.h>
#include <yetty/ydraw/flyweight.h>
#include <yetty/ydraw/scrolling-canvas.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>
#if YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#if YETTY_HAS_YVIDEO
#include <yetty/yvideo/yvideo-gen.h>
#endif
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ymsdf/generator.h>
#endif

/*===========================================================================
 * Constants
 *===========================================================================*/

/* Glyph drawable type — not in ysdf types.gen.h since it's not SDF. */
#define YETTY_YSDF_GLYPH 200
/* Glyph prim layout: type, z_order, x, y, font_size,
 * packed(glyph_idx | font_id), color */
#define YDRAW_GLYPH_WORDS 7

#define YDRAW_STAGING_INITIAL_CAPACITY 4096

/*===========================================================================
 * Per-envelope font map
 *===========================================================================*/

struct font_map_entry {
    char hex[17];
    bool declared;
    bool resolved;
    struct yetty_yfont_font *font;
    yetty_yfont_cache_handle handle;
};

struct font_map {
    struct font_map_entry *entries;
    uint32_t capacity;
};

static void font_map_init(struct font_map *m)
{
    m->entries = NULL;
    m->capacity = 0;
}

static struct yetty_ycore_void_result font_map_grow(struct font_map *m, uint32_t want)
{
    if (want <= m->capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = m->capacity ? m->capacity * 2 : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    struct font_map_entry *grown = realloc(m->entries, new_cap * sizeof(*grown));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "font_map_grow: realloc failed");
    }
    m->entries = grown;
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        m->entries[i].hex[0] = '\0';
        m->entries[i].declared = false;
        m->entries[i].resolved = false;
        m->entries[i].font = NULL;
        m->entries[i].handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    m->capacity = new_cap;
    return YETTY_OK_VOID();
}

static void font_map_release_all(struct font_map *m, struct yetty_yfont_cache *cache)
{
    if (!m->entries) {
        return;
    }
    for (uint32_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].resolved) {
            yetty_yfont_cache_release_font(cache, m->entries[i].handle);
            m->entries[i].resolved = false;
            m->entries[i].handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
            m->entries[i].font = NULL;
        }
    }
}

/*===========================================================================
 * Per-envelope attach list (one entry per unique font handle, highest row).
 *===========================================================================*/

struct attach_entry {
    yetty_yfont_cache_handle handle;
    uint32_t max_row;
};

struct attach_list {
    struct attach_entry *entries;
    uint32_t count;
    uint32_t capacity;
};

static void attach_list_init(struct attach_list *l)
{
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void attach_list_free(struct attach_list *l)
{
    free(l->entries);
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

static struct yetty_ycore_void_result attach_list_note(struct attach_list *l,
                                                       yetty_yfont_cache_handle handle,
                                                       uint32_t row)
{
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID) {
        return YETTY_OK_VOID();
    }
    for (uint32_t i = 0; i < l->count; i++) {
        if (l->entries[i].handle == handle) {
            if (row > l->entries[i].max_row) {
                l->entries[i].max_row = row;
            }
            return YETTY_OK_VOID();
        }
    }
    if (l->count >= l->capacity) {
        uint32_t nc = l->capacity ? l->capacity * 2 : 8;
        struct attach_entry *ne = realloc(l->entries, nc * sizeof(*ne));
        if (!ne) {
            return YETTY_ERR(yetty_ycore_void, "attach_list_note: realloc failed");
        }
        l->entries = ne;
        l->capacity = nc;
    }
    l->entries[l->count++] = (struct attach_entry){.handle = handle, .max_row = row};
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Variant struct
 *===========================================================================*/

struct scrolling_canvas {
    /* Polymorphic handle MUST be first — vtable methods downcast at offset 0. */
    struct yetty_ydraw_canvas base;

    /* Dimensions + dirty bit. */
    struct yetty_ycore_pixel_size cell_size;
    struct yetty_ycore_grid_size grid_size;
    bool dirty;

    /* Packed GPU staging buffers. */
    uint32_t *grid_staging;
    uint32_t grid_staging_count;
    uint32_t grid_staging_capacity;
    uint32_t *drawable_staging;
    uint32_t drawable_staging_count;
    uint32_t drawable_staging_capacity;

    /* Default font (slot 0). */
    struct yetty_yfont_font *default_font;
    yetty_yfont_cache_handle default_handle;

    /* 0 = MSDF, 1 = raster. */
    int font_render_method;
    float raster_base_size;

    /* Resource dirs + family from config. */
    char shaders_dir[512];
    char fonts_dir[512];
    char font_family[128];

    /* Registries + factory. */
    struct yetty_ydraw_flyweight_registry *flyweight_registry;
    struct yetty_ydraw_raw_figure_factory *figure_factory;
    struct yetty_ymsdf_generator *msdf_generator; /* borrowed */
    struct yetty_yfont_cache *font_cache;

    /* Cursor (screen-row offset from rolling_row_0). */
    uint16_t cursor_col;
    uint16_t cursor_row;

    /* Rolling row of visible line 0 (advances on scroll). Always tracks
     * the LIVE viewport top — never reset by scrollback view. */
    uint32_t rolling_row_0;

    /* Scrollback view override. While active, rebuild_grid / shader
     * uniform use view_top_override instead of rolling_row_0. */
    bool view_top_override_active;
    uint32_t view_top_override;

    /* Opaque grid (line storage + scrollbuffer eviction). */
    struct yetty_ydraw_scrolling_grid *grid;

    /* Scroll / cursor callbacks. */
    yetty_ydraw_canvas_scroll_callback scroll_callback;
    void *scroll_callback_user_data;
    yetty_ydraw_canvas_cursor_callback cursor_set_callback;
    void *cursor_set_callback_user_data;
};

static const struct yetty_ydraw_canvas_ops scrolling_canvas_ops;

static inline struct scrolling_canvas *as_scrolling(struct yetty_ydraw_canvas *base)
{
    return (struct scrolling_canvas *)base;
}

static inline const struct scrolling_canvas *as_scrolling_const(
    const struct yetty_ydraw_canvas *base)
{
    return (const struct scrolling_canvas *)base;
}

/*===========================================================================
 * Staging growth helpers
 *===========================================================================*/

static struct yetty_ycore_void_result ensure_grid_staging(struct scrolling_canvas *c,
                                                          uint32_t min_size)
{
    if (min_size <= c->grid_staging_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap =
        c->grid_staging_capacity == 0 ? YDRAW_STAGING_INITIAL_CAPACITY : c->grid_staging_capacity;
    while (new_cap < min_size) {
        new_cap *= 2;
    }
    uint32_t *grown = realloc(c->grid_staging, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: grid_staging realloc");
    }
    c->grid_staging = grown;
    c->grid_staging_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_drawable_staging(struct scrolling_canvas *c,
                                                              uint32_t min_size)
{
    if (min_size <= c->drawable_staging_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = c->drawable_staging_capacity == 0 ? YDRAW_STAGING_INITIAL_CAPACITY
                                                         : c->drawable_staging_capacity;
    while (new_cap < min_size) {
        new_cap *= 2;
    }
    uint32_t *grown = realloc(c->drawable_staging, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: drawable_staging realloc");
    }
    c->drawable_staging = grown;
    c->drawable_staging_capacity = new_cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Font-blob utilities
 *===========================================================================*/

static int blob_is_raster(const char *name, int canvas_method)
{
    if (name) {
        size_t n = strlen(name);
        if (n >= 4) {
            const char *ext = name + n - 4;
            if (strcasecmp(ext, ".ttf") == 0 || strcasecmp(ext, ".otf") == 0) {
                return 1;
            }
            if (strcasecmp(ext, ".cdb") == 0) {
                return 0;
            }
        }
    }
    return canvas_method;
}

static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void sanitize_identifier(char *dst, size_t dst_cap, const char *src)
{
    size_t ni = 0;
    for (const char *s = src; *s && ni + 1 < dst_cap; s++) {
        char c = *s;
        dst[ni++] = (c == '-' || c == ' ' || c == '.') ? '_' : c;
    }
    dst[ni] = '\0';
}

static struct yetty_yfont_cache_ref_result get_default_font_ref(struct scrolling_canvas *c)
{
    if (c->font_render_method == 1) {
        return YETTY_ERR(yetty_yfont_cache_ref,
                         "raster default font not yet wired through font cache");
    }
    char cdb_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", c->fonts_dir,
             c->font_family);
    char ns[128];
    sanitize_identifier(ns, sizeof(ns), c->font_family);
    ydebug("scrolling-canvas: default msdf font cdb='%s' key='%s'", cdb_path, ns);
    return yetty_yfont_cache_get_font(c->font_cache, ns, cdb_path);
}

static struct yetty_ycore_void_result ensure_blob_font_cdb(struct scrolling_canvas *c,
                                                           const uint8_t *ttf, uint32_t ttf_len,
                                                           const char *hint_name, char out_hex[17])
{
    if (!ttf || ttf_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "blob is empty");
    }
    if (blob_is_raster(hint_name, c->font_render_method)) {
        return YETTY_ERR(yetty_ycore_void, "raster blob fonts not yet wired through font cache");
    }
    uint64_t h = fnv1a64(ttf, ttf_len);
    snprintf(out_hex, 17, "%016llx", (unsigned long long)h);

    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_ycore_void, "no cache dir");
    }
    char fonts_dir[768];
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/ydraw-fonts", cache_dir);
    char ttf_path[1024], cdb_path[1024];
    snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, out_hex);
    snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, out_hex);

    if (yetty_yplatform_file_exists(cdb_path)) {
        return YETTY_OK_VOID();
    }

    yetty_yplatform_mkdir_p(fonts_dir);
    if (!yetty_yplatform_file_exists(ttf_path)) {
        FILE *f = fopen(ttf_path, "wb");
        if (!f) {
            return YETTY_ERR(yetty_ycore_void, "open ttf cache for write");
        }
        size_t written = fwrite(ttf, 1, ttf_len, f);
        if (fclose(f) != 0) {
            return YETTY_ERR(yetty_ycore_void, "fclose ttf cache failed");
        }
        if (written != ttf_len) {
            return YETTY_ERR(yetty_ycore_void, "short write ttf cache");
        }
        ydebug("scrolling-canvas: cached TTF '%s' (%u bytes) hint='%s'", ttf_path, ttf_len,
               hint_name ? hint_name : "");
    }

#if YETTY_HAS_YMSDF_GEN
    if (!c->msdf_generator) {
        yerror("scrolling-canvas: CDB '%s' missing and no MSDF generator", cdb_path);
        return YETTY_ERR(yetty_ycore_void, "no MSDF generator available");
    }
    struct yetty_ymsdf_generator_config gen = {0};
    gen.ttf_path = ttf_path;
    gen.cdb_path = cdb_path;
    gen.font_size = 32.0f;
    gen.pixel_range = 4.0f;
    struct yetty_ycore_void_result gr = c->msdf_generator->ops->generate(c->msdf_generator, &gen);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "msdf generator failed");
    ydebug("scrolling-canvas: generated CDB '%s' via %s generator", cdb_path,
           c->msdf_generator->ops->name(c->msdf_generator));
    return YETTY_OK_VOID();
#else
    return YETTY_ERR(yetty_ycore_void, "ymsdf-gen disabled in this build");
#endif
}

static struct yetty_yfont_cache_ref_result resolve_blob_font_handle(struct scrolling_canvas *c,
                                                                    const char *hex)
{
    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_yfont_cache_ref, "no cache dir");
    }
    char cdb_path[1024];
    snprintf(cdb_path, sizeof(cdb_path), "%s/ydraw-fonts/pdf_%s.cdb", cache_dir, hex);
    return yetty_yfont_cache_get_font(c->font_cache, hex, cdb_path);
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

static void scrolling_canvas_destroy_internals(struct scrolling_canvas *c)
{
    /* No per-envelope state to tear down — process_input runs on a coro
     * stack, all per-envelope state is local. If a coro is in-flight at
     * Wire Statemachine destroy time, the Wire Statemachine drops the coro handle (process is exiting,
     * OS reclaims the stack). */
    if (c->grid) {
        struct yetty_ycore_void_result gr =
            yetty_ydraw_scrolling_grid_destroy(c->grid, c->font_cache);
        if (YETTY_IS_ERR(gr)) {
            yerror("scrolling-canvas: grid destroy: %s", gr.error.msg);
            yetty_ycore_error_destroy(gr.error);
        }
        c->grid = NULL;
    }
    if (c->font_cache && c->default_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) {
        yetty_yfont_cache_release_font(c->font_cache, c->default_handle);
        c->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    if (c->font_cache) {
        yetty_yfont_cache_destroy(c->font_cache);
        c->font_cache = NULL;
    }
    if (c->figure_factory) {
        yetty_ydraw_raw_figure_factory_destroy(c->figure_factory);
        c->figure_factory = NULL;
    }
    if (c->flyweight_registry) {
        yetty_ydraw_flyweight_registry_destroy(c->flyweight_registry);
        c->flyweight_registry = NULL;
    }
    free(c->grid_staging);
    free(c->drawable_staging);
}

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_scrolling_canvas_create(
    const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "context is NULL");
    }

    struct scrolling_canvas *c = calloc(1, sizeof(*c));
    if (!c) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scrolling-canvas alloc failed");
    }
    c->base.ops = &scrolling_canvas_ops;
    c->dirty = true;
    c->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    c->raster_base_size = 32.0f;

    /* Flyweight registry. */
    struct yetty_ydraw_flyweight_registry_ptr_result flyweight_res = yetty_ydraw_flyweight_create();
    if (YETTY_IS_ERR(flyweight_res)) {
        free(c);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "flyweight create", flyweight_res);
    }
    c->flyweight_registry = flyweight_res.value;

    /* figure factory + built-in registrations. */
    struct yetty_ydraw_raw_figure_factory_ptr_result factory_res =
        yetty_ydraw_raw_figure_factory_create(context->runtime->gpu.device,
                                              context->runtime->gpu.queue,
                                              context->runtime->gpu.surface_format,
                                              context->runtime->gpu.allocator, context->event_loop);
    if (YETTY_IS_ERR(factory_res)) {
        scrolling_canvas_destroy_internals(c);
        free(c);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "factory create", factory_res);
    }
    c->figure_factory = factory_res.value;

    {
        struct yetty_ydraw_concrete_factory *f = yetty_yplot_factory_create();
        if (!f) {
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yplot factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_raw_figure_factory_register(c->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_yplot_factory_destroy(f);
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yplot register", rr);
        }
    }
    {
        struct yetty_ydraw_concrete_factory *f = yetty_yimage_factory_create();
        if (!f) {
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yimage factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_raw_figure_factory_register(c->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_yimage_factory_destroy(f);
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yimage register", rr);
        }
    }
#if YETTY_HAS_YMESH
    {
        struct yetty_ydraw_concrete_factory *f = yetty_ymesh_factory_create();
        if (!f) {
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "ymesh factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_raw_figure_factory_register(c->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_ymesh_factory_destroy(f);
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "ymesh register", rr);
        }
    }
#endif
#if YETTY_HAS_YVIDEO
    {
        struct yetty_ydraw_concrete_factory *f = yetty_yvideo_factory_create();
        if (!f) {
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yvideo factory create");
        }
        struct yetty_ycore_void_result rr =
            yetty_ydraw_raw_figure_factory_register(c->figure_factory, f);
        if (YETTY_IS_ERR(rr)) {
            yetty_yvideo_factory_destroy(f);
            scrolling_canvas_destroy_internals(c);
            free(c);
            return YETTY_ERR(yetty_ydraw_canvas_ptr, "yvideo register", rr);
        }
    }
#endif

    /* Dirs + font kind from config. */
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = config->ops->font_family(config);
    if (!font_family || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }
    const char *render_method = config->ops->get_string(config, "ydraw/font/render-method", "msdf");
    strncpy(c->shaders_dir, shaders_dir, sizeof(c->shaders_dir) - 1);
    strncpy(c->fonts_dir, fonts_dir, sizeof(c->fonts_dir) - 1);
    strncpy(c->font_family, font_family, sizeof(c->font_family) - 1);
    c->font_render_method = (strcmp(render_method, "raster") == 0) ? 1 : 0;
    c->msdf_generator = context->runtime->gpu.msdf_generator;
    ydebug("scrolling-canvas: font render_method='%s'", render_method);

    /* Per-canvas font cache. */
    struct yetty_yfont_cache_ptr_result cache_res = yetty_yfont_cache_create(shaders_dir);
    if (YETTY_IS_ERR(cache_res)) {
        scrolling_canvas_destroy_internals(c);
        free(c);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "font cache create", cache_res);
    }
    c->font_cache = cache_res.value;

    /* Default font installs at slot 0. */
    struct yetty_yfont_cache_ref_result def_res = get_default_font_ref(c);
    if (YETTY_IS_ERR(def_res)) {
        scrolling_canvas_destroy_internals(c);
        free(c);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "default font create", def_res);
    }
    c->default_font = def_res.value.font;
    c->default_handle = def_res.value.handle;
    ydebug("scrolling-canvas: default font installed at handle=%u", c->default_handle);

    /* Opaque grid. */
    struct yetty_ydraw_scrolling_grid_ptr_result grid_res = yetty_ydraw_scrolling_grid_create();
    if (YETTY_IS_ERR(grid_res)) {
        scrolling_canvas_destroy_internals(c);
        free(c);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "scrolling-grid create", grid_res);
    }
    c->grid = grid_res.value;

    return YETTY_OK(yetty_ydraw_canvas_ptr, &c->base);
}

static struct yetty_ycore_void_result scrolling_destroy(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_OK_VOID();
    }
    struct scrolling_canvas *c = as_scrolling(base);
    scrolling_canvas_destroy_internals(c);
    free(c);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Config / accessors
 *===========================================================================*/

static struct yetty_ycore_void_result scrolling_set_cell_size(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_pixel_size pixel_size)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    if (pixel_size.width <= 0.0f || pixel_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "cell size must be > 0");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->cell_size = pixel_size;
    c->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrolling_set_grid_size(struct yetty_ydraw_canvas *base,
                                                              struct yetty_ycore_grid_size size)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->grid_size = size;
    c->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_pixel_size scrolling_get_cell_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->cell_size : (struct yetty_ycore_pixel_size){0, 0};
}

static struct yetty_ycore_grid_size scrolling_get_grid_size(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->grid_size : (struct yetty_ycore_grid_size){0, 0};
}

static struct yetty_ycore_void_result scrolling_mark_dirty(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    as_scrolling(base)->dirty = true;
    return YETTY_OK_VOID();
}

static bool scrolling_is_dirty(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->dirty : false;
}

/*===========================================================================
 * Cursor / scroll
 *===========================================================================*/

static uint32_t effective_view_top(const struct scrolling_canvas *c)
{
    return c->view_top_override_active ? c->view_top_override : c->rolling_row_0;
}

static struct yetty_ycore_void_result scrolling_set_cursor_pos(
    struct yetty_ydraw_canvas *base, struct yetty_ycore_grid_cursor_pos pos)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->cursor_col = pos.cols;
    c->cursor_row = pos.rows;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrolling_scroll_lines(struct yetty_ydraw_canvas *base,
                                                             uint16_t num_lines)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    if (num_lines == 0) {
        return YETTY_OK_VOID();
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->rolling_row_0 += num_lines;
    if (c->cursor_row >= num_lines) {
        c->cursor_row -= num_lines;
    } else {
        c->cursor_row = 0;
    }
    c->dirty = true;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrolling_set_view_top(struct yetty_ydraw_canvas *base,
                                                             bool active, uint32_t view_top)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->view_top_override_active = active;
    c->view_top_override = view_top;
    c->dirty = true;
    return YETTY_OK_VOID();
}

static uint32_t scrolling_rolling_row_0(struct yetty_ydraw_canvas *base)
{
    return base ? effective_view_top(as_scrolling_const(base)) : 0;
}

static uint32_t scrolling_live_rolling_row_0(struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->rolling_row_0 : 0;
}

static struct yetty_ycore_void_result scrolling_set_scroll_callback(
    struct yetty_ydraw_canvas *base, yetty_ydraw_canvas_scroll_callback callback, void *userdata)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->scroll_callback = callback;
    c->scroll_callback_user_data = userdata;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrolling_set_cursor_callback(
    struct yetty_ydraw_canvas *base, yetty_ydraw_canvas_cursor_callback callback, void *userdata)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    c->cursor_set_callback = callback;
    c->cursor_set_callback_user_data = userdata;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Add a single SDF / complex drawable
 *===========================================================================*/

static struct uint32_result add_drawable_internal(
    struct scrolling_canvas *c, const struct yetty_ydraw_drawable_flyweight *flyweight)
{
    if (!flyweight || !flyweight->data || !flyweight->ops) {
        return YETTY_ERR(uint32, "invalid flyweight");
    }
    if (c->cell_size.height <= 0.0f) {
        return YETTY_ERR(uint32, "cell_height <= 0");
    }
    if (c->cell_size.width <= 0.0f) {
        return YETTY_ERR(uint32, "cell_width <= 0");
    }
    if (!flyweight->ops->aabb || !flyweight->ops->size) {
        return YETTY_ERR(uint32, "handler missing ops");
    }

    uint32_t drawable_type = flyweight->data[0];

    struct rectangle_result aabb_res = flyweight->ops->aabb(flyweight->data);
    YETTY_RETURN_IF_ERR(uint32, aabb_res, "add_drawable: aabb");
    struct yetty_ycore_rectangle aabb = aabb_res.value;

    struct yetty_ycore_size_result size_res = flyweight->ops->size(flyweight->data);
    YETTY_RETURN_IF_ERR(uint32, size_res, "add_drawable: size");
    uint32_t word_count = size_res.value / sizeof(uint32_t);

    if (aabb.min.y > aabb.max.y) {
        float tmp = aabb.min.y;
        aabb.min.y = aabb.max.y;
        aabb.max.y = tmp;
    }
    if (aabb.min.x > aabb.max.x) {
        float tmp = aabb.min.x;
        aabb.min.x = aabb.max.x;
        aabb.max.x = tmp;
    }

    uint32_t cursor_canvas_line = c->rolling_row_0 + c->cursor_row;
    float min_valid_y = -(float)cursor_canvas_line * c->cell_size.height;
    if (aabb.max.y < min_valid_y) {
        aabb.max.y = min_valid_y;
    }
    if (aabb.min.y < min_valid_y) {
        aabb.min.y = min_valid_y;
    }
    /* Grid x starts at column 0; clamp negative AABB x so the
     * float->uint32_t cast below is well-defined. */
    if (aabb.max.x < 0.0f) {
        aabb.max.x = 0.0f;
    }
    if (aabb.min.x < 0.0f) {
        aabb.min.x = 0.0f;
    }

    float max_rows = aabb.max.y / c->cell_size.height;
    uint32_t drawable_max_in_rows = (max_rows > 0.0f) ? ((uint32_t)ceilf(max_rows) - 1u) : 0u;
    uint32_t drawable_grid_line = cursor_canvas_line + drawable_max_in_rows;
    uint32_t drawable_rolling_row = cursor_canvas_line;

    struct yetty_ycore_void_result el =
        yetty_ydraw_scrolling_grid_ensure_lines(c->grid, drawable_grid_line + 1);
    YETTY_RETURN_IF_ERR(uint32, el, "add_drawable: ensure_lines");

    struct yetty_ycore_void_result dl =
        yetty_ydraw_scrolling_grid_dirty_line(c->grid, drawable_grid_line);
    YETTY_RETURN_IF_ERR(uint32, dl, "add_drawable: dirty_line");

    struct uint32_result push_res =
        yetty_ydraw_scrolling_grid_push_prim(c->grid, drawable_grid_line, drawable_rolling_row,
                                             (const float *)flyweight->data, word_count);
    YETTY_RETURN_IF_ERR(uint32, push_res, "add_drawable: push_prim");
    uint32_t drawable_index = push_res.value;

    uint32_t drawable_col_min = (uint32_t)(aabb.min.x / c->cell_size.width);
    uint32_t drawable_col_max = (uint32_t)(aabb.max.x / c->cell_size.width);

    int32_t row_min_rel = (int32_t)floorf(aabb.min.y / c->cell_size.height);
    int32_t row_max_rel = (int32_t)ceilf(aabb.max.y / c->cell_size.height) - 1;
    if (row_min_rel < 0) {
        row_min_rel = 0;
    }
    if (row_max_rel < 0) {
        row_max_rel = 0;
    }
    if (row_max_rel < row_min_rel) {
        row_max_rel = row_min_rel;
    }

    uint32_t drawable_row_min = cursor_canvas_line + (uint32_t)row_min_rel;
    uint32_t drawable_row_max = cursor_canvas_line + (uint32_t)row_max_rel;
    if (drawable_row_min > drawable_row_max) {
        return YETTY_ERR(uint32, "AABB row min > max after clamp");
    }
    if (drawable_col_min > drawable_col_max) {
        return YETTY_ERR(uint32, "AABB col min > max");
    }
    if (c->grid_size.cols == 0) {
        return YETTY_ERR(uint32, "grid_size.cols is 0");
    }
    if (drawable_col_max >= c->grid_size.cols) {
        drawable_col_max = c->grid_size.cols - 1;
    }

    for (uint32_t row = drawable_row_min; row <= drawable_row_max; row++) {
        struct yetty_ycore_void_result ec =
            yetty_ydraw_scrolling_grid_ensure_cells(c->grid, row, drawable_col_max + 1);
        YETTY_RETURN_IF_ERR(uint32, ec, "add_drawable: ensure_cells");
        struct yetty_ycore_void_result dr = yetty_ydraw_scrolling_grid_dirty_line(c->grid, row);
        YETTY_RETURN_IF_ERR(uint32, dr, "add_drawable: dirty_line (cell row)");
        uint16_t lines_ahead = (uint16_t)(drawable_grid_line - row);
        for (uint32_t col = drawable_col_min; col <= drawable_col_max; col++) {
            struct yetty_ycore_void_result rp = yetty_ydraw_scrolling_grid_push_ref(
                c->grid, row, col, lines_ahead, (uint16_t)drawable_index);
            YETTY_RETURN_IF_ERR(uint32, rp, "add_drawable: push_ref");
        }
    }

    if (yetty_ydraw_is_figure(drawable_type)) {
        struct yetty_ydraw_figure_ptr_result inst_res =
            yetty_ydraw_raw_figure_factory_create_instance(c->figure_factory, flyweight->data,
                                                           word_count * sizeof(uint32_t),
                                                           drawable_rolling_row);
        YETTY_RETURN_IF_ERR(uint32, inst_res, "add_drawable: create_instance");
        /* Mark fresh — the FIRST render must paint this figure even
         * though no listener has fired yet. ydraw_layer_render's
         * figure loop gates on inst->dirty || force. */
        inst_res.value->dirty = 1;
        struct yetty_ycore_void_result fr =
            yetty_ydraw_scrolling_grid_push_figure(c->grid, drawable_grid_line, inst_res.value);
        if (YETTY_IS_ERR(fr)) {
            yetty_ydraw_figure_destroy(inst_res.value);
            return YETTY_ERR(uint32, "add_drawable: push_figure", fr);
        }
    }

    c->dirty = true;
    return YETTY_OK(uint32, drawable_grid_line);
}

/*===========================================================================
 * Expand a TEXT_SPAN view into per-glyph SDF drawables
 *===========================================================================*/

static struct uint32_result expand_text_span_to_glyphs(
    struct scrolling_canvas *c, const struct yetty_ydraw_text_span_drawable_view *ts,
    struct yetty_yfont_font *font, yetty_yfont_cache_handle font_handle)
{
    static uint32_t glyph_z_order = 0;
    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0) ? ts->font_size / base_size : 1.0f;
    float cursor_x = ts->x;
    uint32_t glyph_max_row = 0;

    const uint8_t *ptr = (const uint8_t *)ts->text;
    const uint8_t *end = ptr + ts->text_len;

    while (ptr < end) {
        uint32_t cp = 0;
        if ((*ptr & 0x80) == 0) {
            cp = *ptr++;
        } else if ((*ptr & 0xE0) == 0xC0) {
            cp = (*ptr++ & 0x1F) << 6;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else if ((*ptr & 0xF0) == 0xE0) {
            cp = (*ptr++ & 0x0F) << 12;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 6;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else if ((*ptr & 0xF8) == 0xF0) {
            cp = (*ptr++ & 0x07) << 18;
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 12;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F) << 6;
            }
            if (ptr < end) {
                cp |= (*ptr++ & 0x3F);
            }
        } else {
            ptr++;
            continue;
        }

        struct uint32_result gi_res = font->ops->get_glyph_index(font, cp);
        if (YETTY_IS_ERR(gi_res)) {
            /* Missing glyph — PDF subset fonts routinely omit codepoints
             * the producer's ToUnicode CMap nonetheless decodes to (e.g.
             * a hyphen byte routed through a symbol font's Differences
             * array). Standard PDF behavior is to advance by a default
             * em-fraction and render nothing (.notdef equivalent), not
             * abort the document. */
            ydebug("expand_text_span: skip missing glyph cp=U+%04X", cp);
            yetty_ycore_error_destroy(gi_res.error);
            cursor_x += (ts->font_size * 0.25f) + ts->char_spacing;
            if (cp == 0x20) {
                cursor_x += ts->word_spacing;
            }
            continue;
        }
        uint32_t glyph_index = gi_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res = font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(uint32, rs_res, "expand_text_span: get_gpu_resource_set");
        const struct yetty_ydraw_gpu_resource_set *rs = rs_res.value;
        if (rs->buffer_count == 0 || !rs->buffers[0].data) {
            return YETTY_ERR(uint32,
                             "expand_text_span: font resource set has no glyph metadata buffer");
        }
        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs->buffers[0].size / (6 * sizeof(float)));
        if (glyph_index >= meta_count) {
            return YETTY_ERR(uint32, "expand_text_span: glyph_index out of metadata range");
        }
        const float *gm = meta + glyph_index * 6;
        float size_x = gm[0], size_y = gm[1];
        float bearing_x = gm[2], bearing_y = gm[3];
        float advance = gm[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale;
            continue;
        }

        float gx = cursor_x + bearing_x * scale;
        float gy = ts->y - bearing_y * scale;
        float gw = size_x * scale;
        float gh = size_y * scale;

        uint32_t slot = (font_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) ? font_handle : 0u;
        float glyph_data[YDRAW_GLYPH_WORDS];
        uint32_t tmp;
        tmp = YETTY_YSDF_GLYPH;
        memcpy(&glyph_data[0], &tmp, sizeof(float));
        tmp = glyph_z_order++;
        memcpy(&glyph_data[1], &tmp, sizeof(float));
        glyph_data[2] = gx;
        glyph_data[3] = gy;
        glyph_data[4] = ts->font_size;
        uint32_t packed_gf = (glyph_index & 0xFFFF) | (((uint32_t)(slot + 1) & 0xFFFF) << 16);
        memcpy(&glyph_data[5], &packed_gf, sizeof(float));
        memcpy(&glyph_data[6], &ts->color, sizeof(float));

        uint32_t cursor_canvas_line = c->rolling_row_0 + c->cursor_row;
        float abs_y = gy + (float)cursor_canvas_line * c->cell_size.height;
        float abs_y_max = abs_y + gh;
        float gymax_rows = abs_y_max / c->cell_size.height;
        uint32_t glyph_row_max = (gymax_rows > 0.0f) ? ((uint32_t)ceilf(gymax_rows) - 1u) : 0u;

        struct yetty_ycore_void_result el =
            yetty_ydraw_scrolling_grid_ensure_lines(c->grid, glyph_row_max + 1);
        YETTY_RETURN_IF_ERR(uint32, el, "expand_text_span: ensure_lines");

        uint32_t rolling_row = cursor_canvas_line;

        struct yetty_ycore_void_result dl =
            yetty_ydraw_scrolling_grid_dirty_line(c->grid, glyph_row_max);
        YETTY_RETURN_IF_ERR(uint32, dl, "expand_text_span: dirty_line");
        struct uint32_result push_res = yetty_ydraw_scrolling_grid_push_prim(
            c->grid, glyph_row_max, rolling_row, glyph_data, YDRAW_GLYPH_WORDS);
        YETTY_RETURN_IF_ERR(uint32, push_res, "expand_text_span: push_prim");
        uint32_t drawable_idx = push_res.value;

        uint32_t col_min = (c->cell_size.width > 0) ? (uint32_t)(gx / c->cell_size.width) : 0;
        uint32_t col_max =
            (c->cell_size.width > 0) ? (uint32_t)((gx + gw) / c->cell_size.width) : 0;
        uint32_t row_min = (uint32_t)(abs_y / c->cell_size.height);

        if (col_max >= c->grid_size.cols && c->grid_size.cols > 0) {
            col_max = c->grid_size.cols - 1;
        }

        for (uint32_t row = row_min; row <= glyph_row_max; row++) {
            struct yetty_ycore_void_result ec =
                yetty_ydraw_scrolling_grid_ensure_cells(c->grid, row, col_max + 1);
            YETTY_RETURN_IF_ERR(uint32, ec, "expand_text_span: ensure_cells");
            struct yetty_ycore_void_result dr = yetty_ydraw_scrolling_grid_dirty_line(c->grid, row);
            YETTY_RETURN_IF_ERR(uint32, dr, "expand_text_span: dirty_line (cell row)");
            uint16_t lines_ahead = (uint16_t)(glyph_row_max - row);
            for (uint32_t col = col_min; col <= col_max; col++) {
                struct yetty_ycore_void_result rp = yetty_ydraw_scrolling_grid_push_ref(
                    c->grid, row, col, lines_ahead, (uint16_t)drawable_idx);
                YETTY_RETURN_IF_ERR(uint32, rp, "expand_text_span: push_ref");
            }
        }

        if (glyph_row_max > glyph_max_row) {
            glyph_max_row = glyph_row_max;
        }

        cursor_x += advance * scale + ts->char_spacing;
        if (cp == 0x20) {
            cursor_x += ts->word_spacing;
        }
    }

    return YETTY_OK(uint32, glyph_max_row);
}

/*===========================================================================
 * Process input — streaming OSC ingestion
 *===========================================================================*/

static struct yetty_ycore_void_result scrolling_clear(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    struct yetty_ycore_void_result r = yetty_ydraw_scrolling_grid_clear(c->grid, c->font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "scrolling clear: grid_clear");
    c->grid_staging_count = 0;
    c->drawable_staging_count = 0;
    c->cursor_col = 0;
    c->cursor_row = 0;
    c->rolling_row_0 = 0;
    c->view_top_override_active = false;
    c->view_top_override = 0;
    c->dirty = true;
    return YETTY_OK_VOID();
}

/* Per-envelope streaming state — lives on the process_input coro
 * stack. The iter parses on the same coro and yields via the Wire Statemachine when
 * more body bytes are needed; everything in this struct survives the
 * yield because it sits on the same stack. */
struct env_state {
    struct font_map fonts_map;
    struct attach_list attach_list;
    uint32_t initial_canvas_line;
    uint32_t max_row_seen;
};

static struct yetty_ycore_void_result dispatch_one(
    struct scrolling_canvas *c, const struct yetty_ydraw_drawable_flyweight *flyweight,
    struct env_state *env)
{
    uint32_t drawable_type = flyweight->data[0];
    if (drawable_type <= YETTY_YDRAW_CMD_END) {
        if (drawable_type == YETTY_YDRAW_CMD_ZERO) {
            ydebug("process_input: CMD_ZERO — clearing canvas + cursor (0,0)");
            struct yetty_ycore_void_result clr = scrolling_clear(&c->base);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, clr, "dispatch: clear");
            struct yetty_ycore_grid_cursor_pos pos = {.cols = 0, .rows = 0};
            struct yetty_ycore_void_result cr = scrolling_set_cursor_pos(&c->base, pos);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "dispatch: set_cursor_pos");
            env->initial_canvas_line = c->rolling_row_0 + c->cursor_row;
            env->max_row_seen = env->initial_canvas_line;
        }
        return YETTY_OK_VOID();
    }
    if (drawable_type == YETTY_YDRAW_CMD_GROUP) {
        /* Scrolling-canvas has no entity model — GROUP is a debug-log
         * no-op. The iter has already consumed the full record (header
         * + payload bytes); the payload's nested commands are simply
         * not processed. */
        uint32_t group_id = flyweight->data[1];
        ydebug("scrolling-canvas: CMD_GROUP id=%u — no-op (entities not supported)", group_id);
        return YETTY_OK_VOID();
    }
    if (drawable_type == YETTY_YDRAW_TYPE_FONT) {
        struct yetty_yfont_font_drawable_view fv;
        if (yetty_yfont_font_drawable_parse(flyweight->data, &fv) != 0 || fv.font_id < 0) {
            return YETTY_OK_VOID();
        }
        char hex[17];
        /* Hash-ref form: producers (ypdf streaming, future ymarkdown
         * streaming) ship a FONT prim with no TTF bytes when the same
         * font was already sent in a prior envelope. The on-disk MSDF
         * cache is content-addressed by FNV1a64(TTF), and that hash
         * lives in `name` as 16 hex chars. Resolve directly from the
         * name without re-running the bytes path. */
        if (fv.ttf_len == 0) {
            if (fv.name_len != 16) {
                return YETTY_ERR(yetty_ycore_void,
                                 "FONT prim: ttf_len=0 but name is not a 16-char hex hash");
            }
            for (uint32_t i = 0; i < 16; i++) {
                char ch = fv.name[i];
                bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                          (ch >= 'A' && ch <= 'F');
                if (!ok) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "FONT prim: ttf_len=0 name has non-hex char");
                }
            }
            memcpy(hex, fv.name, 16);
            hex[16] = '\0';
        } else {
            char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
            size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
            memcpy(hint, fv.name, hl);
            hint[hl] = '\0';
            struct yetty_ycore_void_result er =
                ensure_blob_font_cdb(c, fv.ttf, fv.ttf_len, hint, hex);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "dispatch: ensure_blob_font_cdb");
        }
        struct yetty_ycore_void_result gr =
            font_map_grow(&env->fonts_map, (uint32_t)fv.font_id + 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "dispatch: font_map_grow");
        memcpy(env->fonts_map.entries[fv.font_id].hex, hex, 17);
        env->fonts_map.entries[fv.font_id].declared = true;
        return YETTY_OK_VOID();
    }
    if (drawable_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        struct yetty_ydraw_text_span_drawable_view tv;
        if (yetty_ydraw_text_span_drawable_parse(flyweight->data, &tv) != 0) {
            return YETTY_OK_VOID();
        }
        struct yetty_yfont_font *font = NULL;
        yetty_yfont_cache_handle handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
        if (tv.font_id >= 0 && (uint32_t)tv.font_id < env->fonts_map.capacity) {
            struct font_map_entry *e = &env->fonts_map.entries[tv.font_id];
            if (e->resolved) {
                font = e->font;
                handle = e->handle;
            } else if (e->declared) {
                struct yetty_yfont_cache_ref_result rr = resolve_blob_font_handle(c, e->hex);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "dispatch: resolve_blob_font_handle");
                e->font = rr.value.font;
                e->handle = rr.value.handle;
                e->resolved = true;
                font = e->font;
                handle = e->handle;
            }
        }
        if (!font) {
            font = c->default_font;
            handle = c->default_handle;
        }
        if (!font) {
            return YETTY_OK_VOID();
        }

        struct uint32_result gmr = expand_text_span_to_glyphs(c, &tv, font, handle);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gmr, "dispatch: expand_text_span");
        if (gmr.value > env->max_row_seen) {
            env->max_row_seen = gmr.value;
        }
        struct yetty_ycore_void_result an = attach_list_note(&env->attach_list, handle, gmr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, an, "dispatch: attach_list_note");
        return YETTY_OK_VOID();
    }
    struct uint32_result drawable_res = add_drawable_internal(c, flyweight);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, drawable_res, "dispatch: add_drawable");
    if (drawable_res.value > env->max_row_seen) {
        env->max_row_seen = drawable_res.value;
    }
    return YETTY_OK_VOID();
}

/* End-of-envelope: attach pass, drop buffer-scoped font refs, advance
 * cursor, scroll, evict scrollback. All per-envelope state arrives by
 * value via `env` (local on the coro stack); only `c` carries
 * canvas-persistent state. */
static struct yetty_ycore_void_result env_finalize(struct scrolling_canvas *c,
                                                   struct env_state *env)
{
    /* Attach pass — one cache ref per unique font, parked on its highest row. */
    for (uint32_t i = 0; i < env->attach_list.count; i++) {
        struct yetty_ycore_void_result ar = yetty_ydraw_scrolling_grid_attach_font(
            c->grid, c->font_cache, c->default_handle, env->attach_list.entries[i].handle,
            env->attach_list.entries[i].max_row);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "env_finalize: attach_font");
    }

    /* Release buffer-scoped font refs BEFORE scroll/evict. */
    font_map_release_all(&env->fonts_map, c->font_cache);

    uint32_t target_cursor_canvas_line = env->max_row_seen;
    uint32_t viewport_bottom = c->rolling_row_0 + c->grid_size.rows - 1;

    if (target_cursor_canvas_line > viewport_bottom) {
        uint32_t lines_to_scroll = target_cursor_canvas_line - viewport_bottom;
        if (c->scroll_callback) {
            struct yetty_ycore_void_result sb =
                c->scroll_callback(c->scroll_callback_user_data, (uint16_t)lines_to_scroll);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, sb, "env_finalize: scroll_callback failed");
        }
        struct yetty_ycore_void_result sr =
            scrolling_scroll_lines(&c->base, (uint16_t)lines_to_scroll);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "env_finalize: scroll_lines");
    }

    uint32_t cursor_screen_row = (target_cursor_canvas_line >= c->rolling_row_0)
                                     ? (target_cursor_canvas_line - c->rolling_row_0)
                                     : 0;
    c->cursor_row = (uint16_t)cursor_screen_row;
    if (c->cursor_set_callback) {
        struct yetty_ycore_void_result cb =
            c->cursor_set_callback(c->cursor_set_callback_user_data, c->cursor_row);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cb, "env_finalize: cursor_set_callback");
    }

    struct yetty_ycore_void_result er = yetty_ydraw_scrolling_grid_evict_scrollback(
        c->grid, c->rolling_row_0, c->font_cache, c->grid_size.cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "env_finalize: evict_scrollback");
    c->dirty = true;
    return YETTY_OK_VOID();
}

/* Runs on a persistent coroutine spawned by the wire Wire Statemachine
 * at register time. Loops forever: each iteration handles one envelope
 * (iter_init → drain prims → iter_destroy → env_finalize). Yields from
 * inside the iter when the Wire Statemachine has no more body bytes;
 * the Wire Statemachine resumes us when fresh PTY bytes arrive. Returns
 * only on fatal error — the SM treats a return as a fatal exit. */
static struct yetty_ycore_void_result scrolling_process_input(
    struct yetty_ydraw_canvas *base, struct yetty_ywire_wire_statemachine *wire_statemachine)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    if (!wire_statemachine) {
        return YETTY_ERR(yetty_ycore_void, "wire_statemachine: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);

    for (;;) {
        struct yetty_ydraw_drawable_iterator iter = {0};
        struct env_state env = {
            .initial_canvas_line = c->rolling_row_0 + c->cursor_row,
            .max_row_seen = c->rolling_row_0 + c->cursor_row,
        };
        font_map_init(&env.fonts_map);
        attach_list_init(&env.attach_list);

        struct yetty_ycore_void_result ret = YETTY_OK_VOID();

        struct yetty_ycore_void_result ir =
            yetty_ydraw_drawable_iterator_init(&iter, wire_statemachine, c->flyweight_registry);
        if (YETTY_IS_ERR(ir)) {
            ret = YETTY_ERR(yetty_ycore_void, "process_input: iter init", ir);
            goto cleanup;
        }

        for (;;) {
            struct yetty_ydraw_drawable_iterator_status_result sr =
                yetty_ydraw_drawable_iterator_next(&iter);
            if (YETTY_IS_ERR(sr)) {
                ret = YETTY_ERR(yetty_ycore_void, "process_input: iter_next", sr);
                goto cleanup;
            }
            if (sr.value == YETTY_YDRAW_ITERATOR_EOE) {
                break;
            }
            /* Scrolling-canvas has no addressable-entity model yet, so
             * DELETE is a debug-log no-op. ADD goes through the existing
             * dispatch path unchanged. */
            if (iter.command.kind == YETTY_YDRAW_COMMAND_DELETE) {
                ydebug("scrolling-canvas: DELETE id=%u — no-op (entities not supported)",
                       iter.command.id);
                continue;
            }
            struct yetty_ycore_void_result dr = dispatch_one(c, &iter.command.flyweight, &env);
            if (YETTY_IS_ERR(dr)) {
                ret = YETTY_ERR(yetty_ycore_void, "process_input: dispatch_one", dr);
                goto cleanup;
            }
        }

        ret = env_finalize(c, &env);
        if (YETTY_IS_ERR(ret)) {
            struct yetty_ycore_void_result wrap =
                YETTY_ERR(yetty_ycore_void, "process_input: finalize", ret);
            ret = wrap;
        }

    cleanup:
        yetty_ydraw_drawable_iterator_destroy(&iter);
        attach_list_free(&env.attach_list);
        /* If we never reached env_finalize (or it failed before the
         * font_map release), drop any held cache refs now. Idempotent —
         * release_all skips already-released entries. */
        font_map_release_all(&env.fonts_map, c->font_cache);
        free(env.fonts_map.entries);

        if (YETTY_IS_ERR(ret)) {
            return ret;
        }
        /* Envelope handled cleanly. Yield so sm_coro can observe
         * terminator_seen and transition state to SCAN_RAW (or set up
         * the next envelope). Without this yield, the very next
         * iter_next would call sm_read which would return 0 (EOE) again
         * — terminator still set — and we'd spin returning EOE. The
         * yield gives sm_coro the chance to run, drop terminator_seen,
         * and only resume us when a fresh envelope body is queued for
         * this layer. */
        yetty_yplatform_coro_yield();
    }
}

/*===========================================================================
 * GPU staging
 *===========================================================================*/

static struct yetty_ycore_void_result scrolling_rebuild_grid(struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    if (!c->dirty && c->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }
    uint32_t grid_w = c->grid_size.cols;
    uint32_t grid_h = c->grid_size.rows;
    uint32_t window_top = effective_view_top(c);

    /* Widen grid_w for any line in the visible window that has more
     * cells than grid_w. */
    uint32_t max_cells = yetty_ydraw_scrolling_grid_max_cell_count_in_window(c->grid, window_top,
                                                                             window_top + grid_h);
    if (max_cells > grid_w) {
        grid_w = max_cells;
    }

    if (grid_w == 0 || grid_h == 0) {
        c->grid_staging_count = 0;
        c->dirty = false;
        return YETTY_OK_VOID();
    }

    /* Pre-grow staging to the minimum (num_cells * 4 — covers the
     * header + one ref per cell average; grid will realloc up if needed). */
    uint32_t num_cells = grid_w * grid_h;
    struct yetty_ycore_void_result e = ensure_grid_staging(c, num_cells * 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, e, "rebuild_grid: ensure_grid_staging");

    uint32_t count = 0;
    struct yetty_ycore_void_result rr = yetty_ydraw_scrolling_grid_rebuild_staging(
        c->grid, window_top, grid_h, grid_w, &c->grid_staging, &c->grid_staging_capacity, &count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild_grid: grid_rebuild_staging");
    c->grid_staging_count = count;
    c->dirty = false;
    return YETTY_OK_VOID();
}

static const uint32_t *scrolling_grid_data(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->grid_staging : NULL;
}

static uint32_t scrolling_grid_word_count(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->grid_staging_count : 0;
}

static struct yetty_ydraw_drawable_staging_result scrolling_build_drawable_staging(
    struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return YETTY_ERR(yetty_ydraw_drawable_staging, "scrolling-canvas: NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    /* Pre-grow to initial capacity; grid will realloc up if needed. */
    struct yetty_ycore_void_result e = ensure_drawable_staging(c, YDRAW_STAGING_INITIAL_CAPACITY);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_staging, e,
                        "build_drawable_staging: ensure_drawable_staging");

    uint32_t count = 0;
    uint32_t drawable_count = 0;
    struct yetty_ycore_void_result r = yetty_ydraw_scrolling_grid_build_drawable_staging(
        c->grid, &c->drawable_staging, &c->drawable_staging_capacity, &count, &drawable_count);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_staging, r, "build_drawable_staging: grid_build");
    c->drawable_staging_count = count;

    if (count == 0) {
        struct yetty_ydraw_drawable_staging empty = {.data = NULL, .word_count = 0};
        return YETTY_OK(yetty_ydraw_drawable_staging, empty);
    }
    struct yetty_ydraw_drawable_staging out = {.data = c->drawable_staging, .word_count = count};
    return YETTY_OK(yetty_ydraw_drawable_staging, out);
}

static uint32_t scrolling_drawable_gpu_size(const struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return 0;
    }
    /* Match the original semantics: total prim bytes ≈ sum of
     * (word_count + 1) for every prim across the grid times 4. */
    const struct scrolling_canvas *c = as_scrolling_const(base);
    /* Cheap estimate via the staging buffer if it's already built;
     * otherwise approximate from total prim count. */
    return c->drawable_staging_count * (uint32_t)sizeof(uint32_t);
}

/*===========================================================================
 * State / inspection
 *===========================================================================*/

static uint32_t scrolling_drawable_count(const struct yetty_ydraw_canvas *base)
{
    return base ? yetty_ydraw_scrolling_grid_total_drawable_count(as_scrolling_const(base)->grid)
                : 0;
}

/*===========================================================================
 * Fonts
 *===========================================================================*/

static uint32_t scrolling_font_count(const struct yetty_ydraw_canvas *base)
{
    return base ? yetty_yfont_cache_count(as_scrolling_const(base)->font_cache) : 0;
}

static uint32_t scrolling_font_generation(const struct yetty_ydraw_canvas *base)
{
    return base ? yetty_yfont_cache_generation(as_scrolling_const(base)->font_cache) : 0;
}

static struct yetty_yfont_font *scrolling_get_font_at(const struct yetty_ydraw_canvas *base,
                                                      uint32_t slot)
{
    if (!base) {
        return NULL;
    }
    return yetty_yfont_cache_font_at(as_scrolling_const(base)->font_cache,
                                     (yetty_yfont_cache_handle)slot);
}

static struct yetty_yfont_font *scrolling_get_default_font(const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->default_font : NULL;
}

static const struct yetty_ydraw_flyweight_registry *scrolling_get_flyweight_registry(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->flyweight_registry : NULL;
}

static struct yetty_ydraw_raw_figure_factory *scrolling_get_figure_factory(
    const struct yetty_ydraw_canvas *base)
{
    return base ? as_scrolling_const(base)->figure_factory : NULL;
}

/*===========================================================================
 * Figures (visible window only)
 *===========================================================================*/

static void visible_window(const struct scrolling_canvas *c, uint32_t *out_top, uint32_t *out_end)
{
    uint32_t top = effective_view_top(c);
    uint32_t end = top + c->grid_size.rows;
    *out_top = top;
    *out_end = end;
}

static uint32_t scrolling_figure_count(const struct yetty_ydraw_canvas *base)
{
    if (!base) {
        return 0;
    }
    const struct scrolling_canvas *c = as_scrolling_const(base);
    uint32_t top, end;
    visible_window(c, &top, &end);
    return yetty_ydraw_scrolling_grid_figure_count_in_window(c->grid, top, end);
}

static struct yetty_ydraw_figure *scrolling_get_figure(const struct yetty_ydraw_canvas *base,
                                                       uint32_t index)
{
    if (!base) {
        return NULL;
    }
    const struct scrolling_canvas *c = as_scrolling_const(base);
    uint32_t top, end;
    visible_window(c, &top, &end);
    return yetty_ydraw_scrolling_grid_figure_in_window(c->grid, top, end, index);
}

/*===========================================================================
 * Glyph iteration
 *===========================================================================*/

struct glyph_adapter_ctx {
    yetty_ydraw_canvas_glyph_visitor visitor;
    void *user;
};

YETTY_EXTERNAL_CALLBACK
static void glyph_adapter(float x, float y, uint32_t glyph_idx, int32_t font_slot, void *user)
{
    struct glyph_adapter_ctx *ctx = user;
    struct yetty_ydraw_glyph_view view = {
        .x = x,
        .y = y,
        .glyph_idx = glyph_idx,
        .font_slot = font_slot,
    };
    ctx->visitor(&view, ctx->user);
}

static struct yetty_ycore_void_result scrolling_for_each_glyph(
    struct yetty_ydraw_canvas *base, yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    if (!base) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: NULL");
    }
    if (!visitor) {
        return YETTY_ERR(yetty_ycore_void, "scrolling-canvas: visitor NULL");
    }
    struct scrolling_canvas *c = as_scrolling(base);
    struct glyph_adapter_ctx ctx = {.visitor = visitor, .user = user};
    yetty_ydraw_scrolling_grid_for_each_glyph(c->grid, c->cell_size.height, glyph_adapter, &ctx);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Vtable
 *===========================================================================*/

static const struct yetty_ydraw_canvas_ops scrolling_canvas_ops = {
    .name = "scrolling",
    .destroy = scrolling_destroy,
    .set_cell_size = scrolling_set_cell_size,
    .set_grid_size = scrolling_set_grid_size,
    .get_cell_size = scrolling_get_cell_size,
    .get_grid_size = scrolling_get_grid_size,
    .process_input = scrolling_process_input,
    .set_cursor_pos = scrolling_set_cursor_pos,
    .scroll_lines = scrolling_scroll_lines,
    .set_view_top = scrolling_set_view_top,
    .rolling_row_0 = scrolling_rolling_row_0,
    .live_rolling_row_0 = scrolling_live_rolling_row_0,
    .set_scroll_callback = scrolling_set_scroll_callback,
    .set_cursor_callback = scrolling_set_cursor_callback,
    .mark_dirty = scrolling_mark_dirty,
    .is_dirty = scrolling_is_dirty,
    .rebuild_grid = scrolling_rebuild_grid,
    .grid_data = scrolling_grid_data,
    .grid_word_count = scrolling_grid_word_count,
    .build_drawable_staging = scrolling_build_drawable_staging,
    .drawable_gpu_size = scrolling_drawable_gpu_size,
    .clear = scrolling_clear,
    .drawable_count = scrolling_drawable_count,
    .font_count = scrolling_font_count,
    .font_generation = scrolling_font_generation,
    .get_font_at = scrolling_get_font_at,
    .get_default_font = scrolling_get_default_font,
    .get_flyweight_registry = scrolling_get_flyweight_registry,
    .get_figure_factory = scrolling_get_figure_factory,
    .figure_count = scrolling_figure_count,
    .get_figure = scrolling_get_figure,
    .for_each_glyph = scrolling_for_each_glyph,
};
