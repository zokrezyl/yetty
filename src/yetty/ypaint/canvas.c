/* canvas.c — implementations of shared ypaint canvas helpers.
 *
 * See canvas-internal.h for the design overview and type definitions. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas-internal.h"

#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ypaint-factory/complex-prim-factory.h>
#include <yetty/ypaint/flyweight.h>
#include <yetty/ytrace/ytrace.h>
#if YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ymsdf/generator.h>
#endif

/* Provided per-platform (yplatform/{linux,macos,windows,android,ios,webasm}/
 * platform-paths.{c,m}). Returns a writable directory unique to the user. */
extern const char *yetty_yplatform_get_cache_dir(void);

/*===========================================================================
 * prim_ref_array
 *===========================================================================*/

void ypaint_canvas_prim_ref_array_init(struct ypaint_canvas_prim_ref_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void ypaint_canvas_prim_ref_array_free(struct ypaint_canvas_prim_ref_array *arr)
{
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void ypaint_canvas_prim_ref_array_push(struct ypaint_canvas_prim_ref_array *arr,
                                       struct ypaint_canvas_prim_ref ref)
{
    if (arr->count >= arr->capacity) {
        uint32_t new_cap =
            arr->capacity == 0 ? YPAINT_CANVAS_INITIAL_REF_CAPACITY : arr->capacity * 2;
        arr->data = realloc(arr->data, new_cap * sizeof(struct ypaint_canvas_prim_ref));
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = ref;
}

/*===========================================================================
 * prim_data_array
 *===========================================================================*/

void ypaint_canvas_prim_data_array_init(struct ypaint_canvas_prim_data_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void ypaint_canvas_prim_data_array_free(struct ypaint_canvas_prim_data_array *arr)
{
    /* Payloads live in the owning line's arena (freed in grid_line_free);
     * only the index records need releasing here. */
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

/*===========================================================================
 * grid_line
 *===========================================================================*/

struct yetty_ycore_void_result ypaint_canvas_grid_line_init(
    struct ypaint_canvas_grid_line *line)
{
    /* Cells are allocated lazily by grid_line_ensure_cells on first touch.
     * Lines with no primitives (common in PDFs — empty space between
     * paragraphs) stay at zero cell-array bytes. */
    ypaint_canvas_prim_data_array_init(&line->prims);
    line->arena = NULL;
    line->arena_count = 0;
    line->arena_capacity = 0;
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ypaint_canvas_grid_line_free(
    struct ypaint_canvas_grid_line *line, struct yetty_yfont_cache *cache)
{
    ypaint_canvas_prim_data_array_free(&line->prims);
    free(line->arena);
    line->arena = NULL;
    line->arena_count = 0;
    line->arena_capacity = 0;
    /* Lines hold one cache ref per attached font — release each before
     * dropping the array. Cache may free the slot if this was the last ref. */
    for (uint32_t i = 0; i < line->font_count; i++) {
        yetty_yfont_cache_release_font(cache, line->fonts[i].handle);
    }
    free(line->fonts);
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    /* Destroy complex prim instances owned by this line. */
    for (uint32_t i = 0; i < line->complex_prim_count; i++) {
        yetty_ypaint_core_complex_prim_instance_destroy(line->complex_prims[i]);
    }
    free(line->complex_prims);
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;
    for (uint32_t i = 0; i < line->cell_count; i++) {
        ypaint_canvas_prim_ref_array_free(&line->cells[i].refs);
    }
    free(line->cells);
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ypaint_canvas_grid_line_ensure_cells(
    struct ypaint_canvas_grid_line *line, uint32_t min_cells)
{
    if (min_cells <= line->cell_capacity) {
        if (min_cells > line->cell_count) {
            for (uint32_t i = line->cell_count; i < min_cells; i++) {
                ypaint_canvas_prim_ref_array_init(&line->cells[i].refs);
            }
            line->cell_count = min_cells;
        }
        return YETTY_OK_VOID();
    }

    uint32_t new_cap =
        line->cell_capacity == 0 ? YPAINT_CANVAS_INITIAL_CELL_CAPACITY : line->cell_capacity;
    while (new_cap < min_cells) {
        new_cap *= 2;
    }

    struct ypaint_canvas_grid_cell *new_cells =
        realloc(line->cells, new_cap * sizeof(struct ypaint_canvas_grid_cell));
    if (!new_cells) {
        return YETTY_ERR(yetty_ycore_void, "realloc failed for grid cells");
    }
    line->cells = new_cells;
    for (uint32_t i = line->cell_capacity; i < new_cap; i++) {
        ypaint_canvas_prim_ref_array_init(&line->cells[i].refs);
    }
    line->cell_capacity = new_cap;
    line->cell_count = min_cells;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ypaint_canvas_grid_line_arena_append(
    struct ypaint_canvas_grid_line *line, const float *data, uint32_t word_count,
    uint32_t *out_offset)
{
    if (line->arena_count + word_count > line->arena_capacity) {
        uint32_t new_cap = line->arena_capacity ? line->arena_capacity : 32;
        while (new_cap < line->arena_count + word_count) {
            new_cap *= 2;
        }
        uint32_t *new_arena = realloc(line->arena, new_cap * sizeof(uint32_t));
        if (!new_arena) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed for prim arena");
        }
        line->arena = new_arena;
        line->arena_capacity = new_cap;
    }
    *out_offset = line->arena_count;
    /* memcpy by bytes to stay endian/alignment-agnostic — the source is the
     * iter's raw word stream already in the layout the GPU expects. */
    memcpy(line->arena + line->arena_count, data, word_count * sizeof(uint32_t));
    line->arena_count += word_count;
    return YETTY_OK_VOID();
}

uint32_t ypaint_canvas_grid_line_push_prim(struct ypaint_canvas_grid_line *line,
                                           uint32_t rolling_row,
                                           const float *data, uint32_t word_count)
{
    struct ypaint_canvas_prim_data_array *arr = &line->prims;
    if (arr->count >= arr->capacity) {
        uint32_t new_cap =
            arr->capacity == 0 ? YPAINT_CANVAS_INITIAL_PRIM_CAPACITY : arr->capacity * 2;
        struct ypaint_canvas_prim_data *grown =
            realloc(arr->data, new_cap * sizeof(struct ypaint_canvas_prim_data));
        if (!grown) {
            return UINT32_MAX;
        }
        arr->data = grown;
        arr->capacity = new_cap;
    }
    uint32_t offset = 0;
    struct yetty_ycore_void_result ar =
        ypaint_canvas_grid_line_arena_append(line, data, word_count, &offset);
    if (YETTY_IS_ERR(ar)) {
        yetty_ycore_error_destroy(ar.error);
        return UINT32_MAX;
    }
    uint32_t idx = arr->count++;
    arr->data[idx].rolling_row = rolling_row;
    arr->data[idx].arena_offset = offset;
    arr->data[idx].word_count = word_count;
    return idx;
}

/*===========================================================================
 * line_buffer
 *===========================================================================*/

void ypaint_canvas_line_buffer_init(struct ypaint_canvas_line_buffer *buf)
{
    buf->lines = NULL;
    buf->capacity = 0;
    buf->count = 0;
}

struct yetty_ycore_void_result ypaint_canvas_line_buffer_free(
    struct ypaint_canvas_line_buffer *buf, struct yetty_yfont_cache *cache)
{
    for (uint32_t i = 0; i < buf->count; i++) {
        struct yetty_ycore_void_result res =
            ypaint_canvas_grid_line_free(&buf->lines[i], cache);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    free(buf->lines);
    buf->lines = NULL;
    buf->capacity = 0;
    buf->count = 0;
    return YETTY_OK_VOID();
}

struct ypaint_canvas_grid_line *ypaint_canvas_line_buffer_get(
    struct ypaint_canvas_line_buffer *buf, uint32_t index)
{
    if (index >= buf->count) {
        return NULL;
    }
    return &buf->lines[index];
}

/*===========================================================================
 * Font-blob utilities
 *===========================================================================*/

/* Decide whether a font-blob name resolves as a raster (TTF) or MSDF (CDB)
 * source. Looks at the file extension so a single buffer can mix both;
 * falls back to the canvas-wide render method when the extension is
 * unknown. */
int ypaint_canvas_blob_is_raster(const char *name, int canvas_method)
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

/* FNV-1a 64-bit hash — content-addressing for on-disk PDF blob CDB cache. */
uint64_t ypaint_canvas_fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Sanitise a string into something safe to splice into a WGSL identifier
 * (and therefore safe as a font cache key, which the cache passes straight
 * to msdf_font_create as the shader namespace). */
void ypaint_canvas_sanitize_identifier(char *dst, size_t dst_cap, const char *src)
{
    size_t ni = 0;
    for (const char *s = src; *s && ni + 1 < dst_cap; s++) {
        char c = *s;
        dst[ni++] = (c == '-' || c == ' ' || c == '.') ? '_' : c;
    }
    dst[ni] = '\0';
}

/*===========================================================================
 * Font resolution
 *===========================================================================*/

struct yetty_yfont_cache_ref_result ypaint_canvas_get_default_font_ref(
    struct yetty_ypaint_canvas *canvas)
{
    if (canvas->font_render_method == 1) {
        return YETTY_ERR(yetty_yfont_cache_ref,
                         "raster default font is not yet wired through the font cache");
    }
    char cdb_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", canvas->fonts_dir,
             canvas->font_family);
    char ns[128];
    ypaint_canvas_sanitize_identifier(ns, sizeof(ns), canvas->font_family);
    ydebug("ypaint_canvas: default msdf font cdb='%s' key='%s'", cdb_path, ns);
    return yetty_yfont_cache_get_font(canvas->font_cache, ns, cdb_path);
}

struct yetty_ycore_void_result ypaint_canvas_ensure_blob_font_cdb(
    struct yetty_ypaint_canvas *canvas, const uint8_t *ttf, uint32_t ttf_len,
    const char *hint_name, char out_hex[17])
{
    if (!ttf || ttf_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "blob is empty");
    }
    if (ypaint_canvas_blob_is_raster(hint_name, canvas->font_render_method)) {
        return YETTY_ERR(yetty_ycore_void,
                         "raster blob fonts are not yet wired through the font cache");
    }

    uint64_t h = ypaint_canvas_fnv1a64(ttf, ttf_len);
    snprintf(out_hex, 17, "%016llx", (unsigned long long)h);

    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_ycore_void, "no cache dir");
    }

    char fonts_dir[768];
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/ypaint-fonts", cache_dir);

    char ttf_path[1024], cdb_path[1024];
    snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, out_hex);
    snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, out_hex);

    /* Hot path: CDB already on disk from a previous run / earlier envelope.
     * Nothing else to do — bytes of `ttf` are discarded. */
    if (yetty_yplatform_file_exists(cdb_path)) {
        return YETTY_OK_VOID();
    }

    yetty_yplatform_mkdir_p(fonts_dir);
    if (!yetty_yplatform_file_exists(ttf_path)) {
        FILE *f = fopen(ttf_path, "wb");
        if (!f) {
            return YETTY_ERR(yetty_ycore_void, "open ttf cache for write");
        }
        fwrite(ttf, 1, ttf_len, f);
        fclose(f);
        ydebug("ypaint_canvas: cached TTF '%s' (%u bytes) hint='%s'", ttf_path, ttf_len,
               hint_name ? hint_name : "");
    }

#if YETTY_HAS_YMSDF_GEN
    if (!canvas->msdf_generator) {
        yerror("ypaint_canvas: CDB '%s' missing and no MSDF generator on "
               "the canvas — host must initialise gpu_context.msdf_generator.",
               cdb_path);
        return YETTY_ERR(yetty_ycore_void, "no MSDF generator available");
    }
    struct yetty_ymsdf_generator_config gen = {0};
    gen.ttf_path = ttf_path;
    gen.cdb_path = cdb_path;
    gen.font_size = 32.0f;
    gen.pixel_range = 4.0f;
    struct yetty_ycore_void_result gr =
        canvas->msdf_generator->ops->generate(canvas->msdf_generator, &gen);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "msdf generator failed");
    ydebug("ypaint_canvas: generated CDB '%s' via %s generator", cdb_path,
           canvas->msdf_generator->ops->name(canvas->msdf_generator));
    return YETTY_OK_VOID();
#else
    return YETTY_ERR(yetty_ycore_void, "ymsdf-gen disabled in this build");
#endif
}

struct yetty_yfont_cache_ref_result ypaint_canvas_resolve_blob_font_handle(
    struct yetty_ypaint_canvas *canvas, const char *hex)
{
    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_yfont_cache_ref, "no cache dir");
    }
    char cdb_path[1024];
    snprintf(cdb_path, sizeof(cdb_path), "%s/ypaint-fonts/pdf_%s.cdb", cache_dir, hex);
    return yetty_yfont_cache_get_font(canvas->font_cache, hex, cdb_path);
}

