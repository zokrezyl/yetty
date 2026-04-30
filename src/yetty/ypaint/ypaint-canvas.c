// YPaint Canvas - Implementation
// Rolling offset approach for O(1) scrolling

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/yplatform/compat.h>
#include <yetty/yplatform/fs.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ypaint-core/font-prim.h>
#include <yetty/ypaint-core/text-span-prim.h>
#include <yetty/ypaint/flyweight.h>
#include <yetty/ypaint/core/ypaint-canvas.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/raster-font.h>
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#endif
#include <yetty/ysdf/types.gen.h>
#include <yetty/yconfig.h>
#include <yetty/yetty.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/ytrace.h>

/* Provided per-platform (yplatform/{linux,macos,windows,android,ios,webasm}/
 * platform-paths.{c,m}). Returns a writable directory unique to the user. */
extern const char *yetty_yplatform_get_cache_dir(void);

/* Glyph primitive type (not in ysdf types.gen.h since not SDF) */
#define YETTY_YSDF_GLYPH 200

/* Glyph primitive: type, z_order, x, y, font_size, packed(glyph_idx|font_id), color */
#define YPAINT_GLYPH_WORDS 7

//=============================================================================
// Internal data structures
//=============================================================================

// Reference to a primitive in another line
struct yetty_ypaint_canvas_prim_ref {
    uint16_t lines_ahead; // relative offset to base line (0 = same line)
    uint16_t prim_index;  // index within base line's prims array
};

// Dynamic array of prim_ref
struct yetty_ypaint_canvas_prim_ref_array {
    struct yetty_ypaint_canvas_prim_ref *data;
    uint32_t count;
    uint32_t capacity;
};

// A single grid cell
struct yetty_ypaint_canvas_grid_cell {
    struct yetty_ypaint_canvas_prim_ref_array refs;
};

// A single primitive's data
struct yetty_ypaint_canvas_prim_data {
    uint32_t rolling_row; // rolling_row at insertion (cursor row or explicit)
    float *data;
    uint32_t word_count;
};

// Dynamic array of prim_data
struct yetty_ypaint_canvas_prim_data_array {
    struct yetty_ypaint_canvas_prim_data *data;
    uint32_t count;
    uint32_t capacity;
};

// Font resource attached to a grid line — pointer to the font object
struct yetty_ypaint_canvas_font_entry {
    struct yetty_ypaint_font *font; // the font object (owns atlas + metadata)
    int32_t font_id;              // buffer-level font id
};

// Complex primitive stored on last overlapping line - uses factory instance
// (replaces old canvas-specific struct with factory instance pointer)

// A single row/line in the grid
struct yetty_ypaint_canvas_grid_line {
    struct yetty_ypaint_canvas_prim_data_array
        prims; // All primitives (SDF + glyph) whose BASE is this line
    struct yetty_ypaint_canvas_grid_cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;

    // Font resources owned by this line (moved down as needed)
    struct yetty_ypaint_canvas_font_entry *fonts;
    uint32_t font_count;
    uint32_t font_capacity;

    // Complex primitives whose BASE (last overlapping line) is this line
    // Uses factory instances instead of canvas-specific struct
    struct yetty_ypaint_core_complex_prim_instance **complex_prims;
    uint32_t complex_prim_count;
    uint32_t complex_prim_capacity;
};

// Simple line array
struct yetty_ypaint_canvas_line_buffer {
    struct yetty_ypaint_canvas_grid_line *lines;
    uint32_t capacity;
    uint32_t count;
};

// Canvas structure
struct yetty_ypaint_canvas {
    bool scrolling_mode;

    struct yetty_ycore_pixel_size cell_size;
    struct yetty_ycore_grid_size grid_size;

    // Cursor (scrolling mode)
    uint16_t cursor_col;
    uint16_t cursor_row;

    // Rolling row of visible line 0 (increments on scroll). Always tracks
    // the *live* viewport top — never reset by scrollback view.
    uint32_t rolling_row_0;

    // Scrollback view override. While view_top_override_active is true, the
    // shader uniform and rebuild_grid use view_top_override instead of
    // rolling_row_0, so the user sees a frozen historical viewport even as
    // rolling_row_0 advances in the background due to new content.
    bool view_top_override_active;
    uint32_t view_top_override;

    // Lines
    struct yetty_ypaint_canvas_line_buffer lines;

    // Packed grid staging
    uint32_t *grid_staging;
    uint32_t grid_staging_count;
    uint32_t grid_staging_capacity;
    bool dirty;

    // Primitive staging
    uint32_t *prim_staging;
    uint32_t prim_staging_count;
    uint32_t prim_staging_capacity;

    // Scroll callback
    yetty_ypaint_canvas_scroll_callback scroll_callback;
    struct yetty_ycore_void_result *scroll_callback_user_data;

    // Cursor set callback (when cursor moves without scroll)
    yetty_ypaint_canvas_cursor_set_callback cursor_set_callback;
    struct yetty_ycore_void_result *cursor_set_callback_user_data;

    // Default font for text spans with font_id = -1
    struct yetty_ypaint_font *default_font;

    // Font kind selection for per-buffer fonts created from font blobs
    // 0 = MSDF (default, CDB-based), 1 = raster (TTF-based, FreeType).
    int font_render_method;

    // Base size (pixels) used when constructing raster fonts.
    float raster_base_size;

    // Shaders directory for creating fonts from buffers
    char shaders_dir[512];

    // Fonts directory (for deriving TTF paths in raster mode)
    char fonts_dir[512];

    // Font family used when resolving default font and buffer font names
    // that aren't absolute paths.
    char font_family[128];

    // Flyweight registry for primitive handlers (SDF prims)
    struct yetty_ypaint_core_flyweight_registry *flyweight_registry;

    // Factory for complex primitive ops (yplot, yimage, etc.)
    struct yetty_ypaint_core_complex_prim_factory *complex_prim_factory;
};

#define DEFAULT_MAX_PRIMS_PER_CELL 16
#define INITIAL_LINE_CAPACITY 64
#define INITIAL_CELL_CAPACITY 128
#define INITIAL_PRIM_CAPACITY 16
#define INITIAL_REF_CAPACITY 8
#define INITIAL_STAGING_CAPACITY 4096

//=============================================================================
// Helper: Dynamic arrays
//=============================================================================

