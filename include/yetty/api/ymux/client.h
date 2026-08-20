/* GENERATED — do not edit. */
/* Object API for regular class(es) `client` (implementation module: ymux).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMUX_CLIENT_H
#define YETTY_YCLASSGEN_API_YMUX_CLIENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ymux_client_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_client;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CLIENT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_CLIENT_PTR_RESULT
struct yetty_ymux_client_ptr_result {
    int ok;
    union {
        struct yetty_ymux_client *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_client_ptr_result yetty_ymux_client_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_client_to(struct yetty_ymux_client *data);

struct yetty_yclass_object_ptr_result yetty_ymux_client_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ymux_client_make(const char *socket_path);
struct yetty_ycore_void_result yetty_ymux_client_dispose(struct yetty_yclass_object *obj);
/* Declare the client terminal's TERM name + features string; the next
 * attach carries them (the daemon then resolves the capability profile
 * via the terminfo/features state model). */
struct yetty_ycore_void_result yetty_ymux_client_set_terminal(struct yetty_yclass_object *obj,
                                                              const char *term_name,
                                                              const char *features);
/* Attach to `session_name` (NULL/empty = most recent — tmux attach
 * without -t). The session must exist; new-session creates them. */
struct yetty_ycore_void_result yetty_ymux_client_attach(struct yetty_yclass_object *obj,
                                                        const char *session_name, uint32_t pane_id,
                                                        uint32_t view_rows, uint32_t view_cols,
                                                        uint32_t cell_pixel_height,
                                                        uint32_t capabilities, const char *token);
/* new-session: create a named session (empty name = auto-number) with
 * its initial shell pane. Reply arrives as a session reply. */
struct yetty_ycore_void_result yetty_ymux_client_session_new(struct yetty_yclass_object *obj,
                                                             const char *name, uint32_t rows,
                                                             uint32_t cols);