/*===========================================================================
 * Staging growth
 *===========================================================================*/

struct yetty_ycore_void_result ypaint_canvas_ensure_grid_staging(
    struct yetty_ypaint_canvas *canvas, uint32_t min_size)
{
    if (min_size <= canvas->grid_staging_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = canvas->grid_staging_capacity == 0
                          ? YPAINT_CANVAS_INITIAL_STAGING_CAPACITY
                          : canvas->grid_staging_capacity;
    while (new_cap < min_size) {
        new_cap *= 2;
    }
    uint32_t *new_staging = realloc(canvas->grid_staging, new_cap * sizeof(uint32_t));
    if (!new_staging) {
        return YETTY_ERR(yetty_ycore_void, "realloc failed for grid staging");
    }
    canvas->grid_staging = new_staging;
    canvas->grid_staging_capacity = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ypaint_canvas_ensure_prim_staging(
    struct yetty_ypaint_canvas *canvas, uint32_t min_size)
{
    if (min_size <= canvas->prim_staging_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_cap = canvas->prim_staging_capacity == 0
                          ? YPAINT_CANVAS_INITIAL_STAGING_CAPACITY
                          : canvas->prim_staging_capacity;
    while (new_cap < min_size) {
        new_cap *= 2;
    }
    uint32_t *grown = realloc(canvas->prim_staging, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "realloc failed for prim staging");
    }
    canvas->prim_staging = grown;
    canvas->prim_staging_capacity = new_cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Polymorphic base lifecycle
 *
 * `ypaint_canvas_create` allocates the canvas, wires its vtable + impl
 * back-pointer, and sets up the shared state (flyweight registry,
 * complex-prim factory + yplot/yimage/ymesh registrations, font cache,
 * default font, dirs/family/render-method from context->app_context.config).
 *
 * On any failure, everything allocated so far is rolled back and the
 * canvas is freed before returning.
 *===========================================================================*/

/* Private teardown helper — frees only the shared state and the base
 * struct itself. The polymorphic `yetty_ypaint_canvas_destroy` runs the
 * variant's destroy op first (to free variant-specific state and the
 * variant struct), then calls this. Also used in the error path of
 * `ypaint_canvas_create` to roll back partial allocations. */
static struct yetty_ycore_void_result ypaint_canvas_free_base(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_OK_VOID();
    }
    if (canvas->font_cache &&
        canvas->default_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) {
        yetty_yfont_cache_release_font(canvas->font_cache, canvas->default_handle);
        canvas->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    if (canvas->font_cache) {
        yetty_yfont_cache_destroy(canvas->font_cache);
        canvas->font_cache = NULL;
    }
    if (canvas->complex_prim_factory) {
        yetty_ypaint_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        canvas->complex_prim_factory = NULL;
    }
    if (canvas->flyweight_registry) {
        yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
        canvas->flyweight_registry = NULL;
    }
    free(canvas->grid_staging);
    free(canvas->prim_staging);
    free(canvas);
    return YETTY_OK_VOID();
}

struct yetty_ypaint_canvas_ptr_result ypaint_canvas_create(
    const struct yetty_context *context,
    const struct yetty_ypaint_canvas_ops *ops,
    void *impl)
{
    if (!context) {
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "context is NULL");
    }
    if (!ops) {
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ops is NULL");
    }

    struct yetty_ypaint_canvas *canvas = calloc(1, sizeof(struct yetty_ypaint_canvas));
    if (!canvas) {
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "canvas alloc failed");
    }
    canvas->ops = ops;
    canvas->impl = impl;
    canvas->dirty = true;
    canvas->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;

    /* Flyweight registry (SDF prim handlers). */
    struct yetty_ypaint_core_flyweight_registry_ptr_result fw_res =
        yetty_ypaint_flyweight_create();
    if (YETTY_IS_ERR(fw_res)) {
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "flyweight creation failed", fw_res);
    }
    canvas->flyweight_registry = fw_res.value;