static void prim_ref_array_init(struct yetty_ypaint_canvas_prim_ref_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_ref_array_free(struct yetty_ypaint_canvas_prim_ref_array *arr)
{
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_ref_array_push(struct yetty_ypaint_canvas_prim_ref_array *arr,
                                struct yetty_ypaint_canvas_prim_ref ref)
{
    if (arr->count >= arr->capacity) {
        uint32_t new_cap = arr->capacity == 0 ? INITIAL_REF_CAPACITY : arr->capacity * 2;
        arr->data = realloc(arr->data, new_cap * sizeof(struct yetty_ypaint_canvas_prim_ref));
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = ref;
}

static void prim_data_array_init(struct yetty_ypaint_canvas_prim_data_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_data_array_free(struct yetty_ypaint_canvas_prim_data_array *arr)
{
    for (uint32_t i = 0; i < arr->count; i++) {
        free(arr->data[i].data);
    }
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static uint32_t prim_data_array_push(struct yetty_ypaint_canvas_prim_data_array *arr,
                                     uint32_t rolling_row, const float *data, uint32_t word_count)
{
    if (arr->count >= arr->capacity) {
        uint32_t new_cap = arr->capacity == 0 ? INITIAL_PRIM_CAPACITY : arr->capacity * 2;
        arr->data = realloc(arr->data, new_cap * sizeof(struct yetty_ypaint_canvas_prim_data));
        arr->capacity = new_cap;
    }
    uint32_t idx = arr->count++;
    arr->data[idx].rolling_row = rolling_row;
    arr->data[idx].data = malloc(word_count * sizeof(float));
    arr->data[idx].word_count = word_count;
    memcpy(arr->data[idx].data, data, word_count * sizeof(float));
    return idx;
}

//=============================================================================
// Helper: grid_line
//=============================================================================

static struct yetty_ycore_void_result grid_line_init(struct yetty_ypaint_canvas_grid_line *line,
                                                     uint32_t initial_cells)
{
    prim_data_array_init(&line->prims);
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;
    if (initial_cells > 0) {
        line->cells = calloc(initial_cells, sizeof(struct yetty_ypaint_canvas_grid_cell));
        if (!line->cells) {
            return YETTY_ERR(yetty_ycore_void, "calloc failed for grid cells");
        }
        line->cell_capacity = initial_cells;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_line_free(
    struct yetty_ypaint_canvas_grid_line *line, const struct yetty_ypaint_core_flyweight_registry *reg)
{
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }

    prim_data_array_free(&line->prims);
    /* Destroy fonts owned by this line */
    for (uint32_t i = 0; i < line->font_count; i++) {
        if (line->fonts[i].font && line->fonts[i].font->ops) {
            line->fonts[i].font->ops->destroy(line->fonts[i].font);
        }
    }
    free(line->fonts);
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;
    /* Destroy complex prim instances owned by this line */
    for (uint32_t i = 0; i < line->complex_prim_count; i++) {
        yetty_ypaint_core_complex_prim_instance_destroy(line->complex_prims[i]);
    }
    free(line->complex_prims);
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;
    for (uint32_t i = 0; i < line->cell_count; i++) {
        prim_ref_array_free(&line->cells[i].refs);
    }
    free(line->cells);
    line->cells = NULL;
    line->cell_count = 0;
    line->cell_capacity = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_line_ensure_cells(
    struct yetty_ypaint_canvas_grid_line *line, uint32_t min_cells)
{
    if (min_cells <= line->cell_capacity) {
        if (min_cells > line->cell_count) {
            for (uint32_t i = line->cell_count; i < min_cells; i++) {
                prim_ref_array_init(&line->cells[i].refs);
            }
            line->cell_count = min_cells;
        }
        return YETTY_OK_VOID();
    }

    uint32_t new_cap = line->cell_capacity == 0 ? INITIAL_CELL_CAPACITY : line->cell_capacity;
    while (new_cap < min_cells) {
        new_cap *= 2;
    }

    struct yetty_ypaint_canvas_grid_cell *new_cells =
        realloc(line->cells, new_cap * sizeof(struct yetty_ypaint_canvas_grid_cell));
    if (!new_cells) {
        return YETTY_ERR(yetty_ycore_void, "realloc failed for grid cells");
    }
    line->cells = new_cells;
    for (uint32_t i = line->cell_capacity; i < new_cap; i++) {
        prim_ref_array_init(&line->cells[i].refs);
    }
    line->cell_capacity = new_cap;
    line->cell_count = min_cells;
    return YETTY_OK_VOID();
}

//=============================================================================
// Helper: line_buffer (circular buffer)
//=============================================================================

static void line_buffer_init(struct yetty_ypaint_canvas_line_buffer *buf)
{
    buf->lines = NULL;
    buf->capacity = 0;
    buf->count = 0;
}

static struct yetty_ycore_void_result line_buffer_free(
    struct yetty_ypaint_canvas_line_buffer *buf, const struct yetty_ypaint_core_flyweight_registry *reg)
{
    for (uint32_t i = 0; i < buf->count; i++) {
        struct yetty_ycore_void_result res = grid_line_free(&buf->lines[i], reg);
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

static struct yetty_ypaint_canvas_grid_line *line_buffer_get(
    struct yetty_ypaint_canvas_line_buffer *buf, uint32_t index)
{
    if (index >= buf->count) {
        return NULL;
    }
    return &buf->lines[index];
}

static struct yetty_ycore_void_result canvas_ensure_lines(struct yetty_ypaint_canvas *canvas,
                                                          uint32_t min_count)
{
    struct yetty_ypaint_canvas_line_buffer *buf = &canvas->lines;

    // Grow capacity if needed
    if (min_count > buf->capacity) {
        uint32_t new_cap = buf->capacity == 0 ? INITIAL_LINE_CAPACITY : buf->capacity;
        while (new_cap < min_count) {
            new_cap *= 2;
        }

        struct yetty_ypaint_canvas_grid_line *new_lines =
            realloc(buf->lines, new_cap * sizeof(struct yetty_ypaint_canvas_grid_line));
        if (!new_lines) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed for line buffer");
        }
        buf->lines = new_lines;
        buf->capacity = new_cap;
    }

    // Initialize new lines at the end
    while (buf->count < min_count) {
        struct yetty_ycore_void_result r =
            grid_line_init(&buf->lines[buf->count], canvas->grid_size.cols);
        if (!r.ok) {
            return r;
        }
        buf->count++;
    }
    return YETTY_OK_VOID();
}

//=============================================================================
// Font construction helper
//=============================================================================

/* Decide whether a font-blob name resolves as a raster (TTF) or MSDF (CDB)
 * source. Looks at the file extension so a single buffer can mix both. Falls
 * back to the canvas-wide render method when the extension is unknown. */
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

/* Construct a yetty_font_font for the given font family. `method` = 0 → MSDF,
 * 1 → raster. Looks up paths relative to the canvas's fonts/shaders dirs. */
static struct yetty_font_font_result ypaint_canvas_make_default_font(
    const struct yetty_ypaint_canvas *canvas)
{
    char shader_path[768];
    if (canvas->font_render_method == 1) {
        char ttf_path[768];
        snprintf(ttf_path, sizeof(ttf_path), "%s/%s-Regular.ttf", canvas->fonts_dir,
                 canvas->font_family);
        snprintf(shader_path, sizeof(shader_path), "%s/raster-font.wgsl", canvas->shaders_dir);
        ydebug("ypaint_canvas: default raster font ttf='%s' shader='%s'", ttf_path, shader_path);
        return yetty_yfont_raster_font_create_from_file(ttf_path, shader_path,
                                                       canvas->raster_base_size);
    }
    char cdb_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", canvas->fonts_dir,
             canvas->font_family);
    snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", canvas->shaders_dir);
    ydebug("ypaint_canvas: default msdf font cdb='%s' shader='%s'", cdb_path, shader_path);
    return yetty_yfont_msdf_font_create(cdb_path, shader_path);
}

/* FNV-1a 64-bit hash — content-addressing for font cache filenames. */
static uint64_t fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Materialize a buffer-supplied FONT prim into a yetty_font_font.
 *
 * The TTF bytes travel inline in the prim payload — we hash them, write to
 * a content-addressed file under <cache>/ypaint-fonts/, generate the MSDF
 * CDB on cache miss via ymsdf-gen, then load the MSDF font. Subsequent
 * occurrences of the same font (same content hash) reuse the cached CDB. */
static struct yetty_font_font_result ypaint_canvas_materialize_blob_font(
    const struct yetty_ypaint_canvas *canvas, const uint8_t *ttf, uint32_t ttf_len,
    const char *hint_name)
{
    if (!ttf || ttf_len == 0) {
        return YETTY_ERR(yetty_font_font, "TTF blob is empty");
    }

    uint64_t h = fnv1a64(ttf, ttf_len);
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)h);

    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_font_font, "no cache dir");
    }

    char fonts_dir[768];
    snprintf(fonts_dir, sizeof(fonts_dir), "%s/ypaint-fonts", cache_dir);
    yetty_yplatform_mkdir_p(fonts_dir);

    char ttf_path[1024], cdb_path[1024], shader_path[1024];
    snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, hex);
    snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, hex);

    /* Raster path: just write the TTF and load it directly — no atlas
   * pre-generation needed. Used when the canvas is configured for raster
   * rendering or when the hint name explicitly says so. */
    if (blob_is_raster(hint_name, canvas->font_render_method)) {
        if (!yetty_yplatform_file_exists(ttf_path)) {
            FILE *f = fopen(ttf_path, "wb");
            if (!f) {
                return YETTY_ERR(yetty_font_font, "open ttf cache for write");
            }
            fwrite(ttf, 1, ttf_len, f);
            fclose(f);
        }
        snprintf(shader_path, sizeof(shader_path), "%s/raster-font.wgsl", canvas->shaders_dir);
        return yetty_yfont_raster_font_create_from_file(ttf_path, shader_path,
                                                       canvas->raster_base_size);
    }

    /* MSDF path: write the TTF to cache, then generate the CDB on miss. */
    if (!yetty_yplatform_file_exists(ttf_path)) {
        FILE *f = fopen(ttf_path, "wb");
        if (!f) {
            return YETTY_ERR(yetty_font_font, "open ttf cache for write");
        }
        fwrite(ttf, 1, ttf_len, f);
        fclose(f);
        ydebug("ypaint_canvas: cached TTF '%s' (%u bytes) hint='%s'", ttf_path, ttf_len,
               hint_name ? hint_name : "");
    }

    if (!yetty_yplatform_file_exists(cdb_path)) {
#if YETTY_HAS_YMSDF_GEN
        struct yetty_ymsdf_gen_config gen = {0};
        gen.ttf_path = ttf_path;
        gen.output_dir = fonts_dir;
        gen.font_size = 32.0f;
        gen.pixel_range = 4.0f;
        gen.thread_count = 0;
        gen.all_glyphs = 1; /* PDFs may use any codepoint in the font */
        struct yetty_ycore_void_result gr = yetty_ymsdf_gen_config_cpu_generate(&gen);
        if (YETTY_IS_ERR(gr)) {
            yerror("ypaint_canvas: msdf-gen failed: %s", gr.error.msg);
            return YETTY_ERR(yetty_font_font, gr.error.msg);
        }
        ydebug("ypaint_canvas: generated CDB '%s'", cdb_path);
#else
        yerror("ypaint_canvas: CDB '%s' missing and ymsdf-gen disabled at "
               "build time — pre-bake the CDB or rebuild with "
               "YETTY_ENABLE_FEATURE_YMSDF_GEN=ON.", cdb_path);
        return YETTY_ERR(yetty_font_font, "ymsdf-gen disabled");
#endif
    }

    snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", canvas->shaders_dir);
    return yetty_yfont_msdf_font_create(cdb_path, shader_path);
}

//=============================================================================
// Canvas implementation
//=============================================================================

struct yetty_ypaint_canvas_ptr_result yetty_ypaint_canvas_create(
    bool scrolling_mode, const struct yetty_context *context)
{
    struct yetty_ypaint_canvas *canvas;

    if (!context) {
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "context is NULL");
    }

    canvas = calloc(1, sizeof(struct yetty_ypaint_canvas));
    if (!canvas) {
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "canvas alloc failed");
    }

    canvas->scrolling_mode = scrolling_mode;
    canvas->dirty = true;
    canvas->rolling_row_0 = 0;

    line_buffer_init(&canvas->lines);

    /* Create flyweight registry with all handlers (for SDF prims) */
    struct yetty_ypaint_core_flyweight_registry_ptr_result fw_res = yetty_ypaint_flyweight_create();
    if (YETTY_IS_ERR(fw_res)) {
        yerror("ypaint_canvas: flyweight creation failed: %s", fw_res.error.msg);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ypaint_canvas: flyweight creation failed",
                         fw_res);
    }
    canvas->flyweight_registry = fw_res.value;

    /* Create complex prim factory and register types */
    struct yetty_ypaint_core_complex_prim_factory_ptr_result factory_res =
        yetty_ypaint_core_complex_prim_factory_create(
            context->gpu_context.device, context->gpu_context.queue,
            context->gpu_context.surface_format, context->gpu_context.allocator);
    if (YETTY_IS_ERR(factory_res)) {
        yerror("ypaint_canvas: factory creation failed: %s", factory_res.error.msg);
        yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ypaint_canvas: factory creation failed",
                         factory_res);
    }
    canvas->complex_prim_factory = factory_res.value;

    /* Create and register yplot factory */
    struct yetty_ypaint_core_concrete_factory *yplot_factory = yetty_yplot_factory_create();
    if (!yplot_factory) {
        yerror("ypaint_canvas: yplot factory creation failed");
        yetty_ypaint_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "yplot factory creation failed");
    }
    struct yetty_ycore_void_result yplot_reg_res =
        yetty_ypaint_core_complex_prim_factory_register(canvas->complex_prim_factory, yplot_factory);
    if (YETTY_IS_ERR(yplot_reg_res)) {
        yerror("ypaint_canvas: yplot registration failed: %s", yplot_reg_res.error.msg);
        yetty_yplot_factory_destroy(yplot_factory);
        yetty_ypaint_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ypaint_canvas: yplot registration failed",
                         yplot_reg_res);
    }

    /* Create default font for text spans (font_id = -1).
   * Backend (MSDF vs raster) is selected via ypaint/font/render-method.
   * Default is "msdf" to preserve existing rendering. */
    struct yetty_yconfig *config = context->app_context.config;
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

    ydebug("ypaint_canvas: font render_method='%s'", render_method);
    struct yetty_font_font_result font_res = ypaint_canvas_make_default_font(canvas);
    if (YETTY_IS_OK(font_res)) {
        canvas->default_font = font_res.value;
        ydebug("ypaint_canvas: default font created");
    } else {
        yerror("ypaint_canvas: default font creation failed: %s", font_res.error.msg);
        yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
        /* lines buffer is empty here, no registry needed for cleanup */
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ypaint_canvas_ptr, "ypaint_canvas: default font creation failed",
                         font_res);
    }

    return YETTY_OK(yetty_ypaint_canvas_ptr, canvas);
}

