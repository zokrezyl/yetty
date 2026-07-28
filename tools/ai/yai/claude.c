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
struct YETTY_ANNOTATE("class@yai:claude") YETTY_ANNOTATE("parent@yai:engine") yetty_yai_claude {
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
    /* The CLI advertises the exact model list its own /model picker shows —
     * value (the --model / set_model token), displayName and description.
     * Capture it so /model and the config knob never hardcode models and
     * always offer the current set (fable, …). "default" is normalized to
     * the empty value yai uses for "the CLI's own default". */
    yyjson_val *models_array = payload ? yyjson_obj_get(payload, "models") : NULL;
    if (yyjson_is_arr(models_array)) {
        app->claude_model_count = 0;
        yyjson_val *entry;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(models_array, &iter);
        while ((entry = yyjson_arr_iter_next(&iter)) != NULL &&
               app->claude_model_count < YAI_MODEL_CHOICES_MAX) {
            const char *value = yyjson_get_str(yyjson_obj_get(entry, "value"));
            if (!value) {
                continue;
            }
            struct yai_model_choice *choice = &app->claude_models[app->claude_model_count];
            snprintf(choice->value, sizeof(choice->value), "%s",
                     strcmp(value, "default") == 0 ? "" : value);
            const char *display_name = yyjson_get_str(yyjson_obj_get(entry, "displayName"));
            snprintf(choice->display_name, sizeof(choice->display_name), "%s",
                     display_name ? display_name : value);
            const char *description = yyjson_get_str(yyjson_obj_get(entry, "description"));
            snprintf(choice->description, sizeof(choice->description), "%s",
                     description ? description : "");
            app->claude_model_count++;
        }
        ydebug("yai claude: %d models advertised", app->claude_model_count);
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
YETTY_ANNOTATE("override@yai:claude:resolve_permission")
static struct yetty_ycore_void_result claude_resolve_permission(struct yetty_yclass_object *obj,
                                                                struct yai_app *app, int allowed)
{
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

static struct yetty_ycore_void_result handle_control_request(struct yai_app *app, yyjson_val *event)
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
    /* The CLI ships a friendlier display label and a human-readable summary
     * of the call (e.g. the target path). The summary is a useful detail,
     * but display_name is attacker-influenceable and must NOT stand in for
     * the tool's identity — a hostile one could read "Read File" over a
     * Bash call. Keep the raw tool_name as the authoritative label and show
     * display_name only as a parenthetical hint when it genuinely differs. */
    const char *display_name = yyjson_get_str(yyjson_obj_get(request, "display_name"));
    int has_alias = display_name && display_name[0] && strcmp(display_name, tool_name) != 0;
    const char *description = yyjson_get_str(yyjson_obj_get(request, "description"));
    yyjson_val *input = yyjson_obj_get(request, "input");

    /* Auto-approve without prompting when the session is in full-bypass
     * mode (yai enforces it here, so it holds even if the live
     * set_permission_mode push didn't), or when this tool was marked
     * "always allow". The CLI's own "auto" mode is NOT bypass: its
     * classifier only escalates what it couldn't decide, so those requests
     * still deserve a prompt. Preserve the tool input on the response
     * (fail closed if it can't be copied — never approve with empty
     * input). */
    const char *perm_mode = app->config.claude.permission_mode;
    int bypass_mode =
        strcmp(perm_mode, "bypass") == 0 || strcmp(perm_mode, "bypassPermissions") == 0;
    if (bypass_mode || yai_tool_always_allowed(app, tool_name)) {
        yyjson_mut_doc *input_copy = yyjson_mut_doc_new(NULL);
        if (input_copy && input) {
            yyjson_mut_val *copy = yyjson_val_mut_copy(input_copy, input);
            if (copy) {
                yyjson_mut_doc_set_root(input_copy, copy);
            } else {
                yyjson_mut_doc_free(input_copy);
                input_copy = NULL;
            }
        }
        if (input && !input_copy) {
            return YETTY_ERR(yetty_ycore_void,
                             "handle_control_request: preserve auto-allowed tool input");
        }
        struct yetty_ycore_void_result allow_res =
            send_permission_response(app, request_id, 1, input_copy);
        yyjson_mut_doc_free(input_copy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, allow_res, "handle_control_request: auto-allow");
        return YETTY_OK_VOID();
    }

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
    if (description && description[0]) {
        snprintf(summary, sizeof(summary), "%s", description);
    } else {
        yai_summarize_tool_input(input, summary, sizeof(summary));
    }
    char alias_hint[64];
    if (has_alias) {
        snprintf(alias_hint, sizeof(alias_hint), " " YAI_DIM "(%s)" YAI_RESET, display_name);
    } else {
        alias_hint[0] = '\0';
    }

    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_control_request: suspend");
    printf("\n" YAI_MINT "? permission: " YAI_RESET YAI_BOLD "%s" YAI_RESET "%s " YAI_DIM
           "%s" YAI_RESET "\n" YAI_DIM
           "  press a key:  y=yes  n=no  a=always this tool  !=bypass (approve all)  ·  Ctrl-C "
           "interrupts" YAI_RESET "\n",
           tool_name, alias_hint, summary);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_control_request: flush");
    /* The status line + HUD mirror the pending question (display-only —
     * the verdict is typed at the prompt). */
    char state_line[128];
    if (has_alias) {
        snprintf(state_line, sizeof(state_line), "? %s (%s) %s", tool_name, display_name, summary);
    } else {
        snprintf(state_line, sizeof(state_line), "? %s %s", tool_name, summary);
    }
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

/* rate_limit_event — the CLI reports the account's live rate-limit state
 * around each API call: one event per window (rateLimitType "five_hour" or
 * "seven_day…"), with resetsAt always present and utilization only once the
 * window passes a warning threshold. Fold each into app->engine_quota so the
 * HUD quota works without the usage proxy (yai_quota_get prefers the proxy's
 * richer capture when it has data). */
static struct yetty_ycore_void_result handle_rate_limit_event(struct yai_app *app,
                                                              yyjson_val *event)
{
    yyjson_val *info = yyjson_obj_get(event, "rate_limit_info");
    const char *window_type = yyjson_get_str(yyjson_obj_get(info, "rateLimitType"));
    if (!window_type) {
        return YETTY_OK_VOID();
    }
    int is_session = strcmp(window_type, "five_hour") == 0;
    int is_week = strncmp(window_type, "seven_day", 9) == 0;
    if (!is_session && !is_week) {
        return YETTY_OK_VOID(); /* an unknown window — nothing to file it under */
    }
    struct yai_quota *quota = &app->engine_quota;
    if (!quota->valid) {
        quota->session_pct = -1; /* -1 = utilization not reported yet */
        quota->week_pct = -1;
        quota->valid = 1;
    }
    yyjson_val *utilization = yyjson_obj_get(info, "utilization");
    yyjson_val *resets_at = yyjson_obj_get(info, "resetsAt");
    if (is_session) {
        if (yyjson_is_num(utilization)) {
            quota->session_pct = (int)(yyjson_get_num(utilization) * 100.0 + 0.5);
        }
        if (yyjson_is_num(resets_at)) {
            quota->session_reset = (long long)yyjson_get_num(resets_at);
        }
    } else {
        if (yyjson_is_num(utilization)) {
            quota->week_pct = (int)(yyjson_get_num(utilization) * 100.0 + 0.5);
        }
        if (yyjson_is_num(resets_at)) {
            quota->week_reset = (long long)yyjson_get_num(resets_at);
        }
    }
    /* Keep the #{quota} composite and the decomposed HUD variables live. */
    char quota_summary[96];
    yai_quota_summary(app, quota_summary, sizeof(quota_summary));
    if (quota_summary[0]) {
        snprintf(app->quota_text, sizeof(app->quota_text), "%s", quota_summary);
    }
    struct yetty_ycore_void_result refresh_res = yai_refresh_hud_stats(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, refresh_res, "handle_rate_limit_event: hud refresh");
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
        struct yai_event begin = {.kind = YAI_EVENT_MESSAGE_BEGIN};
        struct yetty_ycore_void_result begin_res = yai_event_dispatch(app, &begin);
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
                struct yai_event delta_event = {
                    .kind = YAI_EVENT_TEXT_DELTA,
                    .text = {.text = yyjson_get_str(text), .len = yyjson_get_len(text)}};
                struct yetty_ycore_void_result delta_res = yai_event_dispatch(app, &delta_event);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "handle_stream_event: text_delta");
            }
        } else if (strcmp(delta_type, "thinking_delta") == 0) {
            yyjson_val *thinking = yyjson_obj_get(delta, "thinking");
            if (yyjson_is_str(thinking)) {
                struct yai_event delta_event = {
                    .kind = YAI_EVENT_THINKING_DELTA,
                    .text = {.text = yyjson_get_str(thinking), .len = yyjson_get_len(thinking)}};
                struct yetty_ycore_void_result delta_res = yai_event_dispatch(app, &delta_event);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res,
                                    "handle_stream_event: thinking_delta");
            }
        }
        return YETTY_OK_VOID();
    }
    if (strcmp(inner_type, "content_block_stop") == 0) {
        struct yai_event end = {.kind = YAI_EVENT_THINKING_END};
        struct yetty_ycore_void_result end_res = yai_event_dispatch(app, &end);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "handle_stream_event: block stop");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_assistant_event(struct yai_app *app,
                                                             struct yetty_yai_claude *claude,
                                                             yyjson_val *event)
{
    struct yai_event thinking_end = {.kind = YAI_EVENT_THINKING_END};
    struct yetty_ycore_void_result end_res = yai_event_dispatch(app, &thinking_end);
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
        struct yai_event call = {
            .kind = YAI_EVENT_TOOL_CALL,
            .tool_call = {.name = name, .input = yyjson_obj_get(block, "input")}};
        struct yetty_ycore_void_result call_res = yai_event_dispatch(app, &call);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, call_res, "handle_assistant_event: tool call");
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
        struct yai_event result = {
            .kind = YAI_EVENT_TOOL_RESULT,
            .tool_result = {.content = yyjson_obj_get(block, "content"),
                            .is_error = is_error,
                            .tool_name = lookup_tool_name(claude, tool_use_id)}};
        struct yetty_ycore_void_result result_res = yai_event_dispatch(app, &result);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "handle_user_event: tool result");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_result_event(struct yai_app *app, yyjson_val *event)
{
    /* Flush the open streamed line and clear the activity glyph, then
     * render the turn's accounting — both engine-neutral, so they go
     * through the event dispatcher; the turn boundary (queue pump /
     * re-prompt) stays app lifecycle. */
    struct yai_event turn_end = {.kind = YAI_EVENT_TURN_END};
    struct yetty_ycore_void_result end_res = yai_event_dispatch(app, &turn_end);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "handle_result_event: turn end");

    /* An error result (e.g. an un-resumable session: "No conversation
     * found with session ID …") carries is_error / a non-"success"
     * subtype and an `errors` array. Surface that message instead of the
     * misleading all-zero usage line it ships with. */
    yyjson_val *is_error_value = yyjson_obj_get(event, "is_error");
    const char *subtype = yyjson_get_str(yyjson_obj_get(event, "subtype"));
    int is_error = (is_error_value && yyjson_get_bool(is_error_value)) ||
                   (subtype && strcmp(subtype, "success") != 0);
    if (is_error) {
        app->turn_failed = 1;
        app->saw_result_error = 1;
        const char *message = NULL;
        yyjson_val *errors = yyjson_obj_get(event, "errors");
        if (yyjson_is_arr(errors)) {
            message = yyjson_get_str(yyjson_arr_get_first(errors));
        }
        if (!message) {
            message = yyjson_get_str(yyjson_obj_get(event, "result"));
        }
        if (!message) {
            message = subtype ? subtype : "error";
        }
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_result_event: suspend");
        printf("\n" YAI_RED "✗ claude: %s" YAI_RESET "\n", message);
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_result_event: flush");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "handle_result_event: resume");
    } else {
        yyjson_val *usage = yyjson_obj_get(event, "usage");
        yyjson_val *cost_value = yyjson_obj_get(event, "total_cost_usd");
        yyjson_val *duration_value = yyjson_obj_get(event, "duration_ms");
        struct yai_event usage_event = {
            .kind = YAI_EVENT_USAGE,
            .usage = {.input = yai_usage_field(usage, "input_tokens"),
                      .output = yai_usage_field(usage, "output_tokens"),
                      .cache_read = yai_usage_field(usage, "cache_read_input_tokens"),
                      .cache_creation = yai_usage_field(usage, "cache_creation_input_tokens"),
                      .cost = cost_value ? yyjson_get_num(cost_value) : 0.0,
                      .seconds = duration_value ? yyjson_get_num(duration_value) / 1000.0 : 0.0,
                      .has_cost = 1}};
        struct yetty_ycore_void_result usage_res = yai_event_dispatch(app, &usage_event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "handle_result_event: usage");
    }

    struct yetty_ycore_void_result boundary_res = yai_turn_finished(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, boundary_res, "handle_result_event: turn boundary");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yai:claude:handle_event")
