/* canvas-internal.h — module-internal shared canvas types and helpers.
 *
 * The ydraw canvas comes in two flavours:
 *
 *   scrolling-canvas — viewport scrolls, scrollbuffer evicts old rows, used
 *                      by the terminal text and ydraw layers.
 *   static-canvas    — viewport fixed, prims that don't fit are discarded,
 *                      used by yui chrome (popups, dialogs, statusbar).
 *
 * They share the grid model, font cache, flyweight registry, complex-prim
 * factory, and GPU staging buffers via `struct yetty_ydraw_canvas` —
 * the polymorphic base. Each variant owns a `struct yetty_ydraw_canvas *`
 * created via `ydraw_canvas_create(context, ops, impl=variant)` and freed
 * via `yetty_ydraw_canvas_destroy(base)` (the polymorphic destroy dispatches
 * the variant's `destroy` op which frees variant-specific state before the
 * base teardown runs).
 *
 * Not part of any inter-module public API — only the canvas .c files in
 * src/yetty/ydraw/ include this header.
 */
#ifndef YETTY_YDRAW_CANVAS_INTERNAL_H
#define YETTY_YDRAW_CANVAS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/ydraw/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ydraw_font;
struct yetty_ymsdf_generator;
struct yetty_ydraw_core_figure_factory;
struct yetty_ydraw_core_figure_instance;
struct yetty_ydraw_core_flyweight_registry;
struct yetty_ywire_wire_statemachine;

/*===========================================================================
 * Shared grid types
 *===========================================================================*/

/* Reference to a primitive on a (possibly different) line. */
struct ydraw_canvas_prim_ref {
    uint16_t lines_ahead; /* relative offset to base line (0 = same line) */
    uint16_t prim_index;  /* index within base line's prims array */
};

struct ydraw_canvas_prim_ref_array {
    struct ydraw_canvas_prim_ref *data;
    uint32_t count;
    uint32_t capacity;
};

struct ydraw_canvas_grid_cell {
    struct ydraw_canvas_prim_ref_array refs;
};

/* A single primitive's payload, stored in its owning line's arena.
 * Storing an offset (not a pointer) keeps prim_data stable across arena
 * reallocations. */
struct ydraw_canvas_prim_data {
    uint32_t rolling_row;  /* cursor-line at insertion (0 for static-canvas) */
    uint32_t arena_offset;
    uint32_t word_count;
};

struct ydraw_canvas_prim_data_array {
    struct ydraw_canvas_prim_data *data;
    uint32_t count;
    uint32_t capacity;
};

/* Refcounted font-cache handle attached to a grid line. The grid_line owns
 * one cache ref per entry, released at grid_line_free. */
struct ydraw_canvas_font_entry {
    yetty_yfont_cache_handle handle;
};

/* A single row/line in the grid. */
struct ydraw_canvas_grid_line {
    struct ydraw_canvas_prim_data_array prims;

    /* Payload arena: every prim stores its words at
     * arena[prim.arena_offset .. prim.arena_offset + prim.word_count).
     * One allocation per line, doubling growth. */
    uint32_t *arena;
    uint32_t  arena_count;
    uint32_t  arena_capacity;

    struct ydraw_canvas_grid_cell *cells;
    uint32_t cell_count;
    uint32_t cell_capacity;

    /* Font cache refs this line owns (one per attached font). */
    struct ydraw_canvas_font_entry *fonts;
    uint32_t font_count;
    uint32_t font_capacity;

    /* Complex prim instances whose base (last overlapping line) is this. */
    struct yetty_ydraw_core_figure_instance **figures;
    uint32_t figure_count;
    uint32_t figure_capacity;
};

/* Simple line array. Both canvases use this. Scrolling-canvas grows it on
 * demand as content arrives; static-canvas sizes it once to grid_size.rows. */
struct ydraw_canvas_line_buffer {
    struct ydraw_canvas_grid_line *lines;
    uint32_t capacity;
    uint32_t count;
};

/*===========================================================================
 * Polymorphic canvas
 *===========================================================================*/

struct yetty_ydraw_canvas_ops;

