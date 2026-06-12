/*
 * claude.c — yclass class `yai:claude`: the Claude Code engine.
 *
 * One persistent `claude --print` child per session, driven over a
 * bidirectional stream-json pipe. Owns everything claude-protocol:
 * the user-message / control_request envelopes, the can_use_tool
 * permission flow, streamed deltas, tool_use/tool_result labelling
 * and the per-turn usage line.
 */
#include "app.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YAI_TOOL_NAME_MAP_SIZE 128
#define YAI_INIT_REQUEST_ID "yai-init"

struct yai_tool_name_entry {
    char id[80];
    char name[80];
};

/* Engine-private state: the tool_use_id -> tool name ring (to label
 * results) and the interrupt request counter. */
struct [[clang::annotate("class@yai:claude")]] [[clang::annotate("parent@yai:engine")]]
yetty_yai_claude {
    struct yai_tool_name_entry tool_names[YAI_TOOL_NAME_MAP_SIZE];
    int tool_name_next;
    int interrupt_request_counter;
};

YETTY_YRESULT_DECLARE(yetty_yai_claude_ptr, struct yetty_yai_claude *);

/* Defined in the appended claude.gen.c (foot of this TU). */
struct yetty_yclass_ptr_result yetty_yai_claude_class_get(void);
struct yetty_yai_claude_ptr_result yetty_yai_claude_from(struct yetty_yclass_object *obj);

/*---------------------------------------------------------------------------
 * Child stdin writes
 *---------------------------------------------------------------------------*/

struct yai_child_write {
    uv_write_t request;
    char *data;
};

YETTY_EXTERNAL_CALLBACK
static void on_child_write_done(uv_write_t *request, int status)
{
    struct yai_child_write *write_request = (struct yai_child_write *)request;
    (void)status;
    free(write_request->data);
    free(write_request);
}

/* Take ownership of `line` (heap, newline-terminated) and ship it. */
static struct yetty_ycore_void_result write_to_child(struct yai_app *app, char *line, size_t len)
{
    if (!app->child_alive || !app->child_stdin_open) {
        free(line);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: child not running");
    }
    struct yai_child_write *write_request = calloc(1, sizeof(*write_request));
    if (!write_request) {
        free(line);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: calloc failed");
    }
    write_request->data = line;
    uv_buf_t buffer = uv_buf_init(line, (unsigned)len);
    if (uv_write(&write_request->request, (uv_stream_t *)&app->child_stdin_pipe, &buffer, 1,
                 on_child_write_done) != 0) {
        free(write_request->data);
        free(write_request);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: uv_write failed");
    }
    return YETTY_OK_VOID();
}

/* Serialize `doc` to a newline-terminated heap line and ship it to the
 * child. Frees the doc. */
static struct yetty_ycore_void_result write_json_to_child(struct yai_app *app, yyjson_mut_doc *doc)
{
    size_t json_len = 0;
    char *json_text = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (!json_text) {
        return YETTY_ERR(yetty_ycore_void, "write_json_to_child: yyjson_mut_write failed");
    }
    char *line = malloc(json_len + 2);
    if (!line) {
        free(json_text);
        return YETTY_ERR(yetty_ycore_void, "write_json_to_child: malloc failed");
    }
    memcpy(line, json_text, json_len);
    line[json_len] = '\n';
    line[json_len + 1] = '\0';
    free(json_text);
    struct yetty_ycore_void_result write_res = write_to_child(app, line, json_len + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "write_json_to_child: write");
    return YETTY_OK_VOID();
}

/* The SDK-style handshake: its response carries the CLI's slash-command
 * list (name + description + argumentHint) — the source the completion
 * menu mirrors. */
