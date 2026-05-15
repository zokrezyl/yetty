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
#include <yetty/ydraw-core/buffer.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/ydraw-factory/complex-prim-factory.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw/flyweight.h>
#include <yetty/ydraw/core/ydraw-canvas.h>
#include <yetty/ydraw/scrollbuffer.h>
#include "canvas-internal.h"
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/yfont/raster-font.h>
#if YETTY_HAS_YMSDF_GEN
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ymsdf/generator.h>
#endif
#include <yetty/ysdf/types.gen.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yimage/yimage-gen.h>
#if YETTY_HAS_YMESH
#include <yetty/ymesh/ymesh-gen.h>
#endif
#include <yetty/ytrace/ytrace.h>

#include <yetty/yplatform/paths.h>

/* Glyph primitive type (not in ysdf types.gen.h since not SDF) */
#define YETTY_YSDF_GLYPH 200

/* Glyph primitive: type, z_order, x, y, font_size, packed(glyph_idx|font_id), color */
#define YDRAW_GLYPH_WORDS 7

//=============================================================================
// Internal data structures
//=============================================================================

// Reference to a primitive in another line
struct yetty_ydraw_canvas_prim_ref {
    uint16_t lines_ahead; // relative offset to base line (0 = same line)
    uint16_t prim_index;  // index within base line's prims array
};

// Dynamic array of prim_ref
struct yetty_ydraw_canvas_prim_ref_array {
    struct yetty_ydraw_canvas_prim_ref *data;
    uint32_t count;
    uint32_t capacity;
};

// A single grid cell
struct yetty_ydraw_canvas_grid_cell {
    struct yetty_ydraw_canvas_prim_ref_array refs;
};

// A single primitive's payload, stored in its owning line's arena. The
// data lives at `grid_line.arena + arena_offset` for `word_count` u32
// words. Storing an offset (not a pointer) keeps prim_data stable
// across arena reallocations.
struct yetty_ydraw_canvas_prim_data {
    uint32_t rolling_row; // rolling_row at insertion (cursor row or explicit)
    uint32_t arena_offset;
    uint32_t word_count;
};

// Dynamic array of prim_data
struct yetty_ydraw_canvas_prim_data_array {
    struct yetty_ydraw_canvas_prim_data *data;
    uint32_t count;
    uint32_t capacity;
};

// Font resource attached to a grid line — refcounted handle into the
// canvas's font cache. The grid_line owns one cache ref per entry, released
// at grid_line_free.
struct yetty_ydraw_canvas_font_entry {
    yetty_yfont_cache_handle handle;
};

// Complex primitive stored on last overlapping line - uses factory instance
// (replaces old canvas-specific struct with factory instance pointer)

// A single row/line in the grid
struct yetty_ydraw_canvas_grid_line {
    struct yetty_ydraw_canvas_prim_data_array
        prims; // All primitives (SDF + glyph) whose BASE is this line

    // Payload arena: every prim in `prims` stores its words at
    // `arena + prim.arena_offset` for prim.word_count u32 words. One
    // allocation per line, doubling growth — replaces the previous
    // per-prim malloc(word_count * 4) and its attendant ~24 bytes of
    // glibc bookkeeping per primitive.
    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;

    struct yetty_ydraw_canvas_grid_cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;

    // Font resources owned by this line (moved down as needed)
    struct yetty_ydraw_canvas_font_entry *fonts;
    uint32_t font_count;
    uint32_t font_capacity;

    // Complex primitives whose BASE (last overlapping line) is this line
    // Uses factory instances instead of canvas-specific struct
    struct yetty_ydraw_core_complex_prim_instance **complex_prims;
    uint32_t complex_prim_count;
    uint32_t complex_prim_capacity;
};

// Simple line array
struct yetty_ydraw_canvas_line_buffer {
    struct yetty_ydraw_canvas_grid_line *lines;
    uint32_t capacity;
    uint32_t count;
    int32_t base_index; /* logical canvas-line index of lines[0]; 0 initially */
};

// Canvas structure
struct yetty_ydraw_canvas {
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
    struct yetty_ydraw_canvas_line_buffer lines;

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
    yetty_ydraw_canvas_scroll_callback scroll_callback;
    struct yetty_ycore_void_result *scroll_callback_user_data;

    // Cursor set callback (when cursor moves without scroll)
    yetty_ydraw_canvas_cursor_set_callback cursor_set_callback;
    struct yetty_ycore_void_result *cursor_set_callback_user_data;

    // Default font for text spans with font_id = -1. Owned via default_handle
    // (a long-lived ref on the cache); cleared at canvas_destroy.
    struct yetty_ydraw_font *default_font;
    yetty_yfont_cache_handle default_handle;

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
    struct yetty_ydraw_core_flyweight_registry *flyweight_registry;

    // Factory for complex primitive ops (yplot, yimage, etc.)
    struct yetty_ydraw_core_complex_prim_factory *complex_prim_factory;

    // Polymorphic MSDF CDB generator (cpu | gpu) — borrowed from
    // context->gpu_context. Used for on-demand CDB generation when a PDF
    // (or any producer) sends a font blob whose CDB isn't cached yet.
    // NULL if the host hasn't created one (e.g. tests that bypass yetty_create).
    struct yetty_ymsdf_generator *msdf_generator;

    // Refcounted MSDF font pool. Owns every font this canvas materialises.
    // Lines hold cache handles via grid_line.fonts[]; refcount tracks total
    // line references across scrollback + viewport. Slot 0 is the default
    // font (first install). Created in canvas_create, destroyed last in
    // canvas_destroy.
    struct yetty_yfont_cache *font_cache;

    // Scrollbuffer: lines whose absolute canvas-row index has fallen
    // below the live viewport are serialised to a compact binary form
    // here, and their expanded grid_line content (prims/arena/cells) is
    // freed. `sb_offsets[i]` is the byte offset of line `i`'s record
    // in the scrollbuffer, or SB_OFFSET_UNSET if the line still lives
    // in canvas->lines.lines[i] in expanded form. Deserialise-on-
    // scrollback is a later step; today, freed lines are gone for
    // rendering purposes until reloaded.
    struct yetty_ydraw_scrollbuffer scrollbuffer;
    uint32_t *sb_offsets;
    uint32_t  sb_offsets_count;
    uint32_t  sb_offsets_capacity;
};

#define SB_OFFSET_UNSET 0xFFFFFFFFu

/* Forward decls — bodies live next to the scrollbuffer machinery; the
 * mutation paths above call into them. canvas_dirty_line restores any
 * previously-evicted content into the expanded form before clearing
 * sb_offsets so the line's history is preserved across re-mutation. */
static void canvas_dirty_line(struct yetty_ydraw_canvas *canvas, uint32_t idx);
static struct yetty_ycore_void_result canvas_restore_line(struct yetty_ydraw_canvas *canvas,
                                                          uint32_t idx);

#define DEFAULT_MAX_PRIMS_PER_CELL 16
#define INITIAL_LINE_CAPACITY 64
/* Initial cell-array capacity for a *touched* line. Tuned for sparse
 * lines (e.g. PDF lines that paint only a handful of columns); the
 * doubling growth in grid_line_ensure_cells keeps wide lines cheap too. */
#define INITIAL_CELL_CAPACITY 16
#define INITIAL_PRIM_CAPACITY 16
/* Each touched cell holds at least one prim_ref; most hold only one. */
#define INITIAL_REF_CAPACITY 2
#define INITIAL_STAGING_CAPACITY 4096

//=============================================================================
// Helper: Dynamic arrays
//=============================================================================

