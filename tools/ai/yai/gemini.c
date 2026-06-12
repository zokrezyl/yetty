/*
 * gemini.c — yclass class `yai:gemini`: the Google AI (gemini CLI)
 * engine.
 *
 * A fresh `gemini --output-format json --prompt <text>` child per turn
 * (lifecycle inherited from yai:turn_engine). The CLI emits ONE JSON
 * object on stdout at the end of the run:
 *
 *   { "response": "<the answer text>",
 *     "stats": { "models": { "<model>": { "tokens": { "prompt": N,
 *                "candidates": N, "total": N, "cached": N, … } } } },
 *     "error": { "type": …, "message": …, "code": … }   (on failure) }
 *
 * No session resume yet: the gemini CLI has no non-interactive resume
 * token to hand back, so every turn is a fresh conversation. The
 * engine still works for one-shot Q&A and tool runs; wire a resume
 * flag here once the CLI grows one.
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
static struct yetty_ycore_void_result gemini_start(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    (void)ctx;
    (void)obj;
    (void)app; /* nothing runs until the first turn spawns */
    /* Gemini events carry no tool output back to yai, so the yetty MCP
     * server must write its figure envelope to /dev/tty itself. */
    if (unsetenv("YETTY_MCP_VIA_PARENT") != 0) {
        return YETTY_ERR(yetty_ycore_void, "gemini start: unsetenv YETTY_MCP_VIA_PARENT failed");
    }
    printf(YAI_DIM "(gemini runs one fresh conversation per turn — the CLI exposes no "
                   "non-interactive resume token yet)" YAI_RESET "\n");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini start: flush");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:send_user_message")]]
static struct yetty_ycore_void_result gemini_send_user_message(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yai_app *app,
                                                               const char *text)
{
    (void)ctx;
    (void)obj;
    if (app->child_open_handles > 0 || app->child_alive) {
        return YETTY_ERR(yetty_ycore_void, "gemini send_user_message: previous turn still open");
    }
    const char *model = getenv("YAI_MODEL");
    /* Non-interactive gemini cannot prompt for tool approval; the
     * default approval mode auto-denies mutating tools. Opt into a
     * looser mode explicitly (e.g. auto_edit, yolo). */
    const char *approval_mode = getenv("YAI_GEMINI_APPROVAL_MODE");

    const char *args[12];
    int arg_count = 0;
    args[arg_count++] = "gemini";
    args[arg_count++] = "--output-format";
    args[arg_count++] = "json";
    if (model && model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    if (approval_mode && approval_mode[0]) {
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

/* Sum the per-model token counters under stats.models.*.tokens. */
static void gemini_sum_tokens(yyjson_val *stats, uint64_t *prompt_tokens,
                              uint64_t *candidate_tokens, uint64_t *cached_tokens)
{
    *prompt_tokens = 0;
    *candidate_tokens = 0;
    *cached_tokens = 0;
    yyjson_val *models = stats ? yyjson_obj_get(stats, "models") : NULL;
    if (!yyjson_is_obj(models)) {
        return;
    }
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(models, &iter);
    yyjson_val *model_key;
    while ((model_key = yyjson_obj_iter_next(&iter)) != NULL) {
        yyjson_val *model_value = yyjson_obj_iter_get_val(model_key);
        yyjson_val *tokens = yyjson_is_obj(model_value) ? yyjson_obj_get(model_value, "tokens")
                                                        : NULL;
        *prompt_tokens += yai_usage_field(tokens, "prompt");
        *candidate_tokens += yai_usage_field(tokens, "candidates");
        *cached_tokens += yai_usage_field(tokens, "cached");
    }
}

static struct yetty_ycore_void_result gemini_render_usage(struct yai_app *app, yyjson_val *stats)
{
    uint64_t prompt_tokens = 0;
    uint64_t candidate_tokens = 0;
    uint64_t cached_tokens = 0;
    gemini_sum_tokens(stats, &prompt_tokens, &candidate_tokens, &cached_tokens);
    app->usage.input += prompt_tokens;
    app->usage.output += candidate_tokens;
    app->usage.cache_read += cached_tokens;
    app->usage.turns++;

    char input_text[16];
    char output_text[16];
    char cached_text[16];
    char session_output_text[16];
    yai_format_tokens(prompt_tokens, input_text, sizeof(input_text));
    yai_format_tokens(candidate_tokens, output_text, sizeof(output_text));
    yai_format_tokens(cached_tokens, cached_text, sizeof(cached_text));
    yai_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));
    char turn_line[192];
    snprintf(turn_line, sizeof(turn_line), "↑%s in · %s cached · ↓%s out", input_text, cached_text,
             output_text);
    char session_line[128];
    snprintf(session_line, sizeof(session_line), "session: ↓%s out · %d turn(s)",
             session_output_text, app->usage.turns);
    if (app->hud) {
        struct yetty_ycore_void_result hud_res = yai_hud_set_turn(app->hud, turn_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "gemini_render_usage: hud turn line");
        hud_res = yai_hud_set_session(app->hud, session_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "gemini_render_usage: hud session line");
        hud_res = yai_hud_set_state(app->hud, "idle");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "gemini_render_usage: hud state");
        hud_res = yai_hud_flush(app->hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "gemini_render_usage: hud flush");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "gemini_render_usage: suspend");
    printf(YAI_MUTED "  %s" YAI_RESET "\n" YAI_DIM "  %s" YAI_RESET "\n", turn_line, session_line);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini_render_usage: flush");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:describe_config")]]