static struct yetty_ycore_void_result send_initialize_request(struct yai_app *app)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "send_initialize_request: yyjson_mut_doc_new failed");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", YAI_INIT_REQUEST_ID);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "initialize");
    yyjson_mut_obj_add_val(doc, request, "hooks", yyjson_mut_obj(doc));
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "send_initialize_request: send");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_control_response(struct yai_app *app,
                                                              yyjson_val *event)
{
    yyjson_val *response = yyjson_obj_get(event, "response");
    const char *request_id = yyjson_get_str(yyjson_obj_get(response, "request_id"));
    if (!request_id || strcmp(request_id, YAI_INIT_REQUEST_ID) != 0) {
        return YETTY_OK_VOID(); /* interrupt acks etc. — nothing to do */
    }
    yyjson_val *payload = yyjson_obj_get(response, "response");
    yyjson_val *commands_array = payload ? yyjson_obj_get(payload, "commands") : NULL;
    if (commands_array) {
        struct yetty_ycore_void_result load_res =
            yai_command_table_load(&app->commands, commands_array);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, load_res, "handle_control_response: command table");
        ydebug("yai claude: command table loaded, %zu entries", app->commands.count);
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Permission prompts — the CLI runs with `--permission-prompt-tool
 * stdio`: tools outside the allowlist arrive as can_use_tool
 * control_requests and block until yai answers. The verdict is typed
 * at the prompt (y/n); main.c routes it into the resolve_permission
 * slot below.
 *---------------------------------------------------------------------------*/

/* Ship one can_use_tool control_response. `input_doc` (may be NULL) is
 * echoed back as updatedInput on allow; borrowed, not freed here. */
static struct yetty_ycore_void_result send_permission_response(struct yai_app *app,
                                                               const char *request_id, int allowed,
                                                               yyjson_mut_doc *input_doc)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "send_permission_response: yyjson_mut_doc_new failed");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_response");
    yyjson_mut_val *response = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, response, "subtype", "success");
    yyjson_mut_obj_add_str(doc, response, "request_id", request_id);
    yyjson_mut_val *verdict = yyjson_mut_obj(doc);
    if (allowed) {
        yyjson_mut_obj_add_str(doc, verdict, "behavior", "allow");
        yyjson_mut_val *input_root = input_doc ? yyjson_mut_doc_get_root(input_doc) : NULL;
        yyjson_mut_val *input_copy = input_root ? yyjson_mut_val_mut_copy(doc, input_root) : NULL;
        /* Fail closed: if the original tool input existed but couldn't be
         * copied, do NOT approve with an empty input — that would mutate
         * the operation the user agreed to. */
        if (input_root && !input_copy) {
            yyjson_mut_doc_free(doc);
            return YETTY_ERR(yetty_ycore_void,
                             "send_permission_response: failed to preserve approved tool input");
        }
        yyjson_mut_obj_add_val(doc, verdict, "updatedInput",
                               input_copy ? input_copy : yyjson_mut_obj(doc));
    } else {
        yyjson_mut_obj_add_str(doc, verdict, "behavior", "deny");
        yyjson_mut_obj_add_str(doc, verdict, "message", "denied from yai");
    }
    yyjson_mut_obj_add_val(doc, response, "response", verdict);
    yyjson_mut_obj_add_val(doc, envelope, "response", response);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "send_permission_response: send");
    return YETTY_OK_VOID();
}

/* resolve_permission slot: answer the pending request. `allowed`:
 * 1 = allow (echoes the input back as updatedInput), 0 = deny. */