struct yetty_ycore_void_result yetty_ypaint_canvas_destroy(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    if (canvas->default_font) {
        canvas->default_font->ops->destroy(canvas->default_font);
    }
    struct yetty_ycore_void_result res =
        line_buffer_free(&canvas->lines, canvas->flyweight_registry);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    yetty_ypaint_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
    yetty_ypaint_core_flyweight_registry_destroy(canvas->flyweight_registry);
    free(canvas->grid_staging);
    free(canvas->prim_staging);
    free(canvas);
    return YETTY_OK_VOID();
}

//=============================================================================
// Configuration
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cell_size(struct yetty_ypaint_canvas *canvas,
                                                                 struct yetty_ycore_pixel_size size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (size.width <= 0.0f || size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "cell size must be > 0");
    }
    canvas->cell_size = size;
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_grid_size(struct yetty_ypaint_canvas *canvas,
                                                                 struct yetty_ycore_grid_size size)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->grid_size = size;
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Accessors
//=============================================================================

struct yetty_ycore_pixel_size yetty_ypaint_canvas_cell_get_pixel_size(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_pixel_size){0, 0};
    }
    return canvas->cell_size;
}

struct yetty_ycore_grid_size yetty_ypaint_canvas_get_grid_size(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_grid_size){0, 0};
    }
    return canvas->grid_size;
}

//=============================================================================
// Cursor
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_pos(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_cursor_pos pos)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->cursor_col = pos.cols;
    canvas->cursor_row = pos.rows;
    return YETTY_OK_VOID();
}

uint16_t yetty_ypaint_canvas_cursor_col(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->cursor_col : 0;
}