static void prim_ref_array_init(struct yetty_ydraw_canvas_prim_ref_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_ref_array_free(struct yetty_ydraw_canvas_prim_ref_array *arr)
{
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_ref_array_push(struct yetty_ydraw_canvas_prim_ref_array *arr,
                                struct yetty_ydraw_canvas_prim_ref ref)
{
    if (arr->count >= arr->capacity) {
        uint32_t new_cap = arr->capacity == 0 ? INITIAL_REF_CAPACITY : arr->capacity * 2;
        arr->data = realloc(arr->data, new_cap * sizeof(struct yetty_ydraw_canvas_prim_ref));
        arr->capacity = new_cap;
    }
    arr->data[arr->count++] = ref;
}

static void prim_data_array_init(struct yetty_ydraw_canvas_prim_data_array *arr)
{
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static void prim_data_array_free(struct yetty_ydraw_canvas_prim_data_array *arr)
{
    /* Payloads live in the owning line's arena (freed in grid_line_free);
     * only the index records need releasing here. */
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

/* Append `word_count` words of payload to `line->arena`, growing the arena
 * (doubling) if needed. Returns the offset where the payload was written
 * — caller stores it in the matching prim_data record. */
static struct yetty_ycore_void_result grid_line_arena_append(
    struct yetty_ydraw_canvas_grid_line *line, const float *data, uint32_t word_count,
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
    /* Source is the iter's raw word stream (already in the layout the
     * GPU expects); copy by bytes to stay endian/alignment-agnostic. */
    memcpy(line->arena + line->arena_count, data, word_count * sizeof(uint32_t));
    line->arena_count += word_count;
    return YETTY_OK_VOID();
}

/* Append a primitive to `line`. On success returns the prim index within
 * line->prims. On allocation failure returns UINT32_MAX. */
static uint32_t grid_line_push_prim(struct yetty_ydraw_canvas_grid_line *line,
                                    uint32_t rolling_row, const float *data, uint32_t word_count)
{
    struct yetty_ydraw_canvas_prim_data_array *arr = &line->prims;
    if (arr->count >= arr->capacity) {
        uint32_t new_cap = arr->capacity == 0 ? INITIAL_PRIM_CAPACITY : arr->capacity * 2;
        struct yetty_ydraw_canvas_prim_data *grown =
            realloc(arr->data, new_cap * sizeof(struct yetty_ydraw_canvas_prim_data));
        if (!grown) {
            return UINT32_MAX;
        }
        arr->data = grown;
        arr->capacity = new_cap;
    }
    uint32_t offset = 0;
    struct yetty_ycore_void_result ar =
        grid_line_arena_append(line, data, word_count, &offset);
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

//=============================================================================
// Helper: grid_line
//=============================================================================

static struct yetty_ycore_void_result grid_line_init(struct yetty_ydraw_canvas_grid_line *line,
                                                     uint32_t initial_cells)
{
    /* Cells are now allocated lazily by grid_line_ensure_cells on first
     * touch. Lines with no primitives (common in PDFs — empty space
     * between paragraphs) stay at zero cell-array bytes. initial_cells
     * kept in the signature for source compatibility with the line
     * buffer caller; ignored. */
    (void)initial_cells;
    prim_data_array_init(&line->prims);
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

static struct yetty_ycore_void_result grid_line_free(
    struct yetty_ydraw_canvas_grid_line *line,
    const struct yetty_ydraw_core_flyweight_registry *reg, struct yetty_yfont_cache *cache)
{
    if (!reg) {
        return YETTY_ERR(yetty_ycore_void, "reg is NULL");
    }

    prim_data_array_free(&line->prims);
    /* Payload arena: one allocation for all prims on this line. */
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
    /* Destroy complex prim instances owned by this line */
    for (uint32_t i = 0; i < line->complex_prim_count; i++) {
        yetty_ydraw_core_complex_prim_instance_destroy(line->complex_prims[i]);
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
    struct yetty_ydraw_canvas_grid_line *line, uint32_t min_cells)
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

    struct yetty_ydraw_canvas_grid_cell *new_cells =
        realloc(line->cells, new_cap * sizeof(struct yetty_ydraw_canvas_grid_cell));
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

static void line_buffer_init(struct yetty_ydraw_canvas_line_buffer *buf)
{
    buf->lines = NULL;
    buf->capacity = 0;
    buf->count = 0;
}

static struct yetty_ycore_void_result line_buffer_free(
    struct yetty_ydraw_canvas_line_buffer *buf,
    const struct yetty_ydraw_core_flyweight_registry *reg, struct yetty_yfont_cache *cache)
{
    for (uint32_t i = 0; i < buf->count; i++) {
        struct yetty_ycore_void_result res = grid_line_free(&buf->lines[i], reg, cache);
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

static struct yetty_ydraw_canvas_grid_line *line_buffer_get(
    struct yetty_ydraw_canvas_line_buffer *buf, uint32_t index)
{
    if (index >= buf->count) {
        return NULL;
    }
    return &buf->lines[index];
}

static struct yetty_ycore_void_result canvas_ensure_lines(struct yetty_ydraw_canvas *canvas,
                                                          uint32_t min_count)
{
    struct yetty_ydraw_canvas_line_buffer *buf = &canvas->lines;

    // Grow capacity if needed
    if (min_count > buf->capacity) {
        uint32_t new_cap = buf->capacity == 0 ? INITIAL_LINE_CAPACITY : buf->capacity;
        while (new_cap < min_count) {
            new_cap *= 2;
        }

        struct yetty_ydraw_canvas_grid_line *new_lines =
            realloc(buf->lines, new_cap * sizeof(struct yetty_ydraw_canvas_grid_line));
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

/* FNV-1a 64-bit hash — content-addressing for on-disk PDF blob CDB cache. */
static uint64_t fnv1a64(const uint8_t *data, size_t len)
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
static void sanitize_identifier(char *dst, size_t dst_cap, const char *src)
{
    size_t ni = 0;
    for (const char *s = src; *s && ni + 1 < dst_cap; s++) {
        char c = *s;
        dst[ni++] = (c == '-' || c == ' ' || c == '.') ? '_' : c;
    }
    dst[ni] = '\0';
}

/* Resolve the default font through the cache. For the MSDF render method
 * the CDB is expected to live next to the configured fonts dir; for raster
 * the legacy code path applied — for now we only support MSDF and surface
 * an error otherwise. The first cache miss installs at slot 0 (this is the
 * very first get_font call on a fresh cache), which is what the binder /
 * shader dispatcher expect. */
static struct yetty_yfont_cache_ref_result ydraw_canvas_get_default_font_ref(
    struct yetty_ydraw_canvas *canvas)
{
    if (canvas->font_render_method == 1) {
        return YETTY_ERR(yetty_yfont_cache_ref,
                         "raster default font is not yet wired through the font cache");
    }
    char cdb_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", canvas->fonts_dir,
             canvas->font_family);
    char ns[128];
    sanitize_identifier(ns, sizeof(ns), canvas->font_family);
    ydebug("ydraw_canvas: default msdf font cdb='%s' key='%s'", cdb_path, ns);
    return yetty_yfont_cache_get_font(canvas->font_cache, ns, cdb_path);
}

/* Materialise a buffer-supplied FONT prim through the font cache.
 *
 * Steps:
 *   1. Hash the TTF bytes -> hex (content key) and write `*out_hex`.
 *   2. If the .cdb file isn't on disk yet, write the TTF (if missing)
 *      and run the MSDF generator.
 *
 * Crucially this does NOT touch the in-memory font cache. The FONT
 * primitive only declares "this hash is available on disk"; the cache
 * is populated lazily by the first TEXT_SPAN that actually references
 * the font_id (see canvas_resolve_blob_font_handle). PDF over-declares
 * fonts per page — a font that never produces a glyph never produces
 * a cache slot. */
static struct yetty_ycore_void_result ydraw_canvas_ensure_blob_font_cdb(
    struct yetty_ydraw_canvas *canvas, const uint8_t *ttf, uint32_t ttf_len,
    const char *hint_name, char out_hex[17])
{
    if (!ttf || ttf_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "blob is empty");
    }
    if (blob_is_raster(hint_name, canvas->font_render_method)) {
        return YETTY_ERR(yetty_ycore_void,
                         "raster blob fonts are not yet wired through the font cache");
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

    /* Hot path: CDB already on disk from a previous run / earlier
     * envelope. Nothing else to do — bytes of `ttf` are discarded. */
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
        ydebug("ydraw_canvas: cached TTF '%s' (%u bytes) hint='%s'", ttf_path, ttf_len,
               hint_name ? hint_name : "");
    }

#if YETTY_HAS_YMSDF_GEN
    if (!canvas->msdf_generator) {
        yerror("ydraw_canvas: CDB '%s' missing and no MSDF generator on "
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
    ydebug("ydraw_canvas: generated CDB '%s' via %s generator", cdb_path,
           canvas->msdf_generator->ops->name(canvas->msdf_generator));
    return YETTY_OK_VOID();
#else
    return YETTY_ERR(yetty_ycore_void, "ymsdf-gen disabled in this build");
#endif
}

/* Lazy cache lookup keyed by the FONT-primitive's content hash. Builds
 * the cdb path from the canvas's cache dir + hex; cache.get_font then
 * hits if any prior envelope (or line still alive in scrollback)
 * caused a slot to exist, or constructs from the CDB on disk on a
 * fresh miss. Returns a buffer-scoped ref the caller is responsible
 * for releasing (via font_map_release_all). */
static struct yetty_yfont_cache_ref_result canvas_resolve_blob_font_handle(
    struct yetty_ydraw_canvas *canvas, const char *hex)
{
    const char *cache_dir = yetty_yplatform_get_cache_dir();
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_yfont_cache_ref, "no cache dir");
    }
    char cdb_path[1024];
    snprintf(cdb_path, sizeof(cdb_path), "%s/ydraw-fonts/pdf_%s.cdb", cache_dir, hex);
    return yetty_yfont_cache_get_font(canvas->font_cache, hex, cdb_path);
}

//=============================================================================
// Canvas implementation
//=============================================================================

struct yetty_ydraw_canvas_ptr_result yetty_ydraw_canvas_create(
    bool scrolling_mode, const struct yetty_context *context)
{
    struct yetty_ydraw_canvas *canvas;

    if (!context) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "context is NULL");
    }

    canvas = calloc(1, sizeof(struct yetty_ydraw_canvas));
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "canvas alloc failed");
    }

    canvas->scrolling_mode = scrolling_mode;
    canvas->dirty = true;
    canvas->rolling_row_0 = 0;

    line_buffer_init(&canvas->lines);
    yetty_ydraw_scrollbuffer_init(&canvas->scrollbuffer);
    canvas->sb_offsets = NULL;
    canvas->sb_offsets_count = 0;
    canvas->sb_offsets_capacity = 0;

    /* Create flyweight registry with all handlers (for SDF prims) */
    struct yetty_ydraw_core_flyweight_registry_ptr_result fw_res = yetty_ydraw_flyweight_create();
    if (YETTY_IS_ERR(fw_res)) {
        yerror("ydraw_canvas: flyweight creation failed: %s", fw_res.error.msg);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: flyweight creation failed",
                         fw_res);
    }
    canvas->flyweight_registry = fw_res.value;

    /* Create complex prim factory and register types */
    struct yetty_ydraw_core_complex_prim_factory_ptr_result factory_res =
        yetty_ydraw_core_complex_prim_factory_create(
            context->gpu_context.device, context->gpu_context.queue,
            context->gpu_context.surface_format, context->gpu_context.allocator);
    if (YETTY_IS_ERR(factory_res)) {
        yerror("ydraw_canvas: factory creation failed: %s", factory_res.error.msg);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: factory creation failed",
                         factory_res);
    }
    canvas->complex_prim_factory = factory_res.value;

    /* Create and register yplot factory */
    struct yetty_ydraw_core_concrete_factory *yetty_yplot_factory = yetty_yplot_factory_create();
    if (!yetty_yplot_factory) {
        yerror("ydraw_canvas: yplot factory creation failed");
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "yplot factory creation failed");
    }
    struct yetty_ycore_void_result yplot_reg_res = yetty_ydraw_core_complex_prim_factory_register(
        canvas->complex_prim_factory, yetty_yplot_factory);
    if (YETTY_IS_ERR(yplot_reg_res)) {
        yerror("ydraw_canvas: yplot registration failed: %s", yplot_reg_res.error.msg);
        yetty_yplot_factory_destroy(yetty_yplot_factory);
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: yplot registration failed",
                         yplot_reg_res);
    }

    /* Create and register yimage factory */
    struct yetty_ydraw_core_concrete_factory *yetty_yimage_factory = yetty_yimage_factory_create();
    if (!yetty_yimage_factory) {
        yerror("ydraw_canvas: yimage factory creation failed");
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "yimage factory creation failed");
    }
    struct yetty_ycore_void_result yimage_reg_res = yetty_ydraw_core_complex_prim_factory_register(
        canvas->complex_prim_factory, yetty_yimage_factory);
    if (YETTY_IS_ERR(yimage_reg_res)) {
        yerror("ydraw_canvas: yimage registration failed: %s", yimage_reg_res.error.msg);
        yetty_yimage_factory_destroy(yetty_yimage_factory);
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: yimage registration failed",
                         yimage_reg_res);
    }

