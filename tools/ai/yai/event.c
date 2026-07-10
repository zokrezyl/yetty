/*
 * event.c — the consumer side of yai's provider-neutral event model
 * (event.h). yai_event_dispatch turns one normalized event into
 * rendering: the scrollback renderer, the HUD window and the session
 * usage accounting. Engines emit; this is the only place events become
 * UI, so it is also the natural seam for transcript replay and renderer
 * tests later.
 */
#include "app.h"

#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdio.h>

/* Accumulate the turn into the session totals and render the per-turn +
 * session lines — to the HUD window when present, else into scrollback.
 * This was per-engine copy-paste (render_turn_usage); normalizing usage
 * into one event lets a single renderer own the format. */
static struct yetty_ycore_void_result dispatch_usage(struct yai_app *app,
                                                     const struct yai_event_usage *usage)
{
    app->usage.input += usage->input;
    app->usage.output += usage->output;
    app->usage.cache_read += usage->cache_read;
    app->usage.cache_creation += usage->cache_creation;
    app->usage.cost += usage->cost;
    app->usage.turns++;

    /* Capture the token time-series point for the /usage plot. */
    yai_usage_record_sample(app);

    char input_text[16];
    char output_text[16];
    char cached_text[16];
    char session_output_text[16];
    yai_format_tokens(usage->input, input_text, sizeof(input_text));
    yai_format_tokens(usage->output, output_text, sizeof(output_text));
    yai_format_tokens(usage->cache_read, cached_text, sizeof(cached_text));
    yai_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));

    char timing_text[48] = "";
    if (usage->seconds > 0.0) {
        snprintf(timing_text, sizeof(timing_text), " · %.1fs · %.0f tok/s", usage->seconds,
                 (double)usage->output / usage->seconds);
    }
    /* Only providers that report a real cost get the "$…" segment; codex
     * and gemini omit it on both the turn and session lines. */
    char turn_cost_text[32] = "";
    char session_cost_text[32] = "";
    if (usage->has_cost) {
        snprintf(turn_cost_text, sizeof(turn_cost_text), " · $%.4f", usage->cost);
        snprintf(session_cost_text, sizeof(session_cost_text), " · $%.4f", app->usage.cost);
    }
    char turn_line[192];
    snprintf(turn_line, sizeof(turn_line), "↑%s in · %s cached · ↓%s out%s%s", input_text,
             cached_text, output_text, timing_text, turn_cost_text);
    char session_line[128];
    snprintf(session_line, sizeof(session_line), "session: ↓%s out%s · %d turn(s)",
             session_output_text, session_cost_text, app->usage.turns);

    /* Cache the per-turn raw usage + the account quota so the HUD format
     * variables (#{turn*}, #{quota}, the #{turn}/#{session} composites) can
     * be recomputed on any refresh. */
    app->last_turn.input = usage->input;
    app->last_turn.output = usage->output;
    app->last_turn.cache_read = usage->cache_read;
    app->last_turn.cost = usage->cost;
    app->last_turn.seconds = usage->seconds;
    app->last_turn.has_cost = usage->has_cost;
    app->last_turn.valid = 1;
    char quota_summary[96];
    yai_quota_summary(app, quota_summary, sizeof(quota_summary));
    if (quota_summary[0]) {
        snprintf(app->quota_text, sizeof(app->quota_text), "%s", quota_summary);
    }

    if (app->hud || app->renderer.text_hud) {
        /* Turn done → idle; the configured HUD format carries the totals,
         * so a single refresh renders the whole bar. */
        snprintf(app->state_text, sizeof(app->state_text), "%s", "idle");
        struct yetty_ycore_void_result refresh_res = yai_refresh_hud_stats(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, refresh_res, "event usage: hud refresh");
        return YETTY_OK_VOID();
    }
    /* No HUD at all (a pipe): the usage goes into the scrollback. */
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "event usage: suspend");
    printf(YAI_MUTED "  %s" YAI_RESET "\n" YAI_DIM "  %s" YAI_RESET "\n", turn_line, session_line);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "event usage: flush");
    return YETTY_OK_VOID();
}