    /* Complex prim factory. */
    struct yetty_ypaint_core_complex_prim_factory_ptr_result factory_res =
        yetty_ypaint_core_complex_prim_factory_create(
            context->gpu_context.device, context->gpu_context.queue,
            context->gpu_context.surface_format, context->gpu_context.allocator);
    if (YETTY_IS_ERR(factory_res)) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "complex prim factory creation failed",
                         factory_res);
    }
    canvas->complex_prim_factory = factory_res.value;

    /* Built-in factory registrations: yplot, yimage, ymesh. */
    struct yetty_ypaint_core_concrete_factory *yplot_factory = yetty_yplot_factory_create();
    if (!yplot_factory) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "yplot factory creation failed");
    }
    struct yetty_ycore_void_result yplot_reg_res =
        yetty_ypaint_core_complex_prim_factory_register(canvas->complex_prim_factory,
                                                        yplot_factory);
    if (YETTY_IS_ERR(yplot_reg_res)) {
        yetty_yplot_factory_destroy(yplot_factory);
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "yplot registration failed",
                         yplot_reg_res);
    }

    struct yetty_ypaint_core_concrete_factory *yimage_factory = yetty_yimage_factory_create();
    if (!yimage_factory) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "yimage factory creation failed");
    }
    struct yetty_ycore_void_result yimage_reg_res =
        yetty_ypaint_core_complex_prim_factory_register(canvas->complex_prim_factory,
                                                        yimage_factory);
    if (YETTY_IS_ERR(yimage_reg_res)) {
        yetty_yimage_factory_destroy(yimage_factory);
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "yimage registration failed",
                         yimage_reg_res);
    }