#if YETTY_HAS_YMESH
    /* Create and register ymesh factory (3D glTF mesh primitive). */
    struct yetty_ydraw_core_concrete_factory *yetty_ymesh_factory = yetty_ymesh_factory_create();
    if (!yetty_ymesh_factory) {
        yerror("ydraw_canvas: ymesh factory creation failed");
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ymesh factory creation failed");
    }
    struct yetty_ycore_void_result ymesh_reg_res = yetty_ydraw_core_complex_prim_factory_register(
        canvas->complex_prim_factory, yetty_ymesh_factory);
    if (YETTY_IS_ERR(ymesh_reg_res)) {
        yerror("ydraw_canvas: ymesh registration failed: %s", ymesh_reg_res.error.msg);
        yetty_ymesh_factory_destroy(yetty_ymesh_factory);
        yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: ymesh registration failed",
                         ymesh_reg_res);
    }
#endif

    /* Create default font for text spans (font_id = -1).
   * Backend (MSDF vs raster) is selected via ydraw/font/render-method.
   * Default is "msdf" to preserve existing rendering. */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = config->ops->font_family(config);
    if (!font_family || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }
    const char *render_method =
        config->ops->get_string(config, "ydraw/font/render-method", "msdf");

    strncpy(canvas->shaders_dir, shaders_dir, sizeof(canvas->shaders_dir) - 1);
    strncpy(canvas->fonts_dir, fonts_dir, sizeof(canvas->fonts_dir) - 1);
    strncpy(canvas->font_family, font_family, sizeof(canvas->font_family) - 1);
    canvas->font_render_method = (strcmp(render_method, "raster") == 0) ? 1 : 0;
    canvas->raster_base_size = 32.0f;
    canvas->msdf_generator = context->gpu_context.msdf_generator;

    ydebug("ydraw_canvas: font render_method='%s'", render_method);

    /* Create the per-canvas font cache. yetty_yfont_cache_create takes the
     * shaders_dir directly (not a yetty_context) so yetty_yfont_core stays
     * GPU-less — the full yetty_context drags a WGPU app context with it. */
    struct yetty_yconfig_config *cache_cfg = context->app_context.config;
    const char *cache_shaders_dir =
        cache_cfg ? cache_cfg->ops->get_string(cache_cfg, "paths/shaders", "") : "";
    struct yetty_yfont_cache_ptr_result cache_res =
        yetty_yfont_cache_create(cache_shaders_dir);
    if (YETTY_IS_ERR(cache_res)) {
        yerror("ydraw_canvas: font cache creation failed: %s", cache_res.error.msg);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: font cache creation failed",
                         cache_res);
    }
    canvas->font_cache = cache_res.value;
    canvas->default_handle = YETTY_YFONT_CACHE_HANDLE_INVALID;

    /* Default font installs at slot 0 (first cache_get_font on a fresh cache).
     * Producer font_id == -1 routes through default_font/default_handle; the
     * binder attaches cache slots in handle order so slot 0 lines up with the
     * shader dispatcher's "default" branch. */
    struct yetty_yfont_cache_ref_result def_res = ydraw_canvas_get_default_font_ref(canvas);
    if (YETTY_IS_OK(def_res)) {
        canvas->default_font = def_res.value.font;
        canvas->default_handle = def_res.value.handle;
        ydebug("ydraw_canvas: default font installed at handle=%u", canvas->default_handle);
    } else {
        yerror("ydraw_canvas: default font creation failed: %s", def_res.error.msg);
        yetty_yfont_cache_destroy(canvas->font_cache);
        yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
        free(canvas->lines.lines);
        free(canvas);
        return YETTY_ERR(yetty_ydraw_canvas_ptr, "ydraw_canvas: default font creation failed",
                         def_res);
    }

    return YETTY_OK(yetty_ydraw_canvas_ptr, canvas);
}

struct yetty_ycore_void_result yetty_ydraw_canvas_destroy(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    /* Free every line first — each line releases its cache refs. After
     * this only the canvas's long-held default ref keeps the default font
     * alive in the cache. */
    struct yetty_ycore_void_result res =
        line_buffer_free(&canvas->lines, canvas->flyweight_registry, canvas->font_cache);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    /* Drop the canvas's default ref, then destroy the cache (which frees
     * any remaining entries — there should be none if every line released
     * its handles correctly). */
    if (canvas->default_handle != YETTY_YFONT_CACHE_HANDLE_INVALID) {
        yetty_yfont_cache_release_font(canvas->font_cache, canvas->default_handle);
    }
    yetty_yfont_cache_destroy(canvas->font_cache);
    yetty_ydraw_core_complex_prim_factory_destroy(canvas->complex_prim_factory);
    yetty_ydraw_core_flyweight_registry_destroy(canvas->flyweight_registry);
    free(canvas->grid_staging);
    free(canvas->prim_staging);
    yetty_ydraw_scrollbuffer_free(&canvas->scrollbuffer);
    free(canvas->sb_offsets);
    free(canvas);
    return YETTY_OK_VOID();
}

//=============================================================================
// Configuration
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_canvas_set_cell_size(struct yetty_ydraw_canvas *canvas,
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

struct yetty_ycore_void_result yetty_ydraw_canvas_set_grid_size(struct yetty_ydraw_canvas *canvas,
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

struct yetty_ycore_pixel_size yetty_ydraw_canvas_cell_get_pixel_size(
    struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_pixel_size){0, 0};
    }
    return canvas->cell_size;
}

struct yetty_ycore_grid_size yetty_ydraw_canvas_get_grid_size(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return (struct yetty_ycore_grid_size){0, 0};
    }
    return canvas->grid_size;
}

//=============================================================================
// Cursor
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_canvas_set_cursor_pos(
    struct yetty_ydraw_canvas *canvas, struct yetty_ycore_grid_cursor_pos pos)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->cursor_col = pos.cols;
    canvas->cursor_row = pos.rows;
    return YETTY_OK_VOID();
}

uint16_t yetty_ydraw_canvas_cursor_col(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->cursor_col : 0;
}

uint16_t yetty_ydraw_canvas_cursor_row(struct yetty_ydraw_canvas *canvas)
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
static uint32_t canvas_effective_view_top(const struct yetty_ydraw_canvas *canvas)
{
    if (canvas->view_top_override_active) {
        return canvas->view_top_override;
    }
    return canvas->rolling_row_0;
}

uint32_t yetty_ydraw_canvas_rolling_row_0(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas_effective_view_top(canvas) : 0;
}

uint32_t yetty_ydraw_canvas_live_rolling_row_0(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->rolling_row_0 : 0;
}