uint16_t yetty_ypaint_canvas_cursor_row(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->cursor_row : 0;
}

//=============================================================================
// Rolling offset
//=============================================================================

/* Effective viewport top: returns the override during scrollback view,
 * otherwise the live rolling_row_0. Both rebuild_grid and the shader
 * uniform must read through this so the GPU and the cell layout stay in
 * sync (the shader's y_offset = (prim.rolling_row - row0) needs row0 to
 * match the canvas-line that gpu_y=0 was filled from). */
static uint32_t canvas_effective_view_top(const struct yetty_ypaint_canvas *canvas)
{
    if (canvas->view_top_override_active) {
        return canvas->view_top_override;
    }
    return canvas->rolling_row_0;
}

uint32_t yetty_ypaint_canvas_rolling_row_0(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas_effective_view_top(canvas) : 0;
}

uint32_t yetty_ypaint_canvas_live_rolling_row_0(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->rolling_row_0 : 0;
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_view_top(struct yetty_ypaint_canvas *canvas,
                                                                bool active, uint32_t view_top)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->view_top_override_active = active;
    canvas->view_top_override = view_top;
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Primitive management
//=============================================================================

// Add a single primitive (internal)
// Returns the grid_line (bottom row of AABB) for this primitive
static struct uint32_result add_primitive_internal(
    struct yetty_ypaint_canvas *canvas, const struct yetty_ypaint_core_primitive_iter *iter)
{
    if (!canvas) {
        return YETTY_ERR(uint32, "canvas is NULL");
    }
    if (!iter || !iter->fw.data || !iter->fw.ops) {
        return YETTY_ERR(uint32, "invalid iterator");
    }
    if (canvas->cell_size.height <= 0.0f) {
        return YETTY_ERR(uint32, "cell_height <= 0");
    }
    if (canvas->cell_size.width <= 0.0f) {
        return YETTY_ERR(uint32, "cell_width <= 0");
    }

    if (!iter->fw.ops->aabb || !iter->fw.ops->size) {
        return YETTY_ERR(uint32, "handler missing ops");
    }

    uint32_t prim_type = iter->fw.data[0];
    ydebug("add_primitive_internal: START type=0x%08x", prim_type);

    struct rectangle_result aabb_res = iter->fw.ops->aabb(iter->fw.data);
    if (YETTY_IS_ERR(aabb_res)) {
        yerror("add_primitive_internal: aabb failed: %s", aabb_res.error.msg);
        return YETTY_ERR(uint32, aabb_res.error.msg);
    }
    struct yetty_ycore_rectangle aabb = aabb_res.value;

    struct yetty_ycore_size_result size_res = iter->fw.ops->size(iter->fw.data);
    if (YETTY_IS_ERR(size_res)) {
        yerror("add_primitive_internal: size failed: %s", size_res.error.msg);
        return YETTY_ERR(uint32, size_res.error.msg);
    }
    uint32_t word_count = size_res.value / sizeof(uint32_t);

    ydebug("add_primitive_internal: type=0x%08x aabb=[%.1f,%.1f,%.1f,%.1f] words=%u", prim_type,
           aabb.min.x, aabb.min.y, aabb.max.x, aabb.max.y, word_count);

    if (aabb.min.y > aabb.max.y) {
        yerror("BUG: inverted AABB! min.y=%.1f > max.y=%.1f", aabb.min.y, aabb.max.y);
        float tmp = aabb.min.y;
        aabb.min.y = aabb.max.y;
        aabb.max.y = tmp;
    }

    uint32_t primitive_max_in_rows = (uint32_t)floorf(aabb.max.y / canvas->cell_size.height);

    /* cursor_row is a screen-row index relative to the viewport top
   * (rolling_row_0). The cursor's absolute canvas-line is the sum. */
    uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
    uint32_t primitive_grid_line = cursor_canvas_line + primitive_max_in_rows;
    uint32_t primitive_rolling_row = cursor_canvas_line;

    canvas_ensure_lines(canvas, primitive_grid_line + 1);

    struct yetty_ypaint_canvas_grid_line *base_line =
        line_buffer_get(&canvas->lines, primitive_grid_line);
    if (!base_line) {
        return YETTY_ERR(uint32, "line_buffer_get returned NULL");
    }

    uint32_t prim_index = prim_data_array_push(&base_line->prims, primitive_rolling_row,
                                               (const float *)iter->fw.data, word_count);

    uint32_t prim_col_min = (uint32_t)(aabb.min.x / canvas->cell_size.width);
    uint32_t prim_col_max = (uint32_t)(aabb.max.x / canvas->cell_size.width);

    int32_t row_min_rel = (int32_t)floorf(aabb.min.y / canvas->cell_size.height);
    int32_t row_max_rel = (int32_t)floorf(aabb.max.y / canvas->cell_size.height);
    if (row_min_rel < 0) {
        row_min_rel = 0;
    }
    if (row_max_rel < 0) {
        row_max_rel = 0;
    }

    uint32_t prim_row_min = cursor_canvas_line + (uint32_t)row_min_rel;
    uint32_t prim_row_max = cursor_canvas_line + (uint32_t)row_max_rel;

    if (prim_row_min > prim_row_max) {
        return YETTY_ERR(uint32, "AABB row min > max after clamp");
    }
    if (prim_col_min > prim_col_max) {
        return YETTY_ERR(uint32, "AABB col min > max");
    }

    if (canvas->grid_size.cols == 0) {
        return YETTY_ERR(uint32, "grid_size.cols is 0");
    }
    if (prim_col_max >= canvas->grid_size.cols) {
        prim_col_max = canvas->grid_size.cols - 1;
    }

    for (uint32_t row = prim_row_min; row <= prim_row_max; row++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, row);
        grid_line_ensure_cells(line, prim_col_max + 1);

        uint16_t lines_ahead = (uint16_t)(primitive_grid_line - row);

        for (uint32_t col = prim_col_min; col <= prim_col_max; col++) {
            struct yetty_ypaint_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_index};
            prim_ref_array_push(&line->cells[col].refs, ref);
        }
    }

    ydebug("add_primitive_internal: aabb_y=[%.1f,%.1f] cell_height=%.1f "
           "cursor_row=%u",
           aabb.min.y, aabb.max.y, canvas->cell_size.height, canvas->cursor_row);
    ydebug("add_primitive_internal: prim_min_row=%u prim_max_row=%u lines.count=%u", prim_row_min,
           prim_row_max, canvas->lines.count);

    // Track complex prims for resource set collection
    if (yetty_ypaint_core_is_complex_type(prim_type)) {
        /* Create factory instance for complex prim */
        struct yetty_ypaint_core_complex_prim_instance_ptr_result inst_res =
            yetty_ypaint_core_complex_prim_factory_create_instance(
                canvas->complex_prim_factory, iter->fw.data, word_count * sizeof(uint32_t),
                primitive_rolling_row);
        if (YETTY_IS_ERR(inst_res)) {
            return YETTY_ERR(uint32, inst_res.error.msg);
        }

        /* Ensure capacity for instance pointer array */
        if (base_line->complex_prim_count >= base_line->complex_prim_capacity) {
            uint32_t new_cap =
                base_line->complex_prim_capacity == 0 ? 4 : base_line->complex_prim_capacity * 2;
            base_line->complex_prims =
                realloc(base_line->complex_prims,
                        new_cap * sizeof(struct yetty_ypaint_core_complex_prim_instance *));
            if (!base_line->complex_prims) {
                yetty_ypaint_core_complex_prim_instance_destroy(inst_res.value);
                return YETTY_ERR(uint32, "realloc complex_prims failed");
            }
            base_line->complex_prim_capacity = new_cap;
        }

        base_line->complex_prims[base_line->complex_prim_count++] = inst_res.value;

        ydebug("add_primitive_internal: added complex prim type=0x%08x to line %u", prim_type,
               primitive_grid_line);
    }

    canvas->dirty = true;
    return YETTY_OK(uint32, primitive_grid_line);
}

