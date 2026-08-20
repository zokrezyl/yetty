/* GENERATED — do not edit. */
/* Public interface for regular class(es) `daemon` (module: ymux).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUX_DAEMON_H
#define YETTY_YCLASSGEN_YMUX_DAEMON_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_yplatform_pty_ptr_result (*yetty_ymux_daemon_spawn_fn)(uint32_t, uint32_t,
                                                                            void *);

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_DAEMON_HOST
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_DAEMON_HOST
/* The PTY seam: the daemon model never forks or opens devices itself. */
struct yetty_ymux_daemon_host {
    yetty_ymux_daemon_spawn_fn spawn;
    void *userdata;
};
#endif

struct yetty_yclass_ptr_result yetty_ymux_daemon_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_daemon;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_DAEMON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_DAEMON_PTR_RESULT
struct yetty_ymux_daemon_ptr_result {
    int ok;
    union {
        struct yetty_ymux_daemon *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_daemon_ptr_result yetty_ymux_daemon_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_daemon_to(struct yetty_ymux_daemon *data);

struct yetty_yclass_object_ptr_result yetty_ymux_daemon_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymux_register(void);

struct yetty_yclass_object_ptr_result yetty_ymux_daemon_make(
    const char *socket_path, uint32_t default_rows, uint32_t default_cols,
    const struct yetty_ymux_daemon_host *host);
struct yetty_ycore_void_result yetty_ymux_daemon_dispose(struct yetty_yclass_object *obj);
/* The copy-mode paste buffer (copied out; returns the length). */
struct yetty_ycore_uint32_result yetty_ymux_daemon_paste_buffer(struct yetty_yclass_object *obj,
                                                                uint8_t *out_bytes,
                                                                uint32_t out_capacity);
/* The chrome-intake seat: total CONSUMED overlay events received (across
 * connections), plus the most recent event's class — the observable end of
 * the overlay consumer route until interactive chrome lands. */
struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_intake(struct yetty_yclass_object *obj);
/* The last CONSUMED chrome event's payload — the seat's identity
 * observation: copies up to capacity, returns (class << 32) | length. */
struct yetty_ycore_uint64_result yetty_ymux_daemon_chrome_last_event(
    struct yetty_yclass_object *obj, uint8_t *out_bytes, uint32_t out_capacity);
/* Tests only: force slow-client recovery (epoch reset) on every attached
 * connection NOW — the high-water trigger needs megabytes of backlog that a
 * unit rig cannot accumulate through the ack window. */
struct yetty_ycore_void_result yetty_ymux_daemon_force_recover(struct yetty_yclass_object *obj);
/* Tests only: make the next `count` vtsink lane enqueues fail on every
 * connection, forcing the feed-failure recovery path. */
struct yetty_ycore_void_result yetty_ymux_daemon_fail_next_vtsink_tx(
    struct yetty_yclass_object *obj, uint32_t count);
struct yetty_ycore_int_result yetty_ymux_daemon_step(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_daemon_socket_path(struct yetty_yclass_object *obj,
                                                             char *out, size_t out_cap);
/* Borrowed — tests inspect controller policy through it. NULL-name (or
 * empty) resolves the most recent session, tmux-style. */
struct yetty_yclass_object_ptr_result yetty_ymux_daemon_session(struct yetty_yclass_object *obj,
                                                                const char *name);
struct yetty_ycore_int_result yetty_ymux_daemon_shutdown_requested(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