struct yetty_ycore_void_result yetty_ydraw_canvas_set_view_top(struct yetty_ydraw_canvas *canvas,
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
    struct yetty_ydraw_canvas *canvas, const struct yetty_ydraw_core_primitive_iter *iter)
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

    /* cursor_row is a screen-row index relative to the viewport top
   * (rolling_row_0). The cursor's absolute canvas-line is the sum. */
    uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;

    /* Cap the storage AABB so that canvas-line indices never go below 0.
     * A bezier (or any prim) whose AABB extends above the absolute canvas
     * origin would produce a row index < 0, which cast to uint32_t wraps to
     * ~4 billion and hangs canvas_ensure_lines.  The primitive's actual
     * coordinate data is untouched; only the indexing bounding box is clamped. */
    float min_valid_y = -(float)cursor_canvas_line * canvas->cell_size.height;
    if (aabb.max.y < min_valid_y)
        aabb.max.y = min_valid_y;
    if (aabb.min.y < min_valid_y)
        aabb.min.y = min_valid_y;

    uint32_t primitive_max_in_rows = (uint32_t)floorf(aabb.max.y / canvas->cell_size.height);

    uint32_t primitive_grid_line = cursor_canvas_line + primitive_max_in_rows;
    uint32_t primitive_rolling_row = cursor_canvas_line;

    canvas_ensure_lines(canvas, primitive_grid_line + 1);

    struct yetty_ydraw_canvas_grid_line *base_line =
        line_buffer_get(&canvas->lines, primitive_grid_line);
    if (!base_line) {
        return YETTY_ERR(uint32, "line_buffer_get returned NULL");
    }
    canvas_dirty_line(canvas, primitive_grid_line);

    uint32_t prim_index = grid_line_push_prim(base_line, primitive_rolling_row,
                                              (const float *)iter->fw.data, word_count);
    if (prim_index == UINT32_MAX) {
        return YETTY_ERR(uint32, "grid_line_push_prim failed");
    }

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
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, row);
        grid_line_ensure_cells(line, prim_col_max + 1);
        canvas_dirty_line(canvas, row);

        uint16_t lines_ahead = (uint16_t)(primitive_grid_line - row);

        for (uint32_t col = prim_col_min; col <= prim_col_max; col++) {
            struct yetty_ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_index};
            prim_ref_array_push(&line->cells[col].refs, ref);
        }
    }

    ydebug("add_primitive_internal: aabb_y=[%.1f,%.1f] cell_height=%.1f "
           "cursor_row=%u",
           aabb.min.y, aabb.max.y, canvas->cell_size.height, canvas->cursor_row);
    ydebug("add_primitive_internal: prim_min_row=%u prim_max_row=%u lines.count=%u", prim_row_min,
           prim_row_max, canvas->lines.count);

    // Track complex prims for resource set collection
    if (yetty_ydraw_core_is_complex_type(prim_type)) {
        /* Create factory instance for complex prim */
        struct yetty_ydraw_core_complex_prim_instance_ptr_result inst_res =
            yetty_ydraw_core_complex_prim_factory_create_instance(
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
                        new_cap * sizeof(struct yetty_ydraw_core_complex_prim_instance *));
            if (!base_line->complex_prims) {
                yetty_ydraw_core_complex_prim_instance_destroy(inst_res.value);
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

/* Per-buffer font map, populated as FONT prims are encountered during one
 * add_buffer call. Maps the producer-assigned font_id (text spans reference
 * fonts by this id) to a cache ref the buffer holds for the duration of
 * the add. Each entry carries one buffer-scoped retain on the cache; all
 * are released at end-of-buffer.
 *
 * Capacity is grown on demand — text spans typically reference a small
 * set of font_ids but a single PDF can carry dozens. */
/* Buffer-scoped lookup table mapping producer-assigned font_id values
 * to the in-memory cache state.
 *
 *   declared=true        — a FONT primitive with this font_id was seen
 *                          in the current buffer; `hex` is its content
 *                          key and the matching CDB has been ensured
 *                          on disk. Does NOT imply a cache slot exists.
 *   resolved=true        — at least one TEXT_SPAN with this font_id was
 *                          dispatched, which triggered a get_font call.
 *                          `font`/`handle` are valid and a buffer-scoped
 *                          ref is owed to the cache (released at end of
 *                          buffer by font_map_release_all).
 *
 * Fonts that are declared but never resolved (PDF over-declares a font
 * per page) never produce a cache entry — there's no ref to drop and
 * no MSDF atlas in memory for them. */
struct font_map_entry {
    char                      hex[17];    /* "" if not declared */
    bool                      declared;
    bool                      resolved;
    struct yetty_ydraw_font *font;
    yetty_yfont_cache_handle  handle;
};

struct font_map {
    struct font_map_entry *entries;
    uint32_t               capacity;
};

static void font_map_init(struct font_map *m)
{
    m->entries = NULL;
    m->capacity = 0;
}

static void font_map_grow(struct font_map *m, uint32_t want)
{
    if (want <= m->capacity) {
        return;
    }
    uint32_t new_cap = m->capacity ? m->capacity * 2 : 8;
    while (new_cap < want) {
        new_cap *= 2;
    }
    m->entries = realloc(m->entries, new_cap * sizeof(struct font_map_entry));
    for (uint32_t i = m->capacity; i < new_cap; i++) {
        m->entries[i].hex[0] = '\0';
        m->entries[i].declared = false;
        m->entries[i].resolved = false;
        m->entries[i].font = NULL;
        m->entries[i].handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
    }
    m->capacity = new_cap;
}

/* Resolved-only access: returns NULL if the font has not yet been
 * looked up in the cache (callers must invoke the lazy resolver first). */
static const struct font_map_entry *font_map_get(const struct font_map *m, uint32_t id)
{
    if (id >= m->capacity || !m->entries[id].resolved) {
        return NULL;
    }
    return &m->entries[id];
}

/* Release every buffer-held cache ref this map accumulated. Declared-
 * but-never-resolved entries hold no ref, so nothing to release. */
static void font_map_release_all(struct font_map *m, struct yetty_yfont_cache *cache)
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

/* Expand a TEXT_SPAN view into per-glyph SDF primitives at the canvas's
 * current cursor. Returns the highest grid row touched (0 if no glyphs
 * placed). `font_handle` is the cache handle the resulting glyphs encode
 * into the shader's per-glyph slot dispatcher. */
static struct uint32_result expand_text_span_to_glyphs(
    struct yetty_ydraw_canvas *canvas, const struct yetty_ydraw_core_text_span_prim_view *ts,
    struct yetty_ydraw_font *font, yetty_yfont_cache_handle font_handle)
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
        const struct yetty_ydraw_core_gpu_resource_set *rs = rs_res.value;
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

        /* Glyph SDF prim (7 words): type, z_order, x, y, font_size, packed, color
         *
         * `packed` carries (glyph_index in the low 16 bits, slot+1 in the
         * high 16 bits). `slot` is the cache handle — the same order the
         * binder attaches cache slots as resource-set children, so the
         * shader's font dispatcher uses it directly. 0 in the high bits
         * means "use slot 0" (default font); +1 lets producers encode
         * "no font" as 0 in pre-existing prims. */
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

        /* cursor_row is a screen-row offset from the viewport top
     * (rolling_row_0); convert to absolute canvas-line space. */
        uint32_t cursor_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
        float abs_y = gy + (float)cursor_canvas_line * canvas->cell_size.height;
        float abs_y_max = abs_y + gh;
        uint32_t glyph_row_max = (uint32_t)(abs_y_max / canvas->cell_size.height);

        canvas_ensure_lines(canvas, glyph_row_max + 1);

        uint32_t rolling_row = cursor_canvas_line;

        struct yetty_ydraw_canvas_grid_line *base_line =
            line_buffer_get(&canvas->lines, glyph_row_max);
        if (!base_line) {
            cursor_x += advance * scale;
            continue;
        }
        canvas_dirty_line(canvas, glyph_row_max);

        uint32_t prim_idx =
            grid_line_push_prim(base_line, rolling_row, glyph_data, YDRAW_GLYPH_WORDS);
        if (prim_idx == UINT32_MAX) {
            cursor_x += advance * scale;
            continue;
        }

        uint32_t col_min =
            (canvas->cell_size.width > 0) ? (uint32_t)(gx / canvas->cell_size.width) : 0;
        uint32_t col_max =
            (canvas->cell_size.width > 0) ? (uint32_t)((gx + gw) / canvas->cell_size.width) : 0;
        uint32_t row_min = (uint32_t)(abs_y / canvas->cell_size.height);

        if (col_max >= canvas->grid_size.cols && canvas->grid_size.cols > 0) {
            col_max = canvas->grid_size.cols - 1;
        }

        for (uint32_t row = row_min; row <= glyph_row_max; row++) {
            struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, row);
            grid_line_ensure_cells(line, col_max + 1);
            canvas_dirty_line(canvas, row);
            uint16_t lines_ahead = (uint16_t)(glyph_row_max - row);
            for (uint32_t col = col_min; col <= col_max; col++) {
                struct yetty_ydraw_canvas_prim_ref ref = {lines_ahead, (uint16_t)prim_idx};
                prim_ref_array_push(&line->cells[col].refs, ref);
            }
        }

        if (glyph_row_max > glyph_max_row) {
            glyph_max_row = glyph_row_max;
        }

        /* Per-glyph displacement: font advance + PDF text-state Tc, plus
         * Tw for ASCII space. ts->char_spacing/word_spacing are already
         * in display pixels (ypdf does the unit conversion); add them
         * straight to the cursor. The values are 0 for any producer that
         * doesn't fill them in (default font in YAML, etc.), so this is
         * a no-op for non-PDF text. */
        cursor_x += advance * scale + ts->char_spacing;
        if (cp == 0x20) {
            cursor_x += ts->word_spacing;
        }
    }

    return YETTY_OK(uint32, glyph_max_row);
}

/* Attach a cache handle to the grid line at `glyph_max_row`.
 *
 * If the handle is already on some other line, MIGRATE the entry to the
 * target line — no refcount change, the line just moves which row "owns"
 * the binder attachment. If it's not on any line yet, push a fresh entry
 * and bump the cache refcount (the line now owns one ref, released at
 * grid_line_free).
 *
 * Skip when the handle is invalid, identifies the canvas default font (we
 * don't track the default per-line — slot 0 is always attached), or when
 * glyph_max_row is 0. */
static void attach_handle_to_line(struct yetty_ydraw_canvas *canvas,
                                  yetty_yfont_cache_handle handle, uint32_t glyph_max_row)
{
    if (handle == YETTY_YFONT_CACHE_HANDLE_INVALID || handle == canvas->default_handle ||
        glyph_max_row == 0) {
        return;
    }
    struct yetty_ydraw_canvas_grid_line *target = line_buffer_get(&canvas->lines, glyph_max_row);
    if (!target) {
        return;
    }

    /* Look for an existing attachment to migrate (still O(L*F) per call,
     * but called once per unique font per buffer rather than per text-span). */
    for (uint32_t li = 0; li < canvas->lines.count; li++) {
        struct yetty_ydraw_canvas_grid_line *l = &canvas->lines.lines[li];
        for (uint32_t fi = 0; fi < l->font_count; fi++) {
            if (l->fonts[fi].handle == handle) {
                if (li == glyph_max_row) {
                    return; /* already in the right place */
                }
                if (target->font_count >= target->font_capacity) {
                    uint32_t new_cap =
                        target->font_capacity == 0 ? 4 : target->font_capacity * 2;
                    target->fonts = realloc(
                        target->fonts, new_cap * sizeof(struct yetty_ydraw_canvas_font_entry));
                    target->font_capacity = new_cap;
                }
                /* Move (preserves the single line-held cache ref). */
                target->fonts[target->font_count++] = l->fonts[fi];
                l->fonts[fi] = l->fonts[--l->font_count];
                return;
            }
        }
    }

    /* New attachment — line takes a fresh cache ref. */
    if (target->font_count >= target->font_capacity) {
        uint32_t new_cap = target->font_capacity == 0 ? 4 : target->font_capacity * 2;
        target->fonts =
            realloc(target->fonts, new_cap * sizeof(struct yetty_ydraw_canvas_font_entry));
        target->font_capacity = new_cap;
    }
    yetty_yfont_cache_retain(canvas->font_cache, handle);
    target->fonts[target->font_count++].handle = handle;
}

/* Per-buffer accumulator: highest row each unique handle was seen on
 * during the prim loop. attach_handle_to_line is called once per entry
 * after the loop, instead of per text-span. */
struct buffer_attach_entry {
    yetty_yfont_cache_handle handle;
    uint32_t                 max_row;
};

struct buffer_attach_list {
    struct buffer_attach_entry *entries;
    uint32_t                    count;
    uint32_t                    capacity;
};

static void buffer_attach_init(struct buffer_attach_list *l)
{
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void buffer_attach_free(struct buffer_attach_list *l)
{
    free(l->entries);
    l->entries = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void buffer_attach_note(struct buffer_attach_list *l, yetty_yfont_cache_handle handle,
                               uint32_t row)
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
        struct buffer_attach_entry *ne =
            realloc(l->entries, nc * sizeof(struct buffer_attach_entry));
        if (!ne) {
            return; /* Best-effort; lose attachment for this font. */
        }
        l->entries = ne;
        l->capacity = nc;
    }
    l->entries[l->count++] = (struct buffer_attach_entry){.handle = handle, .max_row = row};
}