#if YETTY_HAS_YMESH
    struct yetty_ypaint_core_concrete_factory *ymesh_factory = yetty_ymesh_factory_create();
    if (!ymesh_factory) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ymesh factory creation failed");
    }
    struct yetty_ycore_void_result ymesh_reg_res =
        yetty_ypaint_core_complex_prim_factory_register(canvas->complex_prim_factory,
                                                        ymesh_factory);
    if (YETTY_IS_ERR(ymesh_reg_res)) {
        yetty_ymesh_factory_destroy(ymesh_factory);
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ymesh registration failed",
                         ymesh_reg_res);
    }
#endif

    /* Resource paths + font kind from config. */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = config->ops->font_family(config);
    if (!font_family || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }
    const char *render_method =
        config->ops->get_string(config, "ypaint/font/render-method", "msdf");

    strncpy(canvas->shaders_dir, shaders_dir, sizeof(canvas->shaders_dir) - 1);
    strncpy(canvas->fonts_dir, fonts_dir, sizeof(canvas->fonts_dir) - 1);
    strncpy(canvas->font_family, font_family, sizeof(canvas->font_family) - 1);
    canvas->font_render_method = (strcmp(render_method, "raster") == 0) ? 1 : 0;
    canvas->raster_base_size = 32.0f;
    canvas->msdf_generator = context->gpu_context.msdf_generator;

    ydebug("ypaint_canvas: font render_method='%s'", render_method);

    /* Per-canvas font cache. yetty_yfont_cache_create takes the shaders_dir
     * directly (not a yetty_context) so yetty_yfont_core stays GPU-less. */
    struct yetty_yfont_cache_ptr_result cache_res =
        yetty_yfont_cache_create(shaders_dir);
    if (YETTY_IS_ERR(cache_res)) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "font cache creation failed", cache_res);
    }
    canvas->font_cache = cache_res.value;

    /* Default font installs at slot 0 (first get_font on a fresh cache).
     * Producer font_id == -1 routes through default_font/default_handle. */
    struct yetty_yfont_cache_ref_result def_res = ypaint_canvas_get_default_font_ref(canvas);
    if (YETTY_IS_ERR(def_res)) {
        ypaint_canvas_free_base(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "default font creation failed", def_res);
    }
    canvas->default_font = def_res.value.font;
    canvas->default_handle = def_res.value.handle;
    ydebug("ypaint_canvas: default font installed at handle=%u", canvas->default_handle);

    return YETTY_OK(yetty_ypaint_canvas_ptr, canvas);
}

