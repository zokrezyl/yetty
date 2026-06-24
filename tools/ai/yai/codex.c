/*
 * codex.c — yclass class `yai:codex`: the OpenAI Codex engine.
 *
 * A fresh `codex exec --json [resume <id>]` child per turn (the
 * per-turn lifecycle — exit/EOF/interrupt — is inherited from
 * yai:turn_engine). session_id carries the conversation: codex mints
 * a thread id server-side on the first turn.
 */
#include "app.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@yai:codex")]] [[clang::annotate("parent@yai:turn_engine")]]
yetty_yai_codex {
    /* The class@ annotation needs a struct to sit on; codex keeps no
     * engine-private state (the thread id lives in app->session_id,
     * the per-turn lifecycle state in the app's turn fields). */
    char unused;
};

YETTY_YRESULT_DECLARE(yetty_yai_codex_ptr, struct yetty_yai_codex *);

[[clang::annotate("override@yai:codex:start")]]
static struct yetty_ycore_void_result codex_start(struct yetty_yclass_object *obj,
                                                  struct yai_app *app)
{
    (void)obj;
    /* yai owns the terminal: route the yetty MCP server's figures back
     * through us (parent mode) instead of letting it race us to /dev/tty.
     * The server returns a YAI:DRAW / figure sentinel in the tool result,
     * which rides codex's mcp_tool_call item output into yai_render_tool_result.
     * (Requires --codex-sandbox danger-full-access for MCP tools to run.) */
    if (setenv("YETTY_MCP_VIA_PARENT", "1", 1) != 0) {
        return YETTY_ERR(yetty_ycore_void, "codex start: setenv YETTY_MCP_VIA_PARENT failed");
    }
    if (strcmp(app->config.codex.sandbox, "workspace-write") == 0) {
        /* Constrained default (see send_user_message) — say so once,
         * with the explicit opt-in for MCP tools. */
        printf(YAI_DIM "(codex sandbox: workspace-write — MCP tool calls may be auto-cancelled; "
                       "use --codex-sandbox danger-full-access to allow them)" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex start: flush");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:codex:send_user_message")]]
static struct yetty_ycore_void_result codex_send_user_message(struct yetty_yclass_object *obj,
                                                              struct yai_app *app, const char *text)
{
    (void)obj;
    if (app->child_open_handles > 0 || app->child_alive) {
        return YETTY_ERR(yetty_ycore_void, "codex send_user_message: previous turn still open");
    }
    const char *model = app->config.codex.model;
    char model_override[256] = "";
    if (model[0]) {
        int written = snprintf(model_override, sizeof(model_override), "model=%s", model);
        if (written < 0 || (size_t)written >= sizeof(model_override)) {
            return YETTY_ERR(yetty_ycore_void, "codex send_user_message: model too long");
        }
    }

    /* Constrained sandbox by default. MCP tool calls are auto-cancelled
     * by codex exec unless the sandbox allows them ("user cancelled MCP
     * tool call"); empirically only danger-full-access lets them run
     * non-interactively — but full filesystem/process access must be an
     * explicit opt-in (--codex-sandbox danger-full-access), never a silent
     * default. codex_start prints the trade-off once. */
    const char *sandbox_mode = app->config.codex.sandbox;

    /* Approval policy — codex's `approval_policy` config key, a SEPARATE axis
     * from the sandbox. `codex exec` is non-interactive (stdin is UV_IGNORE,
     * the prompt rides argv), so there is nowhere to answer an approval
     * request; the only policy yai can actually honor is `never`, which is
     * therefore the default. A power user externally sandboxing the whole
     * process can opt into another policy via --codex-approval, accepting
     * that anything codex would prompt for will instead stall/deny. Note:
     * `codex exec` has no `--ask-for-approval` flag — it takes the policy via
     * `-c approval_policy=…` only. */
    char approval_override[64];
    int approval_written = snprintf(approval_override, sizeof(approval_override),
                                    "approval_policy=%s", app->config.codex.approval);
    if (approval_written < 0 || (size_t)approval_written >= sizeof(approval_override)) {
        return YETTY_ERR(yetty_ycore_void, "codex send_user_message: approval too long");
    }

    /* Reasoning effort for the OpenAI model — codex's model_reasoning_effort
     * config key (minimal / low / medium / high). Off by default (the
     * model's own default applies); opt in via --codex-effort. */
    const char *effort = app->config.codex.effort;
    char effort_override[64] = "";
    if (effort[0] && strcmp(effort, "default") != 0) {
        int written =
            snprintf(effort_override, sizeof(effort_override), "model_reasoning_effort=%s", effort);
        if (written < 0 || (size_t)written >= sizeof(effort_override)) {
            return YETTY_ERR(yetty_ycore_void, "codex send_user_message: effort too long");
        }
    }