/* ===========================================================================
 * Scrollbuffer eviction
 *
 * When a line scrolls below `rolling_row_0` it's no longer visible. We
 * encode it to canvas->scrollbuffer (compact form, no individual
 * allocations), record the byte offset in sb_offsets[i], then free the
 * line's expanded prims/arena/cells/fonts so the per-line malloc
 * footprint goes away. The grid_line struct itself stays in
 * canvas->lines.lines[i] but ends up empty after grid_line_free; we
 * preserve the slot so absolute canvas-line indices keep their
 * meaning. Deserialise-on-scrollback isn't here yet.
 * ========================================================================= */

/* About to mutate line `idx`. If it was previously evicted to the
 * scrollbuffer, its expanded form is empty and the only copy of the
 * content lives at sb_offsets[idx]. We MUST:
 *   1. restore that content into the line's expanded form first, so
 *      the new prim can stack on top of it,
 *   2. clear sb_offsets[idx] so the next eviction re-encodes the merged
 *      content (the orphaned old record stays in the scrollbuffer but
 *      no offset points to it — bounded waste).
 *
 * Without step 1, clearing sb_offsets[idx] would silently abandon the
 * old content on the next evict pass (re-encode of merely the new prim,
 * the old bytes orphaned but unreachable). PDFs, multi-buffer browsers,
 * and any "ydraw #2 lands on lines previously occupied by ydraw #1"
 * scenario would lose history.
 *
 * Restore failure is logged and we proceed — the new prim still saves,
 * the old content is gone. Mirror of the best-effort policy used in
 * canvas_restore_range. */
static void canvas_dirty_line(struct yetty_ydraw_canvas *canvas, uint32_t idx)
{
    if (idx >= canvas->sb_offsets_count || canvas->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return;
    }
    struct yetty_ycore_void_result r = canvas_restore_line(canvas, idx);
    if (YETTY_IS_ERR(r)) {
        yerror("canvas_dirty_line: restore line %u failed: %s — old content will be dropped",
               idx, r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
    canvas->sb_offsets[idx] = SB_OFFSET_UNSET;
}

static struct yetty_ycore_void_result sb_offsets_ensure(struct yetty_ydraw_canvas *canvas,
                                                        uint32_t min_count)
{
    if (min_count <= canvas->sb_offsets_count) {
        return YETTY_OK_VOID();
    }
    if (min_count > canvas->sb_offsets_capacity) {
        uint32_t new_cap = canvas->sb_offsets_capacity ? canvas->sb_offsets_capacity : 64u;
        while (new_cap < min_count) {
            new_cap *= 2;
        }
        uint32_t *grown = realloc(canvas->sb_offsets, new_cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "sb_offsets: realloc failed");
        }
        canvas->sb_offsets = grown;
        canvas->sb_offsets_capacity = new_cap;
    }
    for (uint32_t i = canvas->sb_offsets_count; i < min_count; i++) {
        canvas->sb_offsets[i] = SB_OFFSET_UNSET;
    }
    canvas->sb_offsets_count = min_count;
    return YETTY_OK_VOID();
}

/* Serialise one line at index `idx` into the scrollbuffer and free its
 * expanded form. No-op if the line is already serialised or doesn't
 * exist. Returns OK even if the line was empty; an empty line still
 * gets a 12-byte header record so sb_offsets[idx] is always meaningful
 * after this call. */
static struct yetty_ycore_void_result canvas_evict_line(struct yetty_ydraw_canvas *canvas,
                                                        uint32_t idx)
{
    if (idx >= canvas->lines.count) {
        return YETTY_OK_VOID();
    }
    /* Already evicted? skip. */
    struct yetty_ycore_void_result er = sb_offsets_ensure(canvas, idx + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "evict: ensure offsets");
    if (canvas->sb_offsets[idx] != SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }

    struct yetty_ydraw_canvas_grid_line *line = &canvas->lines.lines[idx];

    /* Build the cell view: walk cells in ascending col order, emit
     * one entry per cell that has at least one ref. The on-stack
     * buffer is sized by line->cell_count which is bounded by
     * grid_size.cols + any oversize a wide line grew to. */
    struct yetty_ydraw_scrollbuffer_cell *cells = NULL;
    uint32_t n_cells = 0;
    if (line->cell_count > 0) {
        cells = malloc(line->cell_count * sizeof(*cells));
        if (!cells) {
            return YETTY_ERR(yetty_ycore_void, "evict: cell view alloc failed");
        }
        for (uint32_t c = 0; c < line->cell_count; c++) {
            if (line->cells[c].refs.count == 0) {
                continue;
            }
            cells[n_cells].col = (uint16_t)c;
            cells[n_cells].ref_count = (uint16_t)line->cells[c].refs.count;
            /* The codec's ref view shape matches our internal
             * prim_ref byte-for-byte, so we just hand over the
             * dynamic array's data pointer. */
            cells[n_cells].refs =
                (const struct yetty_ydraw_scrollbuffer_ref *)line->cells[c].refs.data;
            n_cells++;
        }
    }

    /* Build the prim view: per prim, point payload at the arena slice
     * (arena_offset .. arena_offset + word_count). */
    struct yetty_ydraw_scrollbuffer_prim *prims = NULL;
    uint32_t n_prims = line->prims.count;
    if (n_prims > 0) {
        prims = malloc(n_prims * sizeof(*prims));
        if (!prims) {
            free(cells);
            return YETTY_ERR(yetty_ycore_void, "evict: prim view alloc failed");
        }
        for (uint32_t p = 0; p < n_prims; p++) {
            struct yetty_ydraw_canvas_prim_data *pd = &line->prims.data[p];
            prims[p].rolling_row = pd->rolling_row;
            prims[p].word_count = pd->word_count;
            prims[p].payload = line->arena + pd->arena_offset;
        }
    }

    struct yetty_ydraw_scrollbuffer_offset_result enc =
        yetty_ydraw_scrollbuffer_encode_line(&canvas->scrollbuffer, idx,
                                              canvas->grid_size.cols, cells, n_cells, prims,
                                              n_prims);
    free(cells);
    free(prims);
    if (YETTY_IS_ERR(enc)) {
        return YETTY_ERR(yetty_ycore_void, "evict: encode failed", enc);
    }
    canvas->sb_offsets[idx] = (uint32_t)enc.value;

    /* Free the expanded form. The line struct itself stays in
     * canvas->lines.lines[idx]; grid_line_free zeroes its internal
     * pointers/counts so it's effectively a placeholder afterwards.
     *
     * Font cache refs the line held are dropped here too — that's
     * intentional: if the only line referencing a given font is being
     * evicted, the cache slot would normally die. With the lazy-
     * resolve commit (69c4dd5), TEXT_SPANs hitting the same font
     * later re-resolve through the cache; the on-disk CDB still
     * caches the MSDF generation, so the cost is one cache miss +
     * atlas load on first reuse.
     *
     * Complex prims (yimage / yplot / yvideo) are GPU-backed
     * instances the scrollbuffer codec can't round-trip — we'd lose
     * them on scroll-back. Detach them before grid_line_free, then
     * re-attach after grid_line_init so they keep living on the
     * (otherwise empty) line and re-render the moment the user
     * scrolls the line back into the visible window.
     *
     * Font attachments get the same treatment for a different reason:
     * each line holds a cache ref per attached font, and the GLYPH
     * payloads we just encoded carry the font's slot index. If we let
     * grid_line_free release these refs, the cache may evict the font
     * — and the ydraw layer's WGSL dispatcher (sized off
     * canvas_font_count) keeps referencing that slot's namespace at
     * render time, causing the shader to fail to compile with
     * "struct member <ns>_base_size not found". Detach + restore keeps
     * the cache ref alive across the line's empty-form phase, so the
     * font slot remains addressable until the line is re-restored. */
    struct yetty_ydraw_core_complex_prim_instance **saved_cp = line->complex_prims;
    uint32_t saved_cp_count = line->complex_prim_count;
    uint32_t saved_cp_cap = line->complex_prim_capacity;
    line->complex_prims = NULL;
    line->complex_prim_count = 0;
    line->complex_prim_capacity = 0;

    struct yetty_ydraw_canvas_font_entry *saved_fonts = line->fonts;
    uint32_t saved_font_count = line->font_count;
    uint32_t saved_font_capacity = line->font_capacity;
    line->fonts = NULL;
    line->font_count = 0;
    line->font_capacity = 0;

    struct yetty_ycore_void_result fr =
        grid_line_free(line, canvas->flyweight_registry, canvas->font_cache);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "evict: grid_line_free");
    /* Re-init so future writes (e.g. an explicit out-of-order place)
     * don't see dangling pointers. */
    grid_line_init(line, 0);

    line->complex_prims = saved_cp;
    line->complex_prim_count = saved_cp_count;
    line->complex_prim_capacity = saved_cp_cap;
    line->fonts = saved_fonts;
    line->font_count = saved_font_count;
    line->font_capacity = saved_font_capacity;
    return YETTY_OK_VOID();
}

/* Walk every line strictly below rolling_row_0 and evict any that
 * still hold expanded content. Called at the end of add_buffer once
 * the new content has been placed and the viewport has scrolled.
 *
 * A line that was previously evicted but restored for a scrollback
 * render (sb_offsets[i] set, but grid_line currently populated) is
 * NOT re-encoded — its bytes haven't changed — but its expanded
 * form is freed again. */
