/*
 * yjupyter protocol contract test.
 *
 * Builds an execute_request through a session, checks the encoded frame decodes
 * back to the same header/content, then feeds simulated IOPub / shell replies
 * whose parent_header.msg_id matches the request and verifies:
 *   - replies correlate back to the caller tag by parent_header.msg_id;
 *   - kernel busy/idle state tracks the IOPub `status` messages;
 *   - an unknown parent yields no tag (an orphan, not a mis-route).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <yetty/yjupyter/protocol.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", (msg));                                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* An IOPub/shell frame whose parent_header.msg_id ties it to a request. */
static void make_frame(char *buf, size_t cap, const char *msg_type, const char *channel,
                       const char *parent_msg_id, const char *content)
{
    snprintf(buf, cap,
             "{\"header\":{\"msg_type\":\"%s\",\"msg_id\":\"reply-x\",\"session\":\"sess-1\","
             "\"version\":\"5.3\"},\"parent_header\":{\"msg_id\":\"%s\"},\"metadata\":{},"
             "\"content\":%s,\"channel\":\"%s\"}",
             msg_type, parent_msg_id, content, channel);
}

int main(void)
{
    struct yetty_ycore_void_result reg_r = yetty_yjupyter_register();
    CHECK(YETTY_IS_OK(reg_r), "module register");

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result session_r = yetty_yjupyter_session_create(&ctx);
    CHECK(YETTY_IS_OK(session_r), "session create");
    if (YETTY_IS_ERR(session_r))
        return 1;
    struct yetty_yclass_object *session = session_r.value;

    struct yetty_ycore_void_result init_r = yetty_yjupyter_session_init(session, "sess-1");
    CHECK(YETTY_IS_OK(init_r), "session init");

    struct yetty_ycore_const_char_ptr_result state0 = yetty_yjupyter_session_kernel_state(session);
    CHECK(YETTY_IS_OK(state0) && strcmp(state0.value, "unknown") == 0, "initial kernel state");

    /* ---- build an execute_request ---- */
    struct yetty_yclass_object_ptr_result request_r = yetty_yjupyter_session_new_request(
        session, "execute_request", "shell", "{\"code\":\"1+1\",\"silent\":false}", "cell-A");
    CHECK(YETTY_IS_OK(request_r), "new_request");
    struct yetty_yclass_object *request = request_r.value;

    struct yetty_ycore_const_char_ptr_result rtype = yetty_yjupyter_message_msg_type(request);
    CHECK(YETTY_IS_OK(rtype) && strcmp(rtype.value, "execute_request") == 0, "request msg_type");
    struct yetty_ycore_const_char_ptr_result rchan = yetty_yjupyter_message_channel(request);
    CHECK(YETTY_IS_OK(rchan) && strcmp(rchan.value, "shell") == 0, "request channel");
    struct yetty_ycore_const_char_ptr_result rsess = yetty_yjupyter_message_session(request);
    CHECK(YETTY_IS_OK(rsess) && strcmp(rsess.value, "sess-1") == 0, "request session");
    struct yetty_ycore_char_ptr_result rcode = yetty_yjupyter_message_content_string(request, "code");
    CHECK(YETTY_IS_OK(rcode) && strcmp(rcode.value, "1+1") == 0, "request content code");
    if (YETTY_IS_OK(rcode))
        free(rcode.value);

    /* capture the request msg_id (borrowed -> copy before further calls) */
    struct yetty_ycore_const_char_ptr_result rid = yetty_yjupyter_message_msg_id(request);
    CHECK(YETTY_IS_OK(rid) && rid.value[0] != 0, "request msg_id non-empty");
    char req_id[320];
    snprintf(req_id, sizeof(req_id), "%s", YETTY_IS_OK(rid) ? rid.value : "");

    /* ---- encode -> decode round-trip ---- */
    struct yetty_ycore_char_ptr_result wire_r = yetty_yjupyter_message_to_wire(request);
    CHECK(YETTY_IS_OK(wire_r), "request to_wire");
    if (YETTY_IS_OK(wire_r)) {
        struct yetty_yclass_object_ptr_result decoded_r = yetty_yjupyter_message_create(&ctx);
        struct yetty_yclass_object *decoded = decoded_r.value;
        struct yetty_ycore_void_result from_r = yetty_yjupyter_message_from_wire(decoded, wire_r.value);
        CHECK(YETTY_IS_OK(from_r), "decode from_wire");
        struct yetty_ycore_const_char_ptr_result dtype = yetty_yjupyter_message_msg_type(decoded);
        CHECK(YETTY_IS_OK(dtype) && strcmp(dtype.value, "execute_request") == 0,
              "decoded msg_type round-trips");
        struct yetty_ycore_const_char_ptr_result did = yetty_yjupyter_message_msg_id(decoded);
        CHECK(YETTY_IS_OK(did) && strcmp(did.value, req_id) == 0, "decoded msg_id round-trips");
        yetty_yjupyter_message_destroy(decoded);
        free(wire_r.value);
    }

    char frame[1024];

    /* ---- IOPub status: busy (parent = our request) ---- */
    make_frame(frame, sizeof(frame), "status", "iopub", req_id, "{\"execution_state\":\"busy\"}");
    struct yetty_yclass_object_ptr_result busy_r = yetty_yjupyter_session_handle_wire(session, frame);
    CHECK(YETTY_IS_OK(busy_r), "handle status busy");
    struct yetty_ycore_const_char_ptr_result state1 = yetty_yjupyter_session_kernel_state(session);
    CHECK(YETTY_IS_OK(state1) && strcmp(state1.value, "busy") == 0, "kernel state busy");
    /* the busy message's parent correlates back to cell-A */
    struct yetty_ycore_const_char_ptr_result busy_parent =
        yetty_yjupyter_message_parent_msg_id(busy_r.value);
    CHECK(YETTY_IS_OK(busy_parent), "busy parent id");
    struct yetty_ycore_const_char_ptr_result tag1 =
        yetty_yjupyter_session_tag_for(session, busy_parent.value);
    CHECK(YETTY_IS_OK(tag1) && strcmp(tag1.value, "cell-A") == 0, "busy correlates to cell-A");
    yetty_yjupyter_message_destroy(busy_r.value);

    /* ---- shell execute_reply (parent = our request) ---- */
    make_frame(frame, sizeof(frame), "execute_reply", "shell", req_id,
               "{\"status\":\"ok\",\"execution_count\":1}");
    struct yetty_yclass_object_ptr_result reply_r = yetty_yjupyter_session_handle_wire(session, frame);
    CHECK(YETTY_IS_OK(reply_r), "handle execute_reply");
    struct yetty_ycore_int_result ec = yetty_yjupyter_message_content_int(reply_r.value, "execution_count");
    CHECK(YETTY_IS_OK(ec) && ec.value == 1, "reply execution_count == 1");
    yetty_yjupyter_message_destroy(reply_r.value);

    /* ---- IOPub status: idle ---- */
    make_frame(frame, sizeof(frame), "status", "iopub", req_id, "{\"execution_state\":\"idle\"}");
    struct yetty_yclass_object_ptr_result idle_r = yetty_yjupyter_session_handle_wire(session, frame);
    CHECK(YETTY_IS_OK(idle_r), "handle status idle");
    struct yetty_ycore_const_char_ptr_result state2 = yetty_yjupyter_session_kernel_state(session);
    CHECK(YETTY_IS_OK(state2) && strcmp(state2.value, "idle") == 0, "kernel state idle");
    yetty_yjupyter_message_destroy(idle_r.value);

    /* ---- orphan: unknown parent yields no tag ---- */
    struct yetty_ycore_const_char_ptr_result orphan =
        yetty_yjupyter_session_tag_for(session, "no-such-request");
    CHECK(YETTY_IS_OK(orphan) && orphan.value[0] == 0, "unknown parent -> no tag");

    yetty_yjupyter_message_destroy(request);
    yetty_yjupyter_session_destroy(session);

    if (failures == 0) {
        printf("ok - yjupyter protocol\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
