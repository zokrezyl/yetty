/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yfigure).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YFIGURE_FIGURE_H
#define YETTY_YCLASSGEN_YFIGURE_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

/* ---- figure base data slice (PRIVATE) -----------------------------------
 * The yclass data slice for the figure base class — the first slice in every
 * figure object (right after the yclass object header). The layout is not
 * exported: figure.h only forward-declares the struct. Members that other
 * classes legitimately read/write carry a `property` annotation, which makes
 * codegen emit the standardized get/set accessors plus the opaque data-block
 * handle (yetty_yfigure_figure_data_get); unannotated members stay private to
 * this TU. There is deliberately no `self_obj` member — the owning object of
 * a base handle is simply `(struct yetty_yclass_object *)handle - 1`, since
 * the base is always the first slice.
 *
 *   rect   : AABB in target pixel space; moves go through the parent's
 *            set_rect so damage tracking stays correct. (No `id` field — id is
 *            a parent-scoped name the container assigns.)
 *   z      : stacking order within the parent (higher renders later / wins
 *            hit-tests); ties break on insertion order.
 *   hidden : parent skips this child entirely (no render, no hit).
 *   dirty  : contents changed without geometry moving; the parent ORs this
 *            into its damage region next render pass and clears it.
 *   absolute_coords : the figure lays out / hit-tests / paints its content
 *            in pane-root pixel space and only scissor-clips to its rect —
 *            no per-figure re-origin (ygui chrome + scrolling sub-figures).
 *            When 0 (the default for every producer figure: yplot/yimage/…),
 *            content is local to the figure rect. The parent's hit-test reads
 *            this to decide whether to report the cursor in pane-local space
 *            (absolute) or re-origined to the figure rect (local). */
struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_figure;
struct yetty_yfigure_figure_ptr_result {
    int ok;
    union {
        struct yetty_yfigure_figure *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yfigure_figure_ptr_result yetty_yfigure_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yfigure_figure_to(struct yetty_yfigure_figure *data);
struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_rectangle value);
struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *obj,
                                                          int value);
struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *obj,
                                                               int value);
struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *obj,
                                                              int value);
struct yetty_ycore_int_result yetty_yfigure_figure_absolute_coords_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_absolute_coords_set(
    struct yetty_yclass_object *obj, int value);

/* destroy: tears down concrete state and frees the figure. The base
 * default is a no-op — the base class is never instantiated as a leaf;
 * every concrete kind overrides this and frees via object_free. */
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
/* dump_state: heap text snapshot for tests. Base default yields a NULL
 * string so the yetty_yfigure_dump wrapper emits its rect fallback. */
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object *obj,
                                                            int indent);
/* process_input: consume the figure's wire body straight off the SM.
 * Base default rejects — a purely visual figure ignores wire updates.
 * The container only routes here for kinds that override it (capability
 * detected via yetty_yfigure_figure_implements). */
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *statemachine);
/* process_bytes: apply a buffered wire body. Base default rejects. */
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
/* reset_content: drop content, keep GPU state. Base default rejects so the
 * container falls back to destroy + mint for kinds that don't support it. */
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object *obj);
/* set_scroll: move the figure's scroll offset (the content coordinate shown
 * at the rect's top-left). Base default rejects — a non-scrolling figure has
 * no content larger than its rect. Scrollable kinds (ygrid) override this so
 * the container can drive the offset by id from a wire SET_CHILD_SCROLL
 * record or the terminal's autonomous wheel/key handler, without re-shipping
 * content. */
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object *obj,
                                                        float scroll_x, float scroll_y);
/* set_content_size: declare the figure's content extent in px (the rect stays
 * the visible window; content may be larger, making the figure a scroll
 * viewport). Base default rejects. Scrollable kinds override it. */
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object *obj,
                                                              float content_w, float content_h);

typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(
    struct yetty_yclass_object *, struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(
    struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_reset_content_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_scroll_fn)(struct yetty_yclass_object *,
                                                                      float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_content_size_fn)(
    struct yetty_yclass_object *, float, float);

struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yfigure_register(void);

#ifdef __cplusplus
}
#endif

#endif