[[clang::annotate("override@yai:claude:resolve_permission")]]
static struct yetty_ycore_void_result claude_resolve_permission(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                struct yai_app *app, int allowed)
{
    (void)ctx;
    (void)obj;
    if (!app->pending_permission.active) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "claude resolve_permission: suspend");
    printf("\n%s%s" YAI_RESET "\n", allowed ? YAI_MINT "✓ allowed: " : YAI_RED "✗ denied: ",
           app->pending_permission.tool_name);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "claude resolve_permission: flush");
    struct yetty_ycore_void_result send_res = send_permission_response(
        app, app->pending_permission.request_id, allowed, app->pending_permission.input_doc);
    /* Only clear the pending request once the verdict is actually on its
     * way: if the write failed the child may still be blocked waiting,
     * and dropping it here would strand the request. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "claude resolve_permission: send");
    yai_drop_pending_permission(app);
    struct yetty_ycore_void_result activity_res =
        yai_set_activity(app, "typing-dots", "… thinking");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res, "claude resolve_permission: activity");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_control_request(struct yai_app *app,
                                                             yyjson_val *event)
{
    yyjson_val *request = yyjson_obj_get(event, "request");
    const char *subtype = yyjson_get_str(yyjson_obj_get(request, "subtype"));
    const char *request_id = yyjson_get_str(yyjson_obj_get(event, "request_id"));
    if (!subtype || !request_id || strcmp(subtype, "can_use_tool") != 0) {
        return YETTY_OK_VOID(); /* only the feature we enabled can arrive */
    }
    if (app->pending_permission.active) {
        /* One at a time; a second concurrent request would deadlock the
         * UI — answer it deny-busy immediately, pending stays intact. */
        struct yetty_ycore_void_result busy_res =
            send_permission_response(app, request_id, 0, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, busy_res, "handle_control_request: deny-busy");
        return YETTY_OK_VOID();
    }

    const char *tool_name = yyjson_get_str(yyjson_obj_get(request, "tool_name"));
    if (!tool_name) {
        tool_name = "?";
    }
    yyjson_val *input = yyjson_obj_get(request, "input");

    app->pending_permission.active = 1;
    snprintf(app->pending_permission.request_id, sizeof(app->pending_permission.request_id), "%s",
             request_id);
    snprintf(app->pending_permission.tool_name, sizeof(app->pending_permission.tool_name), "%s",
             tool_name);
    app->pending_permission.input_doc = yyjson_mut_doc_new(NULL);
    if (app->pending_permission.input_doc && input) {
        yyjson_mut_val *copy = yyjson_val_mut_copy(app->pending_permission.input_doc, input);
        if (copy) {
            yyjson_mut_doc_set_root(app->pending_permission.input_doc, copy);
        }
    }

    char summary[80];
    yai_summarize_tool_input(input, summary, sizeof(summary));

    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_control_request: suspend");
    printf("\n" YAI_MINT "? permission: " YAI_RESET YAI_BOLD "%s" YAI_RESET " " YAI_DIM
           "%s" YAI_RESET "\n" YAI_DIM "  allow? type y / n" YAI_RESET "\n",
           tool_name, summary);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_control_request: flush");
    /* The status line + HUD mirror the pending question (display-only —
     * the verdict is typed at the prompt). */
    char state_line[96];
    snprintf(state_line, sizeof(state_line), "? %s %s", tool_name, summary);
    struct yetty_ycore_void_result activity_res = yai_set_activity(app, "hourglass", state_line);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res, "handle_control_request: activity");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_control_cancel(struct yai_app *app, yyjson_val *event)
{
    const char *request_id = yyjson_get_str(yyjson_obj_get(event, "request_id"));
    if (!app->pending_permission.active || !request_id ||
        strcmp(request_id, app->pending_permission.request_id) != 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_control_cancel: suspend");
    printf("\n" YAI_DIM "(permission request cancelled)" YAI_RESET "\n");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_control_cancel: flush");
    yai_drop_pending_permission(app);
    /* The turn is still in flight — the result event ends it. */
    struct yetty_ycore_void_result activity_res =
        yai_set_activity(app, "typing-dots", "… thinking");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res, "handle_control_cancel: activity");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Tool-name map (tool_use_id -> name, to label results)
 *---------------------------------------------------------------------------*/

static void remember_tool_name(struct yetty_yai_claude *claude, const char *tool_use_id,
                               const char *name)
{
    struct yai_tool_name_entry *entry =
        &claude->tool_names[claude->tool_name_next % YAI_TOOL_NAME_MAP_SIZE];
    claude->tool_name_next++;
    snprintf(entry->id, sizeof(entry->id), "%s", tool_use_id);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static const char *lookup_tool_name(struct yetty_yai_claude *claude, const char *tool_use_id)
{
    if (!tool_use_id) {
        return NULL;
    }
    for (int index = 0; index < YAI_TOOL_NAME_MAP_SIZE; index++) {
        if (strcmp(claude->tool_names[index].id, tool_use_id) == 0) {
            return claude->tool_names[index].name;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Stream-json event handling
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result render_turn_usage(struct yai_app *app,
                                                        yyjson_val *result_event)
{
    yyjson_val *usage = yyjson_obj_get(result_event, "usage");
    yyjson_val *cost_value = yyjson_obj_get(result_event, "total_cost_usd");
    yyjson_val *duration_value = yyjson_obj_get(result_event, "duration_ms");

    uint64_t turn_input = yai_usage_field(usage, "input_tokens");
    uint64_t turn_output = yai_usage_field(usage, "output_tokens");
    uint64_t cache_read = yai_usage_field(usage, "cache_read_input_tokens");
    uint64_t cache_creation = yai_usage_field(usage, "cache_creation_input_tokens");
    double cost = cost_value ? yyjson_get_num(cost_value) : 0.0;
    double seconds = duration_value ? yyjson_get_num(duration_value) / 1000.0 : 0.0;

    app->usage.input += turn_input;
    app->usage.output += turn_output;
    app->usage.cache_read += cache_read;
    app->usage.cache_creation += cache_creation;
    app->usage.cost += cost;
    app->usage.turns++;

    char input_text[16];
    char output_text[16];
    char cached_text[16];
    char session_output_text[16];
    yai_format_tokens(turn_input, input_text, sizeof(input_text));
    yai_format_tokens(turn_output, output_text, sizeof(output_text));
    yai_format_tokens(cache_read, cached_text, sizeof(cached_text));
    yai_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));

    char timing_text[48] = "";
    if (seconds > 0.0) {
        snprintf(timing_text, sizeof(timing_text), " · %.1fs · %.0f tok/s", seconds,
                 (double)turn_output / seconds);
    }
    char turn_line[192];
    snprintf(turn_line, sizeof(turn_line), "↑%s in · %s cached · ↓%s out%s · $%.4f", input_text,
             cached_text, output_text, timing_text, cost);
    char session_line[128];
    snprintf(session_line, sizeof(session_line), "session: ↓%s out · $%.4f · %d turn(s)",
             session_output_text, app->usage.cost, app->usage.turns);

    if (app->hud) {
        /* Tokens go to the HUD window; the scrollback stays clean. */
        struct yetty_ycore_void_result hud_res = yai_hud_set_turn(app->hud, turn_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "render_turn_usage: hud turn line");
        hud_res = yai_hud_set_session(app->hud, session_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "render_turn_usage: hud session line");
        hud_res = yai_hud_set_state(app->hud, "idle");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "render_turn_usage: hud state");
        hud_res = yai_hud_flush(app->hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "render_turn_usage: hud flush");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "render_turn_usage: suspend");
    printf(YAI_MUTED "  %s" YAI_RESET "\n" YAI_DIM "  %s" YAI_RESET "\n", turn_line, session_line);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "render_turn_usage: flush");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_stream_event(struct yai_app *app, yyjson_val *event)
{
    yyjson_val *inner = yyjson_obj_get(event, "event");
    const char *inner_type = yyjson_get_str(yyjson_obj_get(inner, "type"));
    if (!inner_type) {
        return YETTY_OK_VOID();
    }
    if (strcmp(inner_type, "message_start") == 0) {
        struct yetty_ycore_void_result begin_res = yai_renderer_begin_message(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "handle_stream_event: begin_message");
        return YETTY_OK_VOID();
    }
    if (strcmp(inner_type, "content_block_delta") == 0) {
        yyjson_val *delta = yyjson_obj_get(inner, "delta");
        const char *delta_type = yyjson_get_str(yyjson_obj_get(delta, "type"));
        if (!delta_type) {
            return YETTY_OK_VOID();
        }
        if (strcmp(delta_type, "text_delta") == 0) {
            yyjson_val *text = yyjson_obj_get(delta, "text");
            if (yyjson_is_str(text)) {
                struct yetty_ycore_void_result end_res =
                    yai_renderer_end_thinking(&app->renderer);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res,
                                    "handle_stream_event: end_thinking");
                struct yetty_ycore_void_result delta_res = yai_renderer_text_delta(
                    &app->renderer, yyjson_get_str(text), yyjson_get_len(text));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res,
                                    "handle_stream_event: text_delta");
            }
        } else if (strcmp(delta_type, "thinking_delta") == 0) {
            yyjson_val *thinking = yyjson_obj_get(delta, "thinking");
            if (yyjson_is_str(thinking)) {
                struct yetty_ycore_void_result delta_res = yai_renderer_thinking_delta(
                    &app->renderer, yyjson_get_str(thinking), yyjson_get_len(thinking));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res,
                                    "handle_stream_event: thinking_delta");
            }
        }
        return YETTY_OK_VOID();
    }
    if (strcmp(inner_type, "content_block_stop") == 0) {
        struct yetty_ycore_void_result end_res = yai_renderer_end_thinking(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "handle_stream_event: block stop");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_assistant_event(struct yai_app *app,
                                                             struct yetty_yai_claude *claude,
                                                             yyjson_val *event)
{
    struct yetty_ycore_void_result end_res = yai_renderer_end_thinking(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "handle_assistant_event: end_thinking");
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return YETTY_OK_VOID();
    }
    yyjson_val *block;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *block_type = yyjson_get_str(yyjson_obj_get(block, "type"));
        if (!block_type || strcmp(block_type, "tool_use") != 0) {
            continue;
        }
        const char *name = yyjson_get_str(yyjson_obj_get(block, "name"));
        const char *tool_use_id = yyjson_get_str(yyjson_obj_get(block, "id"));
        if (!name) {
            name = "?";
        }
        if (tool_use_id) {
            remember_tool_name(claude, tool_use_id, name);
        }
        struct yetty_ycore_void_result call_res =
            yai_render_tool_call(&app->renderer, name, yyjson_obj_get(block, "input"));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, call_res, "handle_assistant_event: tool call");
        char state_line[96];
        snprintf(state_line, sizeof(state_line), "⚙ %s", name);
        struct yetty_ycore_void_result activity_res =
            yai_set_activity(app, "orbit-dots", state_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res, "handle_assistant_event: activity");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_user_event(struct yai_app *app,
                                                        struct yetty_yai_claude *claude,
                                                        yyjson_val *event)
{
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return YETTY_OK_VOID();
    }
    yyjson_val *block;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *block_type = yyjson_get_str(yyjson_obj_get(block, "type"));
        if (!block_type || strcmp(block_type, "tool_result") != 0) {
            continue;
        }
        const char *tool_use_id = yyjson_get_str(yyjson_obj_get(block, "tool_use_id"));
        yyjson_val *is_error_value = yyjson_obj_get(block, "is_error");
        int is_error = is_error_value && yyjson_get_bool(is_error_value);
        struct yetty_ycore_void_result result_res =
            yai_render_tool_result(&app->renderer, yyjson_obj_get(block, "content"), is_error,
                                   lookup_tool_name(claude, tool_use_id));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "handle_user_event: tool result");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_result_event(struct yai_app *app, yyjson_val *event)
{
    struct yetty_ycore_void_result clear_res = yai_renderer_activity_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "handle_result_event: activity clear");
    struct yetty_ycore_void_result finish_res = yai_renderer_finish_turn(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "handle_result_event: finish turn");
    struct yetty_ycore_void_result usage_res = render_turn_usage(app, event);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "handle_result_event: usage");
    struct yetty_ycore_void_result boundary_res = yai_turn_finished(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, boundary_res, "handle_result_event: turn boundary");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:handle_event")]]