struct yetty_ydraw_canvas {
    const struct yetty_ydraw_canvas_ops *ops;
    void *impl;                                   /* variant pointer (back-ref) */

    struct yetty_ycore_pixel_size cell_size;
    struct yetty_ycore_grid_size  grid_size;

    /* Packed GPU staging buffers (filled by each variant's rebuild_grid /
     * build_prim_staging). */
    uint32_t *grid_staging;
    uint32_t  grid_staging_count;
    uint32_t  grid_staging_capacity;
    uint32_t *prim_staging;
    uint32_t  prim_staging_count;
    uint32_t  prim_staging_capacity;
    bool      dirty;

    /* Default font (slot 0). default_handle is a long-lived cache ref. */
    struct yetty_ydraw_font *default_font;
    yetty_yfont_cache_handle  default_handle;

    /* Font kind: 0 = MSDF (default, CDB-based), 1 = raster (TTF-based). */
    int   font_render_method;
    float raster_base_size;

    /* Resource directories + family (copied from config at create). */
    char shaders_dir[512];
    char fonts_dir[512];
    char font_family[128];

    /* Flyweight registry (SDF prim handlers) + complex-prim factory
     * (yplot / yimage / ymesh instance ops). */
    struct yetty_ydraw_core_flyweight_registry   *flyweight_registry;
    struct yetty_ydraw_core_figure_factory *figure_factory;

    /* Polymorphic MSDF generator (borrowed from context->gpu_context).
     * NULL in tests/hosts that don't initialise it. */
    struct yetty_ymsdf_generator *msdf_generator;

    /* Per-canvas font cache. Owns every font this canvas materialises;
     * lines hold cache handles via grid_line.fonts[]. Slot 0 is the
     * default font (installed at create). */
    struct yetty_yfont_cache *font_cache;
};

/* Vtable — operations that differ between variants. Plain getters that
 * read from canvas fields directly (cell_size, grid_size, dirty, ...) are
 * NOT in the vtable; the polymorphic wrapper reads the base struct. */
struct yetty_ydraw_canvas_ops {
    const char *name;  /* "scrolling" | "static" — diagnostics */

    /* Lifecycle. Receives the base; impl uses canvas->impl to recover the
     * variant. Frees variant-specific state and the variant struct itself;
     * the polymorphic destroy then tears down the base. */
    struct yetty_ycore_void_result (*destroy)(struct yetty_ydraw_canvas *);

    /* Config — set_cell_size is identical for both, lives as a direct
     * setter on the base. set_grid_size differs (static reallocates the
     * fixed lines[] array). */
    struct yetty_ycore_void_result (*set_grid_size)(
        struct yetty_ydraw_canvas *, struct yetty_ycore_grid_size);

    /* The single ingestion op: pull primitives from the SM via the
     * streaming prim_iter. The canvas keeps per-envelope state on its
     * variant struct across SM yield/resume (font_map, attach_list, iter)
     * until the iter signals end-of-envelope; then per-envelope
     * finalisation runs (attach pass, scroll viewport for scrolling, ...).
     */
    struct yetty_ycore_void_result (*process_input)(
        struct yetty_ydraw_canvas *, struct yetty_ywire_wire_statemachine *);

    /* State */
    struct yetty_ycore_void_result (*clear)(struct yetty_ydraw_canvas *);
    uint32_t (*primitive_count)(const struct yetty_ydraw_canvas *);

    /* GPU staging */
    struct yetty_ycore_void_result (*rebuild_grid)(struct yetty_ydraw_canvas *);
    struct yetty_ydraw_prim_staging_result (*build_prim_staging)(
        struct yetty_ydraw_canvas *);
    uint32_t (*prim_gpu_size)(const struct yetty_ydraw_canvas *);

    /* Complex prims */
    uint32_t (*figure_count)(const struct yetty_ydraw_canvas *);
    struct yetty_ydraw_core_figure_instance *(*get_figure)(
        const struct yetty_ydraw_canvas *, uint32_t index);

    /* Glyph iteration */
    void (*for_each_glyph)(struct yetty_ydraw_canvas *,
                           yetty_ydraw_canvas_glyph_visitor visitor, void *user);

