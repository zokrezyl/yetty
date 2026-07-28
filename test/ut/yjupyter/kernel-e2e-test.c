/*
 * yjupyter live-kernel end-to-end test.
 *
 * Drives yjupyter:client against a REAL Jupyter Server: it creates a kernel
 * over REST, opens the WebSocket channel, executes `print('hi'); 6*7`, and
 * checks the streamed stdout and the execute_result over the actual wire.
 *
 * This needs a running server, so it self-skips (exit 0) unless YJUPYTER_URL is
 * set — that keeps it green in headless CI while still being a real test when
 * pointed at a live server:
 *
 *   YJUPYTER_URL=http://127.0.0.1:8899 YJUPYTER_TOKEN=testtok \
 *     ./build-.../test/ut/yjupyter/yjupyter_kernel_e2e-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#include "yetty/gen/impl/yjupyter/client.h"
#include "yetty/gen/impl/yjupyter/protocol.h"

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", (msg));                                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* Pull data["text/plain"] out of an execute_result / display_data content. */
static char *extract_text_plain(const char *content_json)
{
    yyjson_doc *doc = yyjson_read(content_json, strlen(content_json), 0);
    if (!doc) {
        return NULL;
    }
    yyjson_val *data = yyjson_obj_get(yyjson_doc_get_root(doc), "data");
    yyjson_val *text = data ? yyjson_obj_get(data, "text/plain") : NULL;
    const char *str = text && yyjson_is_str(text) ? yyjson_get_str(text) : NULL;
    char *out = str ? strdup(str) : NULL;
    yyjson_doc_free(doc);
    return out;
}

int main(void)
{
    const char *base_url = getenv("YJUPYTER_URL");
    const char *token = getenv("YJUPYTER_TOKEN");
    if (!base_url || !base_url[0]) {
        printf("ok - yjupyter kernel e2e skipped (set YJUPYTER_URL to run)\n");
        return 0;
    }

    CHECK(YETTY_IS_OK(yetty_yjupyter_register()), "module register");

    struct yetty_yclass_ctx ctx = {0};
    struct yetty_yclass_object_ptr_result client_r = yetty_yjupyter_client_create(&ctx);
    if (YETTY_IS_ERR(client_r)) {
        fprintf(stderr, "FAIL: client create\n");
        return 1;
    }
    struct yetty_yclass_object *client = client_r.value;

    struct yetty_ycore_void_result open_r =
        yetty_yjupyter_client_open(client, base_url, token ? token : "");
    if (YETTY_IS_ERR(open_r)) {
        fprintf(stderr, "FAIL: client_open: %s\n", open_r.error.msg);
        yetty_ycore_error_destroy(open_r.error);
        yetty_yjupyter_client_destroy(client);
        return 1;
    }
    printf("opened kernel, state=%s\n", yetty_yjupyter_client_kernel_state(client).value);

    struct yetty_ycore_char_ptr_result exec_r =
        yetty_yjupyter_client_execute(client, "print('hi'); 6*7", "cellA");
    if (YETTY_IS_ERR(exec_r)) {
        fprintf(stderr, "FAIL: client_execute: %s\n", exec_r.error.msg);
        yetty_ycore_error_destroy(exec_r.error);
        yetty_yjupyter_client_close(client);
        yetty_yjupyter_client_destroy(client);
        return 1;
    }
    char *request_id = exec_r.value;
    printf("execute_request msg_id=%s\n", request_id);

    /* Collect until the execute_reply is followed by an idle status — that
     * pair marks the end of this request's activity. Bounded loop so a stuck
     * kernel cannot hang the test. */
    char stream_stdout[4096] = {0};
    char *result_text = NULL;
    int saw_reply = 0;
    int done = 0;
    for (int i = 0; i < 200 && !done; i++) {
        struct yetty_yclass_object_ptr_result poll_r = yetty_yjupyter_client_poll(client, 5000);
        if (YETTY_IS_ERR(poll_r)) {
            fprintf(stderr, "poll ended: %s\n", poll_r.error.msg);
            yetty_ycore_error_destroy(poll_r.error);
            break;
        }
        struct yetty_yclass_object *message = poll_r.value;

        const char *msg_type = yetty_yjupyter_message_msg_type(message).value;
        const char *channel = yetty_yjupyter_message_channel(message).value;
        const char *parent_id = yetty_yjupyter_message_parent_msg_id(message).value;
        const char *tag = yetty_yjupyter_client_tag_for(client, parent_id).value;
        printf("  <- %-8s %-16s parent=%.8s tag=%s\n", channel, msg_type, parent_id,
               tag && tag[0] ? tag : "-");

        if (strcmp(msg_type, "stream") == 0) {
            struct yetty_ycore_char_ptr_result name_r =
                yetty_yjupyter_message_content_string(message, "name");
            struct yetty_ycore_char_ptr_result text_r =
                yetty_yjupyter_message_content_string(message, "text");
            if (YETTY_IS_OK(name_r) && strcmp(name_r.value, "stdout") == 0 && YETTY_IS_OK(text_r)) {
                strncat(stream_stdout, text_r.value,
                        sizeof(stream_stdout) - strlen(stream_stdout) - 1);
            }
            if (YETTY_IS_OK(name_r)) {
                free(name_r.value);
            }
            if (YETTY_IS_OK(text_r)) {
                free(text_r.value);
            }
        } else if (strcmp(msg_type, "execute_result") == 0) {
            struct yetty_ycore_char_ptr_result content_r =
                yetty_yjupyter_message_content_json(message);
            if (YETTY_IS_OK(content_r)) {
                free(result_text);
                result_text = extract_text_plain(content_r.value);
                free(content_r.value);
            }
        } else if (strcmp(msg_type, "execute_reply") == 0) {
            saw_reply = 1;
        } else if (strcmp(msg_type, "status") == 0) {
            struct yetty_ycore_char_ptr_result state_r =
                yetty_yjupyter_message_content_string(message, "execution_state");
            if (YETTY_IS_OK(state_r)) {
                if (saw_reply && strcmp(state_r.value, "idle") == 0) {
                    done = 1;
                }
                free(state_r.value);
            }
        }
        yetty_yjupyter_message_destroy(message);
    }

    printf("stdout stream: \"%s\"\n", stream_stdout);
    printf("execute_result text/plain: \"%s\"\n", result_text ? result_text : "(none)");

    CHECK(saw_reply, "saw execute_reply");
    CHECK(strcmp(stream_stdout, "hi\n") == 0, "stdout stream is exactly \"hi\\n\"");
    CHECK(result_text && strcmp(result_text, "42") == 0, "execute_result text/plain is \"42\"");

    free(request_id);
    free(result_text);
    yetty_yjupyter_client_close(client);
    yetty_yjupyter_client_destroy(client);

    if (failures == 0) {
        printf("ok - yjupyter kernel e2e\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
}
