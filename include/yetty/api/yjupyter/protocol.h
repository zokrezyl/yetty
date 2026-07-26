/* GENERATED — do not edit. */
/* Object API for regular class(es) `message, session` (implementation module: yjupyter).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YJUPYTER_PROTOCOL_H
#define YETTY_YCLASSGEN_API_YJUPYTER_PROTOCOL_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yjupyter_message;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YJUPYTER_MESSAGE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YJUPYTER_MESSAGE_PTR_RESULT
struct yetty_yjupyter_message_ptr_result {
    int ok;
    union {
        struct yetty_yjupyter_message *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yjupyter_message_ptr_result yetty_yjupyter_message_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yjupyter_message_to(struct yetty_yjupyter_message *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yjupyter_session;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YJUPYTER_SESSION_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YJUPYTER_SESSION_PTR_RESULT
struct yetty_yjupyter_session_ptr_result {
    int ok;
    union {
        struct yetty_yjupyter_session *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yjupyter_session_ptr_result yetty_yjupyter_session_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_to(struct yetty_yjupyter_session *data);

/* build: populate this message as a Jupyter v5 frame. `content_json` is the
 * message content object as JSON text (or "" for an empty object);
 * `parent_msg_id` is "" for an originating request. Replaces any prior content. */
struct yetty_ycore_void_result yetty_yjupyter_message_build(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * session_id, const char * msg_id, const char * parent_msg_id, const char * content_json);
/* from_wire: parse an incoming WebSocket JSON frame into this message. */
struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object * obj, const char * json);
/* to_wire: serialize the message as a WebSocket JSON frame (owned; caller frees). */
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object * obj);
/* header/routing accessors (borrowed; valid while the message lives). */
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(struct yetty_yclass_object * obj);
/* content_json: the message content object as JSON text (owned). */
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(struct yetty_yclass_object * obj);
/* content_string: a string field of the content object (e.g. "code", "text",
 * "name", "execution_state", "ename", "status"); owned, "" when absent. */
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(struct yetty_yclass_object * obj, const char * key);
/* content_int: an integer field of the content object (e.g. "execution_count");
 * -1 when absent or not an integer. */
struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object * obj, const char * key);
struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object * obj);
/* init: set the session id (a non-empty value is required — Jupyter Server keys
 * a kernel session on it). Resets counters, correlation, and kernel state. */
struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object * obj, const char * session_id);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(struct yetty_yclass_object * obj);
/* new_request: build a request message on `channel` with `content_json`,
 * assigning a fresh session-unique msg_id. When `tag` is non-empty, the
 * (msg_id -> tag) pair is recorded so replies can be correlated back with
 * tag_for(). Returns a message object the caller owns (destroy it). */
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * content_json, const char * tag);
/* handle_wire: parse an incoming WebSocket frame into a message object (caller
 * owns it). When it is an IOPub `status` message, the kernel busy/idle state is
 * updated from its content.execution_state. */
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(struct yetty_yclass_object * obj, const char * json);
/* tag_for: the caller tag recorded for the request whose msg_id is
 * `parent_msg_id` (i.e. the reply's parent_header.msg_id). "" when unknown —
 * an orphan the caller should route to a diagnostic stream, never an arbitrary
 * request. */
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object * obj);

struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *ctx);



#ifdef __cplusplus
}
#endif

#endif
