/* GENERATED — do not edit. */
/* Object API for regular class(es) `scene` (implementation module: yscene).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YSCENE_SCENE_H
#define YETTY_YCLASSGEN_API_YSCENE_SCENE_H

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
struct yetty_ydraw_drawable_list_registry;
struct yetty_yfigure_figure;
struct yetty_yfigure_registry;
struct yetty_yfont_font;
struct yetty_yscene_scene;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_FACTORY_ARGS
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_FACTORY_ARGS
/* Host inputs every wire-minted scene borrows. Lifetime: owned by the
 * host (terminal), outliving every scene minted through the registry —
 * the same contract as ygrid's factory args bundle. */
struct yetty_yscene_factory_args {
    struct yetty_ydraw_composite_factory *composite_factory;
    struct yetty_yfont_font *default_font;
    struct yetty_yfont_font *bold_font;
    struct yetty_yfont_font *italic_font;
    struct yetty_yfont_font *bold_italic_font;
    int absolute_coords;
};
#endif

struct yetty_yclass_ptr_result yetty_yscene_scene_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yscene_scene;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_SCENE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSCENE_SCENE_PTR_RESULT
struct yetty_yscene_scene_ptr_result {
    int ok;
    union {
        struct yetty_yscene_scene *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yscene_scene_ptr_result yetty_yscene_scene_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_to(struct yetty_yscene_scene *data);

/* Bind the shared wire-record registry (borrowed — the framework's
 * instance) to the scene AND its dom, as one binding. Rejected once the
 * dom holds content — record strides must not change under existing
 * spans. */
struct yetty_ycore_void_result yetty_yscene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry);
struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x,
                                                               float translate_y);
struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity);
struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z);
struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index);
struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *obj);
/* Layout BARRIER (#699 review 19): a multi-call layout change (figure
 * reseat, grid resize, chrome restage) applies as ONE deliberate frame —
 * render kicks between begin and end coalesce into a single kick at end.
 * Nesting-safe (depth counter); pipelined over yRPC the pair brackets the
 * ordered layout calls exactly. */