struct yetty_ycore_void_result yetty_ymux_client_session_list(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_session_has(struct yetty_yclass_object *obj,
                                                             const char *name);
struct yetty_ycore_void_result yetty_ymux_client_session_kill(struct yetty_yclass_object *obj,
                                                              const char *name);
/* detach-client -s: detach every client of the session server-side. */
struct yetty_ycore_void_result yetty_ymux_client_session_detach(struct yetty_yclass_object *obj,
                                                                const char *name);
/* send-keys: (kind, value) u32 pairs — kind 0 codepoint, kind 1 special
 * key — fed to the session's active pane engine, no attachment needed. */
struct yetty_ycore_void_result yetty_ymux_client_session_send_keys(struct yetty_yclass_object *obj,
                                                                   const char *name,
                                                                   const uint32_t *pairs,
                                                                   uint32_t pair_count);
struct yetty_ycore_void_result yetty_ymux_client_session_rename(struct yetty_yclass_object *obj,
                                                                const char *old_name,
                                                                const char *new_name);
/* The last session-verb reply (borrowed text; status via out param).
 * The returned counter moves per arrival — poll it around a verb. */
struct yetty_ycore_uint64_result yetty_ymux_client_session_reply(struct yetty_yclass_object *obj,
                                                                 const char **out_text,
                                                                 uint32_t *out_status);
struct yetty_ycore_void_result yetty_ymux_client_detach(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_input_char(struct yetty_yclass_object *obj,
                                                            uint32_t codepoint, int mods);
struct yetty_ycore_void_result yetty_ymux_client_input_key(struct yetty_yclass_object *obj, int key,
                                                           int mods);
struct yetty_ycore_void_result yetty_ymux_client_input_mouse_move(struct yetty_yclass_object *obj,
                                                                  uint32_t row, uint32_t col,
                                                                  int mods);
struct yetty_ycore_void_result yetty_ymux_client_input_mouse_button(struct yetty_yclass_object *obj,
                                                                    int button, int pressed,
                                                                    int mods);
struct yetty_ycore_void_result yetty_ymux_client_input_paste(struct yetty_yclass_object *obj,
                                                             const char *text, size_t len);
struct yetty_ycore_void_result yetty_ymux_client_resize(struct yetty_yclass_object *obj,
                                                        uint32_t rows, uint32_t cols);
/* Scroll the viewport by `delta` rows (negative = into history; reaching
 * the live top resumes follow-live). The daemon answers with a FULL for
 * the new viewport. */
struct yetty_ycore_void_result yetty_ymux_client_scroll(struct yetty_yclass_object *obj, int delta);
struct yetty_ycore_void_result yetty_ymux_client_takeover(struct yetty_yclass_object *obj);
/* Ask the daemon to resync this attachment: the client's terminal-byte stream
 * became unusable (a dropped or failed VT frame), so request a fresh COMPLETE
 * redraw rather than continue a desynced incremental stream. */
struct yetty_ycore_void_result yetty_ymux_client_resync(struct yetty_yclass_object *obj);
/* Forward RAW terminal-response bytes from THIS attachment's renderer to
 * its daemon-side response parser (review #16). Opaque transport: the
 * bytes are preserved exactly — no decoding, no keyboard re-encoding. */
struct yetty_ycore_void_result yetty_ymux_client_send_tty_response(struct yetty_yclass_object *obj,
                                                                   const uint8_t *bytes,
                                                                   uint32_t byte_count);
/* Forward one CONSUMED overlay input event (drained from the overlay
 * scene's queue) to the daemon's chrome seat, tagged with an acceptance
 * SEQUENCE (review #17): the daemon ACKs it after taking ownership; the
 * caller pops its source event only at that commit point. */
struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(struct yetty_yclass_object *obj,
                                                                    uint32_t sequence,
                                                                    uint32_t input_class,
                                                                    const uint8_t *bytes,
                                                                    uint32_t byte_count);
/* The highest overlay-input sequence the daemon has ACCEPTED. */
struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_nacked(
    struct yetty_yclass_object *obj);
/* CHROME_RELEASE frames observed (the daemon-side chrome exited). */
struct yetty_ycore_uint32_result yetty_ymux_client_chrome_release_count(
    struct yetty_yclass_object *obj);
/* Ops/debug: ask the daemon to force slow-client recovery (epoch reset) on
 * every attached connection — the attach-level reset-ordering probe. */
struct yetty_ycore_void_result yetty_ymux_client_request_recover(struct yetty_yclass_object *obj);
/* Ask the daemon to paste its copy-mode buffer into the target pane
 * (tmux paste-buffer). The SESSION_REPLY ack carries the outcome. */
struct yetty_ycore_void_result yetty_ymux_client_request_paste(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_shutdown_server(struct yetty_yclass_object *obj);
/* One non-blocking pump: recv frames, apply paints (acking each), flush
 * pending tx. Returns the number of frames handled. */
struct yetty_ycore_int_result yetty_ymux_client_step(struct yetty_yclass_object *obj);
/* Feed externally-read bytes into the frame parser (an embedder whose
 * event loop owns the socket reads — e.g. a uv pipe watcher — routes the
 * bytes here instead of letting step() recv). Returns frames handled. */
struct yetty_ycore_int_result yetty_ymux_client_ingest(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len);
/* The connection's file descriptor for event-loop registration (-1 when
 * disconnected or on platforms without one). */
struct yetty_ycore_int_result yetty_ymux_client_fd(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymux_client_attached(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ymux_client_attachment_id(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ymux_client_pane_id(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ymux_client_permissions(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_ymux_client_last_refuse(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymux_client_pane_exited(struct yetty_yclass_object *obj);
/* Borrowed — the most recent rich body (rich-format words; NULL until one
 * arrives). Valid until the next transaction or dispose. */
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_client_rich_body(
    struct yetty_yclass_object *obj, uint32_t *out_word_count);
/* Counts rich-body arrivals — the embedder polls it to detect fresh
 * bodies without comparing content. */
struct yetty_ycore_uint64_result yetty_ymux_client_rich_generation(struct yetty_yclass_object *obj);
/* Bell arrivals (exactly-once effects — never replayed on reconnect). */
struct yetty_ycore_uint64_result yetty_ymux_client_bell_count(struct yetty_yclass_object *obj);
/* The latest pane title (copy-out) + its arrival counter. */
struct yetty_ycore_uint64_result yetty_ymux_client_title(struct yetty_yclass_object *obj, char *out,
                                                         size_t out_cap);
/* The latest clipboard effect text (borrowed; NULL until one arrives).
 * `out_target`: 1 clipboard, 0 primary selection. The returned counter
 * moves per arrival — controller-only routing means non-controllers
 * never see it move. */
struct yetty_ycore_uint64_result yetty_ymux_client_clipboard(struct yetty_yclass_object *obj,
                                                             const char **out_text, size_t *out_len,
                                                             int *out_target);

#ifdef __cplusplus
}
#endif

#endif
