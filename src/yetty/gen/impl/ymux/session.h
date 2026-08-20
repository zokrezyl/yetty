/* GENERATED — do not edit. */
/* Public interface for regular class(es) `session` (module: ymux).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUX_SESSION_H
#define YETTY_YCLASSGEN_YMUX_SESSION_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymux_engine_host;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PERMISSION
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PERMISSION
enum yetty_ymux_permission {
    YETTY_YMUX_PERMISSION_INPUT = 1,
    YETTY_YMUX_PERMISSION_RESIZE = 2,
};
#endif

/* The session — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_session_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_session;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_SESSION_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_SESSION_PTR_RESULT
struct yetty_ymux_session_ptr_result {
    int ok;
    union {
        struct yetty_ymux_session *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_session_ptr_result yetty_ymux_session_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_session_to(struct yetty_ymux_session *data);

struct yetty_yclass_object_ptr_result yetty_ymux_session_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymux_register(void);

struct yetty_yclass_object_ptr_result yetty_ymux_session_make(void);
struct yetty_ycore_void_result yetty_ymux_session_dispose(struct yetty_yclass_object *obj);
/* Create a pane (its engine host is the caller's — the daemon wires PTY
 * output there). Returns the pane id. */
struct yetty_ycore_uint32_result yetty_ymux_session_pane_create(
    struct yetty_yclass_object *obj, uint32_t rows, uint32_t cols, uint32_t hot_rows,
    uint64_t total_row_cap, const struct yetty_ymux_engine_host *host);
/* Close a pane: its attachments detach first (projectors die with them). */
struct yetty_ycore_void_result yetty_ymux_session_pane_close(struct yetty_yclass_object *obj,
                                                             uint32_t pane_id);
struct yetty_yclass_object_ptr_result yetty_ymux_session_pane(struct yetty_yclass_object *obj,
                                                              uint32_t pane_id);
/* Attach a client to a pane. First eligible attachment becomes controller
 * (and only then does the canonical pane resize to ITS geometry — attach
 * itself never resizes for non-controllers); reconnecting with the
 * controller's token resumes control. Returns the attachment id. */
struct yetty_ycore_uint32_result yetty_ymux_session_attach(struct yetty_yclass_object *obj,
                                                           uint32_t pane_id, uint32_t view_rows,
                                                           uint32_t view_cols, const char *token);
/* Detach: the pane survives; controller role frees (token remembered for
 * resume-on-reconnect). */
struct yetty_ycore_void_result yetty_ymux_session_detach(struct yetty_yclass_object *obj,
                                                         uint32_t attachment_id);
/* Explicit authorized takeover: the attachment becomes controller and its
 * geometry becomes canonical. */
struct yetty_ycore_void_result yetty_ymux_session_takeover(struct yetty_yclass_object *obj,
                                                           uint32_t attachment_id);
/* Controller-driven resize; non-controllers are rejected (their VIEW size
 * is attachment state — set_view_size — and crops/pads instead). */
struct yetty_ycore_void_result yetty_ymux_session_resize(struct yetty_yclass_object *obj,
                                                         uint32_t attachment_id, uint32_t rows,
                                                         uint32_t cols);
struct yetty_ycore_uint32_result yetty_ymux_session_controller(struct yetty_yclass_object *obj);
/* The active (most recently relevant) pane id, 0 when the session has no
 * panes — the default attach target (tmux `attach` semantics: reuse the
 * running session; only a pane-less session spawns fresh). */
struct yetty_ycore_uint32_result yetty_ymux_session_active_pane(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ymux_session_permissions(struct yetty_yclass_object *obj,
                                                                uint32_t attachment_id);
struct yetty_ycore_void_result yetty_ymux_session_set_permissions(struct yetty_yclass_object *obj,
                                                                  uint32_t attachment_id,
                                                                  uint32_t permissions);
/* Input from an attachment: permission-gated; encoding happens in the
 * pane's canonical engine. */
struct yetty_ycore_void_result yetty_ymux_session_input_char(struct yetty_yclass_object *obj,
                                                             uint32_t attachment_id,
                                                             uint32_t codepoint, int mods);
/* Special-key input from an attachment: permission-gated exactly like
 * session_input_char — a read-only (non-INPUT) attachment must not be able to
 * inject Enter/arrows/delete/etc. into the application PTY. */
struct yetty_ycore_void_result yetty_ymux_session_input_key(struct yetty_yclass_object *obj,
                                                            uint32_t attachment_id, int key,
                                                            int mods);
/* Mouse motion from an attachment (permission-gated); the engine encodes it per
 * the application's active mouse mode (or drops it when no mode is enabled). */
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_move(struct yetty_yclass_object *obj,
                                                                   uint32_t attachment_id,
                                                                   uint32_t row, uint32_t col,
                                                                   int mods);
/* Mouse button press/release from an attachment (permission-gated). */
struct yetty_ycore_void_result yetty_ymux_session_input_mouse_button(
    struct yetty_yclass_object *obj, uint32_t attachment_id, int button, int pressed, int mods);
/* Bracketed-paste content from an attachment (permission-gated); the engine
 * wraps it per the app's paste mode. */
struct yetty_ycore_void_result yetty_ymux_session_input_paste(struct yetty_yclass_object *obj,
                                                              uint32_t attachment_id,
                                                              const char *text, size_t len);
/* The attachment's projector (the daemon pumps it). Borrowed. */
struct yetty_yclass_object_ptr_result yetty_ymux_session_projector(struct yetty_yclass_object *obj,
                                                                   uint32_t attachment_id);
struct yetty_yclass_object_ptr_result yetty_ymux_session_attachment(struct yetty_yclass_object *obj,
                                                                    uint32_t attachment_id);

#ifdef __cplusplus
}
#endif

#endif