    const char *args[20];
    int arg_count = 0;
    args[arg_count++] = "codex";
    args[arg_count++] = "exec";
    args[arg_count++] = "--json";
    args[arg_count++] = "--skip-git-repo-check";
    args[arg_count++] = "--sandbox";
    args[arg_count++] = sandbox_mode;
    args[arg_count++] = "-c";
    args[arg_count++] = approval_override;
    if (model_override[0]) {
        args[arg_count++] = "-c";
        args[arg_count++] = model_override;
    }
    if (effort_override[0]) {
        args[arg_count++] = "-c";
        args[arg_count++] = effort_override;
    }
    if (app->session_id[0]) {
        args[arg_count++] = "resume";
        args[arg_count++] = app->session_id;
    }
    args[arg_count++] = text;
    args[arg_count] = NULL;

    struct yetty_ycore_void_result spawn_res = yai_turn_engine_spawn(app, "codex", args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, spawn_res, "codex send_user_message: spawn");
    return YETTY_OK_VOID();
}

/* Render one completed thread item. Items arrive whole (exec mode has
 * no deltas), so each maps to one scrollback block. */
static struct yetty_ycore_void_result codex_render_item(struct yai_app *app, yyjson_val *item)
{
    const char *item_type = yyjson_get_str(yyjson_obj_get(item, "type"));
    if (!item_type) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "codex_render_item: suspend");
    if (strcmp(item_type, "agent_message") == 0) {
        const char *text = yyjson_get_str(yyjson_obj_get(item, "text"));
        if (text && text[0]) {
            printf("\n" YAI_MINT YAI_BOLD "codex" YAI_RESET " %s\n", text);
            struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex_render_item: flush");
        }
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "codex_render_item: resume");
        return YETTY_OK_VOID();
    }
    if (strcmp(item_type, "reasoning") == 0) {
        if (app->renderer.show_thinking) {
            const char *text = yyjson_get_str(yyjson_obj_get(item, "text"));
            if (text && text[0]) {
                printf("\n" YAI_MUTED "thinking " YAI_RESET YAI_DIM "%s" YAI_RESET "\n", text);
                struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
                YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex_render_item: flush");
            }
        }
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "codex_render_item: resume");
        return YETTY_OK_VOID();
    }
    if (strcmp(item_type, "command_execution") == 0) {
        const char *command = yyjson_get_str(yyjson_obj_get(item, "command"));
        yyjson_val *exit_code_value = yyjson_obj_get(item, "exit_code");
        int is_error = exit_code_value && yyjson_get_int(exit_code_value) != 0;
        printf("\n" YAI_MUTED "⚙ " YAI_BOLD "shell" YAI_RESET " " YAI_DIM "%s" YAI_RESET "\n",
               command ? command : "?");
        struct yetty_ycore_void_result result_res = yai_render_tool_result(
            &app->renderer, yyjson_obj_get(item, "aggregated_output"), is_error, "shell");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "codex_render_item: command result");
        return YETTY_OK_VOID();
    }
    if (strcmp(item_type, "mcp_tool_call") == 0 || strcmp(item_type, "file_change") == 0 ||
        strcmp(item_type, "todo_list") == 0 || strcmp(item_type, "web_search") == 0) {
        char summary[100];
        yai_summarize_tool_input(item, summary, sizeof(summary));
        printf("\n" YAI_MUTED "⚙ " YAI_BOLD "%s" YAI_RESET " " YAI_DIM "%s" YAI_RESET "\n",
               item_type, summary);
        /* Figure envelopes from the yetty MCP server ride tool output;
         * the generic result renderer decodes the sentinel. */
        struct yetty_ycore_void_result result_res =
            yai_render_tool_result(&app->renderer, item, 0, item_type);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "codex_render_item: tool result");
        return YETTY_OK_VOID();
    }
    if (strcmp(item_type, "error") == 0) {
        const char *message = yyjson_get_str(yyjson_obj_get(item, "message"));
        printf("\n" YAI_RED "✗ %s" YAI_RESET "\n", message ? message : "error");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex_render_item: flush");
        app->turn_failed = 1;
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "codex_render_item: resume");
        return YETTY_OK_VOID();
    }
    char summary[100];
    yai_summarize_tool_input(item, summary, sizeof(summary));
    printf(YAI_DIM "  (%s %s)" YAI_RESET "\n", item_type, summary);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex_render_item: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "codex_render_item: resume");
    return YETTY_OK_VOID();
}