static struct yetty_ycore_void_result claude_handle_event(struct yetty_yclass_object *obj,
                                                          struct yai_app *app,
                                                          struct yyjson_val *event)
{
    struct yetty_yai_claude_ptr_result claude_res = yetty_yai_claude_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, claude_res, "claude handle_event: data slice");
    struct yetty_yai_claude *claude = claude_res.value;

    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));
    if (kind && strcmp(kind, "system") == 0) {
        const char *subtype = yyjson_get_str(yyjson_obj_get(event, "subtype"));
        if (subtype && strcmp(subtype, "init") == 0) {
            const char *session_id = yyjson_get_str(yyjson_obj_get(event, "session_id"));
            /* A /btw side child's fresh session id is deliberately NOT
             * adopted — the main conversation's id comes back at the
             * turn boundary. */
            if (session_id && !app->btw_turn) {
                snprintf(app->session_id, sizeof(app->session_id), "%s", session_id);
            }
        } else if (subtype && strcmp(subtype, "thinking_tokens") == 0) {
            /* Running estimate for the in-flight request (cumulative). Show
             * it next to the "thinking" activity on the left of the HUD. */
            app->estimated_tokens =
                (uint64_t)yyjson_get_uint(yyjson_obj_get(event, "estimated_tokens"));
            char tokens_text[16];
            yai_format_tokens(app->estimated_tokens, tokens_text, sizeof(tokens_text));
            char state[64];
            snprintf(state, sizeof(state), "… thinking · %s", tokens_text);
            struct yetty_ycore_void_result activity_res =
                yai_set_activity(app, "typing-dots", state);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res,
                                "claude handle_event: thinking activity");
        } else if (subtype && strcmp(subtype, "status") == 0) {
            /* The CLI's own phase report ("requesting", retry states, …) —
             * mirror it in the activity while a turn is in flight. */
            const char *status = yyjson_get_str(yyjson_obj_get(event, "status"));
            if (status && status[0] && app->waiting) {
                char state[64];
                snprintf(state, sizeof(state), "… %s", status);
                struct yetty_ycore_void_result activity_res =
                    yai_set_activity(app, "typing-dots", state);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res,
                                    "claude handle_event: status activity");
            }
        }
        return YETTY_OK_VOID();
    }
    if (kind && strcmp(kind, "rate_limit_event") == 0) {
        struct yetty_ycore_void_result quota_res = handle_rate_limit_event(app, event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, quota_res, "claude handle_event: rate limit");
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
        struct yai_event hook = {.kind = YAI_EVENT_HOOK, .hook = {.event = event}};
        struct yetty_ycore_void_result hook_res = yai_event_dispatch(app, &hook);
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

YETTY_ANNOTATE("override@yai:claude:send_user_message")
static struct yetty_ycore_void_result claude_send_user_message(struct yetty_yclass_object *obj,
                                                               struct yai_app *app,
                                                               const char *text)
{
    (void)obj;
    app->saw_result_error = 0; /* fresh turn */
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

/*---------------------------------------------------------------------------
 * /btw side child — a one-shot `claude --print <question>` on a fresh
 * session. The persistent main child is bound to the main session and the
 * CLI refuses /btw itself in stream-json mode ("/btw isn't available in
 * this environment"), so the side question gets its own short-lived
 * process. Its stream-json events flow through the normal claude handler
 * (the main child is idle for the duration — app->waiting gates submits);
 * the result event ends the turn, which restores the main session id. No
 * --permission-prompt-tool is passed, so the CLI denies tool use itself —
 * matching the native /btw's no-tools semantics.
 *---------------------------------------------------------------------------*/

/* One line can be large (stream-json figures), but a child that never
 * emits a newline must not grow the buffer without bound. */
#define YAI_CLAUDE_BTW_OUT_MAX (256u * 1024u * 1024u)

YETTY_EXTERNAL_CALLBACK
static void claude_btw_exit_cb(uv_process_t *process, int64_t exit_status, int term_signal)
{
    struct yai_app *app = process->data;
    (void)exit_status;
    (void)term_signal;
    app->btw_child_alive = 0;
    uv_close((uv_handle_t *)&app->btw_process, yai_handle_closed_cb);
    /* The result event normally already ended the turn; a crash or an
     * interrupt kill without one must still unblock the UI and restore
     * the main session id (yai_turn_finished handles both). */
    if (app->waiting && app->btw_turn) {
        yai_report_error(app, "btw stream finish", yai_renderer_finish_turn(&app->renderer));
        yai_report_error(app, "btw boundary", yai_turn_finished(app));
    }
}

YETTY_EXTERNAL_CALLBACK
static void claude_btw_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
    struct yai_app *app = stream->data;
    if (nread < 0) {
        free(buffer->base);
        uv_read_stop(stream);
        uv_close((uv_handle_t *)&app->btw_stdout_pipe, yai_handle_closed_cb);
        return;
    }
    if (nread == 0) {
        free(buffer->base);
        return;
    }
    if ((size_t)nread > YAI_CLAUDE_BTW_OUT_MAX ||
        app->btw_out_len > YAI_CLAUDE_BTW_OUT_MAX - (size_t)nread) {
        free(buffer->base);
        yai_report_error(app, "btw stdout",
                         YETTY_ERR(yetty_ycore_void, "claude_btw_read_cb: line too large"));
        return;
    }
    if (app->btw_out_len + (size_t)nread + 1 > app->btw_out_cap) {
        size_t new_cap = app->btw_out_cap ? app->btw_out_cap : 64 * 1024;
        while (new_cap < app->btw_out_len + (size_t)nread + 1) {
            new_cap *= 2; /* bounded by YAI_CLAUDE_BTW_OUT_MAX above */
        }
        char *grown = realloc(app->btw_out_buf, new_cap);
        if (!grown) {
            free(buffer->base);
            yai_report_error(app, "btw stdout",
                             YETTY_ERR(yetty_ycore_void, "claude_btw_read_cb: realloc failed"));
            return;
        }
        app->btw_out_buf = grown;
        app->btw_out_cap = new_cap;
    }
    memcpy(app->btw_out_buf + app->btw_out_len, buffer->base, (size_t)nread);
    app->btw_out_len += (size_t)nread;
    free(buffer->base);

    size_t line_start = 0;
    for (size_t index = 0; index < app->btw_out_len; index++) {
        if (app->btw_out_buf[index] != '\n') {
            continue;
        }
        yai_report_error(
            app, "btw child line",
            yai_handle_child_line(app, app->btw_out_buf + line_start, index - line_start));
        line_start = index + 1;
    }
    if (line_start > 0) {
        memmove(app->btw_out_buf, app->btw_out_buf + line_start, app->btw_out_len - line_start);
        app->btw_out_len -= line_start;
    }
}

struct yetty_ycore_void_result yai_claude_btw_start(struct yai_app *app, const char *question)
{
    const char *model = app->config.claude.model;
    const char *args[10];
    int arg_count = 0;
    args[arg_count++] = "claude";
    args[arg_count++] = "--print";
    args[arg_count++] = "--output-format";
    args[arg_count++] = "stream-json";
    args[arg_count++] = "--include-partial-messages";
    args[arg_count++] = "--verbose";
    if (model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    args[arg_count++] = question;
    args[arg_count] = NULL;

    if (uv_pipe_init(&app->loop, &app->btw_stdout_pipe, 0) != 0) {
        return YETTY_ERR(yetty_ycore_void, "yai_claude_btw_start: stdout uv_pipe_init failed");
    }
    app->btw_stdout_pipe.data = app;
    app->btw_out_len = 0;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE; /* the question travels in argv */
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&app->btw_stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = app->child_stderr_fd;

    uv_process_options_t options = {0};
    options.exit_cb = claude_btw_exit_cb;
    options.file = "claude";
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;

    app->btw_process.data = app;
    if (uv_spawn(&app->loop, &app->btw_process, &options) != 0) {
        uv_close((uv_handle_t *)&app->btw_stdout_pipe, yai_handle_closed_cb);
        return YETTY_ERR(yetty_ycore_void,
                         "yai_claude_btw_start: uv_spawn failed (claude not in PATH?)");
    }
    app->btw_child_alive = 1;
    if (uv_read_start((uv_stream_t *)&app->btw_stdout_pipe, yai_child_stdout_alloc_cb,
                      claude_btw_read_cb) != 0) {
        uv_process_kill(&app->btw_process, SIGKILL);
        return YETTY_ERR(yetty_ycore_void, "yai_claude_btw_start: uv_read_start failed");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yai:claude:interrupt")
static struct yetty_ycore_void_result claude_interrupt(struct yetty_yclass_object *obj,
                                                       struct yai_app *app)
{
    if (app->btw_child_alive) {
        /* A /btw side child has no control channel — stop the specific
         * process we spawned; its exit callback finishes the turn. */
        uv_process_kill(&app->btw_process, SIGTERM);
        return YETTY_OK_VOID();
    }
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

/* yai's UI names for the CLI's permission modes. They match the CLI's own
 * set — manual, acceptEdits, plan, auto (the CLI's classifier-driven
 * auto-accept), dontAsk — except that "bypass" is the UI's short name for
 * "bypassPermissions". Legacy persisted values still translate: "default"
 * is the CLI's old name for manual. NOTE: "auto" used to be yai's word for
 * full bypass; it now means the CLI's own auto mode — the full-bypass
 * shortcut ('!' at the prompt) stores "bypass". */
static const char *claude_permission_cli_mode(const char *mode)
{
    if (mode && strcmp(mode, "bypass") == 0) {
        return "bypassPermissions";
    }
    /* The CLI's interactive ask-mode is named "default"; yai's UI name for
     * it is "manual". Older persisted configs may already store "default".
     * Map both to the CLI's current spelling. */
    if (mode && (strcmp(mode, "manual") == 0 || strcmp(mode, "default") == 0)) {
        return "default";
    }
    return mode;
}

static const char *claude_permission_ui_mode(const char *mode)
{
    if (mode && strcmp(mode, "bypassPermissions") == 0) {
        return "bypass";
    }
    if (mode && strcmp(mode, "default") == 0) {
        return "manual";
    }
    return mode;
}

/* The concrete --allowedTools list for a tools preset name (mcp__yetty allows
 * the whole yetty MCP server — all draw_* tools). */
static const char *claude_preset_tools(const char *preset)
{
    if (strcmp(preset, "readonly") == 0) {
        return "Read,mcp__yetty";
    }
    if (strcmp(preset, "edit") == 0) {
        return "Read,Edit,Write,Bash(git *),mcp__yetty";
    }
    if (strcmp(preset, "full") == 0) {
        return "Read,Edit,Write,Bash,mcp__yetty";
    }
    return "Read,Bash(git *),mcp__yetty"; /* curated (default) */
}

YETTY_ANNOTATE("override@yai:claude:start")
static struct yetty_ycore_void_result claude_start(struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    (void)obj;
    /* The loop owns the terminal: the yetty MCP server must hand figures
     * back via the sentinel in the tool result instead of racing us to
     * the PTY. Claude-only — the per-turn engines let the server write
     * /dev/tty directly (their events don't carry tool output back). */
    if (setenv("YETTY_MCP_VIA_PARENT", "1", 1) != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude start: setenv YETTY_MCP_VIA_PARENT failed");
    }

    const char *permission_mode = app->config.claude.permission_mode;
    /* An explicit allowlist wins; otherwise expand the chosen preset. */
    const char *allowed_tools = app->config.claude.allowed_tools[0]
                                    ? app->config.claude.allowed_tools
                                    : claude_preset_tools(app->config.claude.allowed_preset);
    const char *model = app->config.claude.model;

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
    args[arg_count++] = claude_permission_cli_mode(permission_mode);
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
    if (model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    /* "auto" leaves claude to its own default; anything else is passed. */
    if (app->config.claude.effort[0] && strcmp(app->config.claude.effort, "auto") != 0) {
        args[arg_count++] = "--effort";
        args[arg_count++] = app->config.claude.effort;
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

YETTY_ANNOTATE("override@yai:claude:describe_config")
static struct yetty_ycore_void_result claude_describe_config(struct yetty_yclass_object *obj,
                                                             struct yai_app *app, char *out,
                                                             size_t out_size)
{
    (void)obj;
    (void)app;
    /* Mirror the exact defaults claude_start applies. */
    const char *permission_mode = app->config.claude.permission_mode;
    const char *allowed_tools = app->config.claude.allowed_tools[0]
                                    ? app->config.claude.allowed_tools
                                    : claude_preset_tools(app->config.claude.allowed_preset);
    const char *model = app->config.claude.model;
    int written = snprintf(out, out_size,
                           "model: %s  [--model]\n"
                           "permission mode: %s  [--permission-mode]\n"
                           "allowed tools: %s  [--allowed-preset / --allowed-tools]\n"
                           "resume: --resume <session id> (session is persistent)",
                           model[0] ? model : "(CLI default)",
                           claude_permission_ui_mode(permission_mode), allowed_tools);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "claude describe_config: rows truncated");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yai:claude:config_knob")
static struct yetty_ycore_void_result claude_config_knob(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    (void)obj;
    /* Translate legacy stored values (default → manual, bypassPermissions →
     * bypass) so they select the matching radio. */
    const char *permission_mode = claude_permission_ui_mode(app->config.claude.permission_mode);
    /* One knob spec per line: ENGINE_FIELD|label|options|current. "tools" is a
     * preset; claude_preset_tools maps the preset name → the concrete tool
     * list at spawn. Radio option values must stay comma-free, hence the
     * preset names. The modes mirror the CLI's: manual asks, auto is the
     * CLI's classifier-driven auto-accept, dontAsk resolves without asking,
     * bypass approves everything (the CLI's bypassPermissions). Reasoning
     * effort is a model parameter, so it lives on the model tab (main.c
     * build_effort_knob). */
    int written = snprintf(out, out_size,
                           "permission-mode|permission mode|"
                           "manual,acceptEdits,plan,auto,dontAsk,bypass|%s\n"
                           "allowed-preset|tools|curated,readonly,edit,full|%s",
                           permission_mode, app->config.claude.allowed_preset);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "claude config_knob: spec truncated");
    }
    return YETTY_OK_VOID();
}

/* Live control_request pushes into the running session, so a config change
 * applies without waiting for the next spawn (storage is in app->config,
 * written by the caller): permission mode → set_permission_mode, model →
 * set_model. Everything else (tools preset, effort) has no live control
 * command and is read fresh at the next spawn. */
YETTY_ANNOTATE("override@yai:claude:apply_config")
static struct yetty_ycore_void_result claude_apply_config(struct yetty_yclass_object *obj,
                                                          struct yai_app *app, const char *key,
                                                          const char *value)
{
    struct yetty_yai_claude_ptr_result claude_res = yetty_yai_claude_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, claude_res, "claude apply_config: data slice");
    struct yetty_yai_claude *claude = claude_res.value;
    if (!key || !key[0] || !value) {
        return YETTY_ERR(yetty_ycore_void, "claude apply_config: missing key/value");
    }
    const char *subtype = NULL;
    const char *field = NULL;
    const char *field_value = NULL;
    if (strcmp(key, "permission-mode") == 0) {
        subtype = "set_permission_mode";
        field = "mode";
        field_value = claude_permission_cli_mode(value);
    } else if (strcmp(key, "model") == 0 && value[0]) {
        /* An empty model means "the CLI's default" — there is no live
         * "reset to default" push; it takes effect at the next spawn. */
        subtype = "set_model";
        field = "model";
        field_value = value;
    }
    if (!subtype || !app->child_alive || !app->child_stdin_open) {
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
    yyjson_mut_obj_add_str(doc, request, "subtype", subtype);
    yyjson_mut_obj_add_str(doc, request, field, field_value);
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "claude apply_config: live push");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yai:claude:on_child_exit")
static struct yetty_ycore_void_result claude_on_child_exit(struct yetty_yclass_object *obj,
                                                           struct yai_app *app, int64_t exit_status)
{
    (void)obj;
    /* The session child died — best-effort: every teardown step runs,
     * the first error is surfaced at the end. */
    struct yetty_ycore_void_result teardown = YETTY_OK_VOID();
    if (!app->shutting_down) {
        if (exit_status != 0) {
            teardown = yetty_ycore_void_chain(teardown, yai_renderer_pin_hide(&app->renderer));
            printf("\n" YAI_RED "✗ claude exited with status %lld — see %s and tmp/" YAI_RESET "\n",
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

YETTY_ANNOTATE("override@yai:claude:on_child_eof")
static struct yetty_ycore_void_result claude_on_child_eof(struct yetty_yclass_object *obj,
                                                          struct yai_app *app)
{
    (void)obj;
    /* The persistent pipe closed: the session is dead — the turn (if
     * any) will never finish; never hang waiting for it. */
    if (!app->child_alive || app->shutting_down) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result teardown = yai_renderer_pin_hide(&app->renderer);
    /* If claude already reported a fatal error result (e.g. a failed
     * --resume), its exit is explained — don't add a confusing
     * "exited unexpectedly". Otherwise the close really is unexpected. */
    if (!app->saw_result_error) {
        printf("\n" YAI_RED "✗ claude exited unexpectedly — see stderr log under tmp/" YAI_RESET
               "\n");
        teardown = yetty_ycore_void_chain(teardown, yai_render_flush_stdout());
    }
    app->exit_code = 1;
    teardown = yetty_ycore_void_chain(teardown, yai_begin_shutdown(app));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "claude on_child_eof");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/yai/claude.c"