static struct yetty_ycore_void_result claude_handle_event(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yai_app *app,
                                                          struct yyjson_val *event)
{
    (void)ctx;
    struct yetty_yai_claude_ptr_result claude_res = yetty_yai_claude_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, claude_res, "claude handle_event: data slice");
    struct yetty_yai_claude *claude = claude_res.value;

    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));
    if (kind && strcmp(kind, "system") == 0) {
        const char *subtype = yyjson_get_str(yyjson_obj_get(event, "subtype"));
        if (subtype && strcmp(subtype, "init") == 0) {
            const char *session_id = yyjson_get_str(yyjson_obj_get(event, "session_id"));
            if (session_id) {
                snprintf(app->session_id, sizeof(app->session_id), "%s", session_id);
            }
        }
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "stream_event") == 0) {
        struct yetty_ycore_void_result stream_res = handle_stream_event(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stream_res, "claude handle_event: stream");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "assistant") == 0) {
        struct yetty_ycore_void_result assistant_res = handle_assistant_event(app, claude, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, assistant_res, "claude handle_event: assistant");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "user") == 0) {
        struct yetty_ycore_void_result user_res = handle_user_event(app, claude, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, user_res, "claude handle_event: user");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "control_request") == 0) {
        struct yetty_ycore_void_result request_res = handle_control_request(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, request_res, "claude handle_event: control request");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "control_response") == 0) {
        struct yetty_ycore_void_result response_res = handle_control_response(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, response_res,
                            "claude handle_event: control response");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "control_cancel_request") == 0) {
        struct yetty_ycore_void_result cancel_res = handle_control_cancel(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cancel_res, "claude handle_event: control cancel");
        return YETTY_OK_VOID();
    }
    if ((kind && strstr(kind, "hook")) || yyjson_obj_get(event, "hook_event_name")) {
        struct yetty_ycore_void_result hook_res = yai_render_hook(&app->renderer, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_res, "claude handle_event: hook");
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "result") == 0) {
        struct yetty_ycore_void_result result_res = handle_result_event(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "claude handle_event: result");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Turn / lifecycle slots
 *---------------------------------------------------------------------------*/

[[clang::annotate("override@yai:claude:send_user_message")]]
static struct yetty_ycore_void_result claude_send_user_message(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yai_app *app,
                                                               const char *text)
{
    (void)ctx;
    (void)obj;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "claude send_user_message: yyjson_mut_doc_new failed");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "user");
    yyjson_mut_val *message = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, message, "role", "user");
    yyjson_mut_obj_add_str(doc, message, "content", text);
    yyjson_mut_obj_add_val(doc, envelope, "message", message);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "claude send_user_message: send");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:interrupt")]]
