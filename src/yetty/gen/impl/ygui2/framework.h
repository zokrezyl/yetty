/* GENERATED — do not edit. */
/* Public interface for regular class(es) `framework` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_FRAMEWORK_H
#define YETTY_YCLASSGEN_YGUI2_FRAMEWORK_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_ygui2_theme;

typedef int (*yetty_ygui2_key_cb)(uint32_t, uint32_t, void *);
typedef void (*yetty_ygui2_sink_fn)(const uint8_t *, size_t, void *);

struct yetty_yclass_ptr_result yetty_ygui2_framework_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_framework;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_FRAMEWORK_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_FRAMEWORK_PTR_RESULT
struct yetty_ygui2_framework_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_framework *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_framework_ptr_result yetty_ygui2_framework_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_to(struct yetty_ygui2_framework *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_framework_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

/* Explicit lifecycle (the grid pattern): make allocates + initializes,
 * dispose tears down + frees. No virtual ctor/dtor — the widget base owns
 * those slot names for the whole module. */
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_make(void);
struct yetty_ycore_void_result yetty_ygui2_framework_dispose(struct yetty_yclass_object *obj);
/* Create the ROOT widget of the tree (wire GROUP id 1) and the overlay
 * root beside it (wire GROUP id 2, always present so popups can mount
 * later without a fresh top-level insertion). */
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_root_create(
    struct yetty_yclass_object *obj, const struct yetty_yclass *cls);
/* Add a child widget under `parent` (any widget of this framework). */
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_add(struct yetty_yclass_object *parent,
                                                             const struct yetty_yclass *cls);
struct yetty_ycore_void_result yetty_ygui2_framework_set_sink(struct yetty_yclass_object *obj,
                                                              yetty_ygui2_sink_fn sink,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui2_framework_set_viewport(struct yetty_yclass_object *obj,
                                                                  float width, float height);
/* Reservation mode (strategy.md §5): fullscreen (default) reserves the
 * full supported viewport range so every accepted resize is in-budget
 * relayout; inline (`fullscreen = 0`) reserves the declared viewport
 * height only — the insertion lives in the scrollback flow, and growth
 * past that reservation is an explicit set_viewport rejection (the app
 * re-inserts via clear() + emit). Must be chosen BEFORE the first
 * insertion: the reservation is immutable for the insertion's life. */
struct yetty_ycore_void_result yetty_ygui2_framework_set_fullscreen(struct yetty_yclass_object *obj,
                                                                    int fullscreen);
/* The committed HiDPI input divisor. The pane-resize envelope path
 * commits it only after a successful viewport transition, so this always
 * matches the projection mouse coordinates are divided against. */
struct yetty_ycore_void_result yetty_ygui2_framework_content_scale(struct yetty_yclass_object *obj,
                                                                   float *out_scale);
struct yetty_ycore_void_result yetty_ygui2_framework_set_key_cb(struct yetty_yclass_object *obj,
                                                                yetty_ygui2_key_cb callback,
                                                                void *userdata);
struct yetty_yclass_object_ptr_result yetty_ygui2_row_add(struct yetty_yclass_object *parent);
struct yetty_yclass_object_ptr_result yetty_ygui2_column_add(struct yetty_yclass_object *parent);
/* Attach the framework to a PTY: envelopes ship to write_fd; the app keeps
 * owning its read loop and forwards bytes through feed_input. */
struct yetty_ycore_void_result yetty_ygui2_framework_attach(struct yetty_yclass_object *obj,
                                                            int read_fd, int write_fd);
/* Arm the terminal's exit-window input barrier: the host holds keystrokes
 * host-side until this client's PTY closes, and answers with a HOLD-ACK
 * envelope (watch hold_ack_seen while pumping feed_input). */
struct yetty_ycore_void_result yetty_ygui2_framework_send_hold(struct yetty_yclass_object *obj);
/* Nonzero once the host's HOLD-ACK envelope has been parsed — its arrival
 * proves the input barrier is armed and teardown may proceed. */
struct yetty_ycore_int_result yetty_ygui2_framework_hold_ack_seen(struct yetty_yclass_object *obj);
/* Detach: unsubscribe from pane input (flags=0 clears every bit in the
 * terminal). MUST run on every app exit path — a subscription that
 * outlives its client leaves the pane spraying mouse envelopes at the
 * shell, and no amount of `stty sane` can cure that from outside. */
struct yetty_ycore_void_result yetty_ygui2_framework_detach(struct yetty_yclass_object *obj);
/* Clear the surface: delete both top-level groups; the next emit homes the
 * cursor and re-inserts from scratch. Local state is only committed after
 * the delete envelope actually shipped. */
struct yetty_ycore_void_result yetty_ygui2_framework_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_framework_theme_copy(
    struct yetty_yclass_object *obj, struct yetty_ygui2_theme *out_theme);
struct yetty_ycore_void_result yetty_ygui2_framework_set_theme(
    struct yetty_yclass_object *obj, const struct yetty_ygui2_theme *theme);
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_overlay_add(
    struct yetty_yclass_object *obj, const struct yetty_yclass *cls);
struct yetty_ycore_int_result yetty_ygui2_framework_widget_is_focused(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj);
struct yetty_ycore_void_result yetty_ygui2_framework_focus_set(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj);
/* Focus/capture hygiene for a subtree leaving the live tree (removal). */
struct yetty_ycore_void_result yetty_ygui2_framework_forget_subtree(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *widget_obj);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input(struct yetty_yclass_object *obj,
                                                                const uint8_t *bytes,
                                                                size_t byte_count);
/* Idle flush: a retained LONE Escape byte is a real Escape keypress, not
 * the start of a sequence — the host calls this on its select timeout so
 * a bare Esc does not wait for the next unrelated key. */
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input_flush(
    struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui2_framework_is_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_button(
    struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_motion(
    struct yetty_yclass_object *obj, float x, float y, uint32_t buttons_held);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float wheel_dy);
/* Available scroll range of a scrollarea along its flow axis: the
 * MEASURED content extent minus the viewport, floored at 0 — the shared
 * measure sizing the content group also clamps the wheel, so users can
 * always reach the last rows and never scroll into blank space. */
struct yetty_ycore_void_result yetty_ygui2_widget_scroll_limit(struct yetty_yclass_object *obj,
                                                               float *out_limit);
/* Ship ONE addressed update for a node inside `widget_obj`'s group:
 * CMD_PATH(widget's full path incl. its own group) + UPDATE(child_id).
 * Its own tiny envelope — the streaming path (complex data without any
 * repaint). */
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *payload,
    size_t payload_size);
/* Append ONE addressed update for a node inside `widget_obj`'s group into
 * a CALLER-OWNED list (no ship): CMD_PATH(widget path incl. its own
 * group) + UPDATE(child_id). The building block for per-widget follow-ups
 * that must ride the frame envelope being assembled (widget_emit_geometry
 * — the framework's own list is in use during emit, so the streaming path
 * above cannot be taken). */
struct yetty_ycore_void_result yetty_ygui2_framework_append_addressed_update(
    struct yetty_yclass_object *widget_obj, struct yetty_ydraw_drawable_list *list,
    uint32_t child_node_id, const void *payload, size_t payload_size);
struct yetty_ycore_void_result yetty_ygui2_framework_emit(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
