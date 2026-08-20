/* GENERATED — do not edit. */
/* Public interface for regular class(es) `vtermgrid` (module: yscene).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YSCENE_VTERMGRID_H
#define YETTY_YCLASSGEN_YSCENE_VTERMGRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_rectangle;
struct yetty_ydraw_target;
struct yetty_yfont_ms_font;
struct yetty_yframework;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_CELL_ATTR
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_CELL_ATTR
/* Per-cell attribute bits exposed to the GPU packer / consumers. Kept as a
 * compact bitset (not the libvterm bitfield) so the packed GPU cell can carry
 * it in one 16-bit field. */
enum yetty_yscene_vtermgrid_cell_attr {
    YETTY_YSCENE_VTERMGRID_ATTR_BOLD = 1,
    YETTY_YSCENE_VTERMGRID_ATTR_UNDERLINE = 2,
    YETTY_YSCENE_VTERMGRID_ATTR_UNDERLINE2 = 4,
    YETTY_YSCENE_VTERMGRID_ATTR_ITALIC = 8,
    YETTY_YSCENE_VTERMGRID_ATTR_REVERSE = 16,
    YETTY_YSCENE_VTERMGRID_ATTR_BLINK = 32,
    YETTY_YSCENE_VTERMGRID_ATTR_STRIKE = 64,
    YETTY_YSCENE_VTERMGRID_ATTR_CONCEAL = 128,
    YETTY_YSCENE_VTERMGRID_ATTR_FAINT = 256,
    YETTY_YSCENE_VTERMGRID_ATTR_UNDERLINE_CURLY = 512,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_CELL
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_CELL
/* One resolved terminal cell. `glyph` is the Unicode codepoint (the grid runs
 * libvterm with a NULL glyph resolver, so the cell's glyph index IS the
 * codepoint); the GPU packer maps it to an atlas slot. `fg`/`bg` are packed
 * 0xFFBBGGRR. `width` is 1 (normal), 2 (wide head) or 0 (wide continuation /
 * empty). */
struct yetty_yscene_vtermgrid_cell {
    uint32_t glyph;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
    uint8_t width;
    uint8_t pad;
};
#endif

struct yetty_yclass_ptr_result yetty_yscene_vtermgrid_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yscene_vtermgrid;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_VTERMGRID_PTR_RESULT
struct yetty_yscene_vtermgrid_ptr_result {
    int ok;
    union {
        struct yetty_yscene_vtermgrid *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yscene_vtermgrid_ptr_result yetty_yscene_vtermgrid_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yscene_vtermgrid_to(
    struct yetty_yscene_vtermgrid *data);

struct yetty_yclass_object_ptr_result yetty_yscene_vtermgrid_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yscene_register(void);

struct yetty_yclass_object_ptr_result yetty_yscene_vtermgrid_make(uint32_t rows, uint32_t cols);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_dispose(struct yetty_yclass_object *obj);
/* Feed ordinary terminal bytes to libvterm, holding back a trailing partial
 * UTF-8 sequence so a chunk split mid-codepoint never corrupts decoding. Local
 * (expose) — the scene's virtual@ terminal_grid_write is the RPC surface and
 * forwards here. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_write(struct yetty_yclass_object *obj,
                                                            const uint8_t *bytes, size_t len);
/* Discard all client terminal state and rebuild a fresh grid at the given
 * geometry — the reconnect / new-stream-epoch path. No parser checkpoint or
 * private state is carried across. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_reset(struct yetty_yclass_object *obj,
                                                            uint32_t rows, uint32_t cols);
/* Resize the grid in place; the screen layer reallocs its own buffers and
 * lineinfo (no manual fix needed, unlike the state-layer engine). */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_resize(struct yetty_yclass_object *obj,
                                                             uint32_t rows, uint32_t cols);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_dims(struct yetty_yclass_object *obj,
                                                           uint32_t *rows, uint32_t *cols);
struct yetty_ycore_uint32_result yetty_yscene_vtermgrid_generation(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_cursor(struct yetty_yclass_object *obj,
                                                             uint32_t *row, uint32_t *col,
                                                             int *visible);
/* Read one cell of the current screen. Out-of-range positions yield a blank
 * cell. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_cell(struct yetty_yclass_object *obj,
                                                           uint32_t row, uint32_t col,
                                                           struct yetty_yscene_vtermgrid_cell *out);
/* Bind the GPU context (from the framework runtime) + the MSDF font this grid
 * renders with, and build the pipeline. Local (expose): WebGPU handles come
 * from `runtime->gpu`, so no raw WebGPU type ever crosses the generated API. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_gpu_setup(struct yetty_yclass_object *obj,
                                                                struct yetty_yframework *runtime,
                                                                struct yetty_yfont_ms_font *font);
/* Attach the optional FALLBACK face (#89): a raster ms-font whose FreeType
 * fallback chain covers codepoints the MSDF base face lacks (CJK etc.).
 * Borrowed — the scene owns both fonts. Local (expose). */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_gpu_set_fallback_font(
    struct yetty_yclass_object *obj, struct yetty_yfont_ms_font *fallback_font);
/* Packed mode/prop flags for state-equality tests (review #14): bit0 =
 * cursor blink, bit1 = reverse video (DECSCNM); bits 8..15 = cursor shape
 * (VTERM_PROP_CURSORSHAPE_*). Rendering state only — parser/pen internals
 * are pinned behaviorally by fragmentation + continuation vectors. */
struct yetty_ycore_uint32_result yetty_yscene_vtermgrid_mode_flags(struct yetty_yclass_object *obj);
/* Pending (untaken) terminal-reply byte count — peek, does not drain. */
struct yetty_ycore_uint32_result yetty_yscene_vtermgrid_reply_pending(
    struct yetty_yclass_object *obj);
/* Scalar-word access into the pending reply bytes (review #15): 8 bytes at
 * word_index*8, packed LE into a u64 — RPC-marshallable, unlike a buffer
 * return. Consume drops the drained prefix. */
struct yetty_ycore_uint64_result yetty_yscene_vtermgrid_reply_word(struct yetty_yclass_object *obj,
                                                                   uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_reply_consume(struct yetty_yclass_object *obj,
                                                                    uint32_t byte_count);
/* Take (and clear) the accumulated terminal reply bytes — the scalar-word
 * drain (reply_word/reply_consume) is the production route; this whole-copy
 * form serves local embedders and tests. */
struct yetty_ycore_uint32_result yetty_yscene_vtermgrid_take_replies(
    struct yetty_yclass_object *obj, uint8_t *out, uint32_t out_capacity);
struct yetty_ycore_int_result yetty_yscene_vtermgrid_on_alt_screen(struct yetty_yclass_object *obj);
/* Selection span (review #11): a linear [start..end] cell range rendered
 * INVERTED (fg/bg swap, like tmux/xterm selections). active == 0 clears.
 * Local (expose): copy-mode chrome drives this from the bridge. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_set_selection(struct yetty_yclass_object *obj,
                                                                    uint32_t start_row,
                                                                    uint32_t start_col,
                                                                    uint32_t end_row,
                                                                    uint32_t end_col, int active);
/* Draw the current cell grid into `target`, positioned/clipped to `rect`
 * (logical pixels, pane-local). No-op when GPU state or font is absent
 * (headless). Composites with LoadOp_Load over whatever is already in the
 * target. Local (expose): the scene calls this from its render pass. */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_render(struct yetty_yclass_object *obj,
                                                             struct yetty_ydraw_target *target,
                                                             struct yetty_ycore_rectangle rect);

#ifdef __cplusplus
}
#endif

#endif
