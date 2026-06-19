/*
 * gemini.c — yclass class `yai:gemini`: the Google AI (gemini CLI)
 * engine.
 *
 * A fresh `gemini --output-format stream-json --prompt <text>` child per
 * turn (lifecycle inherited from yai:turn_engine). stream-json is clean
 * JSONL — one event object per line, streamed as the turn runs:
 *
 *   {"type":"init", "session_id":…, "model":…}
 *   {"type":"message", "role":"user", "content":…}          (our echo)
 *   {"type":"message", "role":"assistant", "content":…, "delta":true}
 *   {"type":"result", "status":"success",
 *      "stats":{"input_tokens":N, "output_tokens":N, "cached":N,
 *               "duration_ms":N, …}}
 *
 * (The plain `--output-format json` mode emits ONE pretty-printed object
 * instead — valid JSON, but not line-delimited; stream-json is the
 * better fit and streams the response live.)
 *
 * No session resume yet: each turn is a fresh conversation. The CLI now
 * exposes --session-id / --resume and stream-json hands back a
 * session_id, so resume can be wired here as a follow-up.
 */
#include "app.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@yai:gemini")]] [[clang::annotate("parent@yai:turn_engine")]]
yetty_yai_gemini {
    /* The class@ annotation needs a struct to sit on; gemini keeps no
     * engine-private state. */
    char unused;
};

YETTY_YRESULT_DECLARE(yetty_yai_gemini_ptr, struct yetty_yai_gemini *);

[[clang::annotate("override@yai:gemini:start")]]
static struct yetty_ycore_void_result gemini_start(struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    (void)obj;
    (void)app; /* nothing runs until the first turn spawns */
    /* yai owns the terminal: route the yetty MCP server's figures back
     * through us (parent mode) rather than racing us to /dev/tty. This
     * relies on gemini surfacing the MCP tool result (carrying the
     * YAI:DRAW / figure sentinel) back in its stream-json output; if a
     * gemini build does not, the figure simply won't render (no clash). */
    if (setenv("YETTY_MCP_VIA_PARENT", "1", 1) != 0) {
        return YETTY_ERR(yetty_ycore_void, "gemini start: setenv YETTY_MCP_VIA_PARENT failed");
    }
    printf(YAI_DIM
           "(gemini runs one fresh conversation per turn — resume is not wired up yet)" YAI_RESET
           "\n");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini start: flush");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:send_user_message")]]
static struct yetty_ycore_void_result gemini_send_user_message(struct yetty_yclass_object *obj,
                                                               struct yai_app *app,
                                                               const char *text)
{
    (void)obj;
    if (app->child_open_handles > 0 || app->child_alive) {
        return YETTY_ERR(yetty_ycore_void, "gemini send_user_message: previous turn still open");
    }
    const char *model = app->config.gemini.model;
    /* Non-interactive gemini cannot prompt for tool approval; the
     * default approval mode auto-denies mutating tools. Opt into a
     * looser mode explicitly (--gemini-approval auto_edit | yolo). */
    const char *approval_mode = app->config.gemini.approval_mode;

    const char *args[12];
    int arg_count = 0;
    args[arg_count++] = "gemini";
    args[arg_count++] = "--output-format";
    /* stream-json is JSONL (one event per line) and streams the response
     * live; plain "json" emits a single pretty-printed object. */
    args[arg_count++] = "stream-json";
    if (model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    if (approval_mode[0] && strcmp(approval_mode, "default") != 0) {
        args[arg_count++] = "--approval-mode";
        args[arg_count++] = approval_mode;
    }
    args[arg_count++] = "--prompt";
    args[arg_count++] = text;
    args[arg_count] = NULL;

    struct yetty_ycore_void_result spawn_res = yai_turn_engine_spawn(app, "gemini", args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, spawn_res, "gemini send_user_message: spawn");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:describe_config")]]
static struct yetty_ycore_void_result gemini_describe_config(struct yetty_yclass_object *obj,
                                                             struct yai_app *app, char *out,
                                                             size_t out_size)
{
    (void)obj;
    const char *model = app->config.gemini.model;
    const char *approval_mode = app->config.gemini.approval_mode;
    int written = snprintf(out, out_size,
                           "model: %s  [--model]\n"
                           "approval mode: %s  [--gemini-approval]\n"
                           "resume: none (one fresh conversation per turn)",
                           model[0] ? model : "(CLI default)", approval_mode);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "gemini describe_config: rows truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:config_knob")]]