//=============================================================================
// Buffer management (public API)
//=============================================================================

/* Local font map populated as FONT prims are encountered during this
 * add_buffer call. Maps the producer-assigned font_id (text spans
 * reference fonts by this id) to the materialized yetty_font_font.
 *
 * Capacity is grown on demand — text spans typically reference a small
 * set of font_ids but a single PDF can carry dozens. */
struct yetty_ypaint_font_map {
    struct yetty_ypaint_font **fonts; /* fonts[i] for font_id == i, NULL if absent */
    uint32_t capacity;
};

static void font_map_init(struct yetty_ypaint_font_map *m)
{
    m->fonts = NULL;
    m->capacity = 0;
}

static void font_map_grow(struct yetty_ypaint_font_map *m, uint32_t want)
{
    if (want <= m->capacity) {
        return;
    }
    uint32_t new_cap = m->capacity ? m->capacity * 2 : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    m->fonts = realloc(m->fonts, new_cap * sizeof(struct yetty_ypaint_font *));
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        m->fonts[i] = NULL;
    }
    m->capacity = new_cap;
}

static struct yetty_ypaint_font *font_map_get(const struct yetty_ypaint_font_map *m, uint32_t id)
{
    return id < m->capacity ? m->fonts[id] : NULL;
}

/* Expand a TEXT_SPAN view into per-glyph SDF primitives at the canvas's
 * current cursor. Returns the highest grid row touched (0 if no glyphs
 * placed). */
static struct uint32_result expand_text_span_to_glyphs(
    struct yetty_ypaint_canvas *canvas, const struct yetty_ypaint_core_text_span_prim_view *ts,
    struct yetty_ypaint_font *font)
{
    static uint32_t glyph_z_order = 0;
    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0) ? ts->font_size / base_size : 1.0f;
    float cursor_x = ts->x;
    uint32_t glyph_max_row = 0;

    const uint8_t *ptr = (const uint8_t *)ts->text;
    const uint8_t *end = ptr + ts->text_len;

    while (ptr < end) {
        /* UTF-8 decode */
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
            cursor_x += ts->font_size * 0.5f;
            continue;
        }
        uint32_t glyph_index = gi_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res = font->ops->get_gpu_resource_set(font);
        if (YETTY_IS_ERR(rs_res)) {
            continue;
        }
        const struct yetty_ypaint_core_gpu_resource_set *rs = rs_res.value;
        if (rs->buffer_count == 0 || !rs->buffers[0].data) {
            continue;
        }

        /* Per-glyph metadata: 6 floats [size_x, size_y, bearing_x, bearing_y,
     * advance, _pad]. */
        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(rs->buffers[0].size / (6 * sizeof(float)));
        if (glyph_index >= meta_count) {
            cursor_x += ts->font_size * 0.5f;
            continue;
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

        /* Glyph SDF prim (7 words): type, z_order, x, y, font_size, packed, color */
        float glyph_data[YPAINT_GLYPH_WORDS];
        uint32_t tmp;
        tmp = YETTY_YSDF_GLYPH;
        memcpy(&glyph_data[0], &tmp, sizeof(float));
        tmp = glyph_z_order++;
        memcpy(&glyph_data[1], &tmp, sizeof(float));
        glyph_data[2] = gx;
        glyph_data[3] = gy;
        glyph_data[4] = ts->font_size;
        uint32_t packed_gf =
            (glyph_index & 0xFFFF) | (((uint32_t)(ts->font_id + 1) & 0xFFFF) << 16);
        memcpy(&glyph_data[5], &packed_gf, sizeof(float));
        memcpy(&glyph_data[6], &ts->color, sizeof(float));

        /* cursor_row is a screen-row offset from the viewport top
     * (rolling_row_0); convert to absolute canvas-line space. */
        uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
        float abs_y = gy + (float)cursor_canvas_line * canvas->cell_size.height;
        float abs_y_max = abs_y + gh;
        uint32_t glyph_row_max = (uint32_t)(abs_y_max / canvas->cell_size.height);

        canvas_ensure_lines(canvas, glyph_row_max + 1);

        uint32_t rolling_row = cursor_canvas_line;

        struct yetty_ypaint_canvas_grid_line *base_line =
            line_buffer_get(&canvas->lines, glyph_row_max);
        if (!base_line) {
            cursor_x += advance * scale;
            continue;
        }

        uint32_t prim_idx =
            prim_data_array_push(&base_line->prims, rolling_row, glyph_data, YPAINT_GLYPH_WORDS);

        uint32_t col_min =
            (canvas->cell_size.width > 0) ? (uint32_t)(gx / canvas->cell_size.width) : 0;
        uint32_t col_max =
            (canvas->cell_size.width > 0) ? (uint32_t)((gx + gw) / canvas->cell_size.width) : 0;
        uint32_t row_min = (uint32_t)(abs_y / canvas->cell_size.height);

        if (col_max >= canvas->grid_size.cols && canvas->grid_size.cols > 0) {
            col_max = canvas->grid_size.cols - 1;
        }

        for (uint32_t row = row_min; row <= glyph_row_max; row++) {
            struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, row);
            grid_line_ensure_cells(line, col_max + 1);
            uint16_t lines_ahead = (uint16_t)(glyph_row_max - row);
            for (uint32_t col = col_min; col <= col_max; col++) {
                struct yetty_ypaint_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_idx};
                prim_ref_array_push(&line->cells[col].refs, ref);
            }
        }

        if (glyph_row_max > glyph_max_row) {
            glyph_max_row = glyph_row_max;
        }

        cursor_x += advance * scale;
    }

    return YETTY_OK(uint32, glyph_max_row);
}

/* Attach `font` to the grid line at `glyph_max_row`. If the same font
 * was previously attached to a higher (older) line, migrate it down.
 * Skip when font is NULL or is the canvas's default font. */
static void attach_font_to_line(struct yetty_ypaint_canvas *canvas, struct yetty_ypaint_font *font,
                                int32_t font_id, uint32_t glyph_max_row, struct yetty_ypaint_font_map *fonts_map)
{
    if (!font || font == canvas->default_font || glyph_max_row == 0) {
        return;
    }
    struct yetty_ypaint_canvas_grid_line *target = line_buffer_get(&canvas->lines, glyph_max_row);
    if (!target) {
        return;
    }

    bool found = false;
    for (uint32_t li = 0; li < canvas->lines.count && !found; li++) {
        struct yetty_ypaint_canvas_grid_line *l = &canvas->lines.lines[li];
        for (uint32_t fi = 0; fi < l->font_count; fi++) {
            if (l->fonts[fi].font == font) {
                if (li != glyph_max_row) {
                    if (target->font_count >= target->font_capacity) {
                        uint32_t new_cap =
                            target->font_capacity == 0 ? 4 : target->font_capacity * 2;
                        target->fonts = realloc(
                            target->fonts, new_cap * sizeof(struct yetty_ypaint_canvas_font_entry));
                        target->font_capacity = new_cap;
                    }
                    target->fonts[target->font_count++] = l->fonts[fi];
                    l->fonts[fi] = l->fonts[--l->font_count];
                }
                found = true;
                break;
            }
        }
    }
    if (!found) {
        if (target->font_count >= target->font_capacity) {
            uint32_t new_cap = target->font_capacity == 0 ? 4 : target->font_capacity * 2;
            target->fonts =
                realloc(target->fonts, new_cap * sizeof(struct yetty_ypaint_canvas_font_entry));
            target->font_capacity = new_cap;
        }
        struct yetty_ypaint_canvas_font_entry entry = {0};
        entry.font = font;
        entry.font_id = font_id;
        target->fonts[target->font_count++] = entry;
        /* Ownership transferred to the line — drop from the local map so
     * the post-loop cleanup doesn't double-free. */
        if (font_id >= 0 && (uint32_t)font_id < fonts_map->capacity) {
            fonts_map->fonts[font_id] = NULL;
        }
    }
}