static void canvas_evict_scrollback(struct yetty_ydraw_canvas *canvas)
{
    if (canvas->rolling_row_0 == 0) {
        return;
    }
    uint32_t end = canvas->rolling_row_0;
    if (end > canvas->lines.count) {
        end = canvas->lines.count;
    }
    for (uint32_t i = 0; i < end; i++) {
        if (i < canvas->sb_offsets_count && canvas->sb_offsets[i] != SB_OFFSET_UNSET) {
            /* Already serialised. If it was restored, free the
             * expanded form again. Complex prims AND font attachments
             * must survive the re-free for the same reason as in
             * canvas_evict_line — detach before grid_line_free,
             * re-attach after grid_line_init. */
            struct yetty_ydraw_canvas_grid_line *line = &canvas->lines.lines[i];
            if (line->prims.count > 0 || line->cell_count > 0 || line->font_count > 0) {
                struct yetty_ydraw_core_complex_prim_instance **saved_cp = line->complex_prims;
                uint32_t saved_cp_count = line->complex_prim_count;
                uint32_t saved_cp_cap = line->complex_prim_capacity;
                line->complex_prims = NULL;
                line->complex_prim_count = 0;
                line->complex_prim_capacity = 0;

                struct yetty_ydraw_canvas_font_entry *saved_fonts = line->fonts;
                uint32_t saved_font_count = line->font_count;
                uint32_t saved_font_capacity = line->font_capacity;
                line->fonts = NULL;
                line->font_count = 0;
                line->font_capacity = 0;

                struct yetty_ycore_void_result fr = grid_line_free(
                    line, canvas->flyweight_registry, canvas->font_cache);
                if (YETTY_IS_ERR(fr)) {
                    yerror("canvas_evict_scrollback: re-free line %u failed: %s",
                           i, fr.error.msg);
                    yetty_ycore_error_destroy(fr.error);
                }
                grid_line_init(line, 0);

                line->complex_prims = saved_cp;
                line->complex_prim_count = saved_cp_count;
                line->complex_prim_capacity = saved_cp_cap;
                line->fonts = saved_fonts;
                line->font_count = saved_font_count;
                line->font_capacity = saved_font_capacity;
            }
            continue;
        }
        struct yetty_ycore_void_result r = canvas_evict_line(canvas, i);
        if (YETTY_IS_ERR(r)) {
            yerror("canvas_evict_scrollback: evict line %u failed: %s", i, r.error.msg);
            yetty_ycore_error_destroy(r.error);
            /* Keep going — best-effort batch eviction; later lines may
             * still succeed. */
        }
    }
}

/* ===========================================================================
 * Scrollbuffer restore (decode evicted lines back into canvas->lines)
 *
 * Called by rebuild_grid for every visible-window row whose grid_line
 * is empty but sb_offsets[i] is set. After restore, the grid_line
 * has its prims/arena/cells populated and rendering proceeds as if
 * the line had never been evicted. canvas_evict_scrollback frees
 * these restored lines on the next add_buffer.
 * ========================================================================= */

/* Word-count lookup for the scrollbuffer decoder. The codec stores
 * non-default prims as <type, payload-bytes> with NO explicit length —
 * the decoder must know how many words each type occupies.
 *
 * Two source ranges live in canvas->lines and therefore in the
 * scrollbuffer:
 *   - YETTY_YSDF_GLYPH (= 200), the per-character flyweight expanded
 *     from TEXT_SPAN prims. Standalone constant — not in the SDF
 *     types.gen enum range.
 *   - Real SDF prims [0x10000000, 0x1FFFFFFF] from generators that
 *     emit BOX/CIRCLE/SEGMENT/ELLIPSE/… (SVG, PDF rect/line, browser).
 *
 * Returning 0 for an unrecognised type lets the decoder fail cleanly
 * rather than read garbage. */
static uint32_t canvas_sb_word_count_fn(uint32_t type_word, void *ctx)
{
    (void)ctx;
    if (type_word == YETTY_YSDF_GLYPH) {
        return YDRAW_GLYPH_WORDS;
    }
    if (type_word >= 0x10000000u && type_word <= 0x1FFFFFFFu) {
        uint32_t wc = yetty_ysdf_word_count((enum yetty_ysdf_type)type_word);
        if (wc > 0) return wc;
    }
    return 0;
}

struct sb_restore_ctx {
    struct yetty_ydraw_canvas_grid_line *line;
};

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_header(void *ctx,
                                                           uint32_t line_rolling_row,
                                                           uint32_t prim_count)
{
    /* No-op: the line was freed before this restore was triggered,
     * so push_prim handles growing the arena/prims as needed. The
     * header values are only used for sanity checks here. */
    (void)ctx;
    (void)line_rolling_row;
    (void)prim_count;
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_cell(
    void *ctx, uint32_t col, const struct yetty_ydraw_scrollbuffer_ref *refs, uint32_t ref_count)
{
    struct sb_restore_ctx *r = ctx;
    struct yetty_ycore_void_result er = grid_line_ensure_cells(r->line, col + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "restore: ensure_cells");
    for (uint32_t i = 0; i < ref_count; i++) {
        struct yetty_ydraw_canvas_prim_ref pr = {
            .lines_ahead = refs[i].lines_ahead,
            .prim_index  = refs[i].prim_idx,
        };
        prim_ref_array_push(&r->line->cells[col].refs, pr);
    }
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result sb_restore_on_prim(void *ctx, uint32_t rolling_row,
                                                         const uint32_t *payload,
                                                         uint32_t word_count)
{
    struct sb_restore_ctx *r = ctx;
    uint32_t idx = grid_line_push_prim(r->line, rolling_row, (const float *)payload, word_count);
    if (idx == UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "restore: grid_line_push_prim failed");
    }
    return YETTY_OK_VOID();
}

/* Decode line `idx` from the scrollbuffer back into
 * canvas->lines.lines[idx]. Idempotent: if the line is not in
 * scrollbuffer (sb_offsets[idx] == UNSET) or already has expanded
 * content, it's a no-op. */
static struct yetty_ycore_void_result canvas_restore_line(struct yetty_ydraw_canvas *canvas,
                                                          uint32_t idx)
{
    if (idx >= canvas->sb_offsets_count || canvas->sb_offsets[idx] == SB_OFFSET_UNSET) {
        return YETTY_OK_VOID();
    }
    if (idx >= canvas->lines.count) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_canvas_grid_line *line = &canvas->lines.lines[idx];
    if (line->prims.count > 0 || line->cell_count > 0) {
        /* Already expanded — could be a previously-restored line still
         * holding content from the previous render, or content that
         * was placed on top of a serialised line (shouldn't happen
         * for ycat workloads, but harmless to handle). */
        return YETTY_OK_VOID();
    }

    struct sb_restore_ctx rctx = {.line = line};
    struct yetty_ydraw_scrollbuffer_decode_sinks sinks = {
        .ctx = &rctx,
        .on_header = sb_restore_on_header,
        .on_cell   = sb_restore_on_cell,
        .on_prim   = sb_restore_on_prim,
    };
    struct yetty_ydraw_scrollbuffer_offset_result dec = yetty_ydraw_scrollbuffer_decode_line(
        &canvas->scrollbuffer, canvas->sb_offsets[idx], canvas_sb_word_count_fn, NULL, &sinks);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dec, "canvas_restore_line: decode failed");
    return YETTY_OK_VOID();
}

/* Restore every evicted line in [first, last] (inclusive). Used by
 * rebuild_grid to ensure the visible-window rows are populated. */
