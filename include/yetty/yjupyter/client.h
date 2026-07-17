/* GENERATED — do not edit. */
/* Public interface for regular class(es) `client` (module: yjupyter).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YJUPYTER_CLIENT_H
#define YETTY_YCLASSGEN_YJUPYTER_CLIENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_yjupyter_client_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yjupyter_client;
struct yetty_yjupyter_client_ptr_result {
    int ok;
    union {
        struct yetty_yjupyter_client *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yjupyter_client_ptr_result yetty_yjupyter_client_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_to(struct yetty_yjupyter_client *data);

/* open: create a kernel over REST and open its WebSocket channel. `token` may be
 * "" for a tokenless server. */
struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object *obj,
                                                          const char *base_url, const char *token);
/* execute: send an execute_request for `code` on the shell channel, correlated
 * to `tag`. Returns the request msg_id (owned; caller frees). */
struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object *obj,
                                                                 const char *code, const char *tag);
/* poll: wait up to `timeout_ms` for the next incoming message. Returns a
 * message object (caller owns) whose kernel status has already updated the
 * session; errors on timeout or a closed connection. */
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object *obj,
                                                                 int timeout_ms);
/* kernel_state: the tracked busy/idle/... state of the kernel. */
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(
    struct yetty_yclass_object *obj);
/* tag_for: the caller tag correlated to a reply's parent_header.msg_id. */
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(
    struct yetty_yclass_object *obj, const char *parent_msg_id);
/* close: shut the WebSocket and delete the kernel over REST. */
struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object *obj);
/* destroy: close the connection, free the session, and the yclass allocation. */
struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_open_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_client_execute_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_client_poll_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_kernel_state_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_tag_for_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_close_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_destroy_fn)(
    struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yjupyter_register(void);

#ifdef __cplusplus
}
#endif

#endif