/* Accumulate the assistant's answer text so Ctrl-G can pre-fill the
 * external editor with it. Best-effort: a growth failure or the size cap
 * just truncates the captured copy; it never fails rendering. */
static void last_response_append(struct yai_app *app, const char *text, size_t len)
{
    if (len == 0) {
        return;
    }
    if (app->last_response_len + len + 1 > app->last_response_cap) {
        size_t new_cap = app->last_response_cap ? app->last_response_cap : 8192;
        while (new_cap < app->last_response_len + len + 1) {
            if (new_cap > 4u * 1024 * 1024) {
                return; /* cap the captured copy */
            }
            new_cap *= 2;
        }
        char *grown = realloc(app->last_response, new_cap);
        if (!grown) {
            return;
        }
        app->last_response = grown;
        app->last_response_cap = new_cap;
    }
    memcpy(app->last_response + app->last_response_len, text, len);
    app->last_response_len += len;
    app->last_response[app->last_response_len] = '\0';
}

struct yetty_ycore_void_result yai_event_dispatch(struct yai_app *app,
                                                  const struct yai_event *event)
{
    if (!app || !event) {
        return YETTY_ERR(yetty_ycore_void, "yai_event_dispatch: NULL app/event");
    }
    switch (event->kind) {
    case YAI_EVENT_MESSAGE_BEGIN: {
        app->last_response_len = 0; /* a new answer supersedes the last */
        if (app->last_response) {
            app->last_response[0] = '\0';
        }
        struct yetty_ycore_void_result begin_res = yai_renderer_begin_message(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "event: begin message");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_TEXT_DELTA: {
        /* A run of assistant text ends any open thinking block first —
         * the providers interleave the two and the renderer needs the
         * boundary to close the dim "thinking" label. */
        struct yetty_ycore_void_result end_res = yai_renderer_end_thinking(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "event: end thinking");
        struct yetty_ycore_void_result delta_res =
            yai_renderer_text_delta(&app->renderer, event->text.text, event->text.len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "event: text delta");
        last_response_append(app, event->text.text, event->text.len);
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_THINKING_DELTA: {
        struct yetty_ycore_void_result delta_res =
            yai_renderer_thinking_delta(&app->renderer, event->text.text, event->text.len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "event: thinking delta");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_THINKING_END: {
        struct yetty_ycore_void_result end_res = yai_renderer_end_thinking(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "event: thinking end");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_TOOL_CALL: {
        struct yetty_ycore_void_result call_res =
            yai_render_tool_call(&app->renderer, event->tool_call.name, event->tool_call.input);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, call_res, "event: tool call");
        char state_line[96];
        snprintf(state_line, sizeof(state_line), "⚙ %s",
                 event->tool_call.name ? event->tool_call.name : "?");
        struct yetty_ycore_void_result activity_res =
            yai_set_activity(app, "orbit-dots", state_line);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, activity_res, "event: tool call activity");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_TOOL_RESULT: {
        struct yetty_ycore_void_result result_res =
            yai_render_tool_result(&app->renderer, event->tool_result.content,
                                   event->tool_result.is_error, event->tool_result.tool_name);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_res, "event: tool result");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_HOOK: {
        struct yetty_ycore_void_result hook_res =
            yai_render_hook(&app->renderer, event->hook.event);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_res, "event: hook");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_TURN_END: {
        struct yetty_ycore_void_result clear_res = yai_renderer_activity_clear(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "event: activity clear");
        struct yetty_ycore_void_result finish_res = yai_renderer_finish_turn(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "event: finish turn");
        return YETTY_OK_VOID();
    }
    case YAI_EVENT_USAGE:
        return dispatch_usage(app, &event->usage);
    }
    return YETTY_ERR(yetty_ycore_void, "yai_event_dispatch: unknown event kind");
}