static struct yetty_ycore_void_result claude_interrupt(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yai_app *app)
{
    (void)ctx;
    struct yetty_yai_claude_ptr_result claude_res = yetty_yai_claude_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, claude_res, "claude interrupt: data slice");
    struct yetty_yai_claude *claude = claude_res.value;

    char request_id[32];
    snprintf(request_id, sizeof(request_id), "yai-%d", ++claude->interrupt_request_counter);

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "claude interrupt: yyjson_mut_doc_new failed");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", request_id);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "interrupt");
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "claude interrupt: send");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:start")]]
static struct yetty_ycore_void_result claude_start(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    (void)ctx;
    (void)obj;
    /* The loop owns the terminal: the yetty MCP server must hand figures
     * back via the sentinel in the tool result instead of racing us to
     * the PTY. Claude-only — the per-turn engines let the server write
     * /dev/tty directly (their events don't carry tool output back). */
    if (setenv("YETTY_MCP_VIA_PARENT", "1", 1) != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude start: setenv YETTY_MCP_VIA_PARENT failed");
    }

    const char *permission_mode = getenv("YAI_PERMISSION_MODE");
    if (!permission_mode || !permission_mode[0]) {
        /* "default" makes un-allowlisted tools prompt — the whole point
         * of the permission flow. */
        permission_mode = "default";
    }
    const char *allowed_tools = getenv("YAI_ALLOWED_TOOLS");
    if (!allowed_tools || !allowed_tools[0]) {
        /* mcp__yetty allows the whole yetty MCP server (all draw_* tools). */
        allowed_tools = "Read,Bash(git *),mcp__yetty";
    }
    const char *model = getenv("YAI_MODEL");

    const char *args[24];
    int arg_count = 0;
    args[arg_count++] = "claude";
    args[arg_count++] = "--print";
    args[arg_count++] = "--input-format";
    args[arg_count++] = "stream-json";
    args[arg_count++] = "--output-format";
    args[arg_count++] = "stream-json";
    args[arg_count++] = "--include-partial-messages";
    args[arg_count++] = "--include-hook-events";
    args[arg_count++] = "--verbose";
    args[arg_count++] = "--permission-mode";
    args[arg_count++] = permission_mode;
    /* Route permission prompts to us as can_use_tool control_requests. */
    args[arg_count++] = "--permission-prompt-tool";
    args[arg_count++] = "stdio";
    args[arg_count++] = "--allowedTools";
    args[arg_count++] = allowed_tools;
    if (app->resume_requested) {
        args[arg_count++] = "--resume";
        args[arg_count++] = app->session_id;
    } else {
        args[arg_count++] = "--session-id";
        args[arg_count++] = app->session_id;
    }
    if (model && model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    args[arg_count] = NULL;

    if (uv_pipe_init(&app->loop, &app->child_stdin_pipe, 0) != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude start: stdin uv_pipe_init failed");
    }
    if (uv_pipe_init(&app->loop, &app->child_stdout_pipe, 0) != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude start: stdout uv_pipe_init failed");
    }
    app->child_stdout_pipe_initialized = 1;
    app->child_stdin_pipe.data = app;
    app->child_stdout_pipe.data = app;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[0].data.stream = (uv_stream_t *)&app->child_stdin_pipe;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&app->child_stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = app->child_stderr_fd;

    uv_process_options_t options = {0};
    options.exit_cb = yai_child_exit_cb;
    options.file = "claude";
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;
    /* env = NULL inherits ours — including YETTY_MCP_VIA_PARENT. */

    app->child_process.data = app;
    int spawn_status = uv_spawn(&app->loop, &app->child_process, &options);
    if (spawn_status != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude start: uv_spawn failed (claude not in PATH?)");
    }
    app->child_alive = 1;
    app->child_stdin_open = 1;
    /* From here the child is live: any failure must terminate it rather
     * than leak an unmanaged process. */
    if (uv_read_start((uv_stream_t *)&app->child_stdout_pipe, yai_child_stdout_alloc_cb,
                      yai_child_stdout_read_cb) != 0) {
        uv_process_kill(&app->child_process, SIGKILL);
        app->child_alive = 0;
        return YETTY_ERR(yetty_ycore_void, "claude start: uv_read_start failed");
    }
    /* Fetch the CLI's slash-command list for the completion menu. */
    struct yetty_ycore_void_result init_res = send_initialize_request(app);
    if (YETTY_IS_ERR(init_res)) {
        uv_read_stop((uv_stream_t *)&app->child_stdout_pipe);
        uv_process_kill(&app->child_process, SIGKILL);
        app->child_alive = 0;
        return YETTY_ERR(yetty_ycore_void, "claude start: initialize request", init_res);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:describe_config")]]
static struct yetty_ycore_void_result claude_describe_config(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yai_app *app, char *out,
                                                             size_t out_size)
{
    (void)ctx;
    (void)obj;
    (void)app;
    /* Mirror the exact defaults claude_start applies. */
    const char *permission_mode = getenv("YAI_PERMISSION_MODE");
    if (!permission_mode || !permission_mode[0]) {
        permission_mode = "default";
    }
    const char *allowed_tools = getenv("YAI_ALLOWED_TOOLS");
    if (!allowed_tools || !allowed_tools[0]) {
        allowed_tools = "Read,Bash(git *),mcp__yetty";
    }
    const char *model = getenv("YAI_MODEL");
    int written = snprintf(out, out_size,
                           "model: %s  [YAI_MODEL]\n"
                           "permission mode: %s  [YAI_PERMISSION_MODE]\n"
                           "allowed tools: %s  [YAI_ALLOWED_TOOLS]\n"
                           "resume: --resume <session id> (session is persistent)",
                           (model && model[0]) ? model : "(CLI default)", permission_mode,
                           allowed_tools);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "claude describe_config: rows truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:config_knob")]]
static struct yetty_ycore_void_result claude_config_knob(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    (void)ctx;
    (void)obj;
    (void)app;
    const char *permission_mode = getenv("YAI_PERMISSION_MODE");
    if (!permission_mode || !permission_mode[0]) {
        permission_mode = "default";
    }
    int written = snprintf(out, out_size,
                           "YAI_PERMISSION_MODE|permission mode|"
                           "default,acceptEdits,plan,bypassPermissions|%s",
                           permission_mode);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "claude config_knob: spec truncated");
    }
    return YETTY_OK_VOID();
}

