/* GENERATED — do not edit. */
/* Public interface for regular class(es) `grid` (module: ygrid).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGRID_GRID_H
#define YETTY_YCLASSGEN_YGRID_GRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_ydraw_composite_factory;
struct yetty_yfigure_figure;
struct yetty_yfigure_registry;
struct yetty_yfont_font;
struct yetty_ygrid_grid;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGRID_FACTORY_ARGS
#define YETTY_YCLASSGEN_TYPE_YETTY_YGRID_FACTORY_ARGS
/* Bundle of host-owned pointers handed to every ygrid the factory mints.
 * All fields are borrowed — the host (terminal / yui) keeps the fonts and
 * the composite factory alive for the lifetime of every ygrid the
 * registry might still be holding.
 *
 *   default_font       slot-0 font for GLYPH/TEXT_DRAWABLE_LIST
 *                      expansion. NULL → no default font, glyph records
 *                      silently drop.
 *   bold_font / italic_font / bold_italic_font
 *                      optional styled faces registered at font slots
 *                      1/2/3 so producers that emit those font_ids get
 *                      real styled glyphs. NULL leaves the slot
 *                      unregistered (glyphs referencing it are dropped,
 *                      so producers must only emit a styled font_id when
 *                      its face is present).
 *   composite_factory  composite renderer (yplot / yimage / yvideo / …).
 *                      NULL → composite records silently drop.
 *   absolute_coords    coordinate mode for producer-kind figures minted
 *                      via register_factory_for_kind. 0 (default) =
 *                      local: content drawn from the figure origin in
 *                      framebuffer pixels — the terminal's
 *                      scrolling-layer producers. 1 = absolute: content
 *                      is in logical pixels and scaled to framebuffer by
 *                      content_scale — the ygui chrome path (ygreeter /
 *                      ybrowser), where producer widgets emit at their
 *                      absolute widget rect. Must match how the hosting
 *                      app's widgets emit coords. */
struct yetty_ygrid_factory_args {
    struct yetty_yfont_font *default_font;
    struct yetty_yfont_font *bold_font;
    struct yetty_yfont_font *italic_font;
    struct yetty_yfont_font *bold_italic_font;
    struct yetty_ydraw_composite_factory *composite_factory;
    int absolute_coords;
};
#endif

struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygrid_grid;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGRID_GRID_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGRID_GRID_PTR_RESULT
struct yetty_ygrid_grid_ptr_result {
    int ok;
    union {
        struct yetty_ygrid_grid *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygrid_grid_ptr_result yetty_ygrid_grid_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygrid_grid_to(struct yetty_ygrid_grid *data);

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygrid_register(void);

/* Attach a composite figure factory. Borrowed; lifetime must
 * outlive the ygrid. Complex prims (yplot / yimage / etc.) arriving
 * via process_bytes after this call mint a figure instance through
 * the factory and are rendered alongside the SDF / glyph pass. */
void yetty_ygrid_set_composite_factory(struct yetty_ygrid_grid *grid,
                                       struct yetty_ydraw_composite_factory *factory);
/* Create an empty ygrid figure with the given AABB in absolute target
 * pixel space. The grid cell layout (cols × rows) controls how the GPU
 * pipeline will eventually bucket prims for batch dispatch. The
 * context supplies the GPU device / queue / surface_format used to
 * build the pipeline and per-instance bind group. */
struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(struct yetty_ycore_rectangle rect,
                                                      uint32_t grid_cols, uint32_t grid_rows,
                                                      const struct yetty_context *context);
/* Register the ygrid factory under YETTY_YFIGURE_KIND_YGRID with the
 * given registry. Subsequent admin CREATE_CHILD records with kind=YGRID
 * land in the factory, which mints a ygrid at the supplied rect using
 * default grid dims (1x1 — the ygrid is for flat prims; cell bucketing
 * isn't useful for arbitrary widget rects).
 *
 * `args` is borrowed and must outlive every ygrid the factory will ever
 * mint. The host should keep it as part of its own state. */
struct yetty_ycore_void_result yetty_ygrid_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_ygrid_factory_args *args);
/* Register the ygrid factory under an arbitrary kind code. Used by
 * ygui's complex producer widgets (yplot/yimage/yvideo/yzoo/yjungle)
 * so their content lands in dedicated kind slots on the wire — same
 * underlying renderer but a distinct kind tag that proxies and
 * analyzers can route on. Args has the same lifetime contract. */
struct yetty_ycore_void_result yetty_ygrid_register_factory_for_kind(
    struct yetty_yfigure_registry *registry, uint32_t kind,
    const struct yetty_ygrid_factory_args *args);
/* Upcast helper — a ygrid is a figure. */
struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid);
/* Append one wire record to the grid. `record_bytes` points at the
 * full u32-type | u32-payload_size | bytes block; `record_len` is the
 * total size of that block (8 + payload_size). Coordinates inside the
 * record are interpreted as LOCAL to the grid's origin.
 *
 * Storage is opaque at this layer: the bytes are copied verbatim and
 * parsed lazily by the render path via the ydraw-core drawable-list
 * entry registry.
 *
 * NAMING: this is the in-process, raw-bytes entry point — the legacy
 * surface tools and tests still drive directly. The canonical yclass
 * slot is `yetty_ygrid_add_record(obj, struct yetty_ycore_buffer)`; it
 * dispatches via the registered class and works for local AND remote
 * callers. This `_local` variant exists only until every caller
 * migrates. */
struct yetty_ycore_void_result yetty_ygrid_add_record_local(struct yetty_ygrid_grid *grid,
                                                            const uint8_t *record_bytes,
                                                            size_t record_len);
/* Drop every record. The figure stays alive at its current rect with
 * an empty payload (renders nothing). Marks the figure dirty so the
 * compositor exposes the previously-covered region.
 *
 * NAMING: see `yetty_ygrid_add_record_local` — the unsuffixed name is
 * the yclass slot stub; this is the in-process variant. */
struct yetty_ycore_void_result yetty_ygrid_clear_local(struct yetty_ygrid_grid *grid);
/* Attach a font at the given slot. Slot 0 is the default font (the slot
 * a GLYPH record addresses when its packed `font_id` field is 0); higher
 * slots are for alternate fonts (e.g. PDF-embedded). Pass `font = NULL`
 * to clear a slot.
 *
 * The font's gpu_resource_set is attached as a child of the ygrid's rs,
 * its namespaced helpers (<ns>_glyph_size, <ns>_glyph_sample, …) are
 * merged into the compiled shader, and a per-slot dispatcher routes
 * `slot` to the right helper. Changing the font set after the first
 * render triggers a shader recompile via the binder's hash-change path.
 *
 * Caller retains ownership of the font — ygrid borrows the pointer and
 * does NOT destroy it on ygrid_destroy. */
struct yetty_ycore_void_result yetty_ygrid_set_font(struct yetty_ygrid_grid *grid, uint32_t slot,
                                                    struct yetty_yfont_font *font);

#ifdef __cplusplus
}
#endif

#endif