/*===========================================================================
 * font_map
 *===========================================================================*/

void ypaint_canvas_font_map_init(struct ypaint_canvas_font_map *m)
{
    m->entries = NULL;
    m->capacity = 0;
}

void ypaint_canvas_font_map_grow(struct ypaint_canvas_font_map *m, uint32_t want)
{
    if (want <= m->capacity) {
        return;
    }
    uint32_t new_cap = m->capacity ? m->capacity * 2 : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    m->entries = realloc(m->entries, new_cap * sizeof(struct ypaint_canvas_font_map_entry));
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        m->entries[i].hex[0] = '\0';
        m->entries[i].declared = false;
        m->entries[i].resolved = false;
        m->entries[i].font = NULL;
        m->entries[i].handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    m->capacity = new_cap;
}

const struct ypaint_canvas_font_map_entry *ypaint_canvas_font_map_get(
    const struct ypaint_canvas_font_map *m, uint32_t id)
{
    if (id >= m->capacity || !m->entries[id].resolved) {
        return NULL;
    }
    return &m->entries[id];
}

void ypaint_canvas_font_map_release_all(
    struct ypaint_canvas_font_map *m, struct yetty_yfont_cache *cache)
{
    if (!m || !m->entries) {
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
 * buffer_attach_list
 *===========================================================================*/

void ypaint_canvas_buffer_attach_init(struct ypaint_canvas_buffer_attach_list *l)
{
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

void ypaint_canvas_buffer_attach_free(struct ypaint_canvas_buffer_attach_list *l)
{
    free(l->entries);
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

void ypaint_canvas_buffer_attach_note(struct ypaint_canvas_buffer_attach_list *l,
                                      yetty_yfont_cache_handle handle, uint32_t row)
{
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID) {
        return;
    }
    for (uint32_t i = 0; i < l->count; i++) {
        if (l->entries[i].handle == handle) {
            if (row > l->entries[i].max_row) {
                l->entries[i].max_row = row;
            }
            return;
        }
    }
    if (l->count >= l->capacity) {
        uint32_t nc = l->capacity ? l->capacity * 2 : 8;
        struct ypaint_canvas_buffer_attach_entry *ne =
            realloc(l->entries, nc * sizeof(struct ypaint_canvas_buffer_attach_entry));
        if (!ne) {
            return; /* Best-effort; lose attachment for this font. */
        }
        l->entries = ne;
        l->capacity = nc;
    }
    l->entries[l->count++] = (struct ypaint_canvas_buffer_attach_entry){
        .handle = handle, .max_row = row};
}

/*===========================================================================
 * Polymorphic API (public; declarations in <yetty/ypaint/canvas.h>)
 *
 * Most operations dispatch via the vtable. Plain getters that just read
 * shared state are direct field reads.
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ypaint_canvas_destroy(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    /* Variant frees its own state and its own struct first. */
    struct yetty_ycore_void_result r = canvas->ops->destroy(canvas);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "variant destroy failed");
    /* Now tear down the base. */
    return ypaint_canvas_free_base(canvas);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cell_size(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_pixel_size pixel_size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (pixel_size.width <= 0.0f || pixel_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "cell size must be > 0");
    }
    canvas->cell_size = pixel_size;
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_grid_size(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_size grid_size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    return canvas->ops->set_grid_size(canvas, grid_size);
}

struct yetty_ycore_pixel_size yetty_ypaint_canvas_cell_get_pixel_size(
    const struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_pixel_size){0, 0};
    }
    return canvas->cell_size;
}