/* setenv (next session) + a live set_permission_mode control_request
 * into the running session, so the edit applies immediately. */
[[clang::annotate("override@yai:claude:apply_config")]]
static struct yetty_ycore_void_result claude_apply_config(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yai_app *app, const char *key,
                                                          const char *value)
{
    (void)ctx;
    struct yetty_yai_claude_ptr_result claude_res = yetty_yai_claude_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, claude_res, "claude apply_config: data slice");
    struct yetty_yai_claude *claude = claude_res.value;
    if (!key || !key[0] || !value) {
        return YETTY_ERR(yetty_ycore_void, "claude apply_config: missing key/value");
    }
    if (setenv(key, value, 1) != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude apply_config: setenv failed");
    }
    if (strcmp(key, "YAI_PERMISSION_MODE") != 0 || !app->child_alive || !app->child_stdin_open) {
        return YETTY_OK_VOID();
    }
    char request_id[32];
    snprintf(request_id, sizeof(request_id), "yai-%d", ++claude->interrupt_request_counter);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "claude apply_config: yyjson_mut_doc_new failed");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", request_id);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "set_permission_mode");
    yyjson_mut_obj_add_str(doc, request, "mode", value);
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "claude apply_config: live mode push");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:on_child_exit")]]
static struct yetty_ycore_void_result claude_on_child_exit(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           struct yai_app *app,
                                                           int64_t exit_status)
{
    (void)ctx;
    (void)obj;
    /* The session child died — best-effort: every teardown step runs,
     * the first error is surfaced at the end. */
    struct yetty_ycore_void_result teardown = YETTY_OK_VOID();
    if (!app->shutting_down) {
        if (exit_status != 0) {
            teardown = yetty_ycore_void_chain(teardown, yai_renderer_pin_hide(&app->renderer));
            printf("\n" YAI_RED "✗ claude exited with status %lld — see %s and tmp/" YAI_RESET
                   "\n",
                   (long long)exit_status, app->transcript_path);
            teardown = yetty_ycore_void_chain(teardown, yai_render_flush_stdout());
            app->exit_code = 1;
        }
        teardown = yetty_ycore_void_chain(teardown, yai_begin_shutdown(app));
    }
    uv_close((uv_handle_t *)&app->child_process, yai_handle_closed_cb);
    uv_stop(&app->loop);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "claude on_child_exit");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:claude:on_child_eof")]]
static struct yetty_ycore_void_result claude_on_child_eof(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yai_app *app)
{
    (void)ctx;
    (void)obj;
    /* The persistent pipe closed: the session is dead — the turn (if
     * any) will never finish; never hang waiting for it. */
    if (!app->child_alive || app->shutting_down) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result teardown =
        yai_renderer_pin_hide(&app->renderer);
    printf("\n" YAI_RED "✗ claude exited unexpectedly — see stderr log under tmp/" YAI_RESET "\n");
    teardown = yetty_ycore_void_chain(teardown, yai_render_flush_stdout());
    app->exit_code = 1;
    teardown = yetty_ycore_void_chain(teardown, yai_begin_shutdown(app));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "claude on_child_eof");
    return YETTY_OK_VOID();
}

#include "claude.gen.c"