struct yetty_ycore_void_result yetty_yscene_layout_barrier_begin(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_end(struct yetty_yclass_object *obj);
/* The receiver grid generation — remote-observable over yRPC (value call):
 * the reset barrier polls it to CONFIRM the fresh parser exists before the
 * vtsink republishes (review #17). */
struct yetty_ycore_uint32_result yetty_yscene_terminal_grid_generation(
    struct yetty_yclass_object *obj);
/* Remote-facing terminal-grid slots (#699): the ymux attach bridge drives the
 * pane through these over the yclass RPC. */
struct yetty_ycore_void_result yetty_yscene_terminal_grid_create(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols,
                                                                 float cell_width,
                                                                 float cell_height);
/* Synchronous (request/response): the ymux attach bridge streams VT redraws
 * here over the pane's DCS wire. A synchronous call surfaces write errors to
 * the bridge (a oneway call would swallow them — a silently-failing grid write
 * looks exactly like a frozen pane). Keystrokes that arrive on the shared
 * stdin while the bridge blocks on the reply are captured by the transport's
 * raw sink (yetty_yclass_transport_dcs_set_raw_sink) instead of being dropped,
 * so the round-trip no longer costs input. */
struct yetty_ycore_void_result yetty_yscene_terminal_grid_write(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer bytes);
/* Atomic terminal+rich publish over RPC (#699/#4): the ymux bridge sends both
 * halves of one content update in a single call so they render together. */
struct yetty_ycore_void_result yetty_yscene_terminal_write_content(struct yetty_yclass_object *obj,
                                                                   struct yetty_ycore_buffer vt,
                                                                   struct yetty_ycore_buffer rich);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_resize(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols);
/* Terminal-reply drain over yRPC (#699 reply route, review #15). Buffer
 * RETURNS are not wire-marshallable, so the drain is scalar-word shaped:
 * pending() -> byte count, reply_word(index) -> 8 payload bytes packed LE
 * into a u64, reply_consume(count) -> drop the drained prefix. The bridge
 * polls after write batches and forwards the bytes through the attachment
 * input path to the daemon — the single controlling attachment owns the
 * answer, so N clients never reply N times. */
struct yetty_ycore_uint32_result yetty_yscene_terminal_reply_pending(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_terminal_reply_word(struct yetty_yclass_object *obj,
                                                                  uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_terminal_reply_consume(struct yetty_yclass_object *obj,
                                                                   uint32_t byte_count);
/* Overlay-input drain over yRPC (review #15): same scalar-word shape as the
 * reply drain. The head event is addressed as class+length (packed) and
 * word reads; consume pops it. The bridge forwards each drained event to
 * the daemon (OVERLAY_INPUT) — the daemon owns the chrome, tmux-style, so
 * chrome interaction logic runs where the chrome content originates. */
struct yetty_ycore_uint64_result yetty_yscene_input_event_head(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_input_event_word(struct yetty_yclass_object *obj,
                                                               uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_input_event_pop(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yscene_dispatch_key(struct yetty_yclass_object *obj,
                                                           uint32_t input_class,
                                                           struct yetty_ycore_buffer bytes);
/* Chrome key-intake NOTE (review #17): the same recording dispatch_key
 * makes, WITHOUT the consumption verdict or the queue — a void call, so
 * the bridge can pipeline it from the hot key path (a value round-trip
 * there needs an idle RPC window a live feed rarely offers; the bridge
 * already knows the focus verdict client-side and forwards the bytes to
 * the daemon seat directly). */
struct yetty_ycore_void_result yetty_yscene_note_key_intake(struct yetty_yclass_object *obj,
                                                            uint32_t input_class,
                                                            struct yetty_ycore_buffer bytes);
/* Terminal-grid selection over RPC (#699.5, review #12): the bridge's
 * copy-mode/drag path drives the grid's inverted span through the scene —
 * the grid object itself never crosses the wire. */
struct yetty_ycore_void_result yetty_yscene_set_terminal_selection(
    struct yetty_yclass_object *obj, uint32_t start_row, uint32_t start_col, uint32_t end_row,
    uint32_t end_col, uint32_t active);
struct yetty_ycore_uint64_result yetty_yscene_dispatch_pointer(struct yetty_yclass_object *obj,
                                                               uint32_t local_x, uint32_t local_y,
                                                               uint32_t kind, uint32_t button,
                                                               uint32_t mods, uint32_t pressed);
struct yetty_ycore_void_result yetty_yscene_apply_content_transaction(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer rich);

struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *ctx);

/* Create/replace the embedded terminal grid at the given geometry. The cell
 * metrics are the pixel pitch the grid renders at; rich content anchored to
 * (row, col) is positioned against them, so they must match the grid. */
struct yetty_ycore_void_result yetty_yscene_scene_terminal_grid_create(
    struct yetty_yclass_object *obj, uint32_t rows, uint32_t cols, float cell_width,
    float cell_height);
/* Feed ordinary terminal bytes to the embedded grid. */
struct yetty_ycore_void_result yetty_yscene_scene_terminal_grid_write(
    struct yetty_yclass_object *obj, const uint8_t *bytes, size_t len);
/* Atomic content publish (#699/#4): feed the terminal VT bytes to the grid AND
 * apply the rich body together, with a SINGLE render at the end — so a frame is
 * never rendered with the terminal update but not its paired rich update (or
 * vice versa). Either half may be empty. */
struct yetty_ycore_void_result yetty_yscene_scene_terminal_write_content(
    struct yetty_yclass_object *obj, const uint8_t *vt_bytes, size_t vt_len,
    const uint32_t *rich_words, size_t rich_word_count);
/* Resize the embedded grid. */
struct yetty_ycore_void_result yetty_yscene_scene_terminal_grid_resize(
    struct yetty_yclass_object *obj, uint32_t rows, uint32_t cols);
/* The embedded terminal grid object (borrowed), or an error when absent —
 * lets tests/tools inspect the client grid's cells directly. */
struct yetty_yclass_object_ptr_result yetty_yscene_scene_terminal_grid(
    struct yetty_yclass_object *obj);
/* Create a scene figure. `context == NULL` (or a context without a
 * runtime) is HEADLESS mode — tests/tooling: no shader load, no binder;
 * the tree, adapter, derive and hit-test all work, render draws
 * nothing. With a real context the scene binds the framework's shared
 * drawable registry and builds its GPU pipeline (shared ydraw-layer
 * machinery: binder + yscene.wgsl + ysdf/effects libs). */
struct yetty_yscene_scene_ptr_result yetty_yscene_create(struct yetty_ycore_rectangle rect,
                                                         const struct yetty_context *context);
/* Tests only: arm the rich-DOM fault countdown — the Nth fallible stage
 * (declare/transform/append/mint/retire, in call order) fails. */
struct yetty_ycore_void_result yetty_yscene_scene_rich_fault_arm(struct yetty_yclass_object *obj,
                                                                 int countdown);
/* One atomic rich content transaction (#699.3: rich-only — the retired
 * semantic paint half no longer exists in the schema): the body is fully
 * staged/validated before publication, so a malformed rich body publishes
 * NOTHING. A present-but-empty body (record_count 0) clears the rich
 * world. */
struct yetty_ycore_void_result yetty_yscene_scene_apply_content_transaction(
    struct yetty_yclass_object *obj, const uint32_t *rich_words, size_t rich_word_count);
/* The recorded key-event serial (monotonic; 0 = none yet). */
struct yetty_ycore_uint64_result yetty_yscene_scene_key_event_serial(
    struct yetty_yclass_object *obj);
/* Chrome consumer drain (review #15): pop the OLDEST queued input event —
 * LOSSLESSLY. Returns the stored byte length (-1 when the queue is empty);
 * the class lands in out_class. The event is dequeued ONLY when the whole
 * payload fits in out_capacity; a short buffer copies nothing, keeps the
 * event queued, and the (positive) return tells the caller the required
 * size for the retry. */
struct yetty_ycore_int_result yetty_yscene_scene_take_input_event(struct yetty_yclass_object *obj,
                                                                  uint32_t *out_class,
                                                                  uint8_t *out_bytes,
                                                                  uint32_t out_capacity);
/* Whether the chrome currently owns key focus (set by a consumed pointer
 * press on opaque chrome; cleared by a press that fell through). */
struct yetty_ycore_int_result yetty_yscene_scene_key_focus(struct yetty_yclass_object *obj);
/* The published rich world size — tests assert atomicity through it. */
struct yetty_ycore_uint32_result yetty_yscene_scene_rich_entry_count(
    struct yetty_yclass_object *obj);
/* Install the slot-0 default font on a directly-created scene. The wire
 * factory installs from its args bundle; embedders that create scenes
 * programmatically (the ymux viewer) install here. Borrowed — the
 * caller owns the font and must outlive the scene. */
struct yetty_ycore_void_result yetty_yscene_scene_set_default_font(struct yetty_yclass_object *obj,
                                                                   struct yetty_yfont_font *font);
/* Register the "yscene" figure kind so containers can mint scenes from
 * wire CREATE_CHILD records. `args` is BORROWED for the registry's
 * lifetime (host-owned bundle); NULL registers a bare scene (no
 * composites, no default font). Call at terminal/host create time. */
struct yetty_ycore_void_result yetty_yscene_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_yscene_factory_args *args);
/* Register the scene factory under an arbitrary kind code — the ygrid
 * migration path: apps re-point their legacy figure kinds ("ygrid" and
 * the producer kinds yplot / yimage / …) at the retained scene renderer
 * without touching the producers' wire output. Args has the same
 * borrowed-lifetime contract as register_factory. */
struct yetty_ycore_void_result yetty_yscene_register_factory_for_kind(
    struct yetty_yfigure_registry *registry, uint32_t kind,
    const struct yetty_yscene_factory_args *args);
/* Host-side default font (slot 0) for a hand-created scene. Borrowed. */
struct yetty_ycore_void_result yetty_yscene_set_default_font(struct yetty_yclass_object *obj,
                                                             struct yetty_yfont_font *font);
/* Host-side complex renderer for a hand-created scene. Borrowed. */
struct yetty_ycore_void_result yetty_yscene_set_composite_factory(
    struct yetty_yclass_object *obj, struct yetty_ydraw_composite_factory *factory);
/* The figure base of this scene (the container's handle on it). */
struct yetty_yfigure_figure *yetty_yscene_as_figure(struct yetty_yscene_scene *scene);
/* Rebuild the derived world state from the latest committed generation
 * (no-op when already current). Render does this implicitly; tests and
 * hit-test callers may want it explicitly. */
struct yetty_ycore_void_result yetty_yscene_derive(struct yetty_yclass_object *obj);
/* Number of derived paint leaves (after derive). */
struct yetty_ycore_uint32_result yetty_yscene_leaf_count(struct yetty_yclass_object *obj);
/* Hit-test a SCREEN point against the derived scene: reverse paint
 * order over world AABBs + effective clips. Returns the owning node's
 * external id; 0 = no leaf hit (the document root / background). */
struct yetty_ycore_uint64_result yetty_yscene_hit_test(struct yetty_yclass_object *obj,
                                                       float screen_x, float screen_y);
/* Production overlay POINTER dispatch (#699.4, review #12): resolve the dom
 * leaf under the point and RECORD the event on the scene (node id, position,
 * kind/button/mods, a monotonic serial) — the overlay chrome's event intake.
 * Returns the hit node's external id (0 = nothing consumed the point). The
 * bridge calls this for pointer events the overlay consumed; chrome widgets
 * poll/react via the recorded state until a richer widget protocol exists. */
struct yetty_ycore_uint64_result yetty_yscene_scene_dispatch_pointer(
    struct yetty_yclass_object *obj, uint32_t local_x, uint32_t local_y, uint32_t kind,
    uint32_t button, uint32_t mods, uint32_t pressed);
/* The recorded pointer-event serial (monotonic; 0 = none yet) — chrome and
 * tests observe dispatch through it. */
struct yetty_ycore_uint64_result yetty_yscene_scene_pointer_event_serial(
    struct yetty_yclass_object *obj);
/* Render-plan snapshot for headless tests: derive, build staging
 * against the CURRENT rect/content extent (staging is pure CPU — no
 * binder needed), and dump the exact data destined for GPU upload:
 * staged count, extent, per-prim word summaries, cell occupancy, and
 * the view mapping. Caller frees the string. */
struct yetty_ycore_char_ptr_result yetty_yscene_render_plan(struct yetty_yclass_object *obj);
/* View scale (HiDPI / zoom) — derive/view state, never baked into wire
 * coordinates. */
struct yetty_ycore_void_result yetty_yscene_set_view_scale(struct yetty_yclass_object *obj,
                                                           float view_scale);

#ifdef __cplusplus
}
#endif

#endif