struct yetty_ycore_grid_size yetty_ypaint_canvas_get_grid_size(
    const struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_grid_size){0, 0};
    }
    return canvas->grid_size;
}

struct yetty_ycore_void_result yetty_ypaint_canvas_process_input(
    struct yetty_ypaint_canvas *canvas, struct yetty_yterm_osc_statemachine *sm)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "sm is NULL");
    }
    return canvas->ops->process_input(canvas, sm);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_pos(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_cursor_pos cursor_pos)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->ops->set_cursor_pos) {
        return YETTY_OK_VOID();
    }
    return canvas->ops->set_cursor_pos(canvas, cursor_pos);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_scroll_lines(
    struct yetty_ypaint_canvas *canvas, uint16_t num_lines)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->ops->scroll_lines) {
        return YETTY_OK_VOID();
    }
    return canvas->ops->scroll_lines(canvas, num_lines);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_view_top(
    struct yetty_ypaint_canvas *canvas, bool active, uint32_t view_top)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->ops->set_view_top) {
        return YETTY_OK_VOID();
    }
    return canvas->ops->set_view_top(canvas, active, view_top);
}

uint32_t yetty_ypaint_canvas_rolling_row_0(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas || !canvas->ops->rolling_row_0) {
        return 0;
    }
    return canvas->ops->rolling_row_0(canvas);
}