struct yetty_ycore_void_result yetty_ypaint_canvas_add_buffer(
    struct yetty_ypaint_canvas *canvas, struct yetty_ypaint_core_buffer *buffer)
{
    if (!canvas) {
        yerror("yetty_ypaint_canvas_add_buffer: canvas is NULL");
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!buffer) {
        yerror("yetty_ypaint_canvas_add_buffer: buffer is NULL");
        return YETTY_ERR(yetty_ycore_void, "buffer is NULL");
    }

    struct yetty_ypaint_core_primitive_iter_result iter_res =
        yetty_ypaint_core_buffer_prim_first(buffer, canvas->flyweight_registry);
    bool has_primitives = YETTY_IS_OK(iter_res);

    ydebug("add_buffer: START cursor_row=%u grid_rows=%u rolling_row_0=%u "
           "lines.count=%u has_prims=%d",
           canvas->cursor_row, canvas->grid_size.rows, canvas->rolling_row_0, canvas->lines.count,
           has_primitives);

    if (!has_primitives) {
        canvas->dirty = true;
        return YETTY_OK_VOID();
    }

    /* Place primitives at their natural canvas-line positions (relative to the
   * cursor's current canvas-line). The line buffer is append-only so prims
   * that fall past the visible viewport are still retained as scrollback;
   * the viewport is shifted afterwards by bumping rolling_row_0. */
    uint32_t initial_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
    uint32_t max_row_seen = initial_canvas_line;

    struct yetty_ypaint_font_map fonts_map;
    font_map_init(&fonts_map);

    struct yetty_ypaint_core_primitive_iter iter = iter_res.value;
    struct yetty_ycore_void_result final_status = YETTY_OK_VOID();

    while (1) {
        uint32_t prim_type = iter.fw.data[0];

        if (prim_type == YETTY_YPAINT_TYPE_FONT) {
            struct yetty_ypaint_core_font_prim_view fv;
            if (yetty_ypaint_core_font_prim_parse(iter.fw.data, &fv) == 0) {
                char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
                size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
                memcpy(hint, fv.name, hl);
                hint[hl] = '\0';

                struct yetty_font_font_result fr =
                    ypaint_canvas_materialize_blob_font(canvas, fv.ttf, fv.ttf_len, hint);
                if (YETTY_IS_ERR(fr)) {
                    /* Failure is non-fatal. PDFs frequently embed tiny subsetted
           * symbol fonts that msdf-gen rejects ("empty charset" etc.).
           * Skip the font; spans referencing this font_id will fall back
           * to the canvas default in the TEXT_SPAN branch below, so the
           * rest of the buffer still renders and the auto-scroll still
           * fires at the end. */
                    ywarn("add_buffer: font materialize failed (font_id=%d hint='%s'): "
                          "%s — skipping, spans will use default font",
                          fv.font_id, hint, fr.error.msg);
                } else if (fv.font_id >= 0) {
                    font_map_grow(&fonts_map, (uint32_t)fv.font_id + 1);
                    fonts_map.fonts[fv.font_id] = fr.value;
                } else {
                    /* Producer didn't tag — drop it. */
                    fr.value->ops->destroy(fr.value);
                }
            }
        } else if (prim_type == YETTY_YPAINT_TYPE_TEXT_SPAN) {
            struct yetty_ypaint_core_text_span_prim_view tv;
            if (yetty_ypaint_core_text_span_prim_parse(iter.fw.data, &tv) == 0) {
                struct yetty_ypaint_font *font = NULL;
                if (tv.font_id >= 0) {
                    font = font_map_get(&fonts_map, (uint32_t)tv.font_id);
                }
                if (!font && tv.font_id == -1) {
                    font = canvas->default_font;
                }
                if (!font) {
                    /* Fall back to the canvas default rather than failing the whole
           * buffer — keeps PDFs with unknown font_ids partially renderable. */
                    font = canvas->default_font;
                }
                if (font) {
                    struct uint32_result gmr_res =
                        expand_text_span_to_glyphs(canvas, &tv, font);
                    if (YETTY_IS_OK(gmr_res)) {
                        uint32_t glyph_max_row = gmr_res.value;
                        if (glyph_max_row > max_row_seen) {
                            max_row_seen = glyph_max_row;
                        }
                        attach_font_to_line(canvas, font, tv.font_id, glyph_max_row, &fonts_map);
                    } else {
                        yetty_ycore_error_destroy(gmr_res.error);
                    }
                }
            }
        } else {
            /* SDF or complex prim — uniform path. */
            struct uint32_result prim_res = add_primitive_internal(canvas, &iter);
            if (YETTY_IS_ERR(prim_res)) {
                yerror("add_buffer: add_primitive_internal failed: %s", prim_res.error.msg);
                final_status = YETTY_ERR(yetty_ycore_void, prim_res.error.msg);
                break;
            }
            if (prim_res.value > max_row_seen) {
                max_row_seen = prim_res.value;
            }
        }

        struct yetty_ypaint_core_primitive_iter_result nx =
            yetty_ypaint_core_buffer_prim_next(buffer, canvas->flyweight_registry, &iter);
        if (YETTY_IS_ERR(nx)) {
            break;
        }
        iter = nx.value;
    }

    /* Free fonts that didn't get attached to a grid line (typically fonts
   * referenced only by spans we couldn't render, or none at all). */
    for (uint32_t i = 0; i < fonts_map.capacity; i++) {
        if (fonts_map.fonts[i] && fonts_map.fonts[i]->ops) {
            fonts_map.fonts[i]->ops->destroy(fonts_map.fonts[i]);
        }
    }
    free(fonts_map.fonts);

    if (YETTY_IS_ERR(final_status)) {
        return final_status;
    }

    /* Scroll the viewport so the cursor lands on the last visible row.
   * `target_cursor_canvas_line` is one row below the last placed prim,
   * matching text-mode behavior where the cursor sits below the content
   * just emitted. Lines stay in canvas->lines (scrollback).
   *
   * Sparse-tail correction: PDFs often have a "back cover" canvas-line
   * that contains only a page number / footer-mark — using it as the
   * scroll anchor parks the viewport on near-empty rows above it (e.g.
   * optimizing_cpp.pdf: only 8/42 visible rows had any prim). Walk back
   * from max_row_seen past rows with fewer than MIN_DENSE_PRIMS prims
   * placed directly on them; use the first row that crosses the
   * threshold as the effective end-of-content for the scroll calc. The
   * cursor still tracks max_row_seen+1, which may end up below the
   * visible viewport — that's fine, it'll get pulled into view by the
   * next text-mode scroll on the following emit. */
    if (canvas->scrolling_mode) {
        const uint32_t MIN_DENSE_PRIMS = 10;
        uint32_t effective_max_row = max_row_seen;
        while (effective_max_row > initial_canvas_line) {
            struct yetty_ypaint_canvas_grid_line *l =
                line_buffer_get(&canvas->lines, effective_max_row);
            if (l && l->prims.count >= MIN_DENSE_PRIMS) {
                break;
            }
            effective_max_row--;
        }

        uint32_t target_cursor_canvas_line = max_row_seen + 1;
        uint32_t scroll_anchor_canvas_line = effective_max_row + 1;
        uint32_t viewport_bottom = canvas->rolling_row_0 + canvas->grid_size.rows - 1;

        ydebug("add_buffer: scroll_anchor=%u (effective_max=%u, max_row_seen=%u)",
               scroll_anchor_canvas_line, effective_max_row, max_row_seen);

        if (scroll_anchor_canvas_line > viewport_bottom) {
            uint32_t lines_to_scroll = scroll_anchor_canvas_line - viewport_bottom;

            if (!canvas->scroll_callback) {
                yerror("add_buffer: scroll_callback is NULL");
                return YETTY_ERR(yetty_ycore_void, "scroll_callback is NULL");
            }
            struct yetty_ycore_void_result scroll_res = canvas->scroll_callback(
                canvas->scroll_callback_user_data, (uint16_t)lines_to_scroll);
            if (YETTY_IS_ERR(scroll_res)) {
                return scroll_res;
            }
            yetty_ypaint_canvas_scroll_lines(canvas, (uint16_t)lines_to_scroll);
        }

        uint32_t cursor_screen_row = (target_cursor_canvas_line >= canvas->rolling_row_0)
                                         ? (target_cursor_canvas_line - canvas->rolling_row_0)
                                         : 0;
        if (cursor_screen_row >= canvas->grid_size.rows) {
            cursor_screen_row = canvas->grid_size.rows - 1;
        }
        canvas->cursor_row = (uint16_t)cursor_screen_row;

        if (canvas->cursor_set_callback) {
            canvas->cursor_set_callback(canvas->cursor_set_callback_user_data, canvas->cursor_row);
        }
    }