    /* Cursor / scroll. Static-canvas wires these as no-ops. */
    struct yetty_ycore_void_result (*set_cursor_pos)(
        struct yetty_ydraw_canvas *, struct yetty_ycore_grid_cursor_pos);
    struct yetty_ycore_void_result (*scroll_lines)(
        struct yetty_ydraw_canvas *, uint16_t num_lines);
    struct yetty_ycore_void_result (*set_view_top)(
        struct yetty_ydraw_canvas *, bool active, uint32_t view_top);
    uint32_t (*rolling_row_0)(struct yetty_ydraw_canvas *);
    uint32_t (*live_rolling_row_0)(struct yetty_ydraw_canvas *);
    struct yetty_ycore_void_result (*set_scroll_callback)(
        struct yetty_ydraw_canvas *,
        yetty_ydraw_canvas_scroll_callback callback, void *userdata);
    struct yetty_ycore_void_result (*set_cursor_callback)(
        struct yetty_ydraw_canvas *,
        yetty_ydraw_canvas_cursor_callback callback, void *userdata);
};

/*===========================================================================
 * Polymorphic base lifecycle
 *
 * `ydraw_canvas_create` allocates the base and sets up the shared state:
 * flyweight registry, complex-prim factory + yplot/yimage/ymesh
 * registrations, font cache, default font, dirs/family/render-method.
 *
 * Public users only see `yetty_ydraw_canvas_destroy` (in canvas.h) which
 * dispatches the variant's destroy op (frees variant + variant-specific
 * state) and then frees the base + shared state.
 *
 * Variant `*_create` calls this; variant `_destroy_impl` is in the vtable.
 *===========================================================================*/

/* yetty_ydraw_canvas_ptr_result is declared in <yetty/ydraw/canvas.h>. */

struct yetty_ydraw_canvas_ptr_result ydraw_canvas_create(
    const struct yetty_context *context,
    const struct yetty_ydraw_canvas_ops *ops,
    void *impl);

/*===========================================================================
 * Constants
 *===========================================================================*/

/* Glyph primitive type (not in ysdf types.gen.h since not SDF). */
#define YETTY_YSDF_GLYPH 200
/* Glyph prim layout: type, z_order, x, y, font_size,
 * packed(glyph_idx | font_id), color */
#define YDRAW_GLYPH_WORDS 7

#define YDRAW_CANVAS_INITIAL_LINE_CAPACITY     64
#define YDRAW_CANVAS_INITIAL_CELL_CAPACITY     16
#define YDRAW_CANVAS_INITIAL_PRIM_CAPACITY     16
#define YDRAW_CANVAS_INITIAL_REF_CAPACITY      2
#define YDRAW_CANVAS_INITIAL_STAGING_CAPACITY  4096

/*===========================================================================
 * Helpers — implemented in canvas.c
 *===========================================================================*/

/* prim_ref_array */
void ydraw_canvas_prim_ref_array_init(struct ydraw_canvas_prim_ref_array *arr);
void ydraw_canvas_prim_ref_array_free(struct ydraw_canvas_prim_ref_array *arr);
struct yetty_ycore_void_result ydraw_canvas_prim_ref_array_push(
    struct ydraw_canvas_prim_ref_array *arr, struct ydraw_canvas_prim_ref ref);

/* prim_data_array */
void ydraw_canvas_prim_data_array_init(struct ydraw_canvas_prim_data_array *arr);
void ydraw_canvas_prim_data_array_free(struct ydraw_canvas_prim_data_array *arr);

/* grid_line */
struct yetty_ycore_void_result ydraw_canvas_grid_line_init(
    struct ydraw_canvas_grid_line *line);
struct yetty_ycore_void_result ydraw_canvas_grid_line_free(
    struct ydraw_canvas_grid_line *line, struct yetty_yfont_cache *cache);
struct yetty_ycore_void_result ydraw_canvas_grid_line_ensure_cells(
    struct ydraw_canvas_grid_line *line, uint32_t min_cells);