static void canvas_restore_range(struct yetty_ydraw_canvas *canvas, uint32_t first,
                                 uint32_t last)
{
    for (uint32_t i = first; i <= last; i++) {
        struct yetty_ycore_void_result r = canvas_restore_line(canvas, i);
        if (YETTY_IS_ERR(r)) {
            yerror("canvas_restore_range: restore line %u failed: %s", i, r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
}

struct yetty_ycore_void_result yetty_ydraw_canvas_add_buffer(
    struct yetty_ydraw_canvas *canvas, struct yetty_ydraw_core_buffer *buffer)
{
    if (!canvas) {
        yerror("yetty_ydraw_canvas_add_buffer: canvas is NULL");
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!buffer) {
        yerror("yetty_ydraw_canvas_add_buffer: buffer is NULL");
        return YETTY_ERR(yetty_ycore_void, "buffer is NULL");
    }

    struct yetty_ydraw_core_primitive_iter_result iter_res =
        yetty_ydraw_core_buffer_prim_first(buffer, canvas->flyweight_registry);
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

    struct font_map fonts_map;
    font_map_init(&fonts_map);

    struct buffer_attach_list attach_list;
    buffer_attach_init(&attach_list);

    struct yetty_ydraw_core_primitive_iter iter = iter_res.value;
    struct yetty_ycore_void_result final_status = YETTY_OK_VOID();

    while (1) {
        uint32_t prim_type = iter.fw.data[0];

        /* Cmd tier (control, no rendering). Apply side effects on the
         * canvas, fall through the per-type handlers without storing
         * anything. */
        if (prim_type <= YETTY_YDRAW_CMD_END) {
            if (prim_type == YETTY_YDRAW_CMD_ZERO) {
                ydebug("add_buffer: CMD_ZERO — clearing canvas + cursor (0,0)");
                yetty_ydraw_canvas_clear(canvas);
                struct yetty_ycore_grid_cursor_pos pos = {.cols = 0, .rows = 0};
                struct yetty_ycore_void_result cr = yetty_ydraw_canvas_set_cursor_pos(canvas, pos);
                if (YETTY_IS_ERR(cr)) {
                    yetty_ycore_error_destroy(cr.error);
                }
                /* Re-read cursor anchor since clear+reset moved us. */
                initial_canvas_line = canvas->rolling_row_0 + canvas->cursor_row;
                max_row_seen = initial_canvas_line;
            }
            /* Future cmds (cursor-set, …) dispatch here. */
        } else if (prim_type == YETTY_YDRAW_TYPE_FONT) {
            struct yetty_ydraw_core_font_prim_view fv;
            if (yetty_ydraw_core_font_prim_parse(iter.fw.data, &fv) == 0 && fv.font_id >= 0) {
                char hint[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
                size_t hl = fv.name_len < sizeof(hint) - 1 ? fv.name_len : sizeof(hint) - 1;
                memcpy(hint, fv.name, hl);
                hint[hl] = '\0';

                /* FONT primitive only ensures the on-disk CDB exists and
                 * records the content key against this buffer's font_id.
                 * NO cache slot is created here — that happens lazily
                 * the first time a TEXT_SPAN actually references this
                 * font_id. PDFs over-declare fonts (catalogue all fonts
                 * per page, use only some); the unused ones never reach
                 * the cache and never produce an MSDF atlas in memory. */
                char hex[17];
                struct yetty_ycore_void_result er =
                    ydraw_canvas_ensure_blob_font_cdb(canvas, fv.ttf, fv.ttf_len, hint, hex);
                if (YETTY_IS_ERR(er)) {
                    ywarn("add_buffer: font CDB ensure failed (font_id=%d hint='%s'): %s — "
                          "spans will use default font",
                          fv.font_id, hint, er.error.msg);
                    yetty_ycore_error_destroy(er.error);
                } else {
                    font_map_grow(&fonts_map, (uint32_t)fv.font_id + 1);
                    memcpy(fonts_map.entries[fv.font_id].hex, hex, 17);
                    fonts_map.entries[fv.font_id].declared = true;
                }
            }
        } else if (prim_type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
            struct yetty_ydraw_core_text_span_prim_view tv;
            if (yetty_ydraw_core_text_span_prim_parse(iter.fw.data, &tv) == 0) {
                struct yetty_ydraw_font *font = NULL;
                yetty_yfont_cache_handle handle = YETTY_YFONT_CACHE_HANDLE_INVALID;
                if (tv.font_id >= 0 && (uint32_t)tv.font_id < fonts_map.capacity) {
                    struct font_map_entry *e = &fonts_map.entries[tv.font_id];
                    if (e->resolved) {
                        font = e->font;
                        handle = e->handle;
                    } else if (e->declared) {
                        /* Lazy first-use: now we actually want a cache
                         * slot. Get one (hit if a prior envelope or a
                         * scrollback line still alive forced
                         * construction; miss → construct from CDB).
                         * The buffer-scoped ref is released by
                         * font_map_release_all at end of buffer; line
                         * attaches keep the slot alive afterwards. */
                        struct yetty_yfont_cache_ref_result rr =
                            canvas_resolve_blob_font_handle(canvas, e->hex);
                        if (YETTY_IS_OK(rr)) {
                            e->font = rr.value.font;
                            e->handle = rr.value.handle;
                            e->resolved = true;
                            font = e->font;
                            handle = e->handle;
                        } else {
                            /* msdf load failed — spans fall back to
                             * default font for the rest of the buffer. */
                            ywarn("add_buffer: font resolve failed (font_id=%d): %s — "
                                  "span falls back to default font",
                                  tv.font_id, rr.error.msg);
                            yetty_ycore_error_destroy(rr.error);
                            e->declared = false;
                        }
                    }
                }
                if (!font) {
                    /* Producer used font_id == -1, or referenced a font_id
                     * never declared by a FONT primitive in this buffer
                     * — fall back to the canvas default. */
                    font = canvas->default_font;
                    handle = canvas->default_handle;
                }
                if (font) {
                    struct uint32_result gmr_res =
                        expand_text_span_to_glyphs(canvas, &tv, font, handle);
                    if (YETTY_IS_OK(gmr_res)) {
                        uint32_t glyph_max_row = gmr_res.value;
                        if (glyph_max_row > max_row_seen) {
                            max_row_seen = glyph_max_row;
                        }
                        /* Defer attach: just record the highest row this
                         * handle reached during the buffer; a single pass
                         * after the loop attaches each unique handle once. */
                        buffer_attach_note(&attach_list, handle, glyph_max_row);
                    } else {
                        yetty_ycore_error_destroy(gmr_res.error);
                    }
                }
            }
        } else {
            /* SDF or complex prim — uniform path. */
            struct uint32_result prim_res = add_primitive_internal(canvas, &iter);
            if (YETTY_IS_ERR(prim_res)) {
                /* One bad prim shouldn't drop the rest of the buffer.
                 * The original `break` here caused symptoms like "only
                 * the first image renders on a Wikipedia page" — every
                 * yimage after a single failed instance-create was
                 * silently lost, along with all later text/SDF prims.
                 * Log and keep going; downstream rendering still works
                 * on the prims that did construct. */
                yerror("add_buffer: add_primitive_internal failed (continuing): %s",
                       prim_res.error.msg);
                yetty_ycore_error_destroy(prim_res.error);
            } else if (prim_res.value > max_row_seen) {
                max_row_seen = prim_res.value;
            }
        }

        struct yetty_ydraw_core_primitive_iter_result nx =
            yetty_ydraw_core_buffer_prim_next(buffer, canvas->flyweight_registry, &iter);
        if (YETTY_IS_ERR(nx)) {
            break;
        }
        iter = nx.value;
    }

    /* End-of-buffer pass: attach each unique font once to its destination
     * line. This replaces the per-text-span attach call that dominated
     * profiling at 33% of CPU on PDF rendering. */
    for (uint32_t i = 0; i < attach_list.count; i++) {
        attach_handle_to_line(canvas, attach_list.entries[i].handle,
                              attach_list.entries[i].max_row);
    }
    buffer_attach_free(&attach_list);

    /* Release every buffer-scoped cache ref (one per FONT prim materialised
     * in this buffer). Lines that took their own ref via attach_handle_to_line
     * keep the font alive. */
    font_map_release_all(&fonts_map, canvas->font_cache);
    free(fonts_map.entries);

    if (YETTY_IS_ERR(final_status)) {
        return final_status;
    }

    /* Scroll the viewport so the cursor lands on the line immediately
   * below the bottom-most prim — same contract as `cat foo.txt`. The
   * shell prompt that runs after the OSC envelope finishes will print
   * at the cursor, so the cursor MUST be at max_row_seen + 1 in canvas
   * coords, never anywhere above (or it would overlap content) and
   * never below the viewport (or it would be clipped).
   *
   * (A previous "sparse-tail correction" tried to park the viewport on
   * dense rows when the trailing rows held only a footer-mark; in
   * practice that parked the cursor on top of still-visible content for
   * SVGs whose density is uniformly low. Removed — predictable
   * cat-like placement beats the heuristic.) */
    if (canvas->scrolling_mode) {
        uint32_t target_cursor_canvas_line = max_row_seen + 1;
        uint32_t viewport_bottom = canvas->rolling_row_0 + canvas->grid_size.rows - 1;

        ydebug("add_buffer: target_cursor=%u max_row_seen=%u viewport_bottom=%u",
               target_cursor_canvas_line, max_row_seen, viewport_bottom);

        if (target_cursor_canvas_line > viewport_bottom) {
            uint32_t lines_to_scroll = target_cursor_canvas_line - viewport_bottom;

            if (!canvas->scroll_callback) {
                yerror("add_buffer: scroll_callback is NULL");
                return YETTY_ERR(yetty_ycore_void, "scroll_callback is NULL");
            }
            struct yetty_ycore_void_result scroll_res = canvas->scroll_callback(
                canvas->scroll_callback_user_data, (uint16_t)lines_to_scroll);
            if (YETTY_IS_ERR(scroll_res)) {
                return scroll_res;
            }
            yetty_ydraw_canvas_scroll_lines(canvas, (uint16_t)lines_to_scroll);
        }

        /* After the scroll above, target_cursor_canvas_line is guaranteed
         * to be ≤ viewport_bottom, so the subtraction stays in
         * [0, grid_rows-1] without further clamping. */
        uint32_t cursor_screen_row = (target_cursor_canvas_line >= canvas->rolling_row_0)
                                         ? (target_cursor_canvas_line - canvas->rolling_row_0)
                                         : 0;
        canvas->cursor_row = (uint16_t)cursor_screen_row;

        if (canvas->cursor_set_callback) {
            canvas->cursor_set_callback(canvas->cursor_set_callback_user_data, canvas->cursor_row);
        }
    }

    /* Serialise every line that just rolled below the live viewport
     * (rolling_row_0). Their expanded grid_line content is freed; the
     * compact form lives in canvas->scrollbuffer keyed by
     * sb_offsets[i]. This is where the per-instance heap growth comes
     * back down — without it canvas->lines holds the full
     * 40+ MB/PDF in expanded form forever. */
    canvas_evict_scrollback(canvas);

    ydebug("add_buffer: END cursor_row=%u rolling_row_0=%u lines.count=%u "
           "max_row_seen=%u scrollbuffer=logical %zu B, compressed %zu B (%u chunks)",
           canvas->cursor_row, canvas->rolling_row_0, canvas->lines.count, max_row_seen,
           yetty_ydraw_scrollbuffer_logical_size(&canvas->scrollbuffer),
           yetty_ydraw_scrollbuffer_compressed_size(&canvas->scrollbuffer),
           canvas->scrollbuffer.chunks_count);

    canvas->dirty = true;
    return YETTY_OK_VOID();
}

//=============================================================================
// Scrolling
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_canvas_scroll_lines(struct yetty_ydraw_canvas *canvas,
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

    ydebug("yetty_ydraw_canvas_scroll_lines: num_lines=%u lines.count=%u "
           "rolling_row_0=%u cursor_row=%u",
           num_lines, canvas->lines.count, canvas->rolling_row_0, canvas->cursor_row);

    canvas->dirty = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_canvas_set_scroll_callback(
    struct yetty_ydraw_canvas *canvas, yetty_ydraw_canvas_scroll_callback callback,
    struct yetty_ycore_void_result *user_data)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    canvas->scroll_callback = callback;
    canvas->scroll_callback_user_data = user_data;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydraw_canvas_set_cursor_callback(
    struct yetty_ydraw_canvas *canvas, yetty_ydraw_canvas_cursor_set_callback callback,
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

struct yetty_ycore_void_result yetty_ydraw_canvas_mark_dirty(struct yetty_ydraw_canvas *canvas)
{
    if (canvas) {
        canvas->dirty = true;
    }
    return YETTY_OK_VOID();
}

bool yetty_ydraw_canvas_is_dirty(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->dirty : false;
}

static struct yetty_ycore_void_result ensure_grid_staging(struct yetty_ydraw_canvas *canvas,
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

struct yetty_ycore_void_result yetty_ydraw_canvas_rebuild_grid(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!canvas->dirty && canvas->grid_staging_count > 0) {
        return YETTY_OK_VOID();
    }

    /* If the visible window dips into scrollback (set_view_top), the
     * grid_lines for that range may have been evicted to the
     * scrollbuffer and emptied. Decode them back into expanded form
     * before the prefix-sum/cell-walk below runs. Lines that aren't
     * evicted, or are already restored, are no-ops. */
    {
        uint32_t window_top = canvas_effective_view_top(canvas);
        uint32_t window_last = window_top + canvas->grid_size.rows;
        if (window_last > canvas->lines.count) {
            window_last = canvas->lines.count;
        }
        if (window_last > window_top) {
            canvas_restore_range(canvas, window_top, window_last - 1u);
        }
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
            struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
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
   * shader never indexes those columns. Capped at YDRAW_GRID_COLS_MAX
   * so a buggy/malicious producer can't blow up grid_staging — anything
   * beyond is clipped on the right edge, like an unwrapped text line. */
    const uint32_t YDRAW_GRID_COLS_MAX = 4096u;
    for (uint32_t gpu_y = 0; gpu_y < grid_h; gpu_y++) {
        uint32_t canvas_y = window_top + gpu_y;
        if (canvas_y >= canvas->lines.count) {
            break;
        }
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, canvas_y);
        if (line->cell_count > grid_w) {
            grid_w = line->cell_count;
        }
    }
    if (grid_w > YDRAW_GRID_COLS_MAX) {
        grid_w = YDRAW_GRID_COLS_MAX;
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
        struct yetty_ydraw_canvas_grid_line *line =
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
                struct yetty_ydraw_canvas_grid_cell *cell = &line->cells[x];
                for (uint32_t ri = 0; ri < cell->refs.count; ri++) {
                    struct yetty_ydraw_canvas_prim_ref *ref = &cell->refs.data[ri];
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

const uint32_t *yetty_ydraw_canvas_grid_data(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->grid_staging : NULL;
}

uint32_t yetty_ydraw_canvas_grid_word_count(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->grid_staging_count : 0;
}

struct yetty_ycore_void_result yetty_ydraw_canvas_clear_staging(struct yetty_ydraw_canvas *canvas)
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

static struct yetty_ycore_void_result ensure_prim_staging(struct yetty_ydraw_canvas *canvas,
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

struct yetty_ydraw_prim_staging_result yetty_ydraw_canvas_build_prim_staging(
    struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ydraw_prim_staging,
                         "yetty_ydraw_canvas_build_prim_staging: NULL canvas");
    }

    // Count primitives and total words (+1 per prim for rolling_row)
    uint32_t prim_count = 0;
    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            prim_count++;
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }

    if (prim_count == 0) {
        canvas->prim_staging_count = 0;
        struct yetty_ydraw_prim_staging empty = {.data = NULL, .word_count = 0};
        return YETTY_OK(yetty_ydraw_prim_staging, empty);
    }

    // Layout: [prim0_offset, prim1_offset, ...][rolling_row0,
    // prim0_data...][rolling_row1, prim1_data...]
    uint32_t total_size = prim_count + total_words;
    struct yetty_ycore_void_result eps = ensure_prim_staging(canvas, total_size);
    YETTY_RETURN_IF_ERR(yetty_ydraw_prim_staging, eps,
                        "yetty_ydraw_canvas_build_prim_staging: ensure_prim_staging failed");

    uint32_t data_offset = 0;
    uint32_t prim_idx = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);

        for (uint32_t p = 0; p < line->prims.count; p++) {
            struct yetty_ydraw_canvas_prim_data *prim = &line->prims.data[p];
            canvas->prim_staging[prim_idx] = data_offset;

            // Prepend rolling_row at insertion (for shader y_offset calculation)
            canvas->prim_staging[prim_count + data_offset] = prim->rolling_row;

            // Copy primitive payload from the line's arena.
            const uint32_t *payload = line->arena + prim->arena_offset;
            memcpy(&canvas->prim_staging[prim_count + data_offset + 1], payload,
                   prim->word_count * sizeof(uint32_t));

            data_offset += prim->word_count + 1; // +1 for rolling_row
            prim_idx++;
        }
    }

    canvas->prim_staging_count = total_size;
    struct yetty_ydraw_prim_staging out = {.data = canvas->prim_staging, .word_count = total_size};
    return YETTY_OK(yetty_ydraw_prim_staging, out);
}

uint32_t yetty_ydraw_canvas_prim_gpu_size(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t total_words = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        for (uint32_t p = 0; p < line->prims.count; p++) {
            total_words += line->prims.data[p].word_count + 1; // +1 for rolling_row
        }
    }
    return total_words * sizeof(float);
}

//=============================================================================
// State management
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_canvas_clear(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    struct yetty_ycore_void_result res =
        line_buffer_free(&canvas->lines, canvas->flyweight_registry, canvas->font_cache);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    line_buffer_init(&canvas->lines);

    /* Reset scrollbuffer too — clear wipes scrollback. */
    yetty_ydraw_scrollbuffer_free(&canvas->scrollbuffer);
    yetty_ydraw_scrollbuffer_init(&canvas->scrollbuffer);
    free(canvas->sb_offsets);
    canvas->sb_offsets = NULL;
    canvas->sb_offsets_count = 0;
    canvas->sb_offsets_capacity = 0;

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

bool yetty_ydraw_canvas_empty(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return true;
    }

    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        if (line->prims.count > 0) {
            return false;
        }
    }
    return true;
}

uint32_t yetty_ydraw_canvas_primitive_count(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < canvas->lines.count; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        count += line->prims.count;
    }
    return count;
}

struct yetty_ydraw_font *yetty_ydraw_canvas_get_default_font(struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->default_font : NULL;
}

uint32_t yetty_ydraw_canvas_font_count(const struct yetty_ydraw_canvas *canvas)
{
    return canvas ? yetty_yfont_cache_count(canvas->font_cache) : 0;
}

struct yetty_ydraw_font *yetty_ydraw_canvas_get_font_at(const struct yetty_ydraw_canvas *canvas,
                                                          uint32_t slot)
{
    if (!canvas) {
        return NULL;
    }
    return yetty_yfont_cache_font_at(canvas->font_cache, (yetty_yfont_cache_handle)slot);
}

//=============================================================================
// Complex primitive access (for atlas rendering)
//=============================================================================

/* Visible window in canvas-line space:
 *   [rolling_row_0 .. rolling_row_0 + grid_size.rows). */
static void canvas_visible_window(const struct yetty_ydraw_canvas *canvas, uint32_t *out_top,
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

uint32_t yetty_ydraw_canvas_complex_prim_count(struct yetty_ydraw_canvas *canvas)
{
    if (!canvas) {
        return 0;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t count = 0;
    for (uint32_t i = top; i < end; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        count += line->complex_prim_count;
    }
    return count;
}

struct yetty_ydraw_core_complex_prim_instance *yetty_ydraw_canvas_get_complex_prim(
    struct yetty_ydraw_canvas *canvas, uint32_t index)
{
    if (!canvas) {
        return NULL;
    }

    uint32_t top, end;
    canvas_visible_window(canvas, &top, &end);

    uint32_t current = 0;
    for (uint32_t i = top; i < end; i++) {
        struct yetty_ydraw_canvas_grid_line *line = line_buffer_get(&canvas->lines, i);
        if (index < current + line->complex_prim_count) {
            uint32_t local_idx = index - current;
            return line->complex_prims[local_idx];
        }
        current += line->complex_prim_count;
    }
    return NULL;
}

const struct yetty_ydraw_core_flyweight_registry *yetty_ydraw_canvas_get_flyweight_registry(
    struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->flyweight_registry : NULL;
}

struct yetty_ydraw_core_complex_prim_factory *yetty_ydraw_canvas_get_complex_prim_factory(
    struct yetty_ydraw_canvas *canvas)
{
    return canvas ? canvas->complex_prim_factory : NULL;
}

/*=============================================================================
 * Glyph iteration
 *===========================================================================*/

void yetty_ydraw_canvas_for_each_glyph(struct yetty_ydraw_canvas *canvas,
                                        yetty_ydraw_canvas_glyph_visitor visitor, void *user)
{
    if (!canvas || !visitor) {
        return;
    }
    float cell_h = canvas->cell_size.height;
    /* Glyph prim layout (see expand_text_span_to_glyphs):
     *   word[0]: u32 type            (== YETTY_YSDF_GLYPH)
     *   word[1]: u32 z_order
     *   word[2]: f32 x               (canvas-pixel)
     *   word[3]: f32 y               (RELATIVE — see below)
     *   word[4]: f32 font_size
     *   word[5]: u32 packed          (low 16 = glyph_idx, high 16 = slot+1)
     *   word[6]: u32 color
     *
     * The stored y is RELATIVE to the cursor-line at insertion time, not
     * absolute canvas y. The canvas reconstructs the absolute position
     * via `gy + pd->rolling_row * cell_h` (see the abs_y computation in
     * expand_text_span_to_glyphs). We do the same here so visitors get
     * absolute canvas pixel coordinates and can filter by viewport y
     * directly. Words carry mixed types — memcpy each one out at decode
     * time to stay endian/alignment-agnostic. */
    for (uint32_t li = 0; li < canvas->lines.count; li++) {
        const struct yetty_ydraw_canvas_grid_line *line = &canvas->lines.lines[li];
        for (uint32_t pi = 0; pi < line->prims.count; pi++) {
            const struct yetty_ydraw_canvas_prim_data *pd = &line->prims.data[pi];
            if (pd->word_count < YDRAW_GLYPH_WORDS) {
                continue;
            }
            const uint32_t *words = line->arena + pd->arena_offset;
            uint32_t type_word;
            memcpy(&type_word, &words[0], sizeof(type_word));
            if (type_word != YETTY_YSDF_GLYPH) {
                continue;
            }
            float gx, gy_rel;
            uint32_t packed;
            memcpy(&gx, &words[2], sizeof(gx));
            memcpy(&gy_rel, &words[3], sizeof(gy_rel));
            memcpy(&packed, &words[5], sizeof(packed));

            struct yetty_ydraw_glyph_view view;
            view.x = gx;
            view.y = gy_rel + (float)pd->rolling_row * cell_h;
            view.glyph_idx = packed & 0xFFFFu;
            uint32_t slot_plus_one = (packed >> 16) & 0xFFFFu;
            view.font_slot = slot_plus_one ? (int32_t)(slot_plus_one - 1) : -1;
            visitor(&view, user);
        }
    }
}