    ydebug("add_buffer: END cursor_row=%u rolling_row_0=%u lines.count=%u "
           "max_row_seen=%u",
           canvas->cursor_row, canvas->rolling_row_0, canvas->lines.count, max_row_seen);

    canvas->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Scrolling
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_scroll_lines(struct yetty_ypaint_canvas *canvas,
                                                                uint16_t num_lines)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (num_lines == 0) {
        return YETTY_OK_VOID();
    }

    /* Non-destructive scroll: lines stay in canvas->lines as scrollback.
   * rolling_row_0 advances to the canvas-line index of the new viewport
   * top; cursor_row is a screen-row, so it shifts up by num_lines. */
    canvas->rolling_row_0 += num_lines;
    if (canvas->cursor_row >= num_lines) {
        canvas->cursor_row -= num_lines;
    } else {
        canvas->cursor_row = 0;
    }

    ydebug("yetty_ypaint_canvas_scroll_lines: num_lines=%u lines.count=%u "
           "rolling_row_0=%u cursor_row=%u",
           num_lines, canvas->lines.count, canvas->rolling_row_0, canvas->cursor_row);

    canvas->dirty = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_scroll_callback(
    struct yetty_ypaint_canvas *canvas, yetty_ypaint_canvas_scroll_callback callback,
    struct yetty_ycore_void_result *user_data)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->scroll_callback = callback;
    canvas->scroll_callback_user_data = user_data;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_callback(
    struct yetty_ypaint_canvas *canvas, yetty_ypaint_canvas_cursor_set_callback callback,
    struct yetty_ycore_void_result *user_data)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->cursor_set_callback = callback;
    canvas->cursor_set_callback_user_data = user_data;
    return YETTY_OK_VOID();
}

//=============================================================================
// Packed GPU format
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_mark_dirty(struct yetty_ypaint_canvas *canvas)
{
    if (canvas) {
        canvas->dirty = true;
    }
    return YETTY_OK_VOID();
}

bool yetty_ypaint_canvas_is_dirty(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->dirty : false;
}

static struct yetty_ycore_void_result ensure_grid_staging(struct yetty_ypaint_canvas *canvas,
                                                          uint32_t min_size)
{
    if (min_size <= canvas->grid_staging_capacity) {
        return YETTY_OK_VOID();
    }

    uint32_t new_cap = canvas->grid_staging_capacity == 0 ? INITIAL_STAGING_CAPACITY
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

struct yetty_ycore_void_result yetty_ypaint_canvas_rebuild_grid(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->dirty && canvas->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }

    /* Prefix-sum of prim counts across ALL canvas lines. The GPU prim buffer
   * holds every prim (so off-screen scrollback prims can still be referenced
   * by visible cells via lines_ahead), and ref->prim_index is local to the
   * line that was appended to. */
    uint32_t total_prims = 0;
    uint32_t *line_base_prim_idx = NULL;
    if (canvas->lines.count > 0) {
        line_base_prim_idx = malloc(canvas->lines.count * sizeof(uint32_t));
        for (uint32_t i = 0; i < canvas->lines.count; i++) {
            line_base_prim_idx[i] = total_prims;
            struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
            total_prims += line->prims.count;
        }
    }

    /* Build a fixed-size GPU grid for the visible viewport only. The viewport
   * spans canvas-line indices [view_top .. view_top + grid_rows). In live
   * mode that's rolling_row_0; in scrollback view it's the override. */
    uint32_t grid_w = canvas->grid_size.cols;
    uint32_t grid_h = canvas->grid_size.rows;
    uint32_t window_top = canvas_effective_view_top(canvas);

    /* Cells beyond grid_size.cols can exist on lines that grew past the
   * default width; widen grid_w to accommodate the visible window's
   * widest line. Off-screen lines don't influence grid_w because the
   * shader never indexes those columns. */
    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        if (canvas_y >= canvas->lines.count) {
            break;
        }
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, canvas_y);
        if (line->cell_count > grid_w) {
            grid_w = line->cell_count;
        }
    }

    if (grid_w == 0 || grid_h == 0) {
        canvas->grid_staging_count = 0;
        canvas->dirty = false;
        free(line_base_prim_idx);
        return YETTY_OK_VOID();
    }

    uint32_t num_cells = grid_w * grid_h;

    ensure_grid_staging(canvas, num_cells * 4);
    canvas->grid_staging_count = num_cells;

    uint32_t cells_with_prims = 0;
    uint32_t total_refs_in_window = 0;
    uint32_t max_refs_in_one_cell = 0;
    uint32_t lines_with_prims_in_window = 0;
    /* Per-row detail kept on the stack — grid_h is small (≤ ~256). */
    uint32_t row_ref_counts[256] = {0};
    uint32_t row_line_prims[256] = {0};

    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        bool has_line = canvas_y < canvas->lines.count;
        struct yetty_ypaint_canvas_grid_line *line =
            has_line ? line_buffer_get(&canvas->lines, canvas_y) : NULL;
        uint32_t line_cell_count = line ? line->cell_count : 0;
        uint32_t row_refs = 0;
        if (line && gpu_y < 256) {
            row_line_prims[gpu_y] = line->prims.count;
        }

        for (uint32_t x = 0; x < grid_w; x++) {
            uint32_t cell_idx = gpu_y * grid_w + x;

            ensure_grid_staging(canvas, canvas->grid_staging_count + 2);
            canvas->grid_staging[cell_idx] = canvas->grid_staging_count;

            uint32_t count_pos = canvas->grid_staging_count++;
            ensure_grid_staging(canvas, canvas->grid_staging_count + 1);
            canvas->grid_staging[count_pos] = 0;
            uint32_t count = 0;

            if (has_line && x < line_cell_count) {
                struct yetty_ypaint_canvas_grid_cell *cell = &line->cells[x];
                for (uint32_t ri = 0; ri < cell->refs.count; ri++) {
                    struct yetty_ypaint_canvas_prim_ref *ref = &cell->refs.data[ri];
                    /* lines_ahead is in canvas-line space, so bl is the canvas-line
           * of the prim's anchor — not a GPU row index. */
                    uint32_t bl = canvas_y + ref->lines_ahead;
                    if (bl < canvas->lines.count && line_base_prim_idx) {
                        ensure_grid_staging(canvas, canvas->grid_staging_count + 1);
                        canvas->grid_staging[canvas->grid_staging_count++] =
                            line_base_prim_idx[bl] + ref->prim_index;
                        count++;
                    }
                }
            }

            canvas->grid_staging[count_pos] = count;
            if (count > 0) {
                cells_with_prims++;
            }
            if (count > max_refs_in_one_cell) {
                max_refs_in_one_cell = count;
            }
            row_refs += count;
        }

        total_refs_in_window += row_refs;
        if (row_refs > 0) {
            lines_with_prims_in_window++;
        }
        if (gpu_y < 256) {
            row_ref_counts[gpu_y] = row_refs;
        }
    }

    ydebug("rebuild_grid: window=[%u..%u] grid=%ux%u cells_with_prims=%u/%u "
           "lines_with_prims=%u total_refs=%u max_refs/cell=%u",
           window_top, window_top + grid_h - 1, grid_w, grid_h, cells_with_prims, grid_w * grid_h,
           lines_with_prims_in_window, total_refs_in_window, max_refs_in_one_cell);
    /* Per-row breakdown so we can see which canvas-lines have prims and
   * which rows of the screen end up blank. */
    for (uint32_t gpu_y = 0; gpu_y < grid_h && gpu_y < 256; gpu_y++) {
        if (row_ref_counts[gpu_y] > 0 || row_line_prims[gpu_y] > 0) {
            ydebug("rebuild_grid:   gpu_y=%2u canvas_y=%u line.prims=%u refs=%u", gpu_y,
                   window_top + gpu_y, row_line_prims[gpu_y], row_ref_counts[gpu_y]);
        }
    }

    free(line_base_prim_idx);
    canvas->dirty = false;
    return YETTY_OK_VOID();
}