/* Normalize codex's turn.completed usage into a USAGE event. Codex
 * reports no cost or duration (has_cost stays 0, seconds 0), so the
 * shared renderer omits the "$…" and timing segments. */
static struct yetty_ycore_void_result codex_render_usage(struct yai_app *app, yyjson_val *usage)
{
    struct yai_event usage_event = {
        .kind = YAI_EVENT_USAGE,
        .usage = {.input = yai_usage_field(usage, "input_tokens"),
                  .output = yai_usage_field(usage, "output_tokens"),
                  .cache_read = yai_usage_field(usage, "cached_input_tokens")}};
    return yai_event_dispatch(app, &usage_event);
}

[[clang::annotate("override@yai:codex:describe_config")]]
static struct yetty_ycore_void_result codex_describe_config(struct yetty_yclass_object *obj,
                                                            struct yai_app *app, char *out,
                                                            size_t out_size)
{
    (void)obj;
    const char *model = app->config.codex.model;
    int written = snprintf(out, out_size,
                           "model: %s  [--model]\n"
                           "sandbox: %s  [--codex-sandbox; danger-full-access enables MCP tools]\n"
                           "approval: %s  [--codex-approval; exec is non-interactive, so only "
                           "'never' is serviceable]\n"
                           "resume: thread %s (one child per turn)",
                           model[0] ? model : "(CLI default)", app->config.codex.sandbox,
                           app->config.codex.approval,
                           app->session_id[0] ? app->session_id : "(minted on the first turn)");
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "codex describe_config: rows truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:codex:config_knob")]]
static struct yetty_ycore_void_result codex_config_knob(struct yetty_yclass_object *obj,
                                                        struct yai_app *app, char *out,
                                                        size_t out_size)
{
    (void)obj;
    /* One knob spec per line: ENGINE_FIELD|label|options|current. Sandbox and
     * approval are independent axes (the earlier design conflated them).
     * Reasoning effort is a model parameter — it lives on the model tab
     * (main.c build_effort_knob), not here. */
    int written = snprintf(out, out_size,
                           "codex-sandbox|sandbox|"
                           "read-only,workspace-write,danger-full-access|%s\n"
                           "codex-approval|approval|never,on-request,untrusted|%s",
                           app->config.codex.sandbox, app->config.codex.approval);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "codex config_knob: spec truncated");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yai:codex:handle_event")]]
static struct yetty_ycore_void_result codex_handle_event(struct yetty_yclass_object *obj,
                                                         struct yai_app *app,
                                                         struct yyjson_val *event)
{
    (void)obj;
    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));
    if (!kind) {
        return YETTY_OK_VOID();
    }
    if (strcmp(kind, "thread.started") == 0) {
        const char *thread_id = yyjson_get_str(yyjson_obj_get(event, "thread_id"));
        if (thread_id && strlen(thread_id) >= sizeof(app->session_id)) {
            /* A truncated thread id would silently break resume — fail
             * loudly rather than store a corrupted token. */
            return YETTY_ERR(yetty_ycore_void, "codex handle_event: thread id too long");
        }
        if (thread_id && strcmp(thread_id, app->session_id) != 0) {
            snprintf(app->session_id, sizeof(app->session_id), "%s", thread_id);
            struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "codex handle_event: suspend");
            printf(YAI_DIM "(codex thread %s)" YAI_RESET "\n", app->session_id);
            struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex handle_event: flush");
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "codex handle_event: resume");
        }
        return YETTY_OK_VOID();
    }
    if (strcmp(kind, "item.completed") == 0) {
        struct yetty_ycore_void_result item_res =
            codex_render_item(app, yyjson_obj_get(event, "item"));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, item_res, "codex handle_event: item");
        return YETTY_OK_VOID();
    }
    if (strcmp(kind, "turn.completed") == 0) {
        struct yetty_ycore_void_result usage_res =
            codex_render_usage(app, yyjson_obj_get(event, "usage"));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "codex handle_event: usage");
        return YETTY_OK_VOID();
    }
    if (strcmp(kind, "turn.failed") == 0 || strcmp(kind, "error") == 0) {
        app->turn_failed = 1;
        char summary[100];
        yai_summarize_tool_input(event, summary, sizeof(summary));
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "codex handle_event: suspend");
        printf("\n" YAI_RED "✗ %s %s" YAI_RESET "\n", kind, summary);
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "codex handle_event: flush");
        return YETTY_OK_VOID();
    }
    /* turn.started, item.started, item.updated, …: nothing to draw. */
    return YETTY_OK_VOID();
}

#include "codex.gen.c"