static struct yetty_ycore_void_result gemini_config_knob(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    (void)obj;
    int written = snprintf(out, out_size,
                           "gemini_approval_mode|approval mode|"
                           "default,auto_edit,yolo,plan|%s",
                           app->config.gemini.approval_mode);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "gemini config_knob: spec truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:handle_event")]]
static struct yetty_ycore_void_result gemini_handle_event(struct yetty_yclass_object *obj,
                                                          struct yai_app *app,
                                                          struct yyjson_val *event)
{
    (void)obj;
    const char *type = yyjson_get_str(yyjson_obj_get(event, "type"));
    if (!type) {
        return YETTY_OK_VOID();
    }

    /* Turn start: reset the streamed-message state so the engine label
     * reprints. (init also carries session_id — future resume hook.) */
    if (strcmp(type, "init") == 0) {
        struct yai_event begin = {.kind = YAI_EVENT_MESSAGE_BEGIN};
        struct yetty_ycore_void_result begin_res = yai_event_dispatch(app, &begin);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "gemini handle_event: message begin");
        return YETTY_OK_VOID();
    }

    /* Assistant text streams as message events; the user-role echo is
     * our own prompt coming back, so it is not rendered. */
    if (strcmp(type, "message") == 0) {
        const char *role = yyjson_get_str(yyjson_obj_get(event, "role"));
        yyjson_val *content = yyjson_obj_get(event, "content");
        if (role && strcmp(role, "assistant") == 0 && yyjson_is_str(content) &&
            yyjson_get_len(content) > 0) {
            struct yai_event delta = {
                .kind = YAI_EVENT_TEXT_DELTA,
                .text = {.text = yyjson_get_str(content), .len = yyjson_get_len(content)}};
            struct yetty_ycore_void_result delta_res = yai_event_dispatch(app, &delta);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "gemini handle_event: text");
        }
        return YETTY_OK_VOID();
    }

    if (strcmp(type, "error") == 0) {
        app->turn_failed = 1;
        const char *message = yyjson_get_str(yyjson_obj_get(event, "message"));
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "gemini handle_event: suspend");
        printf("\n" YAI_RED "✗ %s" YAI_RESET "\n", message ? message : "gemini error");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini handle_event: flush");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "gemini handle_event: resume");
        return YETTY_OK_VOID();
    }

    /* Turn end: flush the streamed line + clear activity, then usage.
     * The actual turn boundary (queue pump / re-prompt) is the child
     * exit, handled by yai:turn_engine — not here. */
    if (strcmp(type, "result") == 0) {
        const char *status = yyjson_get_str(yyjson_obj_get(event, "status"));
        if (status && strcmp(status, "success") != 0) {
            app->turn_failed = 1;
            struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "gemini handle_event: suspend");
            printf("\n" YAI_RED "✗ gemini: %s" YAI_RESET "\n", status);
            struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini handle_event: flush");
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "gemini handle_event: resume");
        }
        struct yai_event turn_end = {.kind = YAI_EVENT_TURN_END};
        struct yetty_ycore_void_result end_res = yai_event_dispatch(app, &turn_end);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "gemini handle_event: turn end");

        yyjson_val *stats = yyjson_obj_get(event, "stats");
        yyjson_val *duration_value = stats ? yyjson_obj_get(stats, "duration_ms") : NULL;
        struct yai_event usage_event = {
            .kind = YAI_EVENT_USAGE,
            .usage = {.input = yai_usage_field(stats, "input_tokens"),
                      .output = yai_usage_field(stats, "output_tokens"),
                      .cache_read = yai_usage_field(stats, "cached"),
                      .seconds = duration_value ? yyjson_get_num(duration_value) / 1000.0 : 0.0}};
        struct yetty_ycore_void_result usage_res = yai_event_dispatch(app, &usage_event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "gemini handle_event: usage");
        return YETTY_OK_VOID();
    }

    /* user-role message echo and any other event types: nothing to draw. */
    return YETTY_OK_VOID();
}

#include "gemini.gen.c"