const uint32_t *yetty_ypaint_canvas_grid_data(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->grid_staging : NULL;
}

uint32_t yetty_ypaint_canvas_grid_word_count(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->grid_staging_count : 0;
}

struct yetty_ycore_void_result yetty_ypaint_canvas_clear_staging(struct yetty_ypaint_canvas *canvas)
{
    if (canvas) {
        canvas->grid_staging_count = 0;
        canvas->prim_staging_count = 0;
    }
    return YETTY_OK_VOID();
}

//=============================================================================
// Primitive staging
//=============================================================================

static struct yetty_ycore_void_result ensure_prim_staging(struct yetty_ypaint_canvas *canvas,
                                                          uint32_t min_size)
{
    if (min_size <= canvas->prim_staging_capacity) {
        return YETTY_OK_VOID();
    }

    uint32_t new_cap = canvas->prim_staging_capacity == 0 ? INITIAL_STAGING_CAPACITY
                                                          : canvas->prim_staging_capacity;
    while (new_cap < min_size) {
        new_cap *= 2;
    }

    canvas->prim_staging = realloc(canvas->prim_staging, new_cap * sizeof(uint32_t));
    canvas->prim_staging_capacity = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ypaint_prim_staging_result yetty_ypaint_canvas_build_prim_staging(
    struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ypaint_prim_staging,
                         "yetty_ypaint_canvas_build_prim_staging: NULL canvas");
    }

    // Count primitives and total words (+1 per prim for rolling_row)
    uint32_t prim_count = 0;
    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            prim_count++;
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }

    if (prim_count == 0) {
        canvas->prim_staging_count = 0;
        struct yetty_ypaint_prim_staging empty = {.data = NULL, .word_count = 0};
        return YETTY_OK(yetty_ypaint_prim_staging, empty);
    }

    // Layout: [prim0_offset, prim1_offset, ...][rolling_row0,
    // prim0_data...][rolling_row1, prim1_data...]
    uint32_t total_size = prim_count + total_words;
    struct yetty_ycore_void_result eps = ensure_prim_staging(canvas, total_size);
    YETTY_RETURN_IF_ERR(yetty_ypaint_prim_staging, eps,
                        "yetty_ypaint_canvas_build_prim_staging: ensure_prim_staging failed");

    uint32_t data_offset = 0;
    uint32_t prim_idx = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);

        for (uint32_t p = 0; p < line->prims.count; p++) {
            struct yetty_ypaint_canvas_prim_data *prim = &line->prims.data[p];
            canvas->prim_staging[prim_idx] = data_offset;

            // Prepend rolling_row at insertion (for shader y_offset calculation)
            canvas->prim_staging[prim_count + data_offset] = prim->rolling_row;

            // Copy primitive data
            for (uint32_t w = 0; w < prim->word_count; w++) {
                uint32_t val;
                memcpy(&val, &prim->data[w], sizeof(uint32_t));
                canvas->prim_staging[prim_count + data_offset + 1 + w] = val;
            }

            data_offset += prim->word_count + 1; // +1 for rolling_row
            prim_idx++;
        }
    }

    canvas->prim_staging_count = total_size;
    struct yetty_ypaint_prim_staging out = {.data = canvas->prim_staging,
                                            .word_count = total_size};
    return YETTY_OK(yetty_ypaint_prim_staging, out);
}

uint32_t yetty_ypaint_canvas_prim_gpu_size(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }
    return total_words * sizeof(float);
}

//=============================================================================
// State management
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_clear(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    struct yetty_ycore_void_result res =
        line_buffer_free(&canvas->lines, canvas->flyweight_registry);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    line_buffer_init(&canvas->lines);

    canvas->grid_staging_count = 0;
    canvas->prim_staging_count = 0;
    canvas->cursor_col = 0;
    canvas->cursor_row = 0;
    canvas->rolling_row_0 = 0;
    canvas->view_top_override_active = false;
    canvas->view_top_override = 0;
    canvas->dirty = true;
    return YETTY_OK_VOID();
}

bool yetty_ypaint_canvas_empty(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return true;
    }

    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        if (line->prims.count > 0) {
            return false;
        }
    }
    return true;
}

uint32_t yetty_ypaint_canvas_primitive_count(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        count += line->prims.count;
    }
    return count;
}

struct yetty_ypaint_font *yetty_ypaint_canvas_get_default_font(struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->default_font : NULL;
}

//=============================================================================
// Complex primitive access (for atlas rendering)
//=============================================================================

/* Visible window in canvas-line space:
 *   [rolling_row_0 .. rolling_row_0 + grid_size.rows). */
static void canvas_visible_window(const struct yetty_ypaint_canvas *canvas, uint32_t *out_top,
                                  uint32_t *out_end)
{
    uint32_t top = canvas_effective_view_top(canvas);
    uint32_t end = top + canvas->grid_size.rows;
    if (end > canvas->lines.count) {
        end = canvas->lines.count;
    }
    if (top > end) {
        top = end;
    }
    *out_top = top;
    *out_end = end;
}

uint32_t yetty_ypaint_canvas_complex_prim_count(struct yetty_ypaint_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t count = 0;
    for (uint32_t i = top; i < end; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        count += line->complex_prim_count;
    }
    return count;
}

struct yetty_ypaint_core_complex_prim_instance *yetty_ypaint_canvas_get_complex_prim(
    struct yetty_ypaint_canvas *canvas, uint32_t index)
{
    if (!canvas) {
        return NULL;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t current = 0;
    for (uint32_t i = top; i < end; i++) {
        struct yetty_ypaint_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        if (index < current + line->complex_prim_count) {
            uint32_t local_idx = index - current;
            return line->complex_prims[local_idx];
        }
        current += line->complex_prim_count;
    }
    return NULL;
}

const struct yetty_ypaint_core_flyweight_registry *yetty_ypaint_canvas_get_flyweight_registry(
    struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->flyweight_registry : NULL;
}

struct yetty_ypaint_core_complex_prim_factory *yetty_ypaint_canvas_get_complex_prim_factory(
    struct yetty_ypaint_canvas *canvas)
{
    return canvas ? canvas->complex_prim_factory : NULL;
}