struct yetty_ycore_void_result ydraw_canvas_grid_line_arena_append(
    struct ydraw_canvas_grid_line *line, const float *data, uint32_t word_count,
    uint32_t *out_offset);
/* Returns the prim index in line->prims as the success value. */
struct uint32_result ydraw_canvas_grid_line_push_prim(
    struct ydraw_canvas_grid_line *line, uint32_t rolling_row,
    const float *data, uint32_t word_count);

/* line_buffer */
void ydraw_canvas_line_buffer_init(struct ydraw_canvas_line_buffer *buf);
struct yetty_ycore_void_result ydraw_canvas_line_buffer_free(
    struct ydraw_canvas_line_buffer *buf, struct yetty_yfont_cache *cache);
struct ydraw_canvas_grid_line *ydraw_canvas_line_buffer_get(
    struct ydraw_canvas_line_buffer *buf, uint32_t index);

/* Font-blob utilities */
int      ydraw_canvas_blob_is_raster(const char *name, int canvas_method);
uint64_t ydraw_canvas_fnv1a64(const uint8_t *data, size_t len);
void     ydraw_canvas_sanitize_identifier(char *dst, size_t dst_cap, const char *src);

/* Font resolution against the canvas's font_cache */
struct yetty_yfont_cache_ref_result ydraw_canvas_get_default_font_ref(
    struct yetty_ydraw_canvas *canvas);
struct yetty_ycore_void_result ydraw_canvas_ensure_blob_font_cdb(
    struct yetty_ydraw_canvas *canvas, const uint8_t *ttf, uint32_t ttf_len,
    const char *hint_name, char out_hex[17]);
struct yetty_yfont_cache_ref_result ydraw_canvas_resolve_blob_font_handle(
    struct yetty_ydraw_canvas *canvas, const char *hex);

/* Staging growth */
struct yetty_ycore_void_result ydraw_canvas_ensure_grid_staging(
    struct yetty_ydraw_canvas *canvas, uint32_t min_size);
struct yetty_ycore_void_result ydraw_canvas_ensure_prim_staging(
    struct yetty_ydraw_canvas *canvas, uint32_t min_size);

/*===========================================================================
 * Per-envelope font map — populated as FONT prims are encountered during
 * one process_input pass over an OSC envelope.
 *===========================================================================*/

struct ydraw_canvas_font_map_entry {
    char                      hex[17];   /* "" if not declared */
    bool                      declared;
    bool                      resolved;
    struct yetty_ydraw_font *font;
    yetty_yfont_cache_handle  handle;
};

struct ydraw_canvas_font_map {
    struct ydraw_canvas_font_map_entry *entries;
    uint32_t                             capacity;
};

void ydraw_canvas_font_map_init(struct ydraw_canvas_font_map *m);
struct yetty_ycore_void_result ydraw_canvas_font_map_grow(
    struct ydraw_canvas_font_map *m, uint32_t want);
const struct ydraw_canvas_font_map_entry *ydraw_canvas_font_map_get(
    const struct ydraw_canvas_font_map *m, uint32_t id);
void ydraw_canvas_font_map_release_all(
    struct ydraw_canvas_font_map *m, struct yetty_yfont_cache *cache);

/*===========================================================================
 * Per-envelope attach list — highest row each unique font handle was seen
 * on. attach_handle_to_line is called once per entry after the envelope
 * ends, instead of per text-span.
 *===========================================================================*/

struct ydraw_canvas_buffer_attach_entry {
    yetty_yfont_cache_handle handle;
    uint32_t                 max_row;
};

struct ydraw_canvas_buffer_attach_list {
    struct ydraw_canvas_buffer_attach_entry *entries;
    uint32_t                                  count;
    uint32_t                                  capacity;
};

void ydraw_canvas_buffer_attach_init(struct ydraw_canvas_buffer_attach_list *l);
void ydraw_canvas_buffer_attach_free(struct ydraw_canvas_buffer_attach_list *l);
struct yetty_ycore_void_result ydraw_canvas_buffer_attach_note(
    struct ydraw_canvas_buffer_attach_list *l,
    yetty_yfont_cache_handle handle, uint32_t row);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CANVAS_INTERNAL_H */