static struct yetty_ycore_void_result gemini_describe_config(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yai_app *app, char *out,
                                                             size_t out_size)
{
    (void)ctx;
    (void)obj;
    (void)app;
    const char *model = getenv("YAI_MODEL");
    const char *approval_mode = getenv("YAI_GEMINI_APPROVAL_MODE");
    int written = snprintf(out, out_size,
                           "model: %s  [YAI_MODEL]\n"
                           "approval mode: %s  [YAI_GEMINI_APPROVAL_MODE]\n"
                           "resume: none (one fresh conversation per turn)",
                           (model && model[0]) ? model : "(CLI default)",
                           (approval_mode && approval_mode[0]) ? approval_mode
                                                               : "(CLI default — auto-denies)");
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "gemini describe_config: rows truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:config_knob")]]
static struct yetty_ycore_void_result gemini_config_knob(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    (void)ctx;
    (void)obj;
    (void)app;
    const char *approval_mode = getenv("YAI_GEMINI_APPROVAL_MODE");
    if (!approval_mode || !approval_mode[0]) {
        approval_mode = "default";
    }
    int written = snprintf(out, out_size,
                           "YAI_GEMINI_APPROVAL_MODE|approval mode|"
                           "default,auto_edit,yolo|%s",
                           approval_mode);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "gemini config_knob: spec truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:gemini:handle_event")]]
static struct yetty_ycore_void_result gemini_handle_event(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yai_app *app,
                                                          struct yyjson_val *event)
{
    (void)ctx;
    (void)obj;
    /* One result object per turn: response text, optional error, stats. */
    yyjson_val *error = yyjson_obj_get(event, "error");
    if (yyjson_is_obj(error)) {
        app->turn_failed = 1;
        const char *message = yyjson_get_str(yyjson_obj_get(error, "message"));
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "gemini handle_event: suspend");
        printf("\n" YAI_RED "✗ %s" YAI_RESET "\n", message ? message : "gemini error");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini handle_event: flush");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "gemini handle_event: resume");
    }
    yyjson_val *response = yyjson_obj_get(event, "response");
    if (yyjson_is_str(response) && yyjson_get_len(response) > 0) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "gemini handle_event: suspend");
        printf("\n" YAI_MINT YAI_BOLD "gemini" YAI_RESET " %s\n", yyjson_get_str(response));
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "gemini handle_event: flush");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "gemini handle_event: resume");
    }
    yyjson_val *stats = yyjson_obj_get(event, "stats");
    if (yyjson_is_obj(stats)) {
        struct yetty_ycore_void_result usage_res = gemini_render_usage(app, stats);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "gemini handle_event: usage");
    }
    return YETTY_OK_VOID();
}

#include "gemini.gen.c"