uint32_t yetty_ypaint_canvas_live_rolling_row_0(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas || !canvas->ops->live_rolling_row_0) {
        return 0;
    }
    return canvas->ops->live_rolling_row_0(canvas);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_scroll_callback(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_scroll_callback callback, void *userdata)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->ops->set_scroll_callback) {
        return YETTY_OK_VOID();
    }
    return canvas->ops->set_scroll_callback(canvas, callback, userdata);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_callback(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_cursor_callback callback, void *userdata)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->ops->set_cursor_callback) {
        return YETTY_OK_VOID();
    }
    return canvas->ops->set_cursor_callback(canvas, callback, userdata);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_mark_dirty(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

bool yetty_ypaint_canvas_is_dirty(const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->dirty : false;
}

struct yetty_ycore_void_result yetty_ypaint_canvas_rebuild_grid(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    return canvas->ops->rebuild_grid(canvas);
}

const uint32_t *yetty_ypaint_canvas_grid_data(const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->grid_staging : NULL;
}

uint32_t yetty_ypaint_canvas_grid_word_count(const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->grid_staging_count : 0;
}

struct yetty_ypaint_prim_staging_result yetty_ypaint_canvas_build_prim_staging(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ypaint_prim_staging, "canvas is NULL");
    }
    return canvas->ops->build_prim_staging(canvas);
}

uint32_t yetty_ypaint_canvas_prim_gpu_size(const struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }
    return canvas->ops->prim_gpu_size(canvas);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_clear(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    return canvas->ops->clear(canvas);
}

uint32_t yetty_ypaint_canvas_primitive_count(const struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }
    return canvas->ops->primitive_count(canvas);
}

uint32_t yetty_ypaint_canvas_font_count(const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? yetty_yfont_cache_count(canvas->font_cache) : 0;
}

uint32_t yetty_ypaint_canvas_font_generation(const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? yetty_yfont_cache_generation(canvas->font_cache) : 0;
}

struct yetty_ypaint_font *yetty_ypaint_canvas_get_font_at(
    const struct yetty_ypaint_canvas *canvas, uint32_t slot)
{
    if (!canvas) {
        return NULL;
    }
    return yetty_yfont_cache_font_at(canvas->font_cache,
                                     (yetty_yfont_cache_handle)slot);
}

struct yetty_ypaint_font *yetty_ypaint_canvas_get_default_font(
    const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->default_font : NULL;
}

const struct yetty_ypaint_core_flyweight_registry *yetty_ypaint_canvas_get_flyweight_registry(
    const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->flyweight_registry : NULL;
}

struct yetty_ypaint_core_complex_prim_factory *yetty_ypaint_canvas_get_complex_prim_factory(
    const struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->complex_prim_factory : NULL;
}

uint32_t yetty_ypaint_canvas_complex_prim_count(const struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }
    return canvas->ops->complex_prim_count(canvas);
}

struct yetty_ypaint_core_complex_prim_instance *yetty_ypaint_canvas_get_complex_prim(
    const struct yetty_ypaint_canvas *canvas, uint32_t index)
{
    if (!canvas) {
        return NULL;
    }
    return canvas->ops->get_complex_prim(canvas, index);
}

void yetty_ypaint_canvas_for_each_glyph(
    struct yetty_ypaint_canvas *canvas,
    yetty_ypaint_canvas_glyph_visitor visitor, void *user)
{
    if (!canvas || !visitor) {
        return;
    }
    canvas->ops->for_each_glyph(canvas, visitor, user);
}
