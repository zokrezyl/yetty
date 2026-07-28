/*
 * yai — a minimal, hackable AI-CLI loop rendering into yetty.
 *
 * Drives a headless AI CLI over a JSONL pipe, owns its own scrollback,
 * streams responses live, and keeps the non-scrolling status (state,
 * tokens, cost) in a floating ygui window (hud.c) while the
 * conversation scrolls like any CLI.
 *
 * The engine is a yclass object — base class `yai:engine` (engine.c),
 * one subclass per CLI: yai:claude (Claude Code, persistent
 * bidirectional stream-json child), yai:codex and yai:gemini (one
 * child per turn, shared lifecycle in yai:turn_engine). Dispatch goes
 * through the generated yetty_yai_* stubs; main.c never branches on
 * the engine kind.
 *
 * Input model — ALL focus is client-side:
 *
 *   yai is the pane's single PTY client. stdin is switched to raw input
 *   (output stays cooked) and every byte runs through a yetty_yface
 *   demux: plain bytes are keystrokes, OSC envelopes are pane-wide
 *   mouse / resize events (yai subscribes via CLIENT_INPUT_SUB +
 *   DECSET ?1500/?1501). Mouse events are hit-tested by yai itself: a
 *   press inside the HUD window moves the focus to the GUI (titlebar
 *   drag, yai-side corner resize, future widgets); a press outside
 *   moves it back to the terminal, where keystrokes feed yai's own
 *   line editor at the prompt. The host arbitrates nothing.
 *
 * Rendering paths into yetty:
 *   1. Agent-drawn figures — the sub-CLI gets the `yetty` MCP server;
 *      under YETTY_MCP_VIA_PARENT its tools hand the OSC envelope back
 *      through a sentinel in the tool result and yai (the single PTY
 *      writer) emits it itself.
 *   2. The ygui HUD window — a compositor figure that does NOT scroll.
 *
 * Scrollback: every JSONL event is mirrored to
 * <state_dir>/yai/transcripts/transcript-<session>.jsonl
 * (state_dir = $XDG_STATE_HOME/yetty, i.e. ~/.local/state/yetty on Linux).
 *
 * Usage (settings come from the config file + CLI flags; see --help):
 *     ./yai                          # fresh session (claude engine)
 *     ./yai --engine codex           # drive `codex exec --json` instead
 *     ./yai --engine gemini          # drive `gemini -o json` per turn
 *     ./yai --model <id>             # model override
 *     ./yai --show-thinking          # show dim thinking text
 *     ./yai --fold-lines 20          # tool-output preview cap (default 8)
 *     ./yai --no-hud                 # stats as plain lines, no ygui window
 *     ./yai --resume <id>            # resume (claude session / codex thread)
 *     ./yai --help                   # full CLI reference
 *
 * Type a message at the prompt. /help lists keys and commands. Ctrl-D or
 * /quit to exit; Ctrl-C interrupts the turn in flight. "!" hands the
 * keyboard to a persistent interop $SHELL (zsh/bash) on its own PTY until
 * the invoked command finishes (Ctrl-] returns early — see shell.c);
 * "/shell" drops to a separate interactive shell until it exits.
 *
 * The prompt is a pinned bottom row and stays available while a turn is
 * in flight (typed messages queue; local commands run immediately). An
 * animated shader glyph on the prompt row shows agent activity, and the
 * in-progress streamed line rides a ticker row above it; both are
 * erased before any history write (render.h: pinned zone).
 *
 * Error discipline: everything returns Results with chained causes;
 * yai_report_error is the single end consumer, called only from the
 * places with nowhere left to propagate (libuv / yface callbacks,
 * main itself, and UI flows that recover and continue).
 */

#include "app.h"
#include "editor-ops.h"
#include "fzy/match.h"

#include "yetty/gen/impl/yai/claude.h"
#include "yetty/gen/impl/yai/codex.h"
#include "yetty/gen/impl/yai/editor-emacs.h"
#include "yetty/gen/impl/yai/editor-vi.h"
#include "yetty/gen/impl/yai/editor.h"
#include "yetty/gen/impl/yai/engine.h"
#include "yetty/gen/impl/yai/gemini.h"

#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <uv.h>
#include <yaml.h>
#include <yyjson.h>

#define YAI_DEFAULT_FOLD_LINES 8
#define YAI_KILL_TIMEOUT_MS 5000
/* Upper bound on a single child-stdout line (figures can be ~100 KB;
 * 256 MB is far past any legitimate line and guards the buffer math). */
#define YAI_CHILD_OUT_MAX (256u * 1024u * 1024u)

#define YAI_PROMPT "\n" YAI_MINT "you ▸ " YAI_RESET

static struct yetty_ycore_void_result handle_input_line(struct yai_app *app, const char *line,
                                                        size_t len);
static struct yetty_ycore_void_result yai_set_edit_mode(struct yai_app *app, const char *mode);
static struct yetty_ycore_void_result yai_release_dock_reservation(struct yai_app *app);
static struct yetty_ycore_void_result apply_config_knob(struct yai_app *app, int knob_index);
static void yai_hud_collect_values(struct yai_app *app, struct yai_hud_var_values *out);
static struct yetty_ycore_void_result yai_text_hud_render(struct yai_app *app,
                                                          const struct yai_hud_var_values *values);

/*---------------------------------------------------------------------------
 * Error surfacing — yai_report_error is THE end consumer: print into
 * the scrollback (our UI), then destroy the chain. Its own rendering
 * problems have nowhere to go and are deliberately swallowed (an error
 * reporter that errors recursively helps nobody).
 *---------------------------------------------------------------------------*/

void yai_report_error(struct yai_app *app, const char *context,
                      struct yetty_ycore_void_result result)
{
    if (!YETTY_IS_ERR(result)) {
        return;
    }
    char message[512];
    yetty_ycore_error_snprint(message, sizeof(message), result.error);
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    if (YETTY_IS_ERR(suspend_res)) {
        yetty_ycore_error_destroy(suspend_res.error);
    }
    printf("\n" YAI_RED "✗ %s: %s" YAI_RESET "\n", context, message);
    fflush(stdout);
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    if (YETTY_IS_ERR(resume_res)) {
        yetty_ycore_error_destroy(resume_res.error);
    }
    yetty_ycore_error_destroy(result.error);
}

/* Update the activity surfaces together: the animated shader glyph on
 * the pinned prompt row and the HUD activity state (#{state}). */
struct yetty_ycore_void_result yai_set_activity(struct yai_app *app, const char *glyph_name,
                                                const char *state_text)
{
    struct yetty_ycore_void_result glyph_res =
        yai_renderer_activity_set(&app->renderer, glyph_name);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, glyph_res, "yai_set_activity: glyph");
    return yai_set_state(app, state_text);
}

/* Record the live activity state and re-render the HUD (whichever backend
 * is active). The state is just one of the HUD format variables, so a
 * change re-expands the whole format — cheap, the widget tree is stable. */
struct yetty_ycore_void_result yai_set_state(struct yai_app *app, const char *state_text)
{
    snprintf(app->state_text, sizeof(app->state_text), "%s", state_text ? state_text : "");
    return yai_refresh_hud_stats(app);
}

void yai_drop_pending_permission(struct yai_app *app)
{
    if (app->pending_permission.input_doc) {
        yyjson_mut_doc_free(app->pending_permission.input_doc);
    }
    memset(&app->pending_permission, 0, sizeof(app->pending_permission));
}

int yai_tool_always_allowed(const struct yai_app *app, const char *tool_name)
{
    if (!tool_name || !tool_name[0]) {
        return 0;
    }
    for (int index = 0; index < app->always_allow_count; index++) {
        if (strcmp(app->always_allow_tools[index], tool_name) == 0) {
            return 1;
        }
    }
    return 0;
}

void yai_tool_remember_allow(struct yai_app *app, const char *tool_name)
{
    if (!tool_name || !tool_name[0] || yai_tool_always_allowed(app, tool_name) ||
        app->always_allow_count >= YAI_ALWAYS_ALLOW_MAX) {
        return;
    }
    snprintf(app->always_allow_tools[app->always_allow_count], sizeof(app->always_allow_tools[0]),
             "%s", tool_name);
    app->always_allow_count++;
}

/*---------------------------------------------------------------------------
 * Small helpers
 *---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 * Settings — yai's own configuration. There are NO environment variables:
 * every setting lives in `app->config` (plus engine_name / editor_mode_name
 * and the renderer's fold_lines / show_thinking). It is seeded with defaults,
 * overlaid by the YAML config file at the standard yetty path, then overridden
 * by command-line flags. The file is rewritten on every config change and on
 * exit. Parsed and emitted with libyaml. The YAML keys are exactly the keys
 * yai_config_set accepts, so the file, the CLI, and the /config dialog all
 * route through one setter.
 *---------------------------------------------------------------------------*/

/* The config section for the active engine (app->engine_name). */
struct yai_engine_config *yai_active_engine_config(struct yai_app *app)
{
    if (strcmp(app->engine_name, "codex") == 0) {
        return &app->config.codex;
    }
    if (strcmp(app->engine_name, "gemini") == 0) {
        return &app->config.gemini;
    }
    return &app->config.claude;
}

/* Parse a boolean-ish scalar ("1"/"true"/"yes" → 1). */
static int yai_config_truthy(const char *value)
{
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0;
}

/* The `defaults:` keys a backend may override (every global key except the
 * `engine` selector itself, which chooses the backend). */
static int yai_is_default_key(const char *key)
{
    return strcmp(key, "edit-mode") == 0 || strcmp(key, "submit-key") == 0 ||
           strcmp(key, "fold-lines") == 0 || strcmp(key, "show-thinking") == 0 ||
           strcmp(key, "hud-on") == 0 || strcmp(key, "hud-float") == 0 ||
           strcmp(key, "hud-mode") == 0 || strcmp(key, "markdown-mode") == 0 ||
           strcmp(key, "hud-format") == 0;
}

/* The active backend's verbatim override of `key`, or NULL when it inherits
 * the global default. */
static const char *yai_engine_override_get(const struct yai_engine_config *cfg, const char *key)
{
    for (int index = 0; index < cfg->override_count; index++) {
        if (strcmp(cfg->overrides[index].key, key) == 0) {
            return cfg->overrides[index].value;
        }
    }
    return NULL;
}

/* Record (or replace) a backend's override of a `defaults:` key. Silently
 * drops the override once the small fixed table is full. */
static void yai_engine_override_set(struct yai_engine_config *cfg, const char *key,
                                    const char *value)
{
    for (int index = 0; index < cfg->override_count; index++) {
        if (strcmp(cfg->overrides[index].key, key) == 0) {
            snprintf(cfg->overrides[index].value, sizeof(cfg->overrides[index].value), "%s", value);
            return;
        }
    }
    if (cfg->override_count >= YAI_BACKEND_OVERRIDE_MAX) {
        return;
    }
    struct yai_setting *slot = &cfg->overrides[cfg->override_count++];
    snprintf(slot->key, sizeof(slot->key), "%s", key);
    snprintf(slot->value, sizeof(slot->value), "%s", value);
}

/* The HUD format in effect for the active engine: its per-backend override if
 * set, else the global template. */
static const char *yai_effective_hud_format(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "hud-format");
    return (override && override[0]) ? override : app->config.hud_format;
}

/* Whether the ygui HUD window is enabled for the active engine. */
static int yai_effective_hud_on(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "hud-on");
    return override ? yai_config_truthy(override) : app->config.hud_on;
}

/* Whether the HUD floats (vs docks) for the active engine. */
static int yai_effective_hud_float(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "hud-float");
    return override ? yai_config_truthy(override) : app->config.hud_float;
}

/* HUD rendering backend for the active engine: "yetty" (ygui window) or "text"
 * (plain status bar). The text bar also serves as the fallback on non-yetty
 * hosts where the ygui HUD has no window to render into. */
static const char *yai_effective_hud_mode(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "hud-mode");
    const char *value = (override && override[0]) ? override : app->config.hud_mode;
    return strcmp(value, "text") == 0 ? "text" : "yetty";
}

/* Markdown rendering backend for the active engine: "yetty" (render answers as
 * SDF/MSDF markdown figures) or "text" (plain styled text). The yetty mode only
 * displays on the yetty host; the renderer gates it on that and falls back to
 * text elsewhere (see the render_markdown wiring in main()). */
static const char *yai_effective_markdown_mode(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "markdown-mode");
    const char *value = (override && override[0]) ? override : app->config.markdown_mode;
    return strcmp(value, "text") == 0 ? "text" : "yetty";
}

/* Ctrl-R message-search UI backend for the active engine: "yetty" (ygui
 * window over the pane) or "text" (menu rows + top-of-screen overlay).
 * The yetty mode needs the ygui HUD framework; the search code falls
 * back to text when there is none (non-yetty host, HUD disabled). */
static const char *yai_effective_message_search_mode(struct yai_app *app)
{
    const char *override = yai_engine_override_get(yai_active_engine_config(app), "message-search");
    const char *value = (override && override[0]) ? override : app->config.message_search_mode;
    return strcmp(value, "text") == 0 ? "text" : "yetty";
}

/* Store the HUD template as `count` verbatim pieces and recompute the
 * concatenated hud_format the renderer parses (pieces joined directly, no
 * separator). The pieces are kept so the YAML list round-trips on save. */
static void yai_config_set_hud_parts(struct yai_config *config, const char *const *parts, int count)
{
    if (count < 0) {
        count = 0;
    }
    if (count > YAI_HUD_FORMAT_PARTS_MAX) {
        count = YAI_HUD_FORMAT_PARTS_MAX;
    }
    config->hud_format_part_count = count;
    config->hud_format[0] = '\0';
    size_t offset = 0;
    for (int index = 0; index < count; index++) {
        snprintf(config->hud_format_parts[index], YAI_HUD_FORMAT_PART_MAX, "%s", parts[index]);
        size_t room = sizeof(config->hud_format) - offset;
        int written = snprintf(config->hud_format + offset, room, "%s", parts[index]);
        if (written > 0) {
            offset += ((size_t)written < room) ? (size_t)written : room - 1;
        }
    }
}

/* Canonicalize an engine name to one of the three string literals, defaulting
 * to "claude" for anything unrecognized. The returned pointer has static
 * lifetime, so it is safe to store in app->engine_name. */
static const char *engine_canonical(const char *value)
{
    if (strcmp(value, "codex") == 0) {
        return "codex";
    }
    if (strcmp(value, "gemini") == 0) {
        return "gemini";
    }
    return "claude";
}

/* Seed every setting with its default. Call once, before the file load. */
static void yai_config_defaults(struct yai_app *app)
{
    struct yai_config *config = &app->config;
    app->engine_name = "claude";
    snprintf(config->engine, sizeof(config->engine), "claude");
    app->editor_mode_name = "emacs";
    app->enter_submits = 1;
    app->edit_vmotion_cursor = (size_t)-1; /* no up/down run in progress yet */
    /* defaults */
    snprintf(config->edit_mode, sizeof(config->edit_mode), "emacs");
    snprintf(config->submit_key, sizeof(config->submit_key), "enter");
    config->fold_lines = YAI_DEFAULT_FOLD_LINES;
    config->show_thinking = 0;
    config->hud_on = 1;
    config->hud_float = 0;
    snprintf(config->hud_mode, sizeof(config->hud_mode), "yetty");
    snprintf(config->markdown_mode, sizeof(config->markdown_mode), "yetty");
    snprintf(config->message_search_mode, sizeof(config->message_search_mode), "yetty");
    const char *const default_hud_parts[] = {YAI_DEFAULT_HUD_PART_0, YAI_DEFAULT_HUD_PART_1,
                                             YAI_DEFAULT_HUD_PART_2, YAI_DEFAULT_HUD_PART_3,
                                             YAI_DEFAULT_HUD_PART_4};
    yai_config_set_hud_parts(config, default_hud_parts,
                             (int)(sizeof(default_hud_parts) / sizeof(default_hud_parts[0])));
    app->renderer.fold_lines = YAI_DEFAULT_FOLD_LINES;
    app->renderer.show_thinking = 0;
    /* claude */
    config->claude.model[0] = '\0';
    snprintf(config->claude.effort, sizeof(config->claude.effort), "auto");
    snprintf(config->claude.permission_mode, sizeof(config->claude.permission_mode), "manual");
    snprintf(config->claude.allowed_preset, sizeof(config->claude.allowed_preset), "curated");
    config->claude.allowed_tools[0] = '\0';
    config->claude.override_count = 0;
    /* codex */
    config->codex.model[0] = '\0';
    snprintf(config->codex.effort, sizeof(config->codex.effort), "default");
    snprintf(config->codex.sandbox, sizeof(config->codex.sandbox), "workspace-write");
    snprintf(config->codex.approval, sizeof(config->codex.approval), "never");
    config->codex.override_count = 0;
    /* gemini */
    config->gemini.model[0] = '\0';
    snprintf(config->gemini.approval_mode, sizeof(config->gemini.approval_mode), "default");
    config->gemini.override_count = 0;
}

/* Apply a GLOBAL (`defaults:`) setting by key. `engine` selects the active
 * backend; the rest seed the global default fields. The resolved live values
 * (editor mode, renderer fold_lines / show_thinking) are produced separately
 * by yai_config_resolve. Returns 1 if the key was a known global. */
static int yai_config_set_global(struct yai_app *app, const char *key, const char *value)
{
    struct yai_config *config = &app->config;
    if (strcmp(key, "engine") == 0) {
        /* This path is the config file + /config dialog: the choice is a
         * persisted default, so update both the active engine and the saved
         * field. The CLI --engine override sets app->engine_name directly and
         * deliberately does NOT come through here. */
        const char *engine_name = engine_canonical(value);
        if (strcmp(engine_name, app->engine_name) != 0) {
            /* The live quota was fed by the previous engine's protocol —
             * it must not leak into the new engine's HUD. */
            memset(&app->engine_quota, 0, sizeof(app->engine_quota));
        }
        app->engine_name = engine_name;
        snprintf(config->engine, sizeof(config->engine), "%s", app->engine_name);
    } else if (strcmp(key, "edit-mode") == 0) {
        snprintf(config->edit_mode, sizeof(config->edit_mode), "%s",
                 strcmp(value, "vi") == 0 ? "vi" : "emacs");
    } else if (strcmp(key, "submit-key") == 0) {
        snprintf(config->submit_key, sizeof(config->submit_key), "%s",
                 strcmp(value, "alt-enter") == 0 ? "alt-enter" : "enter");
    } else if (strcmp(key, "fold-lines") == 0) {
        int folded = atoi(value);
        config->fold_lines = folded > 0 ? folded : YAI_DEFAULT_FOLD_LINES;
    } else if (strcmp(key, "show-thinking") == 0) {
        config->show_thinking = yai_config_truthy(value);
    } else if (strcmp(key, "hud-on") == 0) {
        config->hud_on = yai_config_truthy(value);
    } else if (strcmp(key, "hud-float") == 0) {
        config->hud_float = yai_config_truthy(value);
    } else if (strcmp(key, "hud-mode") == 0) {
        /* Legacy defaults: key; the saved shape is visuals: hud. */
        snprintf(config->hud_mode, sizeof(config->hud_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else if (strcmp(key, "markdown-mode") == 0) {
        /* Legacy defaults: key; the saved shape is visuals: markdown. */
        snprintf(config->markdown_mode, sizeof(config->markdown_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else if (strcmp(key, "message-search") == 0) {
        snprintf(config->message_search_mode, sizeof(config->message_search_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else if (strcmp(key, "hud-format") == 0) {
        /* A scalar hud_format is a one-piece list. */
        const char *single[] = {value};
        yai_config_set_hud_parts(config, single, 1);
    } else {
        return 0;
    }
    return 1;
}

/* Apply one key of the top-level `visuals:` section — the yetty/text
 * rendering switches. Each maps onto the same config field its legacy
 * `defaults:` spelling wrote (hud-mode / markdown-mode), so old files
 * and new files land in one place. Returns 1 if the key was known. */
static int yai_config_set_visual(struct yai_app *app, const char *key, const char *value)
{
    struct yai_config *config = &app->config;
    if (strcmp(key, "hud") == 0) {
        snprintf(config->hud_mode, sizeof(config->hud_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else if (strcmp(key, "markdown") == 0) {
        snprintf(config->markdown_mode, sizeof(config->markdown_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else if (strcmp(key, "message-search") == 0) {
        snprintf(config->message_search_mode, sizeof(config->message_search_mode), "%s",
                 strcmp(value, "text") == 0 ? "text" : "yetty");
    } else {
        return 0;
    }
    return 1;
}

/* Apply a setting to one engine's section. `key` is the unprefixed section
 * key (model, effort, permission_mode, allowed_preset, allowed_tools,
 * sandbox, approval, approval_mode). Returns 1 if known. */
static int yai_config_set_engine_field(struct yai_engine_config *cfg, const char *key,
                                       const char *value)
{
    if (strcmp(key, "model") == 0) {
        snprintf(cfg->model, sizeof(cfg->model), "%s", value);
    } else if (strcmp(key, "effort") == 0) {
        snprintf(cfg->effort, sizeof(cfg->effort), "%s", value);
    } else if (strcmp(key, "permission-mode") == 0) {
        snprintf(cfg->permission_mode, sizeof(cfg->permission_mode), "%s", value);
    } else if (strcmp(key, "allowed-preset") == 0) {
        snprintf(cfg->allowed_preset, sizeof(cfg->allowed_preset), "%s", value);
    } else if (strcmp(key, "allowed-tools") == 0) {
        snprintf(cfg->allowed_tools, sizeof(cfg->allowed_tools), "%s", value);
    } else if (strcmp(key, "sandbox") == 0) {
        snprintf(cfg->sandbox, sizeof(cfg->sandbox), "%s", value);
    } else if (strcmp(key, "approval") == 0) {
        snprintf(cfg->approval, sizeof(cfg->approval), "%s", value);
    } else if (strcmp(key, "approval-mode") == 0) {
        snprintf(cfg->approval_mode, sizeof(cfg->approval_mode), "%s", value);
    } else {
        return 0;
    }
    return 1;
}

/* Apply one key/value found inside a backend's section: a backend-specific
 * field, or — when it names a global default — a per-backend override. */
static void yai_config_apply_engine_key(struct yai_engine_config *cfg, const char *key,
                                        const char *value)
{
    if (yai_config_set_engine_field(cfg, key, value)) {
        return;
    }
    if (yai_is_default_key(key)) {
        yai_engine_override_set(cfg, key, value);
    }
}

/* Resolve the live values consumed at runtime from the global defaults and the
 * active backend's overrides: the editor mode and the renderer's fold_lines /
 * show_thinking. The HUD-related defaults are resolved on demand instead (see
 * yai_effective_hud_*). Call whenever the active engine or a default changes. */
static void yai_config_resolve(struct yai_app *app)
{
    const struct yai_engine_config *cfg = yai_active_engine_config(app);
    const char *edit_mode = yai_engine_override_get(cfg, "edit-mode");
    if (!edit_mode) {
        edit_mode = app->config.edit_mode;
    }
    app->editor_mode_name = (strcmp(edit_mode, "vi") == 0) ? "vi" : "emacs";
    const char *submit_key = yai_engine_override_get(cfg, "submit-key");
    if (!submit_key) {
        submit_key = app->config.submit_key;
    }
    app->enter_submits = (strcmp(submit_key, "alt-enter") == 0) ? 0 : 1;
    const char *fold = yai_engine_override_get(cfg, "fold-lines");
    int folded = fold ? atoi(fold) : app->config.fold_lines;
    app->renderer.fold_lines = folded > 0 ? folded : YAI_DEFAULT_FOLD_LINES;
    const char *thinking = yai_engine_override_get(cfg, "show-thinking");
    app->renderer.show_thinking =
        thinking ? yai_config_truthy(thinking) : app->config.show_thinking;
}

/* The single live write path: CLI flags, the /config dialog, and the
 * permission "auto" shortcut. Global keys set the defaults; every other key
 * targets the ACTIVE engine's section. Legacy/prefixed knob + CLI keys
 * (codex_sandbox, claude_effort, …) are normalized to section keys. The live
 * values are re-resolved before returning. Returns 1 if the key was known. */
static int yai_config_set(struct yai_app *app, const char *key, const char *value)
{
    int known;
    if (yai_config_set_global(app, key, value)) {
        known = 1;
    } else {
        const char *section_key = key;
        if (strcmp(key, "claude-effort") == 0 || strcmp(key, "codex-effort") == 0) {
            section_key = "effort";
        } else if (strcmp(key, "codex-sandbox") == 0) {
            section_key = "sandbox";
        } else if (strcmp(key, "codex-approval") == 0) {
            section_key = "approval";
        } else if (strcmp(key, "gemini-approval-mode") == 0) {
            section_key = "approval-mode";
        }
        known = yai_config_set_engine_field(yai_active_engine_config(app), section_key, value);
    }
    yai_config_resolve(app);
    return known;
}

/* $XDG_CONFIG_HOME/yetty/yai.yaml, else $HOME/.config/yetty/yai.yaml.
 * Returns 0 on success, -1 if neither base directory is known. */
static int yai_config_path(char *out, size_t out_size)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    int written;
    if (xdg && xdg[0]) {
        written = snprintf(out, out_size, "%s/yetty/yai.yaml", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            return -1;
        }
        written = snprintf(out, out_size, "%s/.config/yetty/yai.yaml", home);
    }
    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

/* mkdir -p of every path component (best-effort; existing dirs are fine). */
static void yai_mkdir_p(const char *path)
{
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
        return;
    }
    for (char *cursor = tmp + 1; *cursor; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            (void)mkdir(tmp, 0700);
            *cursor = '/';
        }
    }
    (void)mkdir(tmp, 0700);
}

/* Overlay the YAML config file onto app->config. Best-effort: a missing,
 * unreadable, or malformed file just leaves the defaults (and any CLI
 * overrides applied afterwards) in place. Unknown keys are ignored. */
static void yai_config_load(struct yai_app *app)
{
    char path[512];
    if (yai_config_path(path, sizeof(path)) != 0) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        (void)fclose(file);
        return;
    }
    yaml_parser_set_input_file(&parser, file);

    /* Three-level layout: a top mapping holding `defaults:` (global scalar
     * keys), `visuals:` (yetty/text rendering switches) and `backends:`
     * (claude/codex/gemini sub-mappings). `depth` tracks mapping nesting
     * (1 = top, 2 = inside defaults/visuals/backends, 3 = inside one
     * backend); `section` selects where the scalars at depth 2/3 land. The one
     * sequence value is `hud_format:` (a list of pieces); it is collected
     * separately into hud_parts and concatenated at its SEQUENCE_END. */
    enum {
        SECTION_NONE,
        SECTION_DEFAULTS,
        SECTION_VISUALS,
        SECTION_BACKENDS
    } section = SECTION_NONE;
    int depth = 0;
    int have_key = 0;
    char pending_key[64] = {0};
    struct yai_engine_config *backend_cfg = NULL;
    int collecting_hud = 0;
    struct yai_engine_config *hud_backend = NULL; /* non-NULL = backend override */
    int hud_part_count = 0;
    char hud_parts[YAI_HUD_FORMAT_PARTS_MAX][YAI_HUD_FORMAT_PART_MAX];
    int done = 0;
    while (!done) {
        yaml_event_t event;
        if (!yaml_parser_parse(&parser, &event)) {
            break; /* malformed — stop, keep whatever was restored so far */
        }
        switch (event.type) {
        case YAML_SEQUENCE_START_EVENT:
            /* `hud_format:` as a list — collect its scalar pieces. */
            if (have_key && strcmp(pending_key, "hud-format") == 0 &&
                ((depth == 2 && section == SECTION_DEFAULTS) ||
                 (depth == 3 && section == SECTION_BACKENDS && backend_cfg))) {
                collecting_hud = 1;
                hud_backend = (depth == 3) ? backend_cfg : NULL;
                hud_part_count = 0;
                have_key = 0;
                break;
            }
            /* fallthrough: any other sequence is treated like a mapping value */
            __attribute__((fallthrough));
        case YAML_MAPPING_START_EVENT:
            /* A container value. At depth 1 it opens `defaults:`/`backends:`;
             * at depth 2 inside backends it opens one engine's section. */
            if (depth == 1 && have_key) {
                if (strcmp(pending_key, "defaults") == 0) {
                    section = SECTION_DEFAULTS;
                } else if (strcmp(pending_key, "visuals") == 0) {
                    section = SECTION_VISUALS;
                } else if (strcmp(pending_key, "backends") == 0) {
                    section = SECTION_BACKENDS;
                } else {
                    section = SECTION_NONE;
                }
            } else if (depth == 2 && section == SECTION_BACKENDS && have_key) {
                if (strcmp(pending_key, "codex") == 0) {
                    backend_cfg = &app->config.codex;
                } else if (strcmp(pending_key, "gemini") == 0) {
                    backend_cfg = &app->config.gemini;
                } else if (strcmp(pending_key, "claude") == 0) {
                    backend_cfg = &app->config.claude;
                } else {
                    backend_cfg = NULL; /* unknown backend: ignore its keys */
                }
            }
            have_key = 0;
            depth++;
            break;
        case YAML_SEQUENCE_END_EVENT:
            if (collecting_hud) {
                /* Finalize the hud_format list: concatenate the pieces. */
                const char *part_ptrs[YAI_HUD_FORMAT_PARTS_MAX];
                for (int index = 0; index < hud_part_count; index++) {
                    part_ptrs[index] = hud_parts[index];
                }
                if (hud_backend) {
                    char joined[1024];
                    size_t offset = 0;
                    joined[0] = '\0';
                    for (int index = 0; index < hud_part_count; index++) {
                        size_t room = sizeof(joined) - offset;
                        int written = snprintf(joined + offset, room, "%s", hud_parts[index]);
                        if (written > 0) {
                            offset += ((size_t)written < room) ? (size_t)written : room - 1;
                        }
                    }
                    yai_engine_override_set(hud_backend, "hud-format", joined);
                } else {
                    yai_config_set_hud_parts(&app->config, part_ptrs, hud_part_count);
                }
                collecting_hud = 0;
                hud_backend = NULL;
                have_key = 0;
                break;
            }
            __attribute__((fallthrough));
        case YAML_MAPPING_END_EVENT:
            depth--;
            if (depth <= 1) {
                section = SECTION_NONE;
                backend_cfg = NULL;
            } else if (depth == 2) {
                backend_cfg = NULL;
            }
            have_key = 0;
            break;
        case YAML_SCALAR_EVENT: {
            const char *text = (const char *)event.data.scalar.value;
            if (collecting_hud) {
                if (hud_part_count < YAI_HUD_FORMAT_PARTS_MAX) {
                    snprintf(hud_parts[hud_part_count++], YAI_HUD_FORMAT_PART_MAX, "%s", text);
                }
            } else if (!have_key) {
                snprintf(pending_key, sizeof(pending_key), "%s", text);
                have_key = 1;
            } else {
                if (depth == 2 && section == SECTION_DEFAULTS) {
                    yai_config_set_global(app, pending_key, text);
                } else if (depth == 2 && section == SECTION_VISUALS) {
                    yai_config_set_visual(app, pending_key, text);
                } else if (depth == 3 && section == SECTION_BACKENDS && backend_cfg) {
                    yai_config_apply_engine_key(backend_cfg, pending_key, text);
                }
                have_key = 0;
            }
            break;
        }
        case YAML_STREAM_END_EVENT:
            done = 1;
            break;
        default:
            break;
        }
        yaml_event_delete(&event);
    }
    yaml_parser_delete(&parser);
    (void)fclose(file);
    yai_config_resolve(app);
}

/* Emit one `key: value` pair only when `value` is non-empty. */
static int yai_config_emit_if_set(yaml_emitter_t *emitter, const char *key, const char *value);

/* Emit one `key: value` pair into the open block mapping. Tag NULL +
 * implicit lets libyaml pick the plain/quoted style; on load the raw scalar
 * string is read back regardless. Returns the emitter success flag (0 =
 * failure; the caller aborts). */
static int yai_config_emit_pair(yaml_emitter_t *emitter, const char *key, const char *value)
{
    yaml_event_t event;
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)key, -1, 1, 1,
                                      YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(emitter, &event)) {
        return 0;
    }
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)value, -1, 1, 1,
                                      YAML_PLAIN_SCALAR_STYLE)) {
        return 0;
    }
    return yaml_emitter_emit(emitter, &event);
}

/* Emit `key: value` only when value is non-empty (skip unset settings). */
static int yai_config_emit_if_set(yaml_emitter_t *emitter, const char *key, const char *value)
{
    if (!value || !value[0]) {
        return 1;
    }
    return yai_config_emit_pair(emitter, key, value);
}

/* Emit `key: "value"` with the value double-quoted. Needed for hud_format:
 * it starts with '#' and embeds newlines, neither of which a plain YAML
 * scalar permits. libyaml escapes the newlines as \n; the loader unescapes
 * them back, and the format parser also accepts the two-char \n form. */
static int yai_config_emit_quoted(yaml_emitter_t *emitter, const char *key, const char *value)
{
    yaml_event_t event;
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)key, -1, 1, 1,
                                      YAML_PLAIN_SCALAR_STYLE) ||
        !yaml_emitter_emit(emitter, &event)) {
        return 0;
    }
    if (!yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)value, -1, 0, 1,
                                      YAML_DOUBLE_QUOTED_SCALAR_STYLE)) {
        return 0;
    }
    return yaml_emitter_emit(emitter, &event);
}

/* Emit a bare plain scalar (a key whose value is a nested block mapping). */
static int yai_config_emit_scalar(yaml_emitter_t *emitter, const char *text)
{
    yaml_event_t event;
    return yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)text, -1, 1, 1,
                                        YAML_PLAIN_SCALAR_STYLE) &&
           yaml_emitter_emit(emitter, &event);
}

/* Open / close a block mapping as the current value. */
static int yai_config_emit_map_start(yaml_emitter_t *emitter)
{
    yaml_event_t event;
    return yaml_mapping_start_event_initialize(&event, NULL, NULL, /*implicit=*/1,
                                               YAML_BLOCK_MAPPING_STYLE) &&
           yaml_emitter_emit(emitter, &event);
}

static int yai_config_emit_map_end(yaml_emitter_t *emitter)
{
    yaml_event_t event;
    return yaml_mapping_end_event_initialize(&event) && yaml_emitter_emit(emitter, &event);
}

/* Emit a single double-quoted scalar value (a sequence item). */
static int yai_config_emit_quoted_value(yaml_emitter_t *emitter, const char *value)
{
    yaml_event_t event;
    return yaml_scalar_event_initialize(&event, NULL, NULL, (const yaml_char_t *)value, -1, 0, 1,
                                        YAML_DOUBLE_QUOTED_SCALAR_STYLE) &&
           yaml_emitter_emit(emitter, &event);
}

/* Emit `hud_format:` as a block sequence of its pieces, each double-quoted (a
 * piece may embed '#' and newlines). The loader concatenates them back. */
static int yai_config_emit_hud_format(yaml_emitter_t *emitter, const struct yai_config *config)
{
    yaml_event_t event;
    if (!yai_config_emit_scalar(emitter, "hud-format")) {
        return 0;
    }
    if (!yaml_sequence_start_event_initialize(&event, NULL, NULL, /*implicit=*/1,
                                              YAML_BLOCK_SEQUENCE_STYLE) ||
        !yaml_emitter_emit(emitter, &event)) {
        return 0;
    }
    /* part_count 0 means the format is a single scalar held in hud_format. */
    int count = config->hud_format_part_count > 0 ? config->hud_format_part_count : 1;
    for (int index = 0; index < count; index++) {
        const char *part = config->hud_format_part_count > 0 ? config->hud_format_parts[index]
                                                             : config->hud_format;
        if (!yai_config_emit_quoted_value(emitter, part)) {
            return 0;
        }
    }
    return yaml_sequence_end_event_initialize(&event) && yaml_emitter_emit(emitter, &event);
}

/* Emit one engine's section under `backends:`: `name:` then a block mapping of
 * that engine's relevant keys, followed by any per-backend overrides of the
 * global defaults (kept verbatim, so they round-trip). Returns the success
 * flag. */
static int yai_config_emit_engine(yaml_emitter_t *emitter, const char *name,
                                  const struct yai_engine_config *cfg)
{
    if (!yai_config_emit_scalar(emitter, name) || !yai_config_emit_map_start(emitter)) {
        return 0;
    }
    int ok = yai_config_emit_if_set(emitter, "model", cfg->model);
    if (strcmp(name, "claude") == 0) {
        ok = ok && yai_config_emit_pair(emitter, "permission-mode", cfg->permission_mode) &&
             yai_config_emit_pair(emitter, "allowed-preset", cfg->allowed_preset) &&
             yai_config_emit_if_set(emitter, "allowed-tools", cfg->allowed_tools) &&
             yai_config_emit_pair(emitter, "effort", cfg->effort);
    } else if (strcmp(name, "codex") == 0) {
        ok = ok && yai_config_emit_pair(emitter, "sandbox", cfg->sandbox) &&
             yai_config_emit_pair(emitter, "approval", cfg->approval) &&
             yai_config_emit_pair(emitter, "effort", cfg->effort);
    } else { /* gemini */
        ok = ok && yai_config_emit_pair(emitter, "approval-mode", cfg->approval_mode);
    }
    /* hud_format is double-quoted (it embeds '#' and newlines); every other
     * override is a plain scalar. */
    for (int index = 0; ok && index < cfg->override_count; index++) {
        const char *key = cfg->overrides[index].key;
        const char *value = cfg->overrides[index].value;
        ok = strcmp(key, "hud-format") == 0 ? yai_config_emit_quoted(emitter, key, value)
                                            : yai_config_emit_pair(emitter, key, value);
    }
    if (!ok) {
        return 0;
    }
    return yai_config_emit_map_end(emitter);
}

/* Write the live settings (app->config + engine/editor/renderer fields) as
 * YAML to the standard path. The keys match the loader exactly, so a written
 * file reloads losslessly. Emitted with libyaml. */
static struct yetty_ycore_void_result yai_config_save(const struct yai_app *app)
{
    char path[512];
    if (yai_config_path(path, sizeof(path)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: no HOME / XDG_CONFIG_HOME");
    }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        yai_mkdir_p(dir);
    }
    /* Write to a sibling temp file and rename() it into place. An interrupted
     * or failed save then never destroys the live config: a plain
     * fopen(path, "w") truncates it up front, so a crash / Ctrl-C / racing
     * second instance mid-write leaves an empty (comment-only) file. */
    char temp_path[544];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path, (int)getpid());
    FILE *file = fopen(temp_path, "w");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: fopen failed");
    }

    /* Header comment first (libyaml's event API emits no comments), then the
     * emitter appends the mapping to the same stream. */
    fputs("# yai config — auto-written on startup, on config change, and on exit.\n"
          "# Global keys under defaults:; yetty/text rendering switches under\n"
          "# visuals:; per-backend settings under backends:.\n"
          "# Any defaults: key may be set under a backend to override it there.\n"
          "#\n",
          file);

    yaml_emitter_t emitter;
    if (!yaml_emitter_initialize(&emitter)) {
        (void)fclose(file);
        (void)unlink(temp_path);
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: emitter init failed");
    }
    yaml_emitter_set_output_file(&emitter, file);

    yaml_event_t event;
    int ok = yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) &&
             yaml_emitter_emit(&emitter, &event);
    if (ok) {
        ok = yaml_document_start_event_initialize(&event, NULL, NULL, NULL, /*implicit=*/1) &&
             yaml_emitter_emit(&emitter, &event);
    }
    if (ok) {
        ok = yaml_mapping_start_event_initialize(&event, NULL, NULL, /*implicit=*/1,
                                                 YAML_BLOCK_MAPPING_STYLE) &&
             yaml_emitter_emit(&emitter, &event);
    }
    if (ok) {
        const struct yai_config *config = &app->config;
        char fold_lines[16];
        snprintf(fold_lines, sizeof(fold_lines), "%d", config->fold_lines);
        /* defaults: { … } — the global settings. */
        ok = yai_config_emit_scalar(&emitter, "defaults") && yai_config_emit_map_start(&emitter) &&
             yai_config_emit_pair(&emitter, "engine", config->engine) &&
             yai_config_emit_pair(&emitter, "edit-mode", config->edit_mode) &&
             yai_config_emit_pair(&emitter, "submit-key", config->submit_key) &&
             yai_config_emit_pair(&emitter, "fold-lines", fold_lines) &&
             yai_config_emit_pair(&emitter, "show-thinking",
                                  config->show_thinking ? "true" : "false") &&
             yai_config_emit_pair(&emitter, "hud-on", config->hud_on ? "true" : "false") &&
             yai_config_emit_pair(&emitter, "hud-float", config->hud_float ? "true" : "false") &&
             yai_config_emit_hud_format(&emitter, config) && yai_config_emit_map_end(&emitter) &&
             /* visuals: { hud, markdown, message-search } — the yetty/text
              * rendering switches (loaded by yai_config_set_visual; the old
              * defaults: hud-mode / markdown-mode spellings still load). */
             yai_config_emit_scalar(&emitter, "visuals") && yai_config_emit_map_start(&emitter) &&
             yai_config_emit_pair(&emitter, "hud", config->hud_mode) &&
             yai_config_emit_pair(&emitter, "markdown", config->markdown_mode) &&
             yai_config_emit_pair(&emitter, "message-search", config->message_search_mode) &&
             yai_config_emit_map_end(&emitter) &&
             /* backends: { claude: {…}, codex: {…}, gemini: {…} }. */
             yai_config_emit_scalar(&emitter, "backends") && yai_config_emit_map_start(&emitter) &&
             yai_config_emit_engine(&emitter, "claude", &config->claude) &&
             yai_config_emit_engine(&emitter, "codex", &config->codex) &&
             yai_config_emit_engine(&emitter, "gemini", &config->gemini) &&
             yai_config_emit_map_end(&emitter);
    }
    if (ok) {
        ok = yaml_mapping_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event);
    }
    if (ok) {
        ok = yaml_document_end_event_initialize(&event, /*implicit=*/1) &&
             yaml_emitter_emit(&emitter, &event);
    }
    if (ok) {
        ok = yaml_stream_end_event_initialize(&event) && yaml_emitter_emit(&emitter, &event);
    }

    yaml_emitter_delete(&emitter);
    if (!ok) {
        (void)fclose(file);
        (void)unlink(temp_path);
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: yaml emit failed");
    }
    if (fclose(file) != 0) {
        (void)unlink(temp_path);
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: fclose failed");
    }
    /* Atomic publish: the live config is replaced in one step, or not at all. */
    if (rename(temp_path, path) != 0) {
        (void)unlink(temp_path);
        return YETTY_ERR(yetty_ycore_void, "yai_config_save: rename failed");
    }
    return YETTY_OK_VOID();
}

/* RFC 4122 v4 UUID from /dev/urandom. Bails on the first error — a
 * session id from a weak fallback would silently degrade resume
 * semantics, so there is no fallback. */
static struct yetty_ycore_void_result generate_session_id(char *out, size_t out_size)
{
    unsigned char bytes[16];
    int urandom_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (urandom_fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "generate_session_id: open /dev/urandom failed");
    }
    size_t filled = 0;
    while (filled < sizeof(bytes)) {
        ssize_t got = read(urandom_fd, bytes + filled, sizeof(bytes) - filled);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            close(urandom_fd);
            return YETTY_ERR(yetty_ycore_void, "generate_session_id: read /dev/urandom failed");
        }
        filled += (size_t)got;
    }
    if (close(urandom_fd) != 0) {
        return YETTY_ERR(yetty_ycore_void, "generate_session_id: close /dev/urandom failed");
    }
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);
    int written = snprintf(
        out, out_size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8],
        bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    if (written < 0 || (size_t)written >= out_size) {
        return YETTY_ERR(yetty_ycore_void, "generate_session_id: output buffer too small");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result show_prompt(struct yai_app *app)
{
    if (app->shutting_down) {
        return YETTY_OK_VOID();
    }
    if (app->renderer.pin_enabled) {
        /* The prompt is the pinned bottom row — always there, busy or
         * not, so commands can be typed/queued mid-turn. */
        struct yetty_ycore_void_result pin_res = yai_renderer_pin_show(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pin_res, "show_prompt: pin");
        return YETTY_OK_VOID();
    }
    if (app->waiting) {
        return YETTY_OK_VOID(); /* legacy (non-tty) mode: no prompt while a turn runs */
    }
    if (fputs(YAI_PROMPT, stdout) == EOF) {
        return YETTY_ERR(yetty_ycore_void, "show_prompt: fputs failed");
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "show_prompt: flush");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Raw input mode (output stays cooked — OPOST survives, so "\n" still
 * renders as CRLF in the scrollback).
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result enter_raw_input(struct yai_app *app)
{
    if (!isatty(STDIN_FILENO)) {
        return YETTY_OK_VOID(); /* pipes need no tty discipline */
    }
    if (tcgetattr(STDIN_FILENO, &app->saved_termios) != 0) {
        return YETTY_ERR(yetty_ycore_void, "enter_raw_input: tcgetattr failed");
    }
    struct termios raw = app->saved_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | INLCR);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return YETTY_ERR(yetty_ycore_void, "enter_raw_input: tcsetattr failed");
    }
    app->termios_saved = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result leave_raw_input(struct yai_app *app)
{
    if (!app->termios_saved) {
        return YETTY_OK_VOID();
    }
    app->termios_saved = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &app->saved_termios) != 0) {
        return YETTY_ERR(yetty_ycore_void, "leave_raw_input: tcsetattr failed");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Pane-wide mouse subscription (client-input OSC envelopes on stdin)
 *---------------------------------------------------------------------------*/

/* Emit a yface envelope to stdout, tolerating EAGAIN. yai's stdout is
 * non-blocking (it shares the tty file description with the polled stdin),
 * so the straight yetty_yface_emit_to_fd — which retries only EINTR —
 * fails mid-write when the master pipe is briefly full (a resize storm, or
 * a fast mouse-drag spewing reinject envelopes). Build the bytes, then
 * drain them with a POLLOUT wait, the discipline the HUD pty shim uses.
 * Pending stdio is flushed first so the envelope serialises after buffered
 * text (yai is the single PTY writer). */
static struct yetty_ycore_void_result yai_emit_stdout_envelope(int wire_code, const void *payload,
                                                               size_t payload_len)
{
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_emit_stdout_envelope: flush");
    struct yetty_ycore_buffer buffer = {0};
    struct yetty_ycore_void_result emit_res =
        yetty_yface_emit(wire_code, 0, NULL, 0, payload, payload_len, &buffer);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&buffer);
        return YETTY_ERR(yetty_ycore_void, "yai_emit_stdout_envelope: encode", emit_res);
    }
    size_t written = 0;
    while (written < buffer.size) {
        ssize_t chunk = write(STDOUT_FILENO, buffer.data + written, buffer.size - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd ready = {.fd = STDOUT_FILENO, .events = POLLOUT};
                (void)poll(&ready, 1, -1);
                continue;
            }
            yetty_ycore_buffer_destroy(&buffer);
            return YETTY_ERR(yetty_ycore_void, "yai_emit_stdout_envelope: write failed");
        }
        written += (size_t)chunk;
    }
    yetty_ycore_buffer_destroy(&buffer);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_mouse_sub(uint32_t flags)
{
    struct yetty_client_input_sub sub = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
    };
    struct yetty_ycore_void_result emit_res =
        yai_emit_stdout_envelope(YETTY_OSC_CS_CLIENT_INPUT_SUB, &sub, sizeof(sub));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "emit_mouse_sub: emit");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result subscribe_mouse(struct yai_app *app)
{
    struct yetty_ycore_void_result sub_res =
        emit_mouse_sub(YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK | YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                       YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_res, "subscribe_mouse: sub envelope");
    if (fputs("\x1b[?1500h\x1b[?1501h", stdout) == EOF) {
        return YETTY_ERR(yetty_ycore_void, "subscribe_mouse: fputs failed");
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "subscribe_mouse: flush");
    app->mouse_subscribed = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result unsubscribe_mouse(struct yai_app *app)
{
    if (!app->mouse_subscribed) {
        return YETTY_OK_VOID();
    }
    app->mouse_subscribed = 0;
    if (fputs("\x1b[?1500l\x1b[?1501l", stdout) == EOF) {
        return YETTY_ERR(yetty_ycore_void, "unsubscribe_mouse: fputs failed");
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "unsubscribe_mouse: flush");
    struct yetty_ycore_void_result unsub_res = emit_mouse_sub(0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, unsub_res, "unsubscribe_mouse: sub envelope");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Shell escape — /shell [cmd…] or !cmd. yai hands the PTY to a shell
 * (cooked tty, no mouse envelopes, no stdin polling) and blocks until
 * it exits; then it re-takes ownership and the prompt continues.
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_stdin_readable(uv_poll_t *poll_handle, int status, int events);
YETTY_EXTERNAL_CALLBACK
static void on_sigint(uv_signal_t *signal_handle, int signum);

/* Give the terminal back to plain processes. Mirror of resume below. */
static struct yetty_ycore_void_result suspend_terminal_ownership(struct yai_app *app)
{
    /* Hand the full screen to the plain process: drop the text-HUD scroll
     * region first, else the child's output is confined above our bar. */
    struct yetty_ycore_void_result hud_res = yai_renderer_text_hud_release(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hud_res, "suspend_terminal: text hud release");
    struct yetty_ycore_void_result unsub_res = unsubscribe_mouse(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, unsub_res, "suspend_terminal: unsubscribe mouse");
    struct yetty_ycore_void_result cooked_res = leave_raw_input(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cooked_res, "suspend_terminal: restore tty");
    if (uv_poll_stop(&app->stdin_poll) != 0) {
        return YETTY_ERR(yetty_ycore_void, "suspend_terminal: uv_poll_stop failed");
    }
    if (uv_signal_stop(&app->sigint_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "suspend_terminal: uv_signal_stop failed");
    }
    return YETTY_OK_VOID();
}

/* Re-take the terminal. Best-effort: every step runs even if an
 * earlier one failed — a half-resumed yai is unusable. */
static struct yetty_ycore_void_result resume_terminal_ownership(struct yai_app *app)
{
    struct yetty_ycore_void_result resume = YETTY_OK_VOID();
    resume = yetty_ycore_void_chain(resume, enter_raw_input(app));
    if (app->hud) {
        resume = yetty_ycore_void_chain(resume, subscribe_mouse(app));
        /* The shell/editor that owned the screen may have cleared it
         * (`clear`, Ctrl-L) — the host answered by dropping every
         * compositor figure. Forget + re-emit so ours re-materialize;
         * figures that survived are reused host-side. */
        resume = yetty_ycore_void_chain(resume, yai_hud_forget_remote(app->hud));
    }
    /* Re-reserve the text-HUD bottom row on the restored screen; the
     * conversation is still on screen, so keep the prompt at the bottom. */
    resume = yetty_ycore_void_chain(resume, yai_renderer_text_hud_reserve(&app->renderer, 0));
    if (uv_poll_start(&app->stdin_poll, UV_READABLE, on_stdin_readable) != 0) {
        resume = yetty_ycore_void_chain(
            resume, YETTY_ERR(yetty_ycore_void, "resume_terminal: uv_poll_start failed"));
    }
    if (uv_signal_start(&app->sigint_handle, on_sigint, SIGINT) != 0) {
        resume = yetty_ycore_void_chain(
            resume, YETTY_ERR(yetty_ycore_void, "resume_terminal: uv_signal_start failed"));
    }
    return resume;
}

/* Run `command` (NULL = interactive $SHELL) as the foreground process
 * group of the tty — real job control inside, Ctrl-C hits the shell,
 * not yai. Blocks the event loop on purpose: while the shell owns the
 * screen, yai must draw nothing. A turn in flight simply keeps running
 * in the background — the engine child writes into its pipe (blocking
 * once the kernel buffer fills) and the output drains the moment the
 * shell exits and the loop resumes. */
static struct yetty_ycore_void_result run_shell(struct yai_app *app, const char *command)
{
    /* The shell owns the screen: drop the pinned prompt zone until the
     * caller re-shows it after the handoff ends. */
    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "run_shell: pin hide");
    const char *shell = getenv("SHELL");
    if (!shell || !shell[0]) {
        shell = "/bin/sh";
    }
    struct yetty_ycore_void_result suspend_res = suspend_terminal_ownership(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "run_shell: suspend");
    struct yetty_ycore_void_result state_res = yai_set_state(app, "⌨ shell");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "run_shell: hud state");
    printf(YAI_DIM "(entering %s — exit to return to yai%s)" YAI_RESET "\n",
           command ? command : shell, app->waiting ? "; the turn continues in the background" : "");
    struct yetty_ycore_void_result banner_flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, banner_flush_res, "run_shell: flush");

    pid_t child = fork();
    if (child < 0) {
        struct yetty_ycore_void_result resume_res = resume_terminal_ownership(app);
        if (YETTY_IS_ERR(resume_res)) {
            return YETTY_ERR(yetty_ycore_void, "run_shell: fork failed AND resume failed",
                             resume_res);
        }
        return YETTY_ERR(yetty_ycore_void, "run_shell: fork failed");
    }
    if (child == 0) {
        /* Child: own process group, foreground on the tty, default
         * signal dispositions, then become the shell. */
        setpgid(0, 0);
        tcsetpgrp(STDIN_FILENO, getpid());
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        unsetenv("YETTY_MCP_VIA_PARENT");
        if (command && command[0]) {
            execl(shell, shell, "-c", command, (char *)NULL);
        } else {
            execl(shell, shell, (char *)NULL);
        }
        _exit(127);
    }

    /* Parent: mirror the pgrp/foreground handoff (whichever side runs
     * first wins the race), then block until the shell is done. */
    setpgid(child, child);
    tcsetpgrp(STDIN_FILENO, child);
    int wait_status = 0;
    while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {
    }
    /* Take the tty back; the tcsetpgrp from a non-foreground group
     * raises SIGTTOU, which must be ignored for the takeback itself. */
    void (*saved_ttou_handler)(int) = signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(STDIN_FILENO, getpgrp());
    signal(SIGTTOU, saved_ttou_handler);

    if (WIFEXITED(wait_status)) {
        printf(YAI_DIM "(shell exited %d)" YAI_RESET "\n", WEXITSTATUS(wait_status));
    } else if (WIFSIGNALED(wait_status)) {
        printf(YAI_DIM "(shell killed by signal %d)" YAI_RESET "\n", WTERMSIG(wait_status));
    }

    /* Best-effort from here: yai must re-take the terminal whatever the
     * earlier steps did — chain every error, surface the first. */
    struct yetty_ycore_void_result teardown = yai_render_flush_stdout();
    teardown = yetty_ycore_void_chain(teardown, resume_terminal_ownership(app));
    /* Restore the activity surfaces AFTER the re-subscribe, so the HUD
     * emit lands once the tty is ours again. A turn may still be in
     * flight behind the shell — say so instead of "idle". */
    if (app->waiting) {
        teardown =
            yetty_ycore_void_chain(teardown, yai_set_activity(app, "typing-dots", "… thinking"));
    } else {
        teardown = yetty_ycore_void_chain(teardown, yai_set_state(app, "idle"));
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "run_shell: resume");
    return YETTY_OK_VOID();
}

/* Ctrl-G: edit the current message in an external editor ($VISUAL /
 * $EDITOR, default nvim then vi). The compose file holds the assistant's
 * last reply — above a separator, for reference/quoting — followed below
 * by the current draft, where the user writes. On save+quit the text
 * BELOW the separator becomes the prompt; it is NOT submitted (the user
 * reviews, then presses Enter). Mirrors run_shell's terminal handoff. */
#define YAI_COMPOSE_SEPARATOR                                                                      \
    "──────── the assistant's last reply is above (reference) · write your message BELOW ────────"

static struct yetty_ycore_void_result open_external_editor(struct yai_app *app)
{
    if (!app->renderer.pin_enabled) {
        return YETTY_OK_VOID(); /* need a tty to hand off to an editor */
    }
    const char *temp_dir = getenv("TMPDIR");
    if (!temp_dir || !temp_dir[0]) {
        temp_dir = "/tmp";
    }
    char path[1024];
    int written = snprintf(path, sizeof(path), "%s/yetty-compose-XXXXXX", temp_dir);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: temp path too long");
    }
    int fd = mkstemp(path);
    if (fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: mkstemp failed");
    }
    FILE *compose = fdopen(fd, "w");
    if (!compose) {
        close(fd);
        unlink(path);
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: fdopen failed");
    }
    if (app->last_response_len > 0) {
        fwrite(app->last_response, 1, app->last_response_len, compose);
        fputs("\n" YAI_COMPOSE_SEPARATOR "\n", compose);
    }
    fwrite(app->stdin_buf, 1, app->stdin_len, compose);
    if (fclose(compose) != 0) {
        unlink(path);
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: temp write failed");
    }

    const char *editor = getenv("VISUAL");
    if (!editor || !editor[0]) {
        editor = getenv("EDITOR");
    }
    if (!editor || !editor[0]) {
        editor = "nvim";
    }

    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    if (YETTY_IS_ERR(hide_res)) {
        unlink(path);
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: pin hide", hide_res);
    }
    struct yetty_ycore_void_result suspend_res = suspend_terminal_ownership(app);
    if (YETTY_IS_ERR(suspend_res)) {
        unlink(path);
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: suspend", suspend_res);
    }

    pid_t child = fork();
    if (child < 0) {
        struct yetty_ycore_void_result resume_res = resume_terminal_ownership(app);
        unlink(path);
        if (YETTY_IS_ERR(resume_res)) {
            return YETTY_ERR(yetty_ycore_void, "open_external_editor: fork AND resume failed",
                             resume_res);
        }
        return YETTY_ERR(yetty_ycore_void, "open_external_editor: fork failed");
    }
    if (child == 0) {
        setpgid(0, 0);
        tcsetpgrp(STDIN_FILENO, getpid());
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        unsetenv("YETTY_MCP_VIA_PARENT");
        execlp(editor, editor, path, (char *)NULL);
        execlp("vi", "vi", path, (char *)NULL); /* fallback if $EDITOR is bad */
        _exit(127);
    }
    setpgid(child, child);
    tcsetpgrp(STDIN_FILENO, child);
    int wait_status = 0;
    while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {
    }
    void (*saved_ttou_handler)(int) = signal(SIGTTOU, SIG_IGN);
    tcsetpgrp(STDIN_FILENO, getpgrp());
    signal(SIGTTOU, saved_ttou_handler);

    /* Read the edited file back: the text below the separator is the new
     * message (or the whole file if the separator was removed). */
    FILE *result = fopen(path, "r");
    if (result) {
        char buffer[8192];
        size_t got = fread(buffer, 1, sizeof(buffer) - 1, result);
        fclose(result);
        buffer[got] = '\0';
        char *message = buffer;
        char *separator = strstr(buffer, YAI_COMPOSE_SEPARATOR);
        if (separator) {
            message = separator + strlen(YAI_COMPOSE_SEPARATOR);
        }
        /* Drop the line break that follows the separator (and any blank
         * lines the user left above their reply). */
        while (*message == '\n' || *message == '\r') {
            message++;
        }
        size_t prompt_len = strlen(message);
        while (prompt_len > 0 &&
               (message[prompt_len - 1] == '\n' || message[prompt_len - 1] == '\r' ||
                message[prompt_len - 1] == ' ' || message[prompt_len - 1] == '\t')) {
            prompt_len--;
        }
        if (prompt_len >= sizeof(app->stdin_buf)) {
            prompt_len = sizeof(app->stdin_buf) - 1;
        }
        memcpy(app->stdin_buf, message, prompt_len);
        app->stdin_len = prompt_len;
        app->stdin_cursor = prompt_len;
        app->history_browse = -1;
    }
    unlink(path);

    /* Re-take the terminal and repaint the prompt with the composed text
     * (never submitted — the user presses Enter). */
    struct yetty_ycore_void_result teardown = resume_terminal_ownership(app);
    if (app->waiting) {
        teardown =
            yetty_ycore_void_chain(teardown, yai_set_activity(app, "typing-dots", "… thinking"));
    }
    teardown = yetty_ycore_void_chain(teardown, yai_renderer_pin_show(&app->renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "open_external_editor: resume");
    return YETTY_OK_VOID();
}

/* Bounce an event yai did not consume back to the host for its default
 * handling (wheel → terminal scrollback, …). Subscribing made the host
 * forward everything; this is the return path for the rest. */
static struct yetty_ycore_void_result reinject_mouse(const struct yetty_client_input_mouse *mouse)
{
    struct yetty_ycore_void_result emit_res =
        yai_emit_stdout_envelope(YETTY_OSC_CS_CLIENT_INPUT_REINJECT, mouse, sizeof(*mouse));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "reinject_mouse: emit");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Child stdout pump — shared by all engines
 *---------------------------------------------------------------------------*/

/* Append one stdout line (plus the joining newline that reproduces a
 * multi-line document) to the JSON reassembly buffer. Bounded by
 * YAI_CHILD_OUT_MAX; on overflow the buffer is reset and an error
 * surfaced so a runaway child can't grow it without limit. */
static struct yetty_ycore_void_result pending_json_append(struct yai_app *app, const char *line,
                                                          size_t len)
{
    if (len + 1 > YAI_CHILD_OUT_MAX || app->pending_json_len > YAI_CHILD_OUT_MAX - len - 1) {
        app->pending_json_len = 0;
        return YETTY_ERR(yetty_ycore_void, "pending_json_append: accumulated JSON too large");
    }
    if (app->pending_json_len + len + 1 > app->pending_json_cap) {
        size_t new_cap = app->pending_json_cap ? app->pending_json_cap : 64 * 1024;
        while (new_cap < app->pending_json_len + len + 1) {
            new_cap *= 2; /* bounded by YAI_CHILD_OUT_MAX above */
        }
        char *grown = realloc(app->pending_json, new_cap);
        if (!grown) {
            app->pending_json_len = 0;
            return YETTY_ERR(yetty_ycore_void, "pending_json_append: realloc failed");
        }
        app->pending_json = grown;
        app->pending_json_cap = new_cap;
    }
    memcpy(app->pending_json + app->pending_json_len, line, len);
    app->pending_json_len += len;
    app->pending_json[app->pending_json_len++] = '\n';
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_handle_child_line(struct yai_app *app, const char *line,
                                                     size_t len)
{
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    /* The transcript is a diagnostic mirror: a dead disk must not kill
     * the session. Disable it on the first write failure and surface
     * the error once, after the event still got dispatched. */
    struct yetty_ycore_void_result transcript_res = YETTY_OK_VOID();
    if (app->transcript_file) {
        if (fwrite(line, 1, len, app->transcript_file) != len ||
            fputc('\n', app->transcript_file) == EOF) {
            fclose(app->transcript_file);
            app->transcript_file = NULL;
            transcript_res = YETTY_ERR(yetty_ycore_void,
                                       "yai_handle_child_line: transcript write failed — disabled");
        } else if (fflush(app->transcript_file) != 0) {
            fclose(app->transcript_file);
            app->transcript_file = NULL;
            transcript_res = YETTY_ERR(yetty_ycore_void,
                                       "yai_handle_child_line: transcript flush failed — disabled");
        }
    }
    /* The interop shell owns the screen: dispatching this line could
     * print over it. Defer the line — replayed in arrival order by
     * yai_shell_focus_end. (The transcript above stays real-time.) */
    if (app->shell_focus) {
        if (app->deferred_count == app->deferred_cap) {
            size_t new_cap = app->deferred_cap ? app->deferred_cap * 2 : 64;
            struct yai_deferred_line *grown =
                realloc(app->deferred_lines, new_cap * sizeof(*grown));
            if (!grown) {
                if (YETTY_IS_ERR(transcript_res)) {
                    yetty_ycore_error_destroy(transcript_res.error);
                }
                return YETTY_ERR(yetty_ycore_void, "yai_handle_child_line: defer realloc");
            }
            app->deferred_lines = grown;
            app->deferred_cap = new_cap;
        }
        char *copy = malloc(len);
        if (!copy) {
            if (YETTY_IS_ERR(transcript_res)) {
                yetty_ycore_error_destroy(transcript_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "yai_handle_child_line: defer malloc");
        }
        memcpy(copy, line, len);
        app->deferred_lines[app->deferred_count].bytes = copy;
        app->deferred_lines[app->deferred_count].len = len;
        app->deferred_count++;
        YETTY_RETURN_IF_ERR(yetty_ycore_void, transcript_res, "yai_handle_child_line");
        return YETTY_OK_VOID();
    }
    /* Reassemble across lines so both framings work: JSONL (claude /
     * codex — one complete object per line) parses and clears on the
     * first line; a single pretty-printed object (gemini) accumulates
     * until its closing brace completes the document. */
    struct yetty_ycore_void_result append_res = pending_json_append(app, line, len);
    if (YETTY_IS_ERR(append_res)) {
        if (YETTY_IS_ERR(transcript_res)) {
            yetty_ycore_error_destroy(transcript_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yai_handle_child_line: accumulate", append_res);
    }

    yyjson_read_err read_err = {0};
    yyjson_doc *doc =
        yyjson_read_opts(app->pending_json, app->pending_json_len, 0, NULL, &read_err);
    if (doc) {
        yyjson_val *root = yyjson_doc_get_root(doc);
        struct yetty_ycore_void_result dispatch_res = YETTY_OK_VOID();
        if (yyjson_is_obj(root)) {
            dispatch_res = yetty_yai_handle_event(app->engine, app, root);
        }
        yyjson_doc_free(doc);
        app->pending_json_len = 0;
        if (YETTY_IS_ERR(dispatch_res)) {
            if (YETTY_IS_ERR(transcript_res)) {
                yetty_ycore_error_destroy(transcript_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "yai_handle_child_line: handle_event", dispatch_res);
        }
    } else if (read_err.code != YYJSON_READ_ERROR_UNEXPECTED_END) {
        /* Not a truncated-but-valid prefix: a stray non-JSON log line or
         * malformed output. Drop the buffer and resync on the next line
         * — non-JSON lines have never been an error on this path. */
        app->pending_json_len = 0;
    }
    /* else: a valid but incomplete document so far — keep accumulating. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, transcript_res, "yai_handle_child_line");
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
void yai_child_stdout_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
    (void)handle;
    buffer->base = malloc(suggested_size);
    buffer->len = buffer->base ? suggested_size : 0;
}

YETTY_EXTERNAL_CALLBACK
void yai_child_stdout_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
    struct yai_app *app = stream->data;
    if (nread < 0) {
        free(buffer->base);
        uv_read_stop(stream);
        /* A new child starts fresh: drop any partial (truncated) JSON
         * document so it can't bleed into the next turn's reassembly. */
        app->pending_json_len = 0;
        yai_report_error(app, "engine eof", yetty_yai_on_child_eof(app->engine, app));
        return;
    }
    if (nread == 0) {
        free(buffer->base);
        return;
    }
    /* A single line (e.g. a base64 figure) can be large, but a child
     * that never emits a newline must not grow this without bound and
     * wrap the capacity math. Cap it. */
    if ((size_t)nread > YAI_CHILD_OUT_MAX ||
        app->child_out_len > YAI_CHILD_OUT_MAX - (size_t)nread) {
        free(buffer->base);
        yai_report_error(app, "child stdout",
                         YETTY_ERR(yetty_ycore_void, "yai_child_stdout_read_cb: line too large"));
        return;
    }
    if (app->child_out_len + (size_t)nread + 1 > app->child_out_cap) {
        size_t new_cap = app->child_out_cap ? app->child_out_cap : 64 * 1024;
        while (new_cap < app->child_out_len + (size_t)nread + 1) {
            new_cap *= 2; /* bounded by YAI_CHILD_OUT_MAX above */
        }
        char *grown = realloc(app->child_out_buf, new_cap);
        if (!grown) {
            free(buffer->base);
            yai_report_error(app, "child stdout",
                             YETTY_ERR(yetty_ycore_void, "yai_child_stdout_read_cb: realloc"));
            return;
        }
        app->child_out_buf = grown;
        app->child_out_cap = new_cap;
    }
    memcpy(app->child_out_buf + app->child_out_len, buffer->base, (size_t)nread);
    app->child_out_len += (size_t)nread;
    free(buffer->base);

    /* Extract complete lines; keep any partial tail. */
    size_t line_start = 0;
    for (size_t index = 0; index < app->child_out_len; index++) {
        if (app->child_out_buf[index] != '\n') {
            continue;
        }
        app->child_out_buf[index] = '\0';
        yai_report_error(
            app, "child line",
            yai_handle_child_line(app, app->child_out_buf + line_start, index - line_start));
        line_start = index + 1;
    }
    if (line_start > 0) {
        memmove(app->child_out_buf, app->child_out_buf + line_start,
                app->child_out_len - line_start);
        app->child_out_len -= line_start;
    }
}

YETTY_EXTERNAL_CALLBACK
void yai_handle_closed_cb(uv_handle_t *handle)
{
    (void)handle;
}

YETTY_EXTERNAL_CALLBACK
void yai_child_exit_cb(uv_process_t *process, int64_t exit_status, int term_signal)
{
    struct yai_app *app = process->data;
    (void)term_signal;
    app->child_alive = 0;
    uv_timer_stop(&app->kill_timer);
    yai_report_error(app, "engine exit", yetty_yai_on_child_exit(app->engine, app, exit_status));
}

/*---------------------------------------------------------------------------
 * Slash-command completion menu — matches live here; the rows render as
 * part of the pinned zone (render.c), so asynchronous history writes
 * repaint them safely even while a turn is in flight.
 *---------------------------------------------------------------------------*/

/* Hand the current matches to the renderer as prerendered rows. */
static struct yetty_ycore_void_result menu_render(struct yai_app *app)
{
    char row_storage[YAI_MENU_ROWS][YAI_RENDERER_MENU_ROW_BYTES];
    const char *row_pointers[YAI_MENU_ROWS];
    for (size_t row = 0; row < app->menu_match_count; row++) {
        const struct yai_command *command = &app->commands.items[app->menu_matches[row]];
        char detail[96];
        snprintf(detail, sizeof(detail), "%s%s%.56s", command->argument_hint,
                 command->argument_hint[0] ? "  " : "", command->description);
        if (row == app->menu_selected) {
            snprintf(row_storage[row], sizeof(row_storage[row]),
                     YAI_MINT "▸ /%s" YAI_RESET " " YAI_DIM "%s" YAI_RESET, command->name, detail);
        } else {
            snprintf(row_storage[row], sizeof(row_storage[row]),
                     "  " YAI_BOLD "/%s" YAI_RESET " " YAI_DIM "%s" YAI_RESET, command->name,
                     detail);
        }
        row_pointers[row] = row_storage[row];
    }
    struct yetty_ycore_void_result set_res =
        yai_renderer_menu_set(&app->renderer, row_pointers, app->menu_match_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "menu_render: set rows");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result menu_close(struct yai_app *app)
{
    if (!app->menu_visible) {
        return YETTY_OK_VOID();
    }
    app->menu_visible = 0;
    app->menu_match_count = 0;
    struct yetty_ycore_void_result clear_res = yai_renderer_menu_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "menu_close: clear");
    return YETTY_OK_VOID();
}

/* Recompute matches + redraw after every edit of the input line. */
static struct yetty_ycore_void_result menu_update(struct yai_app *app)
{
    if (!app->echo_input || !app->renderer.pin_enabled || app->stdin_len == 0 ||
        app->stdin_buf[0] != '/' || memchr(app->stdin_buf, ' ', app->stdin_len)) {
        struct yetty_ycore_void_result close_res = menu_close(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "menu_update: close");
        return YETTY_OK_VOID();
    }
    size_t match_count = yai_command_table_match(
        &app->commands, app->stdin_buf + 1, app->stdin_len - 1, app->menu_matches, YAI_MENU_ROWS);
    if (match_count == 0) {
        struct yetty_ycore_void_result close_res = menu_close(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "menu_update: close (no match)");
        return YETTY_OK_VOID();
    }
    app->menu_match_count = match_count;
    if (app->menu_selected >= match_count) {
        app->menu_selected = 0;
    }
    app->menu_visible = 1;
    struct yetty_ycore_void_result render_res = menu_render(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "menu_update: render");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result menu_move_selection(struct yai_app *app, int delta)
{
    if (!app->menu_visible || app->menu_match_count == 0) {
        return YETTY_OK_VOID();
    }
    app->menu_selected =
        (app->menu_selected + (size_t)(delta + (int)app->menu_match_count)) % app->menu_match_count;
    struct yetty_ycore_void_result render_res = menu_render(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "menu_move_selection: render");
    return YETTY_OK_VOID();
}

/* Replace the input line with the selected command. */
static struct yetty_ycore_void_result menu_adopt_selection(struct yai_app *app, int trailing_space)
{
    if (!app->menu_visible || app->menu_match_count == 0) {
        return YETTY_OK_VOID();
    }
    const struct yai_command *command = &app->commands.items[app->menu_matches[app->menu_selected]];
    int written = snprintf(app->stdin_buf, sizeof(app->stdin_buf), "/%s%s", command->name,
                           (trailing_space && command->argument_hint[0]) ? " " : "");
    app->stdin_len = (written > 0) ? (size_t)written : 0;
    app->stdin_cursor = app->stdin_len;
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "menu_adopt_selection: redraw");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Input history — submitted lines, browsed with up/down when the
 * completion menu is closed.
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result history_add(struct yai_app *app, const char *line, size_t len)
{
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (app->history_len > 0) {
        const char *last = app->history[app->history_len - 1];
        if (strlen(last) == len && memcmp(last, line, len) == 0) {
            return YETTY_OK_VOID(); /* immediate duplicate */
        }
    }
    char *copy = strndup(line, len);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "history_add: strndup failed");
    }
    if (app->history_len == YAI_HISTORY_MAX) {
        free(app->history[0]);
        memmove(&app->history[0], &app->history[1],
                sizeof(app->history[0]) * (YAI_HISTORY_MAX - 1));
        app->history_len--;
    }
    app->history[app->history_len++] = copy;
    return YETTY_OK_VOID();
}

static void history_load_entry(struct yai_app *app, const char *text, size_t len)
{
    if (len >= sizeof(app->stdin_buf)) {
        len = sizeof(app->stdin_buf) - 1;
    }
    memcpy(app->stdin_buf, text, len);
    app->stdin_len = len;
    app->stdin_cursor = len;
    /* The line was replaced wholesale — undo history of the prior line no
     * longer applies. */
    editor_ops_undo_clear(app);
}

/* The next history index from `from` (inclusive) stepping `delta` whose
 * entry fuzzy-matches `filter` (vendored fzy; an empty filter matches
 * everything). -1 when none is left in that direction. */
static int history_find_match(const struct yai_app *app, int from, int delta, const char *filter)
{
    for (int index = from; index >= 0 && index < app->history_len; index += delta) {
        if (!filter[0] || has_match(filter, app->history[index])) {
            return index;
        }
    }
    return -1;
}

static struct yetty_ycore_void_result history_browse_move(struct yai_app *app, int delta)
{
    if (app->history_len == 0) {
        return YETTY_OK_VOID();
    }
    if (app->history_browse < 0) {
        if (delta > 0) {
            return YETTY_OK_VOID(); /* not browsing; down does nothing */
        }
        /* Stash the in-progress line; up enters browsing at the newest
         * match. The stash doubles as the fuzzy filter: a half-typed line
         * narrows the walk to entries that fzy-match it (empty = all). */
        size_t stash_len = app->stdin_len;
        if (stash_len >= sizeof(app->history_stash)) {
            stash_len = sizeof(app->history_stash) - 1;
        }
        memcpy(app->history_stash, app->stdin_buf, stash_len);
        app->history_stash[stash_len] = '\0';
        app->history_stash_len = stash_len;
        int start = history_find_match(app, app->history_len - 1, -1, app->history_stash);
        if (start < 0) {
            return YETTY_OK_VOID(); /* nothing in history matches the line */
        }
        app->history_browse = start;
    } else {
        int next = history_find_match(app, app->history_browse + delta, delta, app->history_stash);
        if (delta < 0) {
            if (next < 0) {
                return YETTY_OK_VOID(); /* already at the oldest match */
            }
            app->history_browse = next;
        } else if (next < 0) {
            /* Walked past the newest match: restore the stashed line. */
            app->history_browse = -1;
            history_load_entry(app, app->history_stash, app->history_stash_len);
            struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "history_browse_move: redraw");
            struct yetty_ycore_void_result menu_res = menu_update(app);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_res, "history_browse_move: menu");
            return YETTY_OK_VOID();
        } else {
            app->history_browse = next;
        }
    }
    const char *entry = app->history[app->history_browse];
    history_load_entry(app, entry, strlen(entry));
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "history_browse_move: redraw");
    struct yetty_ycore_void_result menu_res = menu_update(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_res, "history_browse_move: menu");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Ctrl-R interactive history search — fzy-scored, rendered in the
 * completion-menu rows. While active the search owns the keyboard
 * (keyboard_input routes every byte to history_search_feed): printable
 * bytes edit the query, Ctrl-R / arrows move the selection, Enter or Tab
 * adopts the selected entry onto the input line (without submitting),
 * and ESC / Ctrl-C / Ctrl-G cancels back to the stashed line.
 *---------------------------------------------------------------------------*/

/* Recompute the best-scored matches for the current query. Entries are
 * walked newest first with stable insertion, so equal scores — e.g. the
 * empty query, which matches everything — keep recency order. Entries
 * longer than fzy's MATCH_MAX_LEN still qualify via has_match but score
 * SCORE_MIN, ranking them last. */
static void history_search_refilter(struct yai_app *app)
{
    score_t scores[YAI_MENU_ROWS];
    app->history_search_match_count = 0;
    for (int index = app->history_len - 1; index >= 0; index--) {
        const char *entry = app->history[index];
        if (app->history_search_query[0] && !has_match(app->history_search_query, entry)) {
            continue;
        }
        score_t score = app->history_search_query[0] ? match(app->history_search_query, entry) : 0;
        size_t pos = app->history_search_match_count;
        while (pos > 0 && scores[pos - 1] < score) {
            pos--;
        }
        if (pos >= YAI_MENU_ROWS) {
            continue;
        }
        if (app->history_search_match_count < YAI_MENU_ROWS) {
            app->history_search_match_count++;
        }
        for (size_t shift = app->history_search_match_count - 1; shift > pos; shift--) {
            scores[shift] = scores[shift - 1];
            app->history_search_matches[shift] = app->history_search_matches[shift - 1];
        }
        scores[pos] = score;
        app->history_search_matches[pos] = (size_t)index;
    }
    if (app->history_search_selected >= app->history_search_match_count) {
        app->history_search_selected = 0;
    }
}

/* Flatten one history entry onto a single display row: newlines become ⏎
 * and the text truncates on a codepoint boundary — the first characters
 * of the message, one row per match. */
static void history_search_flatten_entry(const char *entry, char *out, size_t out_size)
{
    size_t used = 0;
    for (const char *cursor = entry; *cursor && used + 4 < out_size; cursor++) {
        if (*cursor == '\n') {
            memcpy(out + used, "⏎", 3);
            used += 3;
        } else {
            out[used++] = *cursor;
        }
    }
    while (used > 0 && (out[used - 1] & 0xC0) == 0x80) {
        used--; /* don't cut a codepoint in half */
    }
    out[used] = '\0';
}

/* The full text of the currently selected match, or NULL when the query
 * matches nothing. */
static const char *history_search_selected_text(const struct yai_app *app)
{
    if (app->history_search_match_count == 0) {
        return NULL;
    }
    return app->history[app->history_search_matches[app->history_search_selected]];
}

/* Text mode's top-of-screen preview: a title row plus the selected
 * message wrapped to the terminal width, truncated with a "+K more"
 * tail when it outgrows its row budget (a third of the screen). */
static struct yetty_ycore_void_result history_search_overlay_render(struct yai_app *app)
{
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_col == 0 || size.ws_row == 0) {
        size.ws_col = 80;
        size.ws_row = 24;
    }
    int columns = size.ws_col;
    int budget = size.ws_row / 3;
    if (budget > YAI_RENDERER_OVERLAY_MAX_ROWS - 1) {
        budget = YAI_RENDERER_OVERLAY_MAX_ROWS - 1;
    }
    if (budget < 3) {
        budget = 3;
    }

    char row_storage[YAI_RENDERER_OVERLAY_MAX_ROWS][YAI_RENDERER_OVERLAY_ROW_BYTES];
    const char *row_pointers[YAI_RENDERER_OVERLAY_MAX_ROWS];
    size_t row_count = 0;
    snprintf(row_storage[row_count], sizeof(row_storage[0]),
             YAI_HUD_BG "\033[2K" YAI_MINT_BRIGHT " message %zu/%zu " YAI_SECONDARY
                        "· Ctrl-P/N select · Enter adopt · Esc cancel" YAI_FG_DEFAULT,
             app->history_search_match_count == 0 ? 0 : app->history_search_selected + 1,
             app->history_search_match_count);
    row_pointers[row_count] = row_storage[row_count];
    row_count++;

    const char *full_text = history_search_selected_text(app);
    const char *cursor = full_text ? full_text : "(no history match)";
    int lines_left = 0;
    while ((size_t)(row_count) < (size_t)budget + 1 && *cursor) {
        /* One wrapped display row: up to `columns - 2` codepoints, hard
         * break at a newline. */
        const char *line_start = cursor;
        int cells = 0;
        while (*cursor && *cursor != '\n' && cells < columns - 2) {
            cursor++;
            while ((*cursor & 0xC0) == 0x80) {
                cursor++;
            }
            cells++;
        }
        snprintf(row_storage[row_count], sizeof(row_storage[0]),
                 YAI_HUD_BG "\033[2K" YAI_PRIMARY " %.*s" YAI_FG_DEFAULT,
                 (int)(cursor - line_start), line_start);
        row_pointers[row_count] = row_storage[row_count];
        row_count++;
        if (*cursor == '\n') {
            cursor++;
        }
    }
    for (const char *rest = cursor; *rest; rest++) {
        if (*rest == '\n') {
            lines_left++;
        }
    }
    if (*cursor) {
        /* Ran out of budget: turn the last row into the truncation tail. */
        snprintf(row_storage[row_count - 1], sizeof(row_storage[0]),
                 YAI_HUD_BG "\033[2K" YAI_MUTED " … (+%d more line%s)" YAI_FG_DEFAULT,
                 lines_left + 1, lines_left ? "s" : "");
    }
    struct yetty_ycore_void_result set_res =
        yai_renderer_overlay_set(&app->renderer, row_pointers, row_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "history_search_overlay: set");
    return YETTY_OK_VOID();
}

/* Text mode: mirror the query into the prompt line, the match rows into
 * the completion menu, and the selected message into the top overlay. */
static struct yetty_ycore_void_result history_search_render_text(struct yai_app *app)
{
    memcpy(app->stdin_buf, app->history_search_query, app->history_search_query_len);
    app->stdin_len = app->history_search_query_len;
    app->stdin_cursor = app->history_search_query_len;

    char row_storage[YAI_MENU_ROWS][YAI_RENDERER_MENU_ROW_BYTES];
    const char *row_pointers[YAI_MENU_ROWS];
    size_t row_count = app->history_search_match_count;
    for (size_t row = 0; row < row_count; row++) {
        /* Headroom for the marker + color escapes around the text. */
        char display[YAI_RENDERER_MENU_ROW_BYTES - 48];
        history_search_flatten_entry(app->history[app->history_search_matches[row]], display,
                                     sizeof(display));
        if (row == app->history_search_selected) {
            snprintf(row_storage[row], sizeof(row_storage[row]), YAI_MINT "▸ %s" YAI_RESET,
                     display);
        } else {
            snprintf(row_storage[row], sizeof(row_storage[row]), "  " YAI_DIM "%s" YAI_RESET,
                     display);
        }
        row_pointers[row] = row_storage[row];
    }
    if (row_count == 0) {
        snprintf(row_storage[0], sizeof(row_storage[0]), "  " YAI_DIM "%s" YAI_RESET,
                 "(no history match)");
        row_pointers[0] = row_storage[0];
        row_count = 1;
    }
    struct yetty_ycore_void_result set_res =
        yai_renderer_menu_set(&app->renderer, row_pointers, row_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "history_search_render: set rows");
    struct yetty_ycore_void_result overlay_res = history_search_overlay_render(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_res, "history_search_render: overlay");
    return YETTY_OK_VOID();
}

/* yetty mode: the whole search lives in a ygui window floating over the
 * pane — the text console (prompt line, menu rows) is left alone. */
static struct yetty_ycore_void_result history_search_render_ygui(struct yai_app *app)
{
    char row_storage[YAI_MENU_ROWS][YAI_RENDERER_MENU_ROW_BYTES];
    const char *row_pointers[YAI_MENU_ROWS];
    size_t row_count = app->history_search_match_count;
    for (size_t row = 0; row < row_count; row++) {
        history_search_flatten_entry(app->history[app->history_search_matches[row]],
                                     row_storage[row], sizeof(row_storage[row]));
        row_pointers[row] = row_storage[row];
    }
    const char *full_text = history_search_selected_text(app);
    struct yetty_ycore_void_result update_res = yai_hud_search_update(
        app->hud, app->history_search_query, row_pointers, row_count, app->history_search_selected,
        full_text ? full_text : "(no history match)");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "history_search_render: hud update");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result history_search_render(struct yai_app *app)
{
    return app->history_search_ygui ? history_search_render_ygui(app)
                                    : history_search_render_text(app);
}

/* Move the selection by `delta` rows, wrapping. */
static struct yetty_ycore_void_result history_search_step(struct yai_app *app, int delta)
{
    if (app->history_search_match_count == 0) {
        return YETTY_OK_VOID();
    }
    app->history_search_selected =
        (app->history_search_selected + (size_t)(delta + (int)app->history_search_match_count)) %
        app->history_search_match_count;
    return history_search_render(app);
}

/* Open the search: take over the keyboard with an empty query (which
 * lists the newest entries). The UI backend is captured here: the ygui
 * window (visuals: message-search = yetty, ygui framework present — the
 * search stays out of the text console) or text (menu rows + overlay;
 * the line under edit is stashed because the query borrows the prompt
 * line). */
static struct yetty_ycore_void_result history_search_begin(struct yai_app *app)
{
    if (app->history_len == 0 || !app->echo_input || !app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result close_res = menu_close(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "history_search_begin: menu close");
    app->history_search_ygui =
        app->hud != NULL && strcmp(yai_effective_message_search_mode(app), "yetty") == 0;
    memcpy(app->history_search_stash, app->stdin_buf, app->stdin_len);
    app->history_search_stash_len = app->stdin_len;
    app->history_search_active = 1;
    app->history_search_query_len = 0;
    app->history_search_query[0] = '\0';
    app->history_search_selected = 0;
    app->history_browse = -1;
    snprintf(app->edit_status, sizeof(app->edit_status), "%s", "[search]");
    history_search_refilter(app);
    if (app->history_search_ygui) {
        /* The prompt line is untouched in ygui mode — repaint it only so
         * the [search] mode indicator shows. */
        struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "history_search_begin: redraw");
    }
    return history_search_render(app);
}

/* Close the search. accept=1 adopts the selected match as the input
 * line. accept=0 in text mode restores the line stashed when the search
 * opened; in ygui mode the line was never touched. Accept with no match
 * keeps the typed query as the line in text mode (it is already
 * mirrored there) and keeps the original line in ygui mode. */
static struct yetty_ycore_void_result history_search_end(struct yai_app *app, int accept)
{
    int was_ygui = app->history_search_ygui;
    app->history_search_active = 0;
    app->edit_status[0] = '\0';
    if (accept && app->history_search_match_count > 0) {
        const char *entry = history_search_selected_text(app);
        history_load_entry(app, entry, strlen(entry));
    } else if (!accept && !was_ygui) {
        history_load_entry(app, app->history_search_stash, app->history_search_stash_len);
    }
    if (was_ygui) {
        struct yetty_ycore_void_result hide_res = yai_hud_search_hide(app->hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "history_search_end: hide window");
        struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "history_search_end: redraw");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result overlay_res = yai_renderer_overlay_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, overlay_res, "history_search_end: clear overlay");
    /* menu_clear repaints the pinned zone, so the adopted/restored line
     * shows up in the same stroke as the rows disappearing. */
    struct yetty_ycore_void_result clear_res = yai_renderer_menu_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "history_search_end: clear rows");
    return YETTY_OK_VOID();
}

/* One keyboard byte while the search owns the keyboard. */
static struct yetty_ycore_void_result history_search_feed(struct yai_app *app, unsigned char byte)
{
    char final_byte = 0;
    int param = 0;
    int status = editor_ops_csi(app, (char)byte, &final_byte, &param);
    if (status == YAI_CSI_MID) {
        return YETTY_OK_VOID();
    }
    if (status == YAI_CSI_COMPLETE) {
        switch (final_byte) {
        case 'A': /* up */
            return history_search_step(app, -1);
        case 'B': /* down */
            return history_search_step(app, 1);
        default: /* other CSI (left/right/home/…): ignored while searching */
            return YETTY_OK_VOID();
        }
    }
    if (status == YAI_CSI_META) {
        /* Lone ESC then a byte: cancel; the trailing byte is swallowed
         * (the usual single-line-editor ESC compromise). */
        return history_search_end(app, /*accept=*/0);
    }
    switch (byte) {
    case '\r':
    case '\n':
    case '\t': /* Enter / Tab: adopt the selection onto the line */
        return history_search_end(app, /*accept=*/1);
    case 0x12: /* Ctrl-R again: step to the next match */
        return history_search_step(app, 1);
    case 0x10: /* Ctrl-P: selection up */
        return history_search_step(app, -1);
    case 0x0E: /* Ctrl-N: selection down */
        return history_search_step(app, 1);
    case 0x03: /* Ctrl-C */
    case 0x07: /* Ctrl-G — readline's search abort */
        return history_search_end(app, /*accept=*/0);
    case 0x15: /* Ctrl-U: clear the query */
        app->history_search_query_len = 0;
        app->history_search_query[0] = '\0';
        app->history_search_selected = 0;
        history_search_refilter(app);
        return history_search_render(app);
    case 0x7F:
    case 0x08: { /* Backspace: drop the last query codepoint */
        size_t len = app->history_search_query_len;
        while (len > 0 && (app->history_search_query[len - 1] & 0xC0) == 0x80) {
            len--;
        }
        if (len > 0) {
            len--;
        }
        app->history_search_query_len = len;
        app->history_search_query[len] = '\0';
        app->history_search_selected = 0;
        history_search_refilter(app);
        return history_search_render(app);
    }
    default:
        break;
    }
    if (byte < 0x20) {
        return YETTY_OK_VOID(); /* other control bytes: ignored */
    }
    if (app->history_search_query_len + 1 < sizeof(app->history_search_query)) {
        app->history_search_query[app->history_search_query_len++] = (char)byte;
        app->history_search_query[app->history_search_query_len] = '\0';
        app->history_search_selected = 0; /* an edited query re-ranks: jump to the best match */
        history_search_refilter(app);
        return history_search_render(app);
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Shutdown / turn boundary
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_kill_timer(uv_timer_t *timer)
{
    struct yai_app *app = timer->data;
    if (app->child_alive) {
        /* The PID we spawned ourselves — never a name/pattern kill. */
        uv_process_kill(&app->child_process, SIGKILL);
    }
}

void yai_arm_child_kill_timer(struct yai_app *app)
{
    uv_timer_start(&app->kill_timer, on_kill_timer, YAI_KILL_TIMEOUT_MS, 0);
}

/* Reserve the bottom band for the docked HUD by placing the terminal content
 * surface on a content rect (YETTY_OSC_CS_CONTENT_RECT) anchored 'dock_px'
 * above the pane's bottom edge ({0, 0, 0, -dock_px} — edge-anchored, so the
 * host re-resolves it across its own resizes with no round-trip). yetty owns
 * the real cell metrics, so it converts the rect to whole rows, shrinks the
 * libvterm grid (text reflows, we get SIGWINCH with fewer rows) and renders
 * terminal content inside the rect — so neither conversation text nor a tall
 * figure (sheet music, a plot) renders under the bar. This genuinely takes
 * the band out of the terminal size, unlike a DECSTBM scroll region which
 * figures ignore. Idempotent: emits only when the reserved height actually
 * changes, because a redundant emit would re-resize the grid and re-raise
 * SIGWINCH (a reflow loop). Reset with yai_release_dock_reservation. A no-op
 * when the HUD floats or stdout is not a tty. */
static struct yetty_ycore_void_result yai_apply_dock_reservation(struct yai_app *app)
{
    if (!app->hud || !app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    float dock_px = yai_hud_dock_height(app->hud);
    if (dock_px <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Idempotent. The host resizes the grid to honor the rect and raises
     * SIGWINCH; our SIGWINCH handler re-applies the dock. If we re-emitted
     * the unchanged rect that would resize + SIGWINCH again — an endless
     * reflow loop. The host persists the spec across its OWN resizes
     * (terminal_apply_pane_geometry re-resolves it), so re-asserting an
     * unchanged value is also redundant. dock_px is a literal constant
     * (no arithmetic), so the exact compare is safe. */
    if (dock_px == app->dock_reserved_px) {
        return YETTY_OK_VOID();
    }
    struct yetty_content_rect content_rect = {
        .magic = YETTY_CONTENT_RECT_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .height = -dock_px,
    };
    struct yetty_ycore_void_result emit_res =
        yai_emit_stdout_envelope(YETTY_OSC_CS_CONTENT_RECT, &content_rect, sizeof(content_rect));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "yai_apply_dock_reservation: emit");
    app->dock_reserved_px = dock_px;
    return YETTY_OK_VOID();
}

/* Drop the HUD reservation: the all-zero rect restores the full-pane grid. */
static struct yetty_ycore_void_result yai_release_dock_reservation(struct yai_app *app)
{
    if (!app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    /* Nothing reserved → nothing to release (and don't send a redundant
     * zero rect that would needlessly reflow the grid). */
    if (app->dock_reserved_px == 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_content_rect content_rect = {
        .magic = YETTY_CONTENT_RECT_MAGIC,
        .version = YMGUI_WIRE_VERSION,
    };
    struct yetty_ycore_void_result emit_res =
        yai_emit_stdout_envelope(YETTY_OSC_CS_CONTENT_RECT, &content_rect, sizeof(content_rect));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "yai_release_dock_reservation: emit");
    app->dock_reserved_px = 0.0f;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_begin_shutdown(struct yai_app *app)
{
    if (app->shutting_down) {
        return YETTY_OK_VOID();
    }
    /* Best-effort: every step must run; first error surfaces at the
     * end. Never leave the CLI blocked on an unanswered permission. */
    struct yetty_ycore_void_result teardown = YETTY_OK_VOID();
    /* If the settings TUI is up (e.g. stdin EOF'd while it was open),
     * leave the alt screen so the user isn't stranded there. */
    if (app->config_tui_active) {
        app->config_tui_active = 0;
        fputs("\033[?1049l", stdout);
        teardown = yetty_ycore_void_chain(teardown, yai_render_flush_stdout());
    }
    if (app->pending_permission.active) {
        teardown =
            yetty_ycore_void_chain(teardown, yetty_yai_resolve_permission(app->engine, app, 0));
    }
    /* No animated glyph may survive the session — drop the pinned zone
     * for good before the teardown output. */
    teardown = yetty_ycore_void_chain(teardown, yai_renderer_pin_hide(&app->renderer));
    /* Restore terminal state NOW, while the pane connection is healthy:
     * stop mouse/resize OSC forwarding and drop the scroll-region
     * reservation. Doing it here (not only at the end of main) means
     * the host stops sending client-input envelopes immediately, so
     * they can't leak into the pane even if the handle drain below is
     * slow or the process is terminated before main's cleanup runs. */
    teardown = yetty_ycore_void_chain(teardown, unsubscribe_mouse(app));
    teardown = yetty_ycore_void_chain(teardown, yai_release_dock_reservation(app));
    teardown = yetty_ycore_void_chain(teardown, yai_renderer_text_hud_release(&app->renderer));
    /* Stop accepting / handling external control so it can't inject during
     * the wind-down; the handles drain with the loop below. */
    yai_control_stop(app);
    app->shutting_down = 1;
    /* The interop shell dies with the session (its handles drain below). */
    yai_shell_stop(app);
    uv_poll_stop(&app->stdin_poll);
    uv_signal_stop(&app->sigint_handle);
    uv_signal_stop(&app->sigwinch_handle);
    uv_signal_stop(&app->sigterm_handle);
    uv_signal_stop(&app->sighup_handle);
    uv_signal_stop(&app->sigchld_handle);
    if (app->btw_child_alive) {
        /* A /btw side child must not outlive the session — the specific
         * PID we spawned, killed directly (it has no stdin channel). */
        uv_process_kill(&app->btw_process, SIGKILL);
    }
    if (app->child_stdin_open) {
        app->child_stdin_open = 0;
        uv_close((uv_handle_t *)&app->child_stdin_pipe, yai_handle_closed_cb);
    }
    if (app->child_alive) {
        /* Closing stdin asks the CLI to exit; SIGKILL backstop later. */
        yai_arm_child_kill_timer(app);
    } else if (app->child_open_handles > 0) {
        /* A per-turn child already exited but its stdout is still
         * draining: let the close callbacks stop the loop once both
         * handles close, so the final partial line isn't dropped. */
    } else {
        uv_stop(&app->loop);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "yai_begin_shutdown");
    return YETTY_OK_VOID();
}

/* Engine-neutral turn boundary: render the failed state if the turn
 * failed, then pump the queue or re-prompt. */
struct yetty_ycore_void_result yai_turn_finished(struct yai_app *app)
{
    app->waiting = 0;
    if (app->btw_turn) {
        /* The /btw side turn is over: put the main conversation's identity
         * back BEFORE anything else — a queued message pumped below must
         * go to the MAIN session, not the side thread. */
        app->btw_turn = 0;
        memcpy(app->session_id, app->btw_session_stash, sizeof(app->session_id));
        app->resume_requested = app->btw_resume_stash;
    }
    struct yetty_ycore_void_result clear_res = yai_renderer_activity_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "yai_turn_finished: activity clear");
    if (app->turn_failed) {
        app->turn_failed = 0;
        /* The scrollback already carries the red ✗ line(s); the
         * persistent state surface must say failed too, not "idle". */
        struct yetty_ycore_void_result state_res = yai_set_state(app, "✗ turn failed");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "yai_turn_finished: hud state");
    }
    if (app->queue_len > 0) {
        /* Pump the next queued message instead of prompting. */
        char *queued = app->queue[0];
        memmove(&app->queue[0], &app->queue[1],
                sizeof(app->queue[0]) * (size_t)(app->queue_len - 1));
        app->queue_len--;
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "yai_turn_finished: suspend");
        printf("\n" YAI_DIM "(sending queued message)" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        if (YETTY_IS_ERR(flush_res)) {
            free(queued);
            return YETTY_ERR(yetty_ycore_void, "yai_turn_finished: flush", flush_res);
        }
        struct yetty_ycore_void_result input_res = handle_input_line(app, queued, strlen(queued));
        free(queued);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "yai_turn_finished: queued message");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result prompt_res = show_prompt(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "yai_turn_finished: prompt");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Line dispatch (terminal-focused typing)
 *---------------------------------------------------------------------------*/

/* Parse the engine's "KEY|label|opt1,opt2,…|current" knob spec into
 * app->config_knobs[knob_index] and the matching dialog knob slot.
 * `label_out` (size `label_size`) backs the dialog label. An empty spec
 * is a no-op (returns 0 knobs). Returns the number of knobs filled. */
static struct yetty_ycore_int_result parse_engine_knob(struct yai_app *app, int knob_index,
                                                       const char *spec, char *label_out,
                                                       size_t label_size,
                                                       struct yai_hud_config_knob *out)
{
    if (!spec[0]) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const char *label_start = strchr(spec, '|');
    const char *options_start = label_start ? strchr(label_start + 1, '|') : NULL;
    const char *current_start = options_start ? strchr(options_start + 1, '|') : NULL;
    if (!label_start || !options_start || !current_start) {
        return YETTY_ERR(yetty_ycore_int, "parse_engine_knob: malformed spec");
    }
    size_t key_len = (size_t)(label_start - spec);
    if (key_len == 0 || key_len >= sizeof(app->config_knobs[knob_index].key)) {
        return YETTY_ERR(yetty_ycore_int, "parse_engine_knob: key too long");
    }
    app->config_knobs[knob_index].is_edit_mode = 0;
    app->config_knobs[knob_index].is_model = 0;
    memcpy(app->config_knobs[knob_index].key, spec, key_len);
    app->config_knobs[knob_index].key[key_len] = '\0';
    size_t label_len = (size_t)(options_start - label_start - 1);
    if (label_len == 0 || label_len >= label_size) {
        return YETTY_ERR(yetty_ycore_int, "parse_engine_knob: label too long");
    }
    memcpy(label_out, label_start + 1, label_len);
    label_out[label_len] = '\0';

    const char *current_value = current_start + 1;
    const char *option_cursor = options_start + 1;
    int count = 0;
    int selected = -1;
    while (option_cursor < current_start && count < YAI_HUD_CONFIG_KNOB_MAX_OPTIONS) {
        const char *option_end =
            memchr(option_cursor, ',', (size_t)(current_start - option_cursor));
        if (!option_end) {
            option_end = current_start;
        }
        size_t option_len = (size_t)(option_end - option_cursor);
        if (option_len == 0 || option_len >= sizeof(app->config_knobs[knob_index].options[0])) {
            return YETTY_ERR(yetty_ycore_int, "parse_engine_knob: option too long");
        }
        memcpy(app->config_knobs[knob_index].options[count], option_cursor, option_len);
        app->config_knobs[knob_index].options[count][option_len] = '\0';
        if (strcmp(app->config_knobs[knob_index].options[count], current_value) == 0) {
            selected = count;
        }
        count++;
        option_cursor = option_end + 1;
    }
    if (count == 0) {
        return YETTY_ERR(yetty_ycore_int, "parse_engine_knob: no options");
    }
    if (selected < 0) {
        selected = 0;
    }
    app->config_knobs[knob_index].option_count = count;
    app->config_knobs[knob_index].selected = selected;
    out->label = label_out;
    for (int option = 0; option < count; option++) {
        out->options[option] = app->config_knobs[knob_index].options[option];
    }
    out->option_count = count;
    out->selected = selected;
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Fill knob 0: the yai edit-mode (emacs / vi) radio group, on tab `tab`. */
static void build_editmode_knob(struct yai_app *app, int tab, struct yai_hud_config_knob *out)
{
    app->config_knobs[0].is_edit_mode = 1;
    app->config_knobs[0].is_model = 0;
    app->config_knobs[0].key[0] = '\0';
    snprintf(app->config_knobs[0].options[0], sizeof(app->config_knobs[0].options[0]), "emacs");
    snprintf(app->config_knobs[0].options[1], sizeof(app->config_knobs[0].options[1]), "vi");
    app->config_knobs[0].option_count = 2;
    app->config_knobs[0].selected = (strcmp(app->editor_mode_name, "vi") == 0) ? 1 : 0;
    out->label = "edit mode";
    out->options[0] = app->config_knobs[0].options[0];
    out->options[1] = app->config_knobs[0].options[1];
    out->option_count = 2;
    out->selected = app->config_knobs[0].selected;
    out->tab = tab;
}

/* Fill knob 1: the submit-key policy radio group. "enter" = Enter submits,
 * Alt+Enter inserts a newline; "alt-enter" = the two swap. A generic knob
 * (keyed "submit-key"): the apply path stores it via yai_config_set, which
 * re-resolves app->enter_submits. */
static void build_submitkey_knob(struct yai_app *app, int tab, struct yai_hud_config_knob *out)
{
    app->config_knobs[1].is_edit_mode = 0;
    app->config_knobs[1].is_model = 0;
    snprintf(app->config_knobs[1].key, sizeof(app->config_knobs[1].key), "submit-key");
    snprintf(app->config_knobs[1].options[0], sizeof(app->config_knobs[1].options[0]), "enter");
    snprintf(app->config_knobs[1].options[1], sizeof(app->config_knobs[1].options[1]), "alt-enter");
    app->config_knobs[1].option_count = 2;
    app->config_knobs[1].selected = app->enter_submits ? 0 : 1;
    out->label = "submit key";
    out->options[0] = app->config_knobs[1].options[0];
    out->options[1] = app->config_knobs[1].options[1];
    out->option_count = 2;
    out->selected = app->config_knobs[1].selected;
    out->tab = tab;
}

/* Fill `out` (capacity `max`) with the active engine's model choices,
 * returning the count. For claude the list is whatever the CLI advertised
 * in its initialize response (never hardcoded — always the current set,
 * fable included); before that lands, or for codex/gemini, a small
 * built-in list is used. out[0] is always the "CLI default" entry (empty
 * value). */
static int build_model_choices(struct yai_app *app, struct yai_model_choice *out, int max)
{
    if (max <= 0) {
        return 0;
    }
    if (strcmp(app->engine_name, "claude") == 0 && app->claude_model_count > 0) {
        int count = app->claude_model_count < max ? app->claude_model_count : max;
        for (int index = 0; index < count; index++) {
            out[index] = app->claude_models[index];
        }
        return count;
    }
    struct builtin_model {
        const char *value;
        const char *name;
        const char *description;
    };
    static const struct builtin_model claude_builtin[] = {
        {"", "Default", "the CLI's configured default model"},
        {"opus", "Opus", ""},
        {"sonnet", "Sonnet", ""},
        {"haiku", "Haiku", ""},
    };
    static const struct builtin_model codex_builtin[] = {
        {"", "Default", "the CLI's configured default model"},
        {"gpt-5", "GPT-5", ""},
        {"gpt-5-codex", "GPT-5 Codex", ""},
        {"o3", "o3", ""},
    };
    static const struct builtin_model gemini_builtin[] = {
        {"", "Default", "the CLI's configured default model"},
        {"gemini-2.5-pro", "Gemini 2.5 Pro", ""},
        {"gemini-2.5-flash", "Gemini 2.5 Flash", ""},
    };
    const struct builtin_model *list = claude_builtin;
    int count = (int)(sizeof(claude_builtin) / sizeof(claude_builtin[0]));
    if (strcmp(app->engine_name, "codex") == 0) {
        list = codex_builtin;
        count = (int)(sizeof(codex_builtin) / sizeof(codex_builtin[0]));
    } else if (strcmp(app->engine_name, "gemini") == 0) {
        list = gemini_builtin;
        count = (int)(sizeof(gemini_builtin) / sizeof(gemini_builtin[0]));
    }
    if (count > max) {
        count = max;
    }
    for (int index = 0; index < count; index++) {
        snprintf(out[index].value, sizeof(out[index].value), "%s", list[index].value);
        snprintf(out[index].display_name, sizeof(out[index].display_name), "%s", list[index].name);
        snprintf(out[index].description, sizeof(out[index].description), "%s",
                 list[index].description);
    }
    return count;
}

/* Fill the model radio group for the active backend, on tab `tab`. The
 * options are the choice values (empty → "default"), drawn from
 * build_model_choices so the knob reflects the same list the /model
 * picker shows. */
static void build_model_knob(struct yai_app *app, int knob_index, int tab,
                             struct yai_hud_config_knob *out)
{
    struct yai_model_choice choices[YAI_MODEL_CHOICES_MAX];
    int count = build_model_choices(app, choices, YAI_MODEL_CHOICES_MAX);
    if (count > YAI_HUD_CONFIG_KNOB_MAX_OPTIONS) {
        count = YAI_HUD_CONFIG_KNOB_MAX_OPTIONS;
    }
    const char *current = yai_active_engine_config(app)->model;
    int selected = 0; /* "default" */
    app->config_knobs[knob_index].is_edit_mode = 0;
    app->config_knobs[knob_index].is_model = 1;
    snprintf(app->config_knobs[knob_index].key, sizeof(app->config_knobs[knob_index].key), "model");
    for (int option = 0; option < count; option++) {
        snprintf(app->config_knobs[knob_index].options[option],
                 sizeof(app->config_knobs[knob_index].options[option]), "%s",
                 choices[option].value[0] ? choices[option].value : "default");
        if (choices[option].value[0] && current[0] && strcmp(choices[option].value, current) == 0) {
            selected = option;
        }
        out->options[option] = app->config_knobs[knob_index].options[option];
    }
    app->config_knobs[knob_index].option_count = count;
    app->config_knobs[knob_index].selected = selected;
    out->label = "model";
    out->option_count = count;
    out->selected = selected;
    out->tab = tab;
}

/* Fill the reasoning-effort knob for the active backend, on tab `tab`.
 * Effort is a MODEL parameter, so it sits on the model tab next to the
 * model knob. Returns 1 if the engine has an effort control, 0 if not
 * (gemini). claude passes it as `--effort`; codex maps it to
 * `-c model_reasoning_effort` at spawn. Both read app->config. */
static int build_effort_knob(struct yai_app *app, int knob_index, int tab,
                             struct yai_hud_config_knob *out)
{
    static const char *const claude_efforts[] = {"auto", "low", "medium", "high", "xhigh", "max"};
    static const char *const codex_efforts[] = {"default", "minimal", "low", "medium", "high"};
    const char *const *list;
    int count;
    const char *key;
    const char *current;
    if (strcmp(app->engine_name, "codex") == 0) {
        list = codex_efforts;
        count = 5;
        key = "codex-effort";
    } else if (strcmp(app->engine_name, "claude") == 0) {
        list = claude_efforts;
        count = 6;
        key = "claude-effort";
    } else {
        return 0; /* gemini: no effort control */
    }
    current = yai_active_engine_config(app)->effort;
    if (count > YAI_HUD_CONFIG_KNOB_MAX_OPTIONS) {
        count = YAI_HUD_CONFIG_KNOB_MAX_OPTIONS;
    }
    int selected = 0;
    app->config_knobs[knob_index].is_edit_mode = 0;
    app->config_knobs[knob_index].is_model = 0;
    snprintf(app->config_knobs[knob_index].key, sizeof(app->config_knobs[knob_index].key), "%s",
             key);
    for (int option = 0; option < count; option++) {
        snprintf(app->config_knobs[knob_index].options[option],
                 sizeof(app->config_knobs[knob_index].options[option]), "%s", list[option]);
        if (strcmp(list[option], current) == 0) {
            selected = option;
        }
        out->options[option] = app->config_knobs[knob_index].options[option];
    }
    app->config_knobs[knob_index].option_count = count;
    app->config_knobs[knob_index].selected = selected;
    out->label = "effort";
    out->option_count = count;
    out->selected = selected;
    out->tab = tab;
    return 1;
}

/* The friendly backend page title (claude → "claude code"). */
static const char *backend_tab_title(const char *engine_name)
{
    if (strcmp(engine_name, "claude") == 0) {
        return "claude code";
    }
    return engine_name;
}

/* Format a quota reset epoch in the local timezone and the user's locale.
 * With include_date: "Mon DD, H:MMam/pm" (a reset days out); otherwise
 * "H:MMam/pm" (a reset later today). `out` is left "" when the epoch is
 * unknown (0). */
static void yai_format_reset_time(long long epoch, int include_date, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (epoch <= 0) {
        return;
    }
    time_t reset = (time_t)epoch;
    struct tm broken;
    if (!localtime_r(&reset, &broken)) {
        return;
    }
    int hour12 = broken.tm_hour % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    char ampm[8] = "";
    strftime(ampm, sizeof(ampm), "%P", &broken); /* locale lowercase am/pm (glibc) */
    if (!ampm[0]) {
        snprintf(ampm, sizeof(ampm), "%s", broken.tm_hour < 12 ? "am" : "pm");
    }
    if (include_date) {
        char month_day[16] = "";
        strftime(month_day, sizeof(month_day), "%b %e", &broken); /* locale month abbr */
        /* %e space-pads single-digit days; collapse the resulting double space. */
        char squeezed[16];
        size_t out_index = 0;
        for (size_t index = 0; month_day[index] && out_index + 1 < sizeof(squeezed); index++) {
            if (month_day[index] == ' ' && (out_index == 0 || squeezed[out_index - 1] == ' ')) {
                continue;
            }
            squeezed[out_index++] = month_day[index];
        }
        squeezed[out_index] = '\0';
        snprintf(out, out_size, "%s, %d:%02d%s", squeezed, hour12, broken.tm_min, ampm);
    } else {
        snprintf(out, out_size, "%d:%02d%s", hour12, broken.tm_min, ampm);
    }
}

/* The active engine's decomposed quota. codex's lives in its rollout file
 * (the model call never reaches the proxy); everyone else prefers the proxy's
 * header capture (the richest source when running) and falls back to the
 * engine-fed live quota (claude's rate_limit_event stream events). */
void yai_quota_get(struct yai_app *app, struct yai_quota *out)
{
    if (strcmp(app->engine_name, "codex") == 0) {
        if (!yai_codex_quota_read(app, out)) {
            memset(out, 0, sizeof(*out));
        }
        return;
    }
    yai_usage_proxy_quota(app, out);
    if (!out->valid && app->engine_quota.valid) {
        *out = app->engine_quota;
    }
}

/* Format one decomposed quota in the proxy's compact style —
 * "<util>% (<reset>) · <util>% (<reset>)" (5h clock, then weekly
 * clock+date) — skipping windows whose utilization is unknown (-1). Empty
 * when neither window has a percentage yet. */
static void yai_quota_format_summary(const struct yai_quota *quota, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    char session_part[48] = "";
    char week_part[48] = "";
    if (quota->session_pct >= 0) {
        char reset_5h[24] = "";
        yai_format_reset_time(quota->session_reset, /*include_date=*/0, reset_5h, sizeof(reset_5h));
        snprintf(session_part, sizeof(session_part), "%d%% (%s)", quota->session_pct, reset_5h);
    }
    if (quota->week_pct >= 0) {
        char reset_7d[24] = "";
        yai_format_reset_time(quota->week_reset, /*include_date=*/1, reset_7d, sizeof(reset_7d));
        snprintf(week_part, sizeof(week_part), "%d%% (%s)", quota->week_pct, reset_7d);
    }
    if (session_part[0] && week_part[0]) {
        snprintf(out, out_size, "%s · %s", session_part, week_part);
    } else if (session_part[0]) {
        snprintf(out, out_size, "%s", session_part);
    } else if (week_part[0]) {
        snprintf(out, out_size, "%s", week_part);
    }
}

/* The active engine's compact #{quota} one-liner: codex from its rollout
 * file, everyone else from the proxy when it has data, else the engine-fed
 * live quota. */
void yai_quota_summary(struct yai_app *app, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (strcmp(app->engine_name, "codex") != 0) {
        yai_usage_proxy_summary(app, out, out_size);
        if (!out[0] && app->engine_quota.valid) {
            yai_quota_format_summary(&app->engine_quota, out, out_size);
        }
        return;
    }
    struct yai_quota quota;
    if (!yai_codex_quota_read(app, &quota) || !quota.valid) {
        return;
    }
    yai_quota_format_summary(&quota, out, out_size);
}

/* Format one decomposed quota as the multi-line block for /usage. Windows
 * with an unknown utilization (-1) show their reset time only. */
static void yai_quota_format_status(const struct yai_quota *quota, const char *heading, char *out,
                                    size_t out_size)
{
    char reset_5h[24] = "";
    char reset_7d[24] = "";
    yai_format_reset_time(quota->session_reset, /*include_date=*/1, reset_5h, sizeof(reset_5h));
    yai_format_reset_time(quota->week_reset, /*include_date=*/1, reset_7d, sizeof(reset_7d));
    char session_line[64];
    char week_line[64];
    if (quota->session_pct >= 0) {
        snprintf(session_line, sizeof(session_line), "%d%% used  (resets %s)", quota->session_pct,
                 reset_5h);
    } else {
        snprintf(session_line, sizeof(session_line), "resets %s", reset_5h);
    }
    if (quota->week_pct >= 0) {
        snprintf(week_line, sizeof(week_line), "%d%% used  (resets %s)", quota->week_pct, reset_7d);
    } else {
        snprintf(week_line, sizeof(week_line), "resets %s", reset_7d);
    }
    snprintf(out, out_size,
             "%s\n"
             "  5h      %s\n"
             "  weekly  %s",
             heading, session_line, week_line);
}

/* The active engine's multi-line quota block for /usage. codex is formatted
 * from its rollout-sourced quota; everyone else uses the proxy's decoded
 * block when it has one, else the engine-fed live quota. */
static void yai_quota_status(struct yai_app *app, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (strcmp(app->engine_name, "codex") != 0) {
        yai_usage_proxy_status(app, out, out_size);
        if (!out[0] && app->engine_quota.valid) {
            yai_quota_format_status(&app->engine_quota, "plan quota", out, out_size);
        }
        return;
    }
    struct yai_quota quota;
    if (!yai_codex_quota_read(app, &quota) || !quota.valid) {
        return;
    }
    yai_quota_format_status(&quota, "codex plan quota", out, out_size);
}

/* Resolve every HUD format variable from current app state. The atomic
 * values come straight from usage / config / engine_name / the cached
 * state + quota + last-turn fields; the #{turn}/#{session}/#{stats}
 * composites reproduce the legacy strings so the default format reads well. */
static void yai_hud_collect_values(struct yai_app *app, struct yai_hud_var_values *out)
{
    memset(out, 0, sizeof(*out));
    const char *engine_model = yai_active_engine_config(app)->model;
    const char *model = engine_model[0] ? engine_model : "default";
    snprintf(out->value[YAI_HUD_VAR_STATE], YAI_HUD_VALUE_MAX, "%s", app->state_text);
    snprintf(out->value[YAI_HUD_VAR_ENGINE], YAI_HUD_VALUE_MAX, "%s", app->engine_name);
    snprintf(out->value[YAI_HUD_VAR_MODEL], YAI_HUD_VALUE_MAX, "%s", model);
    snprintf(out->value[YAI_HUD_VAR_TITLE], YAI_HUD_VALUE_MAX, "%s", app->session_title);
    snprintf(out->value[YAI_HUD_VAR_SESSION_ID], YAI_HUD_VALUE_MAX, "%s", app->session_id);
    yai_format_tokens(app->usage.input, out->value[YAI_HUD_VAR_INPUT], YAI_HUD_VALUE_MAX);
    yai_format_tokens(app->usage.output, out->value[YAI_HUD_VAR_OUTPUT], YAI_HUD_VALUE_MAX);
    yai_format_tokens(app->usage.cache_read, out->value[YAI_HUD_VAR_CACHE], YAI_HUD_VALUE_MAX);
    snprintf(out->value[YAI_HUD_VAR_COST], YAI_HUD_VALUE_MAX, "%.4f", app->usage.cost);
    snprintf(out->value[YAI_HUD_VAR_TURNS], YAI_HUD_VALUE_MAX, "%d", app->usage.turns);
    snprintf(out->value[YAI_HUD_VAR_QUOTA], YAI_HUD_VALUE_MAX, "%s", app->quota_text);
    /* Decomposed quota: per-window percentage + locale reset time. Left empty
     * until a source has it (claude: proxy headers; codex: rollout file). */
    struct yai_quota quota;
    yai_quota_get(app, &quota);
    if (quota.valid) {
        /* A window's utilization may be unknown (-1) while its reset is
         * already known (the engine-fed source) — leave that pct empty. */
        if (quota.session_pct >= 0) {
            snprintf(out->value[YAI_HUD_VAR_QUOTA_SESSION_PCT], YAI_HUD_VALUE_MAX, "%d",
                     quota.session_pct);
        }
        if (quota.week_pct >= 0) {
            snprintf(out->value[YAI_HUD_VAR_QUOTA_WEEK_PCT], YAI_HUD_VALUE_MAX, "%d",
                     quota.week_pct);
        }
        yai_format_reset_time(quota.session_reset, /*include_date=*/0,
                              out->value[YAI_HUD_VAR_QUOTA_SESSION_RESETS], YAI_HUD_VALUE_MAX);
        yai_format_reset_time(quota.week_reset, /*include_date=*/1,
                              out->value[YAI_HUD_VAR_QUOTA_WEEK_RESETS], YAI_HUD_VALUE_MAX);
    }
    yai_format_tokens(app->estimated_tokens, out->value[YAI_HUD_VAR_EST_TOKENS], YAI_HUD_VALUE_MAX);

    const struct yai_turn_usage *turn = &app->last_turn;
    double speed = turn->seconds > 0.0 ? (double)turn->output / turn->seconds : 0.0;
    yai_format_tokens(turn->input, out->value[YAI_HUD_VAR_TURN_INPUT], YAI_HUD_VALUE_MAX);
    yai_format_tokens(turn->output, out->value[YAI_HUD_VAR_TURN_OUTPUT], YAI_HUD_VALUE_MAX);
    yai_format_tokens(turn->cache_read, out->value[YAI_HUD_VAR_TURN_CACHE], YAI_HUD_VALUE_MAX);
    snprintf(out->value[YAI_HUD_VAR_TURN_COST], YAI_HUD_VALUE_MAX, "%.4f", turn->cost);
    snprintf(out->value[YAI_HUD_VAR_TURN_TIME], YAI_HUD_VALUE_MAX, "%.1f", turn->seconds);
    snprintf(out->value[YAI_HUD_VAR_TURN_SPEED], YAI_HUD_VALUE_MAX, "%.0f", speed);

    /* #{turn} — the per-turn line (matches event.c's dispatch_usage). */
    if (turn->valid) {
        char input_text[16];
        char cached_text[16];
        char output_text[16];
        yai_format_tokens(turn->input, input_text, sizeof(input_text));
        yai_format_tokens(turn->cache_read, cached_text, sizeof(cached_text));
        yai_format_tokens(turn->output, output_text, sizeof(output_text));
        char timing_text[48] = "";
        if (turn->seconds > 0.0) {
            snprintf(timing_text, sizeof(timing_text), " · %.1fs · %.0f tok/s", turn->seconds,
                     speed);
        }
        char cost_text[32] = "";
        if (turn->has_cost) {
            snprintf(cost_text, sizeof(cost_text), " · $%.4f", turn->cost);
        }
        snprintf(out->value[YAI_HUD_VAR_TURN], YAI_HUD_VALUE_MAX,
                 "↑%s in · %s cached · ↓%s out%s%s", input_text, cached_text, output_text,
                 timing_text, cost_text);
    } else {
        snprintf(out->value[YAI_HUD_VAR_TURN], YAI_HUD_VALUE_MAX, "waiting for first turn…");
    }

    /* #{session} — cumulative output + cost + turn count. */
    char session_output[16];
    yai_format_tokens(app->usage.output, session_output, sizeof(session_output));
    char session_cost[32] = "";
    if (app->usage.cost > 0.0) {
        snprintf(session_cost, sizeof(session_cost), " · $%.4f", app->usage.cost);
    }
    snprintf(out->value[YAI_HUD_VAR_SESSION], YAI_HUD_VALUE_MAX, "session: ↓%s out%s · %d turn(s)",
             session_output, session_cost, app->usage.turns);

    /* #{stats} — the compact Σ token totals. */
    char stat_input[16];
    char stat_output[16];
    yai_format_tokens(app->usage.input, stat_input, sizeof(stat_input));
    yai_format_tokens(app->usage.output, stat_output, sizeof(stat_output));
    snprintf(out->value[YAI_HUD_VAR_STATS], YAI_HUD_VALUE_MAX, "Σ ↑%s ↓%s", stat_input,
             stat_output);
}

/* Render the format's first row onto the text status bar (left/center/right
 * segments). The single-line text bar shows row 0 only; #[fg=...] becomes a
 * truecolor escape, embedded so it overrides the bar's own segment color. */
static struct yetty_ycore_void_result yai_text_hud_render(struct yai_app *app,
                                                          const struct yai_hud_var_values *values)
{
    const struct yai_hud_format *format = &app->hud_format;
    char cell[YAI_HUD_CELL_COUNT][320];
    for (int index = 0; index < YAI_HUD_CELL_COUNT; index++) {
        cell[index][0] = '\0';
    }
    struct yetty_ycore_rgba primary = {.r = 224, .g = 229, .b = 228, .a = 255};
    for (int index = 0; index < format->span_count; index++) {
        const struct yai_hud_format_span *span = &format->spans[index];
        if (span->row != 0) {
            continue;
        }
        char *destination = cell[span->cell];
        size_t used = strlen(destination);
        char escape[32];
        if (span->color == YAI_HUD_COLOR_DEFAULT) {
            snprintf(escape, sizeof(escape), "%s", YAI_FG_DEFAULT);
        } else {
            struct yetty_ycore_rgba rgba = yai_hud_format_span_rgba(span, primary);
            snprintf(escape, sizeof(escape), "\033[38;2;%u;%u;%um", (unsigned)rgba.r,
                     (unsigned)rgba.g, (unsigned)rgba.b);
        }
        char text[512];
        yai_hud_format_expand_span(span, values, text, sizeof(text));
        snprintf(destination + used, sizeof(cell[0]) - used, "%s%s", escape, text);
    }
    struct yetty_ycore_void_result res =
        yai_renderer_hud_state(&app->renderer, cell[YAI_HUD_CELL_LEFT]);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_text_hud_render: state");
    res = yai_renderer_hud_quota(&app->renderer, cell[YAI_HUD_CELL_CENTER]);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_text_hud_render: quota");
    res = yai_renderer_hud_stats(&app->renderer, cell[YAI_HUD_CELL_RIGHT]);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "yai_text_hud_render: stats");
    return YETTY_OK_VOID();
}

/* Re-collect the HUD variables and re-render the configured format to the
 * active backend (ygui HUD window, the text status bar, or nothing). */
struct yetty_ycore_void_result yai_refresh_hud_stats(struct yai_app *app)
{
    struct yai_hud_var_values values;
    yai_hud_collect_values(app, &values);
    /* A framework-only hud (present just for the ygui message-search) has
     * no status window — the text bar below is the active backend then. */
    if (yai_hud_has_window(app->hud)) {
        struct yetty_ycore_void_result render_res = yai_hud_render(app->hud, &values);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "yai_refresh_hud_stats: render");
        return yai_hud_flush(app->hud);
    }
    if (app->renderer.text_hud) {
        return yai_text_hud_render(app, &values);
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Text settings TUI — the non-yetty counterpart of the ygui config dialog.
 * A temporary alt-screen modal listing the same knobs (edit mode, the
 * engine's knob(s), model, effort) as navigable radio rows. Arrow keys or
 * j/k select a row; ←/→ or h/l (or space) change its value; Enter/q apply
 * & close; Ctrl-C cancels. Reuses app->config_knobs and apply_config_knob.
 *---------------------------------------------------------------------------*/

/* A human label for a knob (config_knobs has no label field; derive it
 * from the kind / config-field key — the same names the knob specs use). */
static const char *config_knob_label(const struct yai_app *app, int index)
{
    if (app->config_knobs[index].is_edit_mode) {
        return "edit mode";
    }
    if (app->config_knobs[index].is_model) {
        return "model";
    }
    const char *key = app->config_knobs[index].key;
    if (strcmp(key, "submit-key") == 0) {
        return "submit key";
    }
    if (strcmp(key, "permission-mode") == 0) {
        return "permission mode";
    }
    if (strcmp(key, "allowed-preset") == 0) {
        return "tools";
    }
    if (strcmp(key, "codex-sandbox") == 0) {
        return "sandbox";
    }
    if (strcmp(key, "codex-approval") == 0) {
        return "approval";
    }
    if (strcmp(key, "codex-effort") == 0 || strcmp(key, "claude-effort") == 0) {
        return "reasoning effort";
    }
    if (strcmp(key, "gemini-approval-mode") == 0) {
        return "approval mode";
    }
    return key;
}

/* Populate app->config_knobs for the text TUI (the same knobs the ygui
 * dialog builds, minus the ygui widget slots — the build helpers fill
 * config_knobs as a side effect; a throwaway slot absorbs the rest). */
static struct yetty_ycore_void_result build_config_knobs_text(struct yai_app *app)
{
    char knob_spec[512];
    struct yetty_ycore_void_result knob_res =
        yetty_yai_config_knob(app->engine, app, knob_spec, sizeof(knob_spec));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "build_config_knobs_text: knob spec");
    memset(app->config_knobs, 0, sizeof(app->config_knobs));
    struct yai_hud_config_knob throwaway;
    build_editmode_knob(app, 0, &throwaway);
    build_submitkey_knob(app, 0, &throwaway);
    int knob_index = 2;
    char engine_labels[YAI_HUD_CONFIG_MAX_KNOBS][64];
    const char *spec_cursor = knob_spec;
    while (*spec_cursor && knob_index < YAI_HUD_CONFIG_MAX_KNOBS - 2) {
        const char *line_end = strchr(spec_cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - spec_cursor) : strlen(spec_cursor);
        if (line_len > 0) {
            char one_spec[256];
            if (line_len >= sizeof(one_spec)) {
                line_len = sizeof(one_spec) - 1;
            }
            memcpy(one_spec, spec_cursor, line_len);
            one_spec[line_len] = '\0';
            struct yetty_ycore_int_result parse_res =
                parse_engine_knob(app, knob_index, one_spec, engine_labels[knob_index],
                                  sizeof(engine_labels[knob_index]), &throwaway);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, parse_res,
                                "build_config_knobs_text: engine knob");
            if (parse_res.value > 0) {
                knob_index++;
            }
        }
        if (!line_end) {
            break;
        }
        spec_cursor = line_end + 1;
    }
    build_model_knob(app, knob_index, 0, &throwaway);
    knob_index++;
    if (build_effort_knob(app, knob_index, 0, &throwaway)) {
        knob_index++;
    }
    app->config_knob_count = knob_index;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result config_tui_render(struct yai_app *app)
{
    fputs("\033[H\033[2J", stdout); /* cursor home + clear the alt screen */
    printf(YAI_MINT YAI_BOLD "  yai settings" YAI_RESET "  " YAI_DIM "(%s)" YAI_RESET "\n",
           app->engine_name);
    printf(YAI_DIM "  ↑/↓ or j/k: select   ←/→ or h/l: change   Enter/q: apply & close   "
                   "Ctrl-C: cancel" YAI_RESET "\n\n");
    for (int index = 0; index < app->config_knob_count; index++) {
        const char *marker = (index == app->config_tui_selected) ? YAI_MINT "▸" YAI_RESET : " ";
        printf("%s %-16s ", marker, config_knob_label(app, index));
        for (int option = 0; option < app->config_knobs[index].option_count; option++) {
            if (option == app->config_knobs[index].selected) {
                printf(YAI_MINT YAI_BOLD "[%s]" YAI_RESET " ",
                       app->config_knobs[index].options[option]);
            } else {
                printf(YAI_DIM "%s" YAI_RESET " ", app->config_knobs[index].options[option]);
            }
        }
        fputc('\n', stdout);
    }
    return yai_render_flush_stdout();
}

static struct yetty_ycore_void_result config_tui_close(struct yai_app *app, int apply)
{
    app->config_tui_active = 0;
    app->config_tui_escape = 0;
    /* Leave the alt screen, restoring the conversation underneath. */
    fputs("\033[?1049l", stdout);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "config_tui_close: leave alt screen");
    if (apply) {
        for (int index = 0; index < app->config_knob_count; index++) {
            if (app->config_knobs[index].selected != app->config_tui_initial[index]) {
                struct yetty_ycore_void_result apply_res = apply_config_knob(app, index);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "config_tui_close: apply");
            }
        }
        /* Persist the new config so it survives the next launch. Best-effort:
         * a save failure is reported but must not break the close flow. */
        yai_report_error(app, "config save", yai_config_save(app));
    }
    /* Re-reserve the text-HUD row, then repaint the prompt above it. */
    struct yetty_ycore_void_result reserve_res = yai_renderer_text_hud_reserve(&app->renderer, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "config_tui_close: text hud reserve");
    struct yetty_ycore_void_result show_res = yai_renderer_pin_show(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, show_res, "config_tui_close: pin show");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result config_tui_key(struct yai_app *app, unsigned char key)
{
    /* Decode arrow keys (ESC [ A/B/C/D) into h/j/k/l. */
    if (app->config_tui_escape == 1) {
        app->config_tui_escape = (key == '[') ? 2 : 0;
        return YETTY_OK_VOID();
    }
    if (app->config_tui_escape == 2) {
        app->config_tui_escape = 0;
        switch (key) {
        case 'A':
            key = 'k';
            break;
        case 'B':
            key = 'j';
            break;
        case 'C':
            key = 'l';
            break;
        case 'D':
            key = 'h';
            break;
        default:
            return YETTY_OK_VOID();
        }
    } else if (key == 0x1b) {
        app->config_tui_escape = 1;
        return YETTY_OK_VOID();
    }

    int count = app->config_knob_count;
    if (count <= 0) {
        return config_tui_close(app, 0);
    }
    int selected = app->config_tui_selected;
    switch (key) {
    case 'k':
        app->config_tui_selected = (selected + count - 1) % count;
        return config_tui_render(app);
    case 'j':
        app->config_tui_selected = (selected + 1) % count;
        return config_tui_render(app);
    case 'l':
    case ' ': {
        int options = app->config_knobs[selected].option_count;
        if (options > 0) {
            app->config_knobs[selected].selected =
                (app->config_knobs[selected].selected + 1) % options;
        }
        return config_tui_render(app);
    }
    case 'h': {
        int options = app->config_knobs[selected].option_count;
        if (options > 0) {
            app->config_knobs[selected].selected =
                (app->config_knobs[selected].selected + options - 1) % options;
        }
        return config_tui_render(app);
    }
    case '\r':
    case '\n':
    case 'q':
        return config_tui_close(app, 1);
    case 0x03: /* Ctrl-C: cancel without applying */
        return config_tui_close(app, 0);
    default:
        return YETTY_OK_VOID();
    }
}

static struct yetty_ycore_void_result config_tui_open(struct yai_app *app)
{
    struct yetty_ycore_void_result build_res = build_config_knobs_text(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, build_res, "config_tui_open: build knobs");
    for (int index = 0; index < app->config_knob_count; index++) {
        app->config_tui_initial[index] = app->config_knobs[index].selected;
    }
    app->config_tui_selected = 0;
    app->config_tui_escape = 0;
    /* Drop the pinned prompt and the text-HUD scroll region, then take
     * the full alt screen. */
    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "config_tui_open: pin hide");
    struct yetty_ycore_void_result release_res = yai_renderer_text_hud_release(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, release_res, "config_tui_open: text hud release");
    fputs("\033[?1049h", stdout);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "config_tui_open: enter alt screen");
    app->config_tui_active = 1;
    return config_tui_render(app);
}

/*---------------------------------------------------------------------------
 * Model picker (/model) — a focused, Claude-Code-style model selector: one
 * list of the engine's models with friendly names + descriptions, not the
 * multi-tab settings dialog. For claude the list is queried live from the
 * CLI (never hardcoded); other engines use a built-in list. On a real tty
 * it is an alt-screen modal (↑/↓ or j/k select, Enter picks, Esc/q cancel);
 * on a pipe it prints the list to the scrollback.
 *---------------------------------------------------------------------------*/

/* Apply the picked model: store it, push it live (claude: set_model),
 * persist, and refresh the HUD. `value` "" clears the override (CLI
 * default). */
static struct yetty_ycore_void_result model_picker_apply(struct yai_app *app, const char *value)
{
    yai_config_set(app, "model", value);
    struct yetty_ycore_void_result push_res =
        yetty_yai_apply_config(app->engine, app, "model", value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "model_picker_apply: live push");
    yai_report_error(app, "config save", yai_config_save(app));
    struct yetty_ycore_void_result stats_res = yai_refresh_hud_stats(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stats_res, "model_picker_apply: hud refresh");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result model_picker_render(struct yai_app *app)
{
    fputs("\033[H\033[2J", stdout); /* cursor home + clear the alt screen */
    printf(YAI_MINT YAI_BOLD "  select a model" YAI_RESET "  " YAI_DIM "(%s)" YAI_RESET "\n",
           app->engine_name);
    printf(YAI_DIM "  ↑/↓ or j/k: select   Enter: use   Esc/q: cancel" YAI_RESET "\n\n");
    for (int index = 0; index < app->model_picker_count; index++) {
        const struct yai_model_choice *choice = &app->model_picker_choices[index];
        int active = (index == app->model_picker_selected);
        printf("%s ", active ? YAI_MINT "▸" YAI_RESET : " ");
        if (active) {
            printf(YAI_MINT YAI_BOLD "%s" YAI_RESET, choice->display_name);
        } else {
            printf(YAI_BOLD "%s" YAI_RESET, choice->display_name);
        }
        if (choice->description[0]) {
            printf("  " YAI_DIM "%s" YAI_RESET, choice->description);
        }
        fputc('\n', stdout);
    }
    return yai_render_flush_stdout();
}

static struct yetty_ycore_void_result model_picker_close(struct yai_app *app, int apply_index)
{
    app->model_picker_active = 0;
    app->model_picker_escape = 0;
    fputs("\033[?1049l", stdout); /* leave the alt screen */
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "model_picker_close: leave alt screen");
    struct yetty_ycore_void_result apply_res = YETTY_OK_VOID();
    if (apply_index >= 0 && apply_index < app->model_picker_count) {
        const struct yai_model_choice *choice = &app->model_picker_choices[apply_index];
        apply_res = model_picker_apply(app, choice->value);
        if (!YETTY_IS_ERR(apply_res)) {
            /* Confirm the choice in the scrollback (Claude-Code-style). */
            printf(YAI_DIM "(model: %s)" YAI_RESET "\n",
                   choice->value[0] ? choice->value : "default");
        }
    }
    /* Re-reserve the text-HUD row, then repaint the prompt above it. */
    struct yetty_ycore_void_result reserve_res = yai_renderer_text_hud_reserve(&app->renderer, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "model_picker_close: text hud reserve");
    struct yetty_ycore_void_result show_res = yai_renderer_pin_show(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, show_res, "model_picker_close: pin show");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "model_picker_close: apply");
    return YETTY_OK_VOID();
}

/* One keystroke while the picker modal is up: ↑/↓ or j/k move, Enter
 * picks, q / Ctrl-C cancel. Arrow keys arrive as ESC [ A/B whose bytes may
 * be split across reads, so decode them with a per-byte state machine (the
 * same split-tolerant approach as the config TUI) rather than lookahead. A
 * lone ESC is indistinguishable from the start of an arrow without a timer,
 * so — like the config TUI — it is not a cancel; use q / Ctrl-C. */
static struct yetty_ycore_void_result model_picker_key(struct yai_app *app, unsigned char key)
{
    if (app->model_picker_escape == 1) {
        app->model_picker_escape = (key == '[') ? 2 : 0;
        return YETTY_OK_VOID();
    }
    if (app->model_picker_escape == 2) {
        app->model_picker_escape = 0;
        if (key == 'A') {
            key = 'k';
        } else if (key == 'B') {
            key = 'j';
        } else {
            return YETTY_OK_VOID();
        }
    } else if (key == 0x1b) {
        app->model_picker_escape = 1;
        return YETTY_OK_VOID();
    }

    int count = app->model_picker_count;
    if (count <= 0) {
        return model_picker_close(app, -1);
    }
    switch (key) {
    case 'k':
        app->model_picker_selected = (app->model_picker_selected + count - 1) % count;
        return model_picker_render(app);
    case 'j':
        app->model_picker_selected = (app->model_picker_selected + 1) % count;
        return model_picker_render(app);
    case '\r':
    case '\n':
        return model_picker_close(app, app->model_picker_selected);
    case 'q':
    case 0x03: /* Ctrl-C: cancel */
        return model_picker_close(app, -1);
    default:
        return YETTY_OK_VOID();
    }
}

/* Open the /model picker. On a pipe (no tty) it just prints the list; with
 * an explicit `preset` (from `/model <name>`) it applies directly without
 * the modal. */
static struct yetty_ycore_void_result model_picker_open(struct yai_app *app, const char *preset,
                                                        size_t preset_len)
{
    app->model_picker_count =
        build_model_choices(app, app->model_picker_choices, YAI_MODEL_CHOICES_MAX);
    const char *current = yai_active_engine_config(app)->model;

    /* `/model <name>`: match the arg against a choice value or display
     * name (case-insensitively); an unmatched arg is used verbatim as a
     * raw model id, mirroring the CLI's --model. */
    if (preset && preset_len > 0) {
        char arg[96];
        if (preset_len >= sizeof(arg)) {
            preset_len = sizeof(arg) - 1;
        }
        memcpy(arg, preset, preset_len);
        arg[preset_len] = '\0';
        const char *chosen = arg;
        for (int index = 0; index < app->model_picker_count; index++) {
            const struct yai_model_choice *choice = &app->model_picker_choices[index];
            if (strcasecmp(arg, choice->display_name) == 0 ||
                (choice->value[0] && strcasecmp(arg, choice->value) == 0) ||
                (!choice->value[0] && strcasecmp(arg, "default") == 0)) {
                chosen = choice->value;
                break;
            }
        }
        if (strcasecmp(arg, "default") == 0) {
            chosen = "";
        }
        struct yetty_ycore_void_result apply_res = model_picker_apply(app, chosen);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "model_picker_open: preset apply");
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "model_picker_open: preset suspend");
        printf(YAI_DIM "(model: %s)" YAI_RESET "\n", chosen[0] ? chosen : "default");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "model_picker_open: preset flush");
        return YETTY_OK_VOID();
    }

    /* Initial highlight: the row matching the active model (else default). */
    app->model_picker_selected = 0;
    for (int index = 0; index < app->model_picker_count; index++) {
        const struct yai_model_choice *choice = &app->model_picker_choices[index];
        if (current[0] ? (choice->value[0] && strcmp(choice->value, current) == 0)
                       : !choice->value[0]) {
            app->model_picker_selected = index;
            break;
        }
    }

    /* No tty (a pipe): print the list to the scrollback and return. */
    if (!app->renderer.pin_enabled) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "model_picker_open: suspend");
        printf("\n" YAI_MINT "models" YAI_RESET " (%s)\n", app->engine_name);
        for (int index = 0; index < app->model_picker_count; index++) {
            const struct yai_model_choice *choice = &app->model_picker_choices[index];
            const char *marker = (index == app->model_picker_selected) ? "▸" : " ";
            printf("  %s " YAI_BOLD "%s" YAI_RESET, marker, choice->display_name);
            if (choice->description[0]) {
                printf("  " YAI_DIM "%s" YAI_RESET, choice->description);
            }
            fputc('\n', stdout);
        }
        printf(YAI_DIM "  set with /model <name>" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "model_picker_open: flush");
        return YETTY_OK_VOID();
    }

    /* Real tty: an alt-screen modal (works with or without the ygui HUD;
     * the floating HUD is unaffected). */
    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "model_picker_open: pin hide");
    struct yetty_ycore_void_result release_res = yai_renderer_text_hud_release(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, release_res, "model_picker_open: text hud release");
    fputs("\033[?1049h", stdout);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "model_picker_open: enter alt screen");
    app->model_picker_active = 1;
    app->model_picker_escape = 0;
    return model_picker_render(app);
}

/*---------------------------------------------------------------------------
 * /agents — a picker over the background/interactive agents the CLI knows
 * about (`claude agents --json`), mirroring Claude Code's agents view. Same
 * focused modal as /model; Enter shows the selected agent's detail + the
 * exact command to resume it (yai has no in-place session swap, so it does
 * not hijack the running conversation).
 *---------------------------------------------------------------------------*/

/* Run argv (NULL-terminated) and capture up to out_cap-1 bytes of its
 * stdout into out (always NUL-terminated); stderr/stdin are /dev/null.
 * Waits at most timeout_ms, killing the child if it overruns. Returns 0 on
 * a clean exit, -1 otherwise. Briefly blocks the event loop — only for
 * quick local queries (claude agents --json ≈ 0.35s). */
static int yai_capture_command(const char *const argv[], char *out, size_t out_cap, int timeout_ms)
{
    if (out_cap == 0) {
        return -1;
    }
    out[0] = '\0';
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return -1;
    }
    pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
        if (pipe_fds[1] > STDERR_FILENO) {
            close(pipe_fds[1]);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(pipe_fds[1]);
    int flags = fcntl(pipe_fds[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    size_t total = 0;
    int clean = 0;
    int timed_out = 0;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms =
            (now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = timeout_ms - elapsed_ms;
        if (remaining <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd waiter = {.fd = pipe_fds[0], .events = POLLIN};
        int ready = poll(&waiter, 1, (int)remaining);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            timed_out = 1;
            break;
        }
        ssize_t got = read(pipe_fds[0], out + total, out_cap - 1 - total);
        if (got > 0) {
            total += (size_t)got;
            if (total >= out_cap - 1) {
                clean = 1; /* buffer full — stop; child gets SIGPIPE below */
                break;
            }
            continue;
        }
        if (got == 0) {
            clean = 1; /* EOF */
            break;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        break;
    }
    out[total] = '\0';
    close(pipe_fds[0]);
    if (timed_out) {
        kill(child, SIGKILL);
    }
    waitpid(child, NULL, 0);
    return clean ? 0 : -1;
}

/* Query the CLI for its agents into app->agents. Returns the count, or -1
 * if the command failed / its output was not parseable. */
static int agents_load(struct yai_app *app)
{
    app->agents_count = 0;
    char buffer[65536];
    const char *argv[] = {"claude", "agents", "--json", NULL};
    if (yai_capture_command(argv, buffer, sizeof(buffer), 3000) != 0) {
        return -1;
    }
    yyjson_doc *doc = yyjson_read(buffer, strlen(buffer), 0);
    if (!doc) {
        return -1;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (yyjson_is_arr(root)) {
        yyjson_val *entry;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(root, &iter);
        while ((entry = yyjson_arr_iter_next(&iter)) != NULL &&
               app->agents_count < YAI_AGENTS_MAX) {
            struct yai_agent_entry *agent = &app->agents[app->agents_count];
            const char *session_id = yyjson_get_str(yyjson_obj_get(entry, "sessionId"));
            snprintf(agent->session_id, sizeof(agent->session_id), "%s",
                     session_id ? session_id : "");
            const char *name = yyjson_get_str(yyjson_obj_get(entry, "name"));
            const char *id = yyjson_get_str(yyjson_obj_get(entry, "id"));
            snprintf(agent->name, sizeof(agent->name), "%s", name ? name : (id ? id : "(session)"));
            const char *state = yyjson_get_str(yyjson_obj_get(entry, "state"));
            snprintf(agent->state, sizeof(agent->state), "%s", state ? state : "");
            const char *kind = yyjson_get_str(yyjson_obj_get(entry, "kind"));
            snprintf(agent->kind, sizeof(agent->kind), "%s", kind ? kind : "");
            const char *cwd = yyjson_get_str(yyjson_obj_get(entry, "cwd"));
            snprintf(agent->cwd, sizeof(agent->cwd), "%s", cwd ? cwd : "");
            app->agents_count++;
        }
    }
    yyjson_doc_free(doc);
    return app->agents_count;
}

/* "background · blocked" / "interactive" — the kind+state descriptor. */
static void agents_status_text(const struct yai_agent_entry *agent, char *out, size_t out_size)
{
    if (agent->state[0]) {
        snprintf(out, out_size, "%s · %s", agent->kind[0] ? agent->kind : "?", agent->state);
    } else {
        snprintf(out, out_size, "%s", agent->kind[0] ? agent->kind : "?");
    }
}

static struct yetty_ycore_void_result agents_picker_render(struct yai_app *app)
{
    fputs("\033[H\033[2J", stdout); /* cursor home + clear the alt screen */
    printf(YAI_MINT YAI_BOLD "  agents" YAI_RESET "  " YAI_DIM "(%d running)" YAI_RESET "\n",
           app->agents_count);
    printf(YAI_DIM "  ↑/↓ or j/k: select   Enter: show resume command   Esc/q: cancel" YAI_RESET
                   "\n\n");
    for (int index = 0; index < app->agents_count; index++) {
        const struct yai_agent_entry *agent = &app->agents[index];
        int active = (index == app->agents_picker_selected);
        char status[48];
        agents_status_text(agent, status, sizeof(status));
        printf("%s ", active ? YAI_MINT "▸" YAI_RESET : " ");
        if (active) {
            printf(YAI_MINT YAI_BOLD "%s" YAI_RESET, agent->name);
        } else {
            printf(YAI_BOLD "%s" YAI_RESET, agent->name);
        }
        printf("  " YAI_DIM "%s" YAI_RESET, status);
        if (agent->cwd[0]) {
            printf("  " YAI_DIM "%s" YAI_RESET, agent->cwd);
        }
        fputc('\n', stdout);
    }
    return yai_render_flush_stdout();
}

static struct yetty_ycore_void_result agents_picker_close(struct yai_app *app, int show_index)
{
    app->agents_picker_active = 0;
    app->agents_picker_escape = 0;
    fputs("\033[?1049l", stdout); /* leave the alt screen */
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "agents_picker_close: leave alt screen");
    if (show_index >= 0 && show_index < app->agents_count) {
        const struct yai_agent_entry *agent = &app->agents[show_index];
        char status[48];
        agents_status_text(agent, status, sizeof(status));
        printf("\n" YAI_MINT "agent" YAI_RESET " " YAI_BOLD "%s" YAI_RESET "\n", agent->name);
        printf("  " YAI_DIM "status : %s" YAI_RESET "\n", status);
        if (agent->cwd[0]) {
            printf("  " YAI_DIM "cwd    : %s" YAI_RESET "\n", agent->cwd);
        }
        if (agent->session_id[0]) {
            printf("  " YAI_DIM "session: %s" YAI_RESET "\n", agent->session_id);
            printf("  " YAI_DIM "resume : " YAI_RESET YAI_MINT
                   "yai --engine %s --resume %s" YAI_RESET "\n",
                   app->engine_name, agent->session_id);
        }
    }
    struct yetty_ycore_void_result reserve_res = yai_renderer_text_hud_reserve(&app->renderer, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "agents_picker_close: text hud reserve");
    struct yetty_ycore_void_result show_res = yai_renderer_pin_show(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, show_res, "agents_picker_close: pin show");
    return YETTY_OK_VOID();
}

/* One keystroke while the /agents modal is up — same split-tolerant
 * arrow decode + keys as the /model picker. */
static struct yetty_ycore_void_result agents_picker_key(struct yai_app *app, unsigned char key)
{
    if (app->agents_picker_escape == 1) {
        app->agents_picker_escape = (key == '[') ? 2 : 0;
        return YETTY_OK_VOID();
    }
    if (app->agents_picker_escape == 2) {
        app->agents_picker_escape = 0;
        if (key == 'A') {
            key = 'k';
        } else if (key == 'B') {
            key = 'j';
        } else {
            return YETTY_OK_VOID();
        }
    } else if (key == 0x1b) {
        app->agents_picker_escape = 1;
        return YETTY_OK_VOID();
    }

    int count = app->agents_count;
    if (count <= 0) {
        return agents_picker_close(app, -1);
    }
    switch (key) {
    case 'k':
        app->agents_picker_selected = (app->agents_picker_selected + count - 1) % count;
        return agents_picker_render(app);
    case 'j':
        app->agents_picker_selected = (app->agents_picker_selected + 1) % count;
        return agents_picker_render(app);
    case '\r':
    case '\n':
        return agents_picker_close(app, app->agents_picker_selected);
    case 'q':
    case 0x03: /* Ctrl-C: cancel */
        return agents_picker_close(app, -1);
    default:
        return YETTY_OK_VOID();
    }
}

/* Emit a one-line note into the scrollback (used by /agents for the
 * empty / error / unsupported cases). */
static struct yetty_ycore_void_result yai_scrollback_note(struct yai_app *app, const char *text)
{
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "scrollback note: suspend");
    printf(YAI_DIM "%s" YAI_RESET "\n", text);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "scrollback note: flush");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result agents_picker_open(struct yai_app *app)
{
    /* `claude agents` is claude-specific. */
    if (strcmp(app->engine_name, "claude") != 0) {
        return yai_scrollback_note(app, "/agents needs the claude engine");
    }
    int count = agents_load(app);
    if (count < 0) {
        return yai_scrollback_note(app, "/agents: could not query agents (claude agents --json)");
    }
    if (count == 0) {
        return yai_scrollback_note(app, "no background agents running");
    }
    app->agents_picker_selected = 0;

    /* No tty (a pipe): print the list to the scrollback and return. */
    if (!app->renderer.pin_enabled) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "agents_picker_open: suspend");
        printf("\n" YAI_MINT "agents" YAI_RESET " (%d)\n", count);
        for (int index = 0; index < count; index++) {
            const struct yai_agent_entry *agent = &app->agents[index];
            char status[48];
            agents_status_text(agent, status, sizeof(status));
            printf("  " YAI_BOLD "%s" YAI_RESET "  " YAI_DIM "%s" YAI_RESET, agent->name, status);
            if (agent->session_id[0]) {
                printf("  " YAI_DIM "%s" YAI_RESET, agent->session_id);
            }
            fputc('\n', stdout);
        }
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "agents_picker_open: flush");
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "agents_picker_open: pin hide");
    struct yetty_ycore_void_result release_res = yai_renderer_text_hud_release(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, release_res, "agents_picker_open: text hud release");
    fputs("\033[?1049h", stdout);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "agents_picker_open: enter alt screen");
    app->agents_picker_active = 1;
    app->agents_picker_escape = 0;
    return agents_picker_render(app);
}

/* /config — an EDITABLE floating ygui dialog when the HUD is up (a
 * second /config toggles it closed): read-only info rows, a
 * show-thinking checkbox, a fold-lines slider, the yai edit-mode knob
 * (emacs / vi), and the engine's knob as a radio group (codex sandbox,
 * claude permission mode, gemini approval mode). On a non-yetty tty an
 * alt-screen text TUI (config_tui_open); plain scrollback text on a pipe.
 * Runnable any time, mid-turn included. */
static struct yetty_ycore_void_result show_config(struct yai_app *app)
{
    char engine_rows[1024];
    struct yetty_ycore_void_result describe_res =
        yetty_yai_describe_config(app->engine, app, engine_rows, sizeof(engine_rows));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, describe_res, "show_config: engine rows");

    /* The ygui config dialog follows the HUD visual: only when the ygui HUD
     * window exists. A framework-only hud (present just for the ygui
     * message-search) keeps /config on the text TUI below. */
    if (yai_hud_has_window(app->hud)) {
        /* Holds the engine's knob specs — several newline-separated lines
         * for an engine that exposes multiple knobs (claude: 3). */
        char knob_spec[512];
        struct yetty_ycore_void_result knob_res =
            yetty_yai_config_knob(app->engine, app, knob_spec, sizeof(knob_spec));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "show_config: knob spec");

        /* Reset the apply-side knob mapping; show_config fully rebuilds it. */
        memset(app->config_knobs, 0, sizeof(app->config_knobs));

        /* Tabs: 0 yai · 1 backend · 2 model · 3 stats. */
        enum { TAB_YAI = 0, TAB_BACKEND = 1, TAB_MODEL = 2, TAB_STATS = 3 };

        char yai_info[512];
        int yai_written = snprintf(
            yai_info, sizeof(yai_info),
            "engine: %s · session: %s\n"
            "transcript: %s\n"
            "edit mode: %s · turn in flight: %s · queued %d/%d",
            app->engine_name, app->session_id[0] ? app->session_id : "(none yet)",
            app->transcript_file ? app->transcript_path : "(disabled)", app->editor_mode_name,
            app->waiting ? "yes" : "no", app->queue_len, YAI_QUEUE_MAX);
        if (yai_written < 0 || (size_t)yai_written >= sizeof(yai_info)) {
            return YETTY_ERR(yetty_ycore_void, "show_config: yai info truncated");
        }

        const char *active_model = yai_active_engine_config(app)->model;
        const char *current_model = active_model[0] ? active_model : "(CLI default)";
        char model_info[256];
        int model_written = snprintf(model_info, sizeof(model_info),
                                     "current: %s\n"
                                     "sets the model · applies next turn (claude: next session)",
                                     current_model);
        if (model_written < 0 || (size_t)model_written >= sizeof(model_info)) {
            return YETTY_ERR(yetty_ycore_void, "show_config: model info truncated");
        }

        char input_text[16];
        char output_text[16];
        char cache_read_text[16];
        char cache_write_text[16];
        yai_format_tokens(app->usage.input, input_text, sizeof(input_text));
        yai_format_tokens(app->usage.output, output_text, sizeof(output_text));
        yai_format_tokens(app->usage.cache_read, cache_read_text, sizeof(cache_read_text));
        yai_format_tokens(app->usage.cache_creation, cache_write_text, sizeof(cache_write_text));
        char stats_info[384];
        int stats_written = snprintf(stats_info, sizeof(stats_info),
                                     "turns: %d\n"
                                     "input: %s · output: %s\n"
                                     "cache read: %s · cache write: %s\n"
                                     "cost: $%.4f",
                                     app->usage.turns, input_text, output_text, cache_read_text,
                                     cache_write_text, app->usage.cost);
        if (stats_written < 0 || (size_t)stats_written >= sizeof(stats_info)) {
            return YETTY_ERR(yetty_ycore_void, "show_config: stats info truncated");
        }

        struct yai_hud_config_setup setup = {0};
        setup.tab_count = 4;
        setup.tab_titles[TAB_YAI] = "yai";
        setup.tab_titles[TAB_BACKEND] = backend_tab_title(app->engine_name);
        setup.tab_titles[TAB_MODEL] = "model";
        setup.tab_titles[TAB_STATS] = "stats";
        setup.tab_info[TAB_YAI] = yai_info;
        setup.tab_info[TAB_BACKEND] = engine_rows;
        setup.tab_info[TAB_MODEL] = model_info;
        setup.tab_info[TAB_STATS] = stats_info;
        setup.controls_tab = TAB_YAI;
        setup.show_thinking = app->renderer.show_thinking;
        setup.fold_lines = (float)app->renderer.fold_lines;

        /* knob 0 = edit mode (yai tab); knobs 1..N = the engine's knobs
         * (backend tab; config_knob may return several, newline-separated);
         * the last knob = model (model tab). */
        build_editmode_knob(app, TAB_YAI, &setup.knobs[0]);
        build_submitkey_knob(app, TAB_YAI, &setup.knobs[1]);
        int knob_index = 2;
        char engine_labels[YAI_HUD_CONFIG_MAX_KNOBS][64];
        const char *spec_cursor = knob_spec;
        /* Reserve the last two slots for the model tab's model + effort knobs. */
        while (*spec_cursor && knob_index < YAI_HUD_CONFIG_MAX_KNOBS - 2) {
            const char *line_end = strchr(spec_cursor, '\n');
            size_t line_len = line_end ? (size_t)(line_end - spec_cursor) : strlen(spec_cursor);
            if (line_len > 0) {
                char one_spec[256];
                if (line_len >= sizeof(one_spec)) {
                    line_len = sizeof(one_spec) - 1;
                }
                memcpy(one_spec, spec_cursor, line_len);
                one_spec[line_len] = '\0';
                struct yetty_ycore_int_result knob_res =
                    parse_engine_knob(app, knob_index, one_spec, engine_labels[knob_index],
                                      sizeof(engine_labels[knob_index]), &setup.knobs[knob_index]);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "show_config: engine knob");
                if (knob_res.value > 0) {
                    setup.knobs[knob_index].tab = TAB_BACKEND;
                    knob_index++;
                }
            }
            if (!line_end) {
                break;
            }
            spec_cursor = line_end + 1;
        }
        build_model_knob(app, knob_index, TAB_MODEL, &setup.knobs[knob_index]);
        knob_index++;
        /* Effort is a model parameter — same tab, right under the model. */
        if (build_effort_knob(app, knob_index, TAB_MODEL, &setup.knobs[knob_index])) {
            knob_index++;
        }
        setup.knob_count = knob_index;
        app->config_knob_count = knob_index;
        app->config_show_thinking_applied = app->renderer.show_thinking;
        app->config_fold_lines_applied = app->renderer.fold_lines;

        struct yetty_ycore_void_result toggle_res = yai_hud_toggle_config(app->hud, &setup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "show_config: dialog");
        return YETTY_OK_VOID();
    }

    /* Non-yetty terminal but a real tty: interactive alt-screen text TUI
     * (toggles closed if already open). */
    if (app->renderer.pin_enabled) {
        if (app->config_tui_active) {
            return config_tui_close(app, 1);
        }
        return config_tui_open(app);
    }

    char config_text[2048];
    int written = snprintf(config_text, sizeof(config_text),
                           "## yai\n"
                           "engine: %s  [--engine]\n"
                           "session: %s\n"
                           "transcript: %s\n"
                           "fold lines: %d  [--fold-lines]\n"
                           "show thinking: %s  [--show-thinking]\n"
                           "markdown: %s  [--markdown/--no-markdown]\n"
                           "hud: off  [--no-hud]\n"
                           "turn in flight: %s · queued %d/%d\n"
                           "## %s\n"
                           "%s",
                           app->engine_name, app->session_id[0] ? app->session_id : "(none yet)",
                           app->transcript_file ? app->transcript_path : "(disabled)",
                           app->renderer.fold_lines, app->renderer.show_thinking ? "on" : "off",
                           yai_effective_markdown_mode(app), app->waiting ? "yes" : "no",
                           app->queue_len, YAI_QUEUE_MAX, app->engine_name, engine_rows);
    if (written < 0 || (size_t)written >= sizeof(config_text)) {
        return YETTY_ERR(yetty_ycore_void, "show_config: config text truncated");
    }

    /* No HUD (plain terminal / pipe): scrollback fallback. */
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "show_config: suspend");
    const char *cursor = config_text;
    while (*cursor) {
        const char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);
        if (line_len > 3 && strncmp(cursor, "## ", 3) == 0) {
            printf("\n" YAI_MUTED "⚙ " YAI_BOLD "%.*s config" YAI_RESET "\n", (int)(line_len - 3),
                   cursor + 3);
        } else {
            printf(YAI_DIM "  %.*s" YAI_RESET "\n", (int)line_len, cursor);
        }
        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "show_config: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "show_config: resume");
    return YETTY_OK_VOID();
}

/* Case-insensitive exact match of a (trimmed) input line against a
 * lowercase keyword — used for the permission verdict words. */
static int verdict_is(const char *line, size_t len, const char *word)
{
    size_t index = 0;
    for (; index < len && word[index]; index++) {
        char typed = line[index];
        if (typed >= 'A' && typed <= 'Z') {
            typed = (char)(typed - 'A' + 'a');
        }
        if (typed != word[index]) {
            return 0;
        }
    }
    return index == len && word[index] == '\0';
}

/* Number of points sampled along the /usage plot's time axis. */
#define YAI_USAGE_PLOT_GRID 96

/* Draw the session's tokens-consumed-over-time curve below the /usage
 * text (yetty mode only). The recorded samples are one-per-turn and thus
 * irregularly spaced in time, but a data-buffer plot spreads its samples
 * evenly across the x axis — so first project the per-turn cumulative
 * totals onto a uniform time grid (a right-continuous step; the shader
 * lerps between grid points). A no-op off the yetty host or before two
 * turns have landed (a single point is not a curve). */
static struct yetty_ycore_void_result usage_render_plot(struct yai_app *app)
{
    if (!app->renderer.pin_shader_glyphs || app->usage_sample_count < 2) {
        return YETTY_OK_VOID();
    }
    const struct yai_usage_sample *samples = app->usage_samples;
    size_t sample_count = app->usage_sample_count;
    double span_seconds = samples[sample_count - 1].elapsed_seconds;
    /* Cumulative totals are monotonic, so the last sample is the peak. */
    uint64_t max_tokens = samples[sample_count - 1].total_tokens;
    if (span_seconds <= 0.0 || max_tokens == 0) {
        return YETTY_OK_VOID();
    }

    float grid[YAI_USAGE_PLOT_GRID];
    size_t turn = 0;
    for (int point = 0; point < YAI_USAGE_PLOT_GRID; point++) {
        double time_at = span_seconds * (double)point / (double)(YAI_USAGE_PLOT_GRID - 1);
        while (turn + 1 < sample_count && samples[turn + 1].elapsed_seconds <= time_at) {
            turn++;
        }
        /* Grid points before the first turn's timestamp: nothing consumed. */
        double value =
            (samples[turn].elapsed_seconds <= time_at) ? (double)samples[turn].total_tokens : 0.0;
        grid[point] = (float)value;
    }

    /* x in minutes for longer sessions, else seconds; y from 0 to the
     * final total plus 8% headroom so the peak clears the top edge. */
    int use_minutes = span_seconds >= 120.0;
    float x_max = use_minutes ? (float)(span_seconds / 60.0) : (float)span_seconds;
    float y_max = (float)max_tokens * 1.08f;
    char heading[96];
    snprintf(heading, sizeof(heading), "tokens consumed over time (x: %s, y: cumulative total)",
             use_minutes ? "minutes" : "seconds");
    struct yetty_ycore_int_result plot_res = yai_render_line_plot(
        &app->renderer, grid, YAI_USAGE_PLOT_GRID, 0.0f, x_max, 0.0f, y_max, heading);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_res, "usage plot: render");
    return YETTY_OK_VOID();
}

/* Print this session's accumulated token usage and cost to the scrollback
 * (the `/usage` command). yai keeps these totals in app->usage, fed by the
 * engine's per-turn usage events; this is the on-demand breakdown the HUD's
 * one-line summary can't show in full. */
static struct yetty_ycore_void_result show_usage(struct yai_app *app)
{
    char input_text[16];
    char output_text[16];
    char cache_read_text[16];
    char cache_creation_text[16];
    yai_format_tokens(app->usage.input, input_text, sizeof(input_text));
    yai_format_tokens(app->usage.output, output_text, sizeof(output_text));
    yai_format_tokens(app->usage.cache_read, cache_read_text, sizeof(cache_read_text));
    yai_format_tokens(app->usage.cache_creation, cache_creation_text, sizeof(cache_creation_text));

    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "show_usage: suspend");

    printf("\n" YAI_MINT "session usage" YAI_RESET " (%s · %d turn(s))\n", app->engine_name,
           app->usage.turns);
    printf("  " YAI_DIM "input      " YAI_RESET " ↑ %s tok\n", input_text);
    printf("  " YAI_DIM "output     " YAI_RESET " ↓ %s tok\n", output_text);
    printf("  " YAI_DIM "cache read " YAI_RESET "   %s tok\n", cache_read_text);
    printf("  " YAI_DIM "cache write" YAI_RESET "   %s tok\n", cache_creation_text);
    if (app->usage.cost > 0.0) {
        printf("  " YAI_DIM "cost       " YAI_RESET "   $%.4f\n", app->usage.cost);
    } else {
        printf("  " YAI_MUTED "(cost not reported by this engine)" YAI_RESET "\n");
    }
    /* Live account quota: claude from the proxy's decoded rate-limit headers,
     * codex from its rollout file. A pre-formatted multi-line block. */
    char quota[4096];
    yai_quota_status(app, quota, sizeof(quota));
    if (quota[0]) {
        printf("\n");
        for (char *line = strtok(quota, "\n"); line; line = strtok(NULL, "\n")) {
            printf("  " YAI_DIM "%s" YAI_RESET "\n", line);
        }
    } else if (app->usage_proxy) {
        printf("  " YAI_MUTED "(quota: not captured yet)" YAI_RESET "\n");
    }
    printf("\n");

    /* In yetty mode, follow the text with a token-vs-time plot. */
    struct yetty_ycore_void_result plot_res = usage_render_plot(app);
    if (YETTY_IS_ERR(plot_res)) {
        (void)yai_renderer_zone_resume(&app->renderer);
        return YETTY_ERR(yetty_ycore_void, "show_usage: plot", plot_res);
    }

    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "show_usage: flush/resume");
    return YETTY_OK_VOID();
}

/* Print the /help block: yai's own keys and input model, the local
 * command list (from the same table the completion menu shows), and the
 * active engine's live configuration via its describe_config slot. */
/* Print one "/name <hint>   description" row, column-aligned by display
 * width (argument hints can carry multi-byte '…'). Shared by the yai and
 * engine command sections of /help. */
static void help_print_command(const struct yai_command *command)
{
    char name_and_hint[96];
    snprintf(name_and_hint, sizeof(name_and_hint), "/%s %s", command->name, command->argument_hint);
    size_t display_width = 0;
    for (const char *byte = name_and_hint; *byte; byte++) {
        display_width += ((*byte & 0xC0) != 0x80);
    }
    int padding = display_width < 24 ? (int)(24 - display_width) : 0;
    printf("  " YAI_BOLD "%s%*s" YAI_RESET " " YAI_DIM "%s" YAI_RESET "\n", name_and_hint, padding,
           "", command->description);
}

static struct yetty_ycore_void_result show_help(struct yai_app *app)
{
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "show_help: suspend");

    printf("\n" YAI_MINT "yai" YAI_RESET " — AI-CLI TUI (engine: " YAI_BOLD "%s" YAI_RESET ")\n",
           app->engine_name);

    printf("\n" YAI_MINT "input" YAI_RESET "\n");
    if (app->enter_submits) {
        printf("  " YAI_DIM "Enter sends the message; Alt+Enter inserts a newline" YAI_RESET "\n");
    } else {
        printf("  " YAI_DIM "Alt+Enter sends the message; Enter inserts a newline" YAI_RESET "\n");
    }
    printf("  " YAI_DIM "edit mode: %s (change via /config or --edit-mode)" YAI_RESET "\n",
           app->editor_mode_name);
    printf("  " YAI_DIM "Up/Down browse input history; a half-typed line fuzzy-filters "
           "it" YAI_RESET "\n");
    printf("  " YAI_DIM "typing /… opens the fuzzy command menu; Tab completes the "
           "selection" YAI_RESET "\n");
    printf("  " YAI_DIM "Ctrl-C interrupts the turn / clears the line · Ctrl-D or /exit "
           "quits" YAI_RESET "\n");
    printf("  " YAI_DIM "messages typed during a turn are queued and sent at the turn "
           "boundary" YAI_RESET "\n");

    printf("\n" YAI_MINT "shell interop" YAI_RESET "\n");
    printf("  " YAI_DIM "!  hands the keyboard to a persistent $SHELL (zsh/bash) on its own "
           "PTY:" YAI_RESET "\n");
    printf("  " YAI_DIM "   your own bindings work (Ctrl-R → fzf, completion, …); cwd and "
           "vars persist" YAI_RESET "\n");
    printf("  " YAI_DIM "   focus returns here when the command finishes · Ctrl-] comes back "
           "early" YAI_RESET "\n");
    printf("  " YAI_DIM "/shell drops to a separate interactive shell until it exits" YAI_RESET
           "\n");

    printf("\n" YAI_MINT "permission prompts" YAI_RESET "\n");
    printf("  " YAI_DIM "y allow · n deny · a always this tool · ! bypass (approve "
           "all)" YAI_RESET "\n");

    /* The command table merges both sources: yai's own local commands and
     * the ones the engine CLI advertised in its initialize handshake. /help
     * aggregates the two into separate sections so nothing is hidden behind
     * a "type / to browse" hint. Both stay alphabetical (the table is
     * sorted); filtering by the local flag preserves that within a group. */
    printf("\n" YAI_MINT "yai commands" YAI_RESET "\n");
    for (size_t index = 0; index < app->commands.count; index++) {
        const struct yai_command *command = &app->commands.items[index];
        if (command->local) {
            help_print_command(command);
        }
    }

    printf("\n" YAI_MINT "engine commands · %s" YAI_RESET "\n", app->engine_name);
    size_t engine_command_count = 0;
    for (size_t index = 0; index < app->commands.count; index++) {
        const struct yai_command *command = &app->commands.items[index];
        if (!command->local) {
            help_print_command(command);
            engine_command_count++;
        }
    }
    if (engine_command_count == 0) {
        printf("  " YAI_DIM "(none yet — the %s handshake may still be pending)" YAI_RESET "\n",
               app->engine_name);
    }

    printf("\n" YAI_MINT "engine settings · %s" YAI_RESET "\n", app->engine_name);
    char engine_rows[1024] = "";
    struct yetty_ycore_void_result describe_res =
        yetty_yai_describe_config(app->engine, app, engine_rows, sizeof(engine_rows));
    if (YETTY_IS_ERR(describe_res)) {
        /* Best-effort — /help still prints without the engine block. */
        yetty_ycore_error_destroy(describe_res.error);
        engine_rows[0] = '\0';
    }
    for (char *row = strtok(engine_rows, "\n"); row; row = strtok(NULL, "\n")) {
        printf("  " YAI_DIM "%s" YAI_RESET "\n", row);
    }
    printf("\n");

    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "show_help: flush/resume");
    return YETTY_OK_VOID();
}

/* /btw — side question in a FRESH conversation, main conversation
 * untouched. session_id is cleared for the spawn and the main identity is
 * restored at the turn boundary (yai_turn_finished); the side
 * conversation's minted id is never adopted (the engines' handle_event
 * guards on app->btw_turn). The answer streams into the scrollback like a
 * normal turn, introduced by a "btw" marker line. Per-turn engines
 * (codex, gemini) go through their normal send (which spawns fresh
 * without a resume token); claude — whose persistent child is bound to
 * the main session, and whose CLI refuses /btw itself in stream-json
 * mode — runs a one-shot side child instead (yai_claude_btw_start). */
static struct yetty_ycore_void_result btw_side_turn(struct yai_app *app, const char *question,
                                                    size_t question_len)
{
    while (question_len > 0 && (question[0] == ' ' || question[0] == '\t')) {
        question++;
        question_len--;
    }
    if (question_len == 0) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "btw_side_turn: suspend");
        printf(YAI_MUTED "usage: /btw <question> — side question, main conversation "
                         "untouched" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "btw_side_turn: usage flush");
        return YETTY_OK_VOID();
    }
    char *copy = strndup(question, question_len);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "btw_side_turn: strndup failed");
    }
    /* Stash the main conversation's identity; restored at the boundary. */
    memcpy(app->btw_session_stash, app->session_id, sizeof(app->session_id));
    app->btw_resume_stash = app->resume_requested;
    app->btw_turn = 1;
    app->session_id[0] = '\0';
    app->resume_requested = 0;

    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "btw_side_turn: suspend");
    printf(YAI_DIM "(btw ▸ side question in a fresh %s conversation)" YAI_RESET "\n",
           app->engine_name);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "btw_side_turn: marker flush");

    app->waiting = 1;
    app->estimated_tokens = 0;
    struct yetty_ycore_void_result activity_res =
        yai_set_activity(app, "typing-dots", "… thinking");
    if (YETTY_IS_ERR(activity_res)) {
        yai_report_error(app, "activity", activity_res);
    }
    struct yetty_ycore_void_result send_res;
    if (strcmp(app->engine_name, "claude") == 0) {
        send_res = yai_claude_btw_start(app, copy);
    } else {
        send_res = yetty_yai_send_user_message(app->engine, app, copy);
    }
    free(copy);
    if (YETTY_IS_ERR(send_res)) {
        /* Recoverable: put the main conversation's identity back NOW (no
         * turn boundary will run) and let the user retry. */
        app->btw_turn = 0;
        memcpy(app->session_id, app->btw_session_stash, sizeof(app->session_id));
        app->resume_requested = app->btw_resume_stash;
        app->waiting = 0;
        yai_report_error(app, "activity clear", yai_renderer_activity_clear(&app->renderer));
        yai_report_error(app, "btw send", send_res);
        struct yetty_ycore_void_result prompt_res = show_prompt(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "btw_side_turn: prompt after send");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_input_line(struct yai_app *app, const char *line,
                                                        size_t len)
{
    while (len > 0 && (line[0] == ' ' || line[0] == '\t')) {
        line++;
        len--;
    }
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
        len--;
    }
    if (len == 0) {
        if (!app->waiting) {
            struct yetty_ycore_void_result prompt_res = show_prompt(app);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "handle_input_line: prompt");
        } else {
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "handle_input_line: resume");
        }
        return YETTY_OK_VOID();
    }
    /* A pending permission consumes the next typed line as its verdict
     * — BEFORE the queue: "y" must never be shipped as a message. */
    if (app->pending_permission.active) {
        if (verdict_is(line, len, "y") || verdict_is(line, len, "yes")) {
            struct yetty_ycore_void_result allow_res =
                yetty_yai_resolve_permission(app->engine, app, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, allow_res, "handle_input_line: allow");
        } else if (verdict_is(line, len, "n") || verdict_is(line, len, "no")) {
            struct yetty_ycore_void_result deny_res =
                yetty_yai_resolve_permission(app->engine, app, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, deny_res, "handle_input_line: deny");
        } else if (verdict_is(line, len, "a") || verdict_is(line, len, "always")) {
            /* Remember this tool BEFORE resolving (resolve clears the
             * pending request and its tool_name), then allow it. */
            yai_tool_remember_allow(app, app->pending_permission.tool_name);
            struct yetty_ycore_void_result allow_res =
                yetty_yai_resolve_permission(app->engine, app, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, allow_res, "handle_input_line: always-allow");
        } else if (verdict_is(line, len, "bypass") || verdict_is(line, len, "auto")) {
            /* Switch the engine to approve-everything, then allow the
             * current one. "bypass" is also the permission-mode knob's
             * value — store it so reopening /config shows it selected.
             * ("auto" is the legacy word for the same shortcut; the mode
             * named auto is now the CLI's own classifier mode.) */
            yai_config_set(app, "permission-mode", "bypass");
            struct yetty_ycore_void_result mode_res =
                yetty_yai_apply_config(app->engine, app, "permission-mode", "bypass");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mode_res, "handle_input_line: bypass mode");
            /* The permission mode is a persisted config knob — keep the file
             * in step with the live change. */
            yai_report_error(app, "config save", yai_config_save(app));
            struct yetty_ycore_void_result allow_res =
                yetty_yai_resolve_permission(app->engine, app, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, allow_res, "handle_input_line: bypass allow");
        } else {
            struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_input_line: suspend");
            printf(YAI_DIM "  permission — y(es) / n(o) / a(lways this tool) / bypass (approve all)"
                           " · Ctrl-C to interrupt" YAI_RESET "\n");
            struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_input_line: flush");
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "handle_input_line: resume");
        }
        return YETTY_OK_VOID();
    }
    /* Slash commands: table-driven. Local entries are dispatched here;
     * everything else starting with '/' falls through and is forwarded
     * as a user message — the CLI executes its own commands. */
    const char *shell_args = NULL;
    size_t shell_args_len = 0;
    int is_shell = 0;
    if (line[0] == '/') {
        size_t word_len = 0;
        while (1 + word_len < len && line[1 + word_len] != ' ') {
            word_len++;
        }
        const struct yai_command *command =
            yai_command_table_find(&app->commands, line + 1, word_len);
        if (command && command->local) {
            if (strcmp(command->name, "quit") == 0 || strcmp(command->name, "exit") == 0) {
                struct yetty_ycore_void_result shutdown_res = yai_begin_shutdown(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, shutdown_res, "handle_input_line: quit");
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "config") == 0) {
                struct yetty_ycore_void_result config_res = show_config(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, config_res, "handle_input_line: config");
                /* The text TUI owns the alt screen — don't draw the prompt
                 * over it; it repaints the prompt itself when it closes. */
                if (!app->config_tui_active) {
                    struct yetty_ycore_void_result prompt_res = show_prompt(app);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                        "handle_input_line: prompt after config");
                }
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "usage") == 0) {
                struct yetty_ycore_void_result usage_res = show_usage(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, usage_res, "handle_input_line: usage");
                struct yetty_ycore_void_result prompt_res = show_prompt(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                    "handle_input_line: prompt after usage");
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "help") == 0) {
                struct yetty_ycore_void_result help_res = show_help(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, help_res, "handle_input_line: help");
                struct yetty_ycore_void_result prompt_res = show_prompt(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                    "handle_input_line: prompt after help");
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "model") == 0) {
                /* /model [name]: an arg picks directly, else open the
                 * focused picker. */
                const char *model_arg = line + 1 + word_len;
                size_t model_len = len - 1 - word_len;
                while (model_len > 0 && model_arg[0] == ' ') {
                    model_arg++;
                    model_len--;
                }
                struct yetty_ycore_void_result model_res =
                    model_picker_open(app, model_len > 0 ? model_arg : NULL, model_len);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, model_res, "handle_input_line: model");
                /* The alt-screen picker repaints the prompt itself when it
                 * closes; the pipe/preset paths need it drawn now. */
                if (!app->model_picker_active) {
                    struct yetty_ycore_void_result prompt_res = show_prompt(app);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                        "handle_input_line: prompt after model");
                }
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "agents") == 0) {
                struct yetty_ycore_void_result agents_res = agents_picker_open(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, agents_res, "handle_input_line: agents");
                /* The alt-screen picker repaints the prompt on close; the
                 * empty/error/pipe paths need it drawn now. */
                if (!app->agents_picker_active) {
                    struct yetty_ycore_void_result prompt_res = show_prompt(app);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                        "handle_input_line: prompt after agents");
                }
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "title") == 0) {
                /* /title <text> — set the session title (#{title} in the HUD).
                 * Any text but newline; the line editor already stripped the
                 * newline. Empty arg clears it. */
                const char *title_arg = line + 1 + word_len;
                size_t title_len = len - 1 - word_len;
                while (title_len > 0 && title_arg[0] == ' ') {
                    title_arg++;
                    title_len--;
                }
                if (title_len >= sizeof(app->session_title)) {
                    title_len = sizeof(app->session_title) - 1;
                }
                memcpy(app->session_title, title_arg, title_len);
                app->session_title[title_len] = '\0';
                struct yetty_ycore_void_result suspend_res =
                    yai_renderer_zone_suspend(&app->renderer);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res,
                                    "handle_input_line: title suspend");
                if (app->session_title[0]) {
                    printf(YAI_DIM "(session title set to \"%s\")" YAI_RESET "\n",
                           app->session_title);
                } else {
                    printf(YAI_DIM "(session title cleared)" YAI_RESET "\n");
                }
                struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
                flush_res =
                    yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "handle_input_line: title flush");
                yai_report_error(app, "hud refresh", yai_refresh_hud_stats(app));
                struct yetty_ycore_void_result prompt_res = show_prompt(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                    "handle_input_line: prompt after title");
                return YETTY_OK_VOID();
            }
            if (strcmp(command->name, "btw") == 0) {
                /* Generic side question in a fresh conversation. While a
                 * turn is in flight, fall through — the raw line queues
                 * and replays at the turn boundary. */
                if (!app->waiting) {
                    return btw_side_turn(app, line + 1 + word_len, len - 1 - word_len);
                }
            }
            if (strcmp(command->name, "shell") == 0) {
                is_shell = 1;
                shell_args = line + 1 + word_len;
                shell_args_len = len - 1 - word_len;
            }
        }
    } else if (line[0] == '!') {
        /* A submitted "!…" line (a paste or a control-inject; live typing
         * switches focus at the '!' keystroke instead): hand the keyboard
         * to the interop shell and type the command for the user — focus
         * returns at the shell's next prompt marker. */
        struct yetty_ycore_void_result begin_res = yai_shell_focus_begin(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "handle_input_line: shell focus");
        if (!app->shell_focus) {
            /* Refused (unsupported $SHELL) — the warning is on screen. */
            struct yetty_ycore_void_result prompt_res = show_prompt(app);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                "handle_input_line: prompt after ! refusal");
            return YETTY_OK_VOID();
        }
        if (len > 1) {
            struct yetty_ycore_void_result forward_res = yai_shell_forward(app, line + 1, len - 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, forward_res, "handle_input_line: ! forward");
            struct yetty_ycore_void_result enter_res = yai_shell_forward(app, "\n", 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, enter_res, "handle_input_line: ! enter");
        }
        return YETTY_OK_VOID();
    }
    if (is_shell) {
        while (shell_args_len > 0 && shell_args[0] == ' ') {
            shell_args++;
            shell_args_len--;
        }
        char *command = (shell_args_len > 0) ? strndup(shell_args, shell_args_len) : NULL;
        if (shell_args_len > 0 && !command) {
            return YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup failed");
        }
        /* The shell flow recovers and continues — absorb its error
         * here (the prompt must come back either way). */
        yai_report_error(app, "shell", run_shell(app, command));
        free(command);
        struct yetty_ycore_void_result prompt_res = show_prompt(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "handle_input_line: prompt after shell");
        return YETTY_OK_VOID();
    }
    if (app->waiting) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_input_line: suspend");
        if (app->queue_len < YAI_QUEUE_MAX) {
            char *copy = strndup(line, len);
            if (!copy) {
                return YETTY_ERR(yetty_ycore_void, "handle_input_line: queue strndup failed");
            }
            app->queue[app->queue_len++] = copy;
            printf(YAI_DIM "(queued — sends when this turn finishes)" YAI_RESET "\n");
        } else {
            /* The queue cap is normal back-pressure, not a failure: tell the
             * user the message was dropped and carry on. Surfacing it as a
             * Result error would bubble an internal ✗ chain all the way up to
             * the keyboard handler, on top of this already-friendly line. */
            printf(YAI_RED "queue full — message dropped" YAI_RESET "\n");
        }
        /* Restore the zone whatever happened; an IO failure HERE is a real
         * error and propagates. */
        struct yetty_ycore_void_result restore_res = yai_render_flush_stdout();
        restore_res = yetty_ycore_void_chain(restore_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, restore_res, "handle_input_line: queue restore");
        return YETTY_OK_VOID();
    }
    char *copy = strndup(line, len);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup failed");
    }
    app->waiting = 1;
    app->estimated_tokens = 0; /* fresh per-request estimate */
    struct yetty_ycore_void_result activity_res =
        yai_set_activity(app, "typing-dots", "… thinking");
    if (YETTY_IS_ERR(activity_res)) {
        yai_report_error(app, "activity", activity_res);
    }
    struct yetty_ycore_void_result send_res = yetty_yai_send_user_message(app->engine, app, copy);
    free(copy);
    if (YETTY_IS_ERR(send_res)) {
        /* Recoverable UI flow: the prompt comes back, the user retries.
         * This is an end-consumer point for the send error. */
        app->waiting = 0;
        yai_report_error(app, "activity clear", yai_renderer_activity_clear(&app->renderer));
        yai_report_error(app, "send", send_res);
        struct yetty_ycore_void_result prompt_res = show_prompt(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "handle_input_line: prompt after send");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Line editor (terminal-focused typing). stdin is raw: yai echoes,
 * erases, and dispatches lines itself.
 *---------------------------------------------------------------------------*/

/* Pinned mode repaints the whole prompt row per edit; legacy (non-tty)
 * mode echoes bytes in place. */
static struct yetty_ycore_void_result editor_render(struct yai_app *app)
{
    if (app->renderer.pin_enabled) {
        struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "editor_render: redraw");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result editor_echo(struct yai_app *app, const char *bytes,
                                                  size_t len)
{
    if (!app->echo_input || app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    if (fwrite(bytes, 1, len, stdout) != len) {
        return YETTY_ERR(yetty_ycore_void, "editor_echo: fwrite failed");
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_echo: flush");
    return YETTY_OK_VOID();
}

/* Every edit invalidates a history-browse position (the edited text
 * becomes the new in-progress line). */
static struct yetty_ycore_void_result editor_edited(struct yai_app *app)
{
    app->history_browse = -1;
    struct yetty_ycore_void_result render_res = editor_render(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "editor_edited: render");
    struct yetty_ycore_void_result menu_res = menu_update(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_res, "editor_edited: menu");
    return YETTY_OK_VOID();
}

/* Commit the current line (Enter): adopt a highlighted menu command,
 * echo the prompt into history, push to input history, reset the
 * buffer, and dispatch. An empty line feeds like a shell's. */
static struct yetty_ycore_void_result editor_submit(struct yai_app *app)
{
    if (app->menu_visible) {
        struct yetty_ycore_void_result adopt_res = menu_adopt_selection(app, /*trailing_space=*/0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, adopt_res, "editor_submit: adopt");
        struct yetty_ycore_void_result close_res = menu_close(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "editor_submit: menu close");
    }
    app->stdin_buf[app->stdin_len] = '\0';
    size_t submitted_len = app->stdin_len;
    /* History is convenience state: an OOM there must not eat the
     * submitted line — absorb and continue. */
    yai_report_error(app, "history", history_add(app, app->stdin_buf, submitted_len));
    app->history_browse = -1;
    /* Reset BEFORE dispatch so zone redraws show an empty prompt; the
     * bytes stay valid for handle_input_line. */
    app->stdin_len = 0;
    app->stdin_cursor = 0;
    editor_ops_undo_clear(app); /* line consumed — its undo history ends here */
    if (app->renderer.pin_enabled) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "editor_submit: suspend");
        printf(YAI_MINT "you ▸ " YAI_RESET "%s\n", app->stdin_buf);
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_submit: flush");
    } else {
        struct yetty_ycore_void_result echo_res = editor_echo(app, "\n", 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, echo_res, "editor_submit: echo");
    }
    struct yetty_ycore_void_result input_res =
        handle_input_line(app, app->stdin_buf, submitted_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, input_res, "editor_submit: input line");
    return YETTY_OK_VOID();
}

/* Inject a line from the external control server as if it were typed and
 * submitted. The user's in-progress line is stashed and restored around
 * the injection, so an outside command never eats what they were typing;
 * the line then flows through the normal submit path (slash command,
 * shell escape, or a message — queued if a turn is already in flight). */
struct yetty_ycore_void_result yai_control_inject(struct yai_app *app, const char *line, size_t len)
{
    if (len >= sizeof(app->stdin_buf)) {
        len = sizeof(app->stdin_buf) - 1;
    }
    char stash[sizeof(app->stdin_buf)];
    size_t stash_len = app->stdin_len;
    size_t stash_cursor = app->stdin_cursor;
    memcpy(stash, app->stdin_buf, stash_len);

    memcpy(app->stdin_buf, line, len);
    app->stdin_len = len;
    app->stdin_cursor = len;
    struct yetty_ycore_void_result submit_res = editor_submit(app);

    /* editor_submit reset the buffer to empty; restore the stashed line. */
    memcpy(app->stdin_buf, stash, stash_len);
    app->stdin_len = stash_len;
    app->stdin_cursor = stash_cursor;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, submit_res, "yai_control_inject: submit");
    struct yetty_ycore_void_result redraw_res = editor_render(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "yai_control_inject: redraw");
    return YETTY_OK_VOID();
}

/* Ctrl-C never exits yai (use /exit or Ctrl-D for that). During a turn
 * it interrupts the request; otherwise it cancels the current input
 * line, and on an already-empty line it reminds the user how to exit. */
static struct yetty_ycore_void_result editor_interrupt(struct yai_app *app)
{
    ydebug("yai: editor_interrupt pending=%d waiting=%d child_alive=%d",
           app->pending_permission.active, app->waiting, app->child_alive);
    /* A permission prompt is waiting for a verdict: Ctrl-C answers it as
     * a deny (unblocking the CLI), then falls through to interrupt the
     * turn — Ctrl-C must work the same here as during any other request. */
    if (app->pending_permission.active) {
        struct yetty_ycore_void_result deny_res = yetty_yai_resolve_permission(app->engine, app, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, deny_res, "editor_interrupt: deny pending");
    }
    if (app->waiting && app->child_alive) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "editor_interrupt: suspend");
        printf("\n" YAI_MUTED "(interrupt requested)" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_interrupt: flush");
        struct yetty_ycore_void_result interrupt_res = yetty_yai_interrupt(app->engine, app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, interrupt_res, "editor_interrupt: interrupt");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "editor_interrupt: resume");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result close_res = menu_close(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "editor_interrupt: menu close");
    if (app->stdin_len > 0) {
        /* Cancel the half-typed line rather than exit. */
        app->stdin_len = 0;
        app->stdin_cursor = 0;
        editor_ops_undo_clear(app); /* line discarded — drop its undo history */
        app->history_browse = -1;
        struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "editor_interrupt: redraw");
        return YETTY_OK_VOID();
    }
    /* Empty line: do not exit — tell the user how. */
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "editor_interrupt: suspend");
    printf("\n" YAI_MUTED "in order to exit, use /exit" YAI_RESET "\n");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_interrupt: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "editor_interrupt: resume");
    return YETTY_OK_VOID();
}

/* Milliseconds on the monotonic clock, for arming short UI timeouts and
 * stamping the token time-series. */
long yai_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Record the current cumulative (input + output) token total at
 * seconds-since-session-start. Decimates the series in place when full,
 * preserving the full time span at half resolution. */
void yai_usage_record_sample(struct yai_app *app)
{
    if (app->usage_sample_count >= YAI_USAGE_SAMPLE_MAX) {
        size_t kept = 0;
        for (size_t i = 0; i < app->usage_sample_count; i += 2) {
            app->usage_samples[kept++] = app->usage_samples[i];
        }
        app->usage_sample_count = kept;
    }
    long now_ms = yai_monotonic_ms();
    double elapsed =
        app->session_start_ms ? (double)(now_ms - app->session_start_ms) / 1000.0 : 0.0;
    if (elapsed < 0.0) {
        elapsed = 0.0;
    }
    struct yai_usage_sample *sample = &app->usage_samples[app->usage_sample_count++];
    sample->elapsed_seconds = elapsed;
    /* "Consumed" = everything the model processed this session: fresh input
     * and output plus the cache reads/writes (the bulk of the cost). */
    sample->total_tokens =
        app->usage.input + app->usage.output + app->usage.cache_read + app->usage.cache_creation;
}

/* Ctrl-D on an empty line: the first press only arms a confirmation
 * window and tells the user; a second press while the window is open
 * quits. Guards against a stray Ctrl-D tearing down the session. */
static struct yetty_ycore_void_result editor_eof(struct yai_app *app)
{
    struct yetty_ycore_void_result close_res = menu_close(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "editor_eof: menu close");
    long now_ms = yai_monotonic_ms();
    if (app->eof_confirm_deadline_ms != 0 && now_ms <= app->eof_confirm_deadline_ms) {
        app->eof_confirm_deadline_ms = 0;
        struct yetty_ycore_void_result shutdown_res = yai_begin_shutdown(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, shutdown_res, "editor_eof: shutdown");
        return YETTY_OK_VOID();
    }
    app->eof_confirm_deadline_ms = now_ms + YAI_EOF_CONFIRM_WINDOW_MS;
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "editor_eof: suspend");
    printf("\n" YAI_MUTED "press Ctrl-D again within %d seconds to exit" YAI_RESET "\n",
           YAI_EOF_CONFIRM_WINDOW_MS / 1000);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_eof: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "editor_eof: resume");
    return YETTY_OK_VOID();
}

/* Map one editor action code (from the feed_byte slot) to its UI
 * effect. The editor owns the buffer mutation; menu / history / submit
 * policy stays here. */
static struct yetty_ycore_void_result apply_editor_action(struct yai_app *app, int action)
{
    switch (action) {
    case YAI_EDIT_NONE:
        return YETTY_OK_VOID();
    case YAI_EDIT_MOVED:
        return editor_render(app);
    case YAI_EDIT_CHANGED:
        return editor_edited(app);
    case YAI_EDIT_SUBMIT:
        return editor_submit(app);
    case YAI_EDIT_EOF:
        return editor_eof(app);
    case YAI_EDIT_INTERRUPT:
        return editor_interrupt(app);
    case YAI_EDIT_NAV_PREV:
        return app->menu_visible ? menu_move_selection(app, -1) : history_browse_move(app, -1);
    case YAI_EDIT_NAV_NEXT:
        return app->menu_visible ? menu_move_selection(app, 1) : history_browse_move(app, 1);
    case YAI_EDIT_COMPLETE:
        if (app->menu_visible) {
            struct yetty_ycore_void_result adopt_res =
                menu_adopt_selection(app, /*trailing_space=*/1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, adopt_res, "apply_editor_action: adopt");
            return menu_update(app);
        }
        return YETTY_OK_VOID();
    case YAI_EDIT_HISTORY_SEARCH:
        return history_search_begin(app);
    default:
        return YETTY_OK_VOID();
    }
}

/* A permission prompt is pending — one keypress is the verdict (no Enter
 * needed). Resolves the request immediately. */
static struct yetty_ycore_void_result permission_key(struct yai_app *app, unsigned char key)
{
    ydebug("yai: permission_key 0x%02x", key);
    switch (key) {
    case 'y':
    case 'Y':
        return yetty_yai_resolve_permission(app->engine, app, 1);
    case 'n':
    case 'N':
    case 0x1b: /* Esc — cancel = deny */
        return yetty_yai_resolve_permission(app->engine, app, 0);
    case 'a':
    case 'A':
        /* Always allow THIS tool: remember before resolving (resolve
         * clears the pending request and its tool name), then allow. */
        yai_tool_remember_allow(app, app->pending_permission.tool_name);
        return yetty_yai_resolve_permission(app->engine, app, 1);
    case '!': {
        /* Bypass: approve everything from now on, then allow the current. */
        yai_config_set(app, "permission-mode", "bypass");
        struct yetty_ycore_void_result mode_res =
            yetty_yai_apply_config(app->engine, app, "permission-mode", "bypass");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mode_res, "permission_key: bypass mode");
        /* The permission mode is a persisted config knob — keep the file in
         * step with the live change. */
        yai_report_error(app, "config save", yai_config_save(app));
        return yetty_yai_resolve_permission(app->engine, app, 1);
    }
    case 0x03: /* Ctrl-C: deny the prompt and interrupt the turn. */
        return editor_interrupt(app);
    case '\r':
    case '\n':
        return YETTY_OK_VOID(); /* ignore a stray Enter */
    default:
        break;
    }
    /* Unrecognised key: remind which keys answer (the zone is repainted). */
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "permission_key: suspend");
    printf(YAI_DIM "  permission — press y / n / a (always this tool) / ! (bypass) · Ctrl-C "
                   "interrupts" YAI_RESET "\n");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "permission_key: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "permission_key: resume");
    return YETTY_OK_VOID();
}

/* Focus returns from the interop shell to yai (shell.c saw the precmd
 * marker, the user pressed Ctrl-], or the shell died). Restores the
 * prompt and replays the engine lines deferred while the shell owned
 * the screen. `note` (may be NULL) is printed dim first. */
struct yetty_ycore_void_result yai_shell_focus_end(struct yai_app *app, const char *note)
{
    if (!app->shell_focus) {
        return YETTY_OK_VOID();
    }
    app->shell_focus = 0;
    if (note && note[0]) {
        printf("\n" YAI_DIM "%s" YAI_RESET "\n", note);
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "shell focus end: flush");
    /* Replay the deferred engine lines in arrival order — printing is
     * safe again. Their errors are absorbed here so one bad line cannot
     * strand the rest. */
    size_t deferred_count = app->deferred_count;
    app->deferred_count = 0;
    for (size_t index = 0; index < deferred_count; index++) {
        struct yai_deferred_line *entry = &app->deferred_lines[index];
        yai_report_error(app, "deferred engine line",
                         yai_handle_child_line(app, entry->bytes, entry->len));
        free(entry->bytes);
        entry->bytes = NULL;
        entry->len = 0;
    }
    struct yetty_ycore_void_result state_res =
        yai_set_state(app, app->waiting ? "… thinking" : "idle");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "shell focus end: state");
    struct yetty_ycore_void_result prompt_res = show_prompt(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res, "shell focus end: prompt");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result keyboard_input(struct yai_app *app, const char *bytes,
                                                     size_t len)
{
    /* The settings TUI owns the keyboard while it's on the alt screen. */
    if (app->config_tui_active) {
        for (size_t index = 0; index < len && app->config_tui_active; index++) {
            struct yetty_ycore_void_result key_res =
                config_tui_key(app, (unsigned char)bytes[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "keyboard_input: config tui key");
        }
        return YETTY_OK_VOID();
    }
    /* The /model picker owns the keyboard while its modal is up. Feed
     * bytes one at a time (model_picker_key decodes split arrow escapes);
     * if a keystroke closes the picker mid-chunk, the tail is reprocessed
     * as normal input so it never swallows what follows. */
    if (app->model_picker_active) {
        size_t index = 0;
        for (; index < len && app->model_picker_active; index++) {
            struct yetty_ycore_void_result key_res =
                model_picker_key(app, (unsigned char)bytes[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "keyboard_input: model picker key");
        }
        if (!app->model_picker_active && index < len) {
            return keyboard_input(app, bytes + index, len - index);
        }
        return YETTY_OK_VOID();
    }
    /* The /agents picker owns the keyboard while its modal is up (same
     * per-byte feed + mid-chunk-close reprocessing as /model). */
    if (app->agents_picker_active) {
        size_t index = 0;
        for (; index < len && app->agents_picker_active; index++) {
            struct yetty_ycore_void_result key_res =
                agents_picker_key(app, (unsigned char)bytes[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "keyboard_input: agents picker key");
        }
        if (!app->agents_picker_active && index < len) {
            return keyboard_input(app, bytes + index, len - index);
        }
        return YETTY_OK_VOID();
    }
    /* A pending permission owns the keyboard: each keypress is the verdict,
     * handled BEFORE GUI focus and the line editor so it works no matter
     * where focus is (e.g. the HUD) and without needing Enter. */
    if (app->pending_permission.active) {
        for (size_t index = 0; index < len && app->pending_permission.active; index++) {
            struct yetty_ycore_void_result verdict_res =
                permission_key(app, (unsigned char)bytes[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, verdict_res,
                                "keyboard_input: permission verdict");
        }
        return YETTY_OK_VOID();
    }
    /* The interop shell owns the keyboard while focused: raw passthrough
     * to its PTY, so the shell's own bindings (Ctrl-R → fzf, completion,
     * …) work as in a bare terminal. Ctrl-] (0x1D) is the manual way
     * back to yai; focus otherwise returns at the shell's prompt marker. */
    if (app->shell_focus) {
        size_t escape_at = len;
        for (size_t index = 0; index < len; index++) {
            if ((unsigned char)bytes[index] == 0x1D) {
                escape_at = index;
                break;
            }
        }
        if (escape_at > 0) {
            struct yetty_ycore_void_result forward_res = yai_shell_forward(app, bytes, escape_at);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, forward_res, "keyboard_input: shell forward");
        }
        if (escape_at == len) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result end_res = yai_shell_focus_end(app, "(back to yai)");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "keyboard_input: shell focus end");
        if (escape_at + 1 < len) {
            return keyboard_input(app, bytes + escape_at + 1, len - escape_at - 1);
        }
        return YETTY_OK_VOID();
    }
    ydebug("yai: keys len=%zu first=0x%02x -> %s", len, len ? (unsigned char)bytes[0] : 0,
           (app->focus_gui && app->hud) ? "gui" : "editor");
    if (app->focus_gui && app->hud) {
        struct yetty_ycore_void_result feed_res = yai_hud_feed_keys(app->hud, bytes, len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "keyboard_input: gui keys");
        return YETTY_OK_VOID();
    }
    /* An open Ctrl-R history search owns the keyboard byte-for-byte. When
     * it closes mid-chunk (accept/cancel), the rest of the chunk belongs
     * to the editor again. */
    if (app->history_search_active) {
        for (size_t index = 0; index < len; index++) {
            struct yetty_ycore_void_result feed_res =
                history_search_feed(app, (unsigned char)bytes[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "keyboard_input: history search");
            if (!app->history_search_active) {
                return keyboard_input(app, bytes + index + 1, len - index - 1);
            }
        }
        return YETTY_OK_VOID();
    }
    /* "!" as the first character of an empty line hands the keyboard to
     * the interop shell; the rest of the chunk (a pasted "!cmd…") already
     * belongs to the shell. On refusal (unsupported $SHELL) the warning
     * was printed and the chunk is dropped. */
    if (len > 0 && bytes[0] == '!' && app->stdin_len == 0 && !app->in_paste) {
        struct yetty_ycore_void_result begin_res = yai_shell_focus_begin(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, begin_res, "keyboard_input: shell focus");
        if (app->shell_focus && len > 1) {
            return keyboard_input(app, bytes + 1, len - 1);
        }
        return YETTY_OK_VOID();
    }
    /* Paste-burst detection: a line break with real content right after it means
     * several lines arrived together (a paste), not an Enter keypress. A lone
     * Enter — or a CR/LF pair — has no content after the break, so it still
     * submits. During the burst, Enter inserts a newline (see editor_cmd_enter),
     * so the whole paste becomes ONE message; the bytes are applied in bulk with
     * a single repaint at the end instead of N. */
    int paste_burst = 0;
    for (size_t index = 0; index + 1 < len; index++) {
        int is_break = bytes[index] == '\n' || bytes[index] == '\r';
        int content_next = bytes[index + 1] != '\n' && bytes[index + 1] != '\r';
        if (is_break && content_next) {
            paste_burst = 1;
            break;
        }
    }
    if (paste_burst) {
        app->in_paste = 1;
        for (size_t index = 0; index < len; index++) {
            struct yetty_ycore_int_result action_res =
                yetty_yai_feed_byte(app->editor, app, (unsigned char)bytes[index]);
            if (YETTY_IS_ERR(action_res)) {
                app->in_paste = 0;
                return YETTY_ERR(yetty_ycore_void, "keyboard_input: paste feed", action_res);
            }
        }
        app->in_paste = 0;
        struct yetty_ycore_void_result edited_res = editor_edited(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, edited_res, "keyboard_input: paste repaint");
        return YETTY_OK_VOID();
    }

    for (size_t index = 0; index < len; index++) {
        if ((unsigned char)bytes[index] == 0x07) {
            /* Ctrl-G: compose the message in an external editor. */
            struct yetty_ycore_void_result editor_res = open_external_editor(app);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, editor_res, "keyboard_input: external editor");
            continue;
        }
        struct yetty_ycore_int_result action_res =
            yetty_yai_feed_byte(app->editor, app, (unsigned char)bytes[index]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, action_res, "keyboard_input: feed byte");
        struct yetty_ycore_void_result apply_res = apply_editor_action(app, action_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "keyboard_input: apply action");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Key-envelope decode — if the host re-encodes keystrokes as key OSCs
 * (a side effect of input subscriptions), normalize them back into the
 * keyboard stream so the host's behavior cannot influence routing.
 *---------------------------------------------------------------------------*/

static size_t utf8_encode(uint32_t codepoint, char *out)
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

/* GLFW keycode -> terminal byte sequence for non-printable keys (the
 * printable ones arrive as CHAR codepoints). */
static size_t key_event_to_bytes(uint32_t kind, int32_t key, int32_t mods, uint32_t codepoint,
                                 char *out, size_t out_size)
{
    if (kind == YETTY_YMGUI_INPUT_KEY_CHAR) {
        /* Control codepoints pass through verbatim — Enter (10), Tab (9)
         * and Ctrl-chords arrive as raw control chars on some injection
         * paths; the line editor gives them their meaning. */
        if (codepoint == 0 || out_size < 4) {
            return 0;
        }
        return utf8_encode(codepoint, out);
    }
    if (kind != YETTY_YMGUI_INPUT_KEY_DOWN) {
        return 0; /* UP: one keypress = one byte sequence */
    }
    static const struct {
        int32_t key;
        const char *bytes;
    } key_table[] = {
        {257, "\r"},      {335, "\r"},      {258, "\t"},      {259, "\x7f"},   {256, "\x1b"},
        {260, "\x1b[2~"}, {261, "\x1b[3~"}, {262, "\x1b[C"},  {263, "\x1b[D"}, {264, "\x1b[B"},
        {265, "\x1b[A"},  {266, "\x1b[5~"}, {267, "\x1b[6~"}, {268, "\x1b[H"}, {269, "\x1b[F"},
    };
    for (size_t index = 0; index < sizeof(key_table) / sizeof(key_table[0]); index++) {
        if (key_table[index].key == key) {
            size_t len = strlen(key_table[index].bytes);
            if (len > out_size) {
                return 0;
            }
            memcpy(out, key_table[index].bytes, len);
            return len;
        }
    }
    if ((mods & 2) && key >= 65 && key <= 90 && out_size >= 1) { /* Ctrl-A .. Ctrl-Z */
        out[0] = (char)(key & 0x1F);
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * yface demux — stdin bytes split into keystrokes vs OSC envelopes
 *---------------------------------------------------------------------------*/

/* Note a config change in the scrollback so the edit is auditable. */
static struct yetty_ycore_void_result config_note(struct yai_app *app, const char *key,
                                                  const char *value)
{
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "config_note: suspend");
    printf(YAI_DIM "(config: %s=%s)" YAI_RESET "\n", key, value);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "config_note: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "config_note: resume");
    return YETTY_OK_VOID();
}

/* Apply one knob's currently-selected value — swap the editor, set the
 * model, or store the field and push the engine's live apply_config — with a
 * scrollback note. Shared by the ygui dialog sync and the text TUI. */
static struct yetty_ycore_void_result apply_config_knob(struct yai_app *app, int knob_index)
{
    const char *value =
        app->config_knobs[knob_index].options[app->config_knobs[knob_index].selected];
    if (app->config_knobs[knob_index].is_edit_mode) {
        struct yetty_ycore_void_result mode_res = yai_set_edit_mode(app, value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, mode_res, "apply_config_knob: edit mode");
        return config_note(app, "edit mode", value);
    }
    if (app->config_knobs[knob_index].is_model) {
        /* "default" clears the override; the active engine reads its section's
         * model at the next spawn. Engines with a live protocol also push the
         * change into the running session (claude: set_model). */
        const char *model = strcmp(value, "default") == 0 ? "" : value;
        yai_config_set(app, "model", model);
        struct yetty_ycore_void_result push_res =
            yetty_yai_apply_config(app->engine, app, "model", model);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "apply_config_knob: model push");
        struct yetty_ycore_void_result stats_res = yai_refresh_hud_stats(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stats_res, "apply_config_knob: model stats");
        return config_note(app, "model", value);
    }
    /* Store the new value (the engines read the config struct at spawn), then
     * let the engine apply any live side-effect (e.g. claude pushes the new
     * permission mode into the running session). */
    const char *key = app->config_knobs[knob_index].key;
    yai_config_set(app, key, value);
    struct yetty_ycore_void_result apply_res = yetty_yai_apply_config(app->engine, app, key, value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "apply_config_knob: apply");
    return config_note(app, key, value);
}

/* After a GUI-owned click: read the config dialog's widgets and apply
 * what changed — renderer fields directly, the edit-mode knob by
 * swapping the editor, an engine knob through the engine's apply_config
 * slot. */
static struct yetty_ycore_void_result config_dialog_sync(struct yai_app *app)
{
    if (!app->hud) {
        return YETTY_OK_VOID();
    }
    struct yai_hud_config_values values;
    struct yetty_ycore_void_result poll_res = yai_hud_config_poll(app->hud, &values);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, poll_res, "config_dialog_sync: poll");
    if (!values.open) {
        return YETTY_OK_VOID();
    }
    if (values.show_thinking != app->config_show_thinking_applied) {
        app->config_show_thinking_applied = values.show_thinking;
        app->config.show_thinking = values.show_thinking;
        yai_config_resolve(app);
    }
    int fold_lines = (int)(values.fold_lines + 0.5f);
    if (fold_lines != app->config_fold_lines_applied) {
        app->config_fold_lines_applied = fold_lines;
        app->config.fold_lines = fold_lines > 0 ? fold_lines : YAI_DEFAULT_FOLD_LINES;
        yai_config_resolve(app);
    }
    for (int knob = 0; knob < app->config_knob_count && knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        int chosen = values.knob_selected[knob];
        if (chosen < 0 || chosen >= app->config_knobs[knob].option_count ||
            chosen == app->config_knobs[knob].selected) {
            continue;
        }
        app->config_knobs[knob].selected = chosen;
        struct yetty_ycore_void_result apply_res = apply_config_knob(app, knob);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "config_dialog_sync: apply knob");
    }
    /* Persist the new config so it survives the next launch. Best-effort:
     * a save failure is reported but must not break the dialog flow. */
    yai_report_error(app, "config save", yai_config_save(app));
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_mouse_envelope(struct yai_app *app,
                                                            const uint8_t *payload,
                                                            size_t payload_len)
{
    if (!app->hud || payload_len < sizeof(struct yetty_client_input_mouse)) {
        return YETTY_OK_VOID();
    }
    struct yetty_client_input_mouse mouse;
    memcpy(&mouse, payload, sizeof(mouse));
    if (mouse.magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return YETTY_OK_VOID();
    }
    switch (mouse.kind) {
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
        struct yetty_ycore_int_result press_res =
            yai_hud_mouse_button(app->hud, mouse.x, mouse.y, mouse.button, mouse.pressed);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, press_res, "mouse envelope: button");
        if (mouse.pressed) {
            /* Client-side focus: the press decides who owns the gesture. */
            app->focus_gui = press_res.value;
            ydebug("yai: focus -> %s", app->focus_gui ? "gui" : "terminal");
        }
        /* Press AND release: a slider drag commits its value on the
         * release, a checkbox/radio flips on the press. */
        struct yetty_ycore_void_result sync_res = config_dialog_sync(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sync_res, "mouse envelope: config sync");
        /* When the GUI does not own the gesture, bounce the click back so
         * the terminal runs its native behaviour — text selection (drag),
         * middle-click paste. The press sets focus; the matching release
         * follows the same owner. */
        if (!app->focus_gui) {
            struct yetty_ycore_void_result reinject_res = reinject_mouse(&mouse);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, reinject_res, "mouse envelope: button reinject");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS: {
        /* GUI owns the pointer (a titlebar/resize/slider drag in flight) →
         * feed the framework. Otherwise the terminal owns it: bounce motion
         * back ONLY while a button is held, so a drag extends the terminal's
         * text selection. A plain hover needs no terminal action and would
         * otherwise double the wire traffic on every pixel of movement. */
        if (app->focus_gui) {
            struct yetty_ycore_void_result motion_res =
                yai_hud_mouse_motion(app->hud, mouse.x, mouse.y);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, motion_res, "mouse envelope: motion");
            return YETTY_OK_VOID();
        }
        if (mouse.buttons_held != 0) {
            struct yetty_ycore_void_result reinject_res = reinject_mouse(&mouse);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, reinject_res, "mouse envelope: motion reinject");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YMGUI_INPUT_MOUSE_WHEEL: {
        /* Client-side decision: wheel over the window scrolls the GUI;
         * anywhere else it belongs to the terminal — bounce it back. */
        struct yetty_ycore_int_result inside_res =
            yai_hud_contains_point(app->hud, mouse.x, mouse.y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, inside_res, "mouse envelope: wheel hit-test");
        ydebug("yai: wheel at (%.0f,%.0f) dy=%.1f -> %s", mouse.x, mouse.y, mouse.wheel_dy,
               inside_res.value ? "gui" : "reinject");
        if (inside_res.value) {
            struct yetty_ycore_void_result wheel_res =
                yai_hud_mouse_wheel(app->hud, mouse.x, mouse.y, mouse.wheel_dy);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, wheel_res, "mouse envelope: wheel");
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result reinject_res = reinject_mouse(&mouse);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reinject_res, "mouse envelope: reinject");
        return YETTY_OK_VOID();
    }
    default:
        return YETTY_OK_VOID();
    }
}

/* Authoritative pane pixel size — sent on the mouse-subscribe rising
 * edge and on every pane resize (the tty winsize often carries no
 * pixel fields, so this envelope is the only real size cue). */
static struct yetty_ycore_void_result handle_resize_envelope(struct yai_app *app,
                                                             const uint8_t *payload,
                                                             size_t payload_len)
{
    if (!app->hud || payload_len < sizeof(struct yetty_client_input_resize)) {
        return YETTY_OK_VOID();
    }
    struct yetty_client_input_resize resize_event;
    memcpy(&resize_event, payload, sizeof(resize_event));
    if (resize_event.magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result viewport_res =
        yai_hud_set_viewport(app->hud, resize_event.width, resize_event.height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, viewport_res, "resize envelope: viewport");
    /* Re-reserve the docked bar's rows for the new pane size, then
     * repaint the prompt at the new region bottom. */
    struct yetty_ycore_void_result dock_res = yai_apply_dock_reservation(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dock_res, "resize envelope: dock");
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "resize envelope: pin redraw");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result handle_key_envelope(struct yai_app *app,
                                                          const uint8_t *payload,
                                                          size_t payload_len)
{
    if (payload_len < sizeof(struct yetty_client_input_key)) {
        return YETTY_OK_VOID();
    }
    struct yetty_client_input_key key_event;
    memcpy(&key_event, payload, sizeof(key_event));
    if (key_event.magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
        return YETTY_OK_VOID();
    }
    char bytes[8];
    size_t len = key_event_to_bytes(key_event.kind, key_event.key, key_event.mods,
                                    key_event.codepoint, bytes, sizeof(bytes));
    ydebug("yai: KEY envelope kind=%u key=%d mods=%d cp=%u -> %zu bytes (0x%02x) pending=%d",
           key_event.kind, key_event.key, key_event.mods, key_event.codepoint, len,
           len ? (unsigned char)bytes[0] : 0, app->pending_permission.active);
    if (len > 0) {
        struct yetty_ycore_void_result keys_res = keyboard_input(app, bytes, len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, keys_res, "key envelope: keyboard");
    }
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static void on_yface_osc(void *user, int wire_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t payload_len)
{
    struct yai_app *app = user;
    (void)args;
    (void)args_len;
    switch (wire_code) {
    case YETTY_OSC_SC_CLIENT_INPUT_MOUSE:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE:
        yai_report_error(app, "mouse envelope", handle_mouse_envelope(app, payload, payload_len));
        return;
    case YETTY_OSC_SC_CLIENT_INPUT_KEY:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY:
        yai_report_error(app, "key envelope", handle_key_envelope(app, payload, payload_len));
        return;
    case YETTY_OSC_SC_CLIENT_INPUT_RESIZE:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE:
        yai_report_error(app, "resize envelope", handle_resize_envelope(app, payload, payload_len));
        return;
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS:
        return; /* focus is client-side; the host's notion is ignored */
    default:
        return; /* unknown envelope: drop */
    }
}

YETTY_EXTERNAL_CALLBACK
static void on_yface_raw(void *user, const char *bytes, size_t n)
{
    struct yai_app *app = user;
    ydebug("yai: raw stdin n=%zu first=0x%02x pending=%d", n, n ? (unsigned char)bytes[0] : 0,
           app->pending_permission.active);
    yai_report_error(app, "keyboard", keyboard_input(app, bytes, n));
}

YETTY_EXTERNAL_CALLBACK
static void on_stdin_readable(uv_poll_t *poll_handle, int status, int events)
{
    struct yai_app *app = poll_handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char chunk[4096];
    ssize_t nread = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (nread < 0) {
        return;
    }
    if (nread == 0) {
        /* EOF only means EOF for non-ttys (pipes); a raw tty with
         * VMIN=0 legitimately returns 0 between keystrokes. */
        if (!isatty(STDIN_FILENO)) {
            yai_report_error(app, "shutdown", yai_begin_shutdown(app));
        }
        return;
    }
    yai_report_error(app, "input demux", yetty_yface_feed_bytes(app->yface, chunk, (size_t)nread));
}

/*---------------------------------------------------------------------------
 * Signals
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_sigint(uv_signal_t *signal_handle, int signum)
{
    struct yai_app *app = signal_handle->data;
    (void)signum;
    ydebug("yai: SIGINT received, waiting=%d pending=%d", app->waiting,
           app->pending_permission.active);
    /* In many terminals (yetty included) Ctrl-C arrives as SIGINT rather
     * than a 0x03 byte, so this — not the line-editor path — is what
     * actually fires. Route it through the SAME handler as the byte path:
     * deny a pending permission AND interrupt the in-flight turn; when
     * idle, just remind how to exit. Ctrl-C never exits yai (use /exit). */
    yai_report_error(app, "interrupt", editor_interrupt(app));
}

YETTY_EXTERNAL_CALLBACK
static void on_sigwinch(uv_signal_t *signal_handle, int signum)
{
    struct yai_app *app = signal_handle->data;
    (void)signum;
    if (app->hud) {
        yai_report_error(app, "hud viewport", yai_hud_viewport_changed(app->hud));
    }
    /* Re-reserve the docked bar's rows, then re-clip the pinned zone. */
    yai_report_error(app, "hud dock", yai_apply_dock_reservation(app));
    /* Re-establish the text-HUD scroll region for the new size; keep the
     * prompt at the bottom of the resized region. */
    yai_report_error(app, "text hud", yai_renderer_text_hud_reserve(&app->renderer, 0));
    yai_report_error(app, "zone re-clip", yai_renderer_pin_redraw(&app->renderer));
    /* Mirror the new size onto the interop shell's PTY. */
    yai_shell_resize(app);
}

/* SIGTERM / SIGHUP: an external kill or the pane closing. Begin a clean
 * shutdown so the terminal state (mouse subscription, scroll region,
 * raw mode) is restored instead of leaking into the pane. */
YETTY_EXTERNAL_CALLBACK
static void on_term_signal(uv_signal_t *signal_handle, int signum)
{
    struct yai_app *app = signal_handle->data;
    (void)signum;
    yai_report_error(app, "shutdown", yai_begin_shutdown(app));
}

/*---------------------------------------------------------------------------
 * main
 *---------------------------------------------------------------------------*/

/* <state_dir>/yai/transcripts — the per-session diagnostics directory.
 * Shared by main.c and proxy.c (declared in app.h) so the layout lives in
 * one place. Does not create the directory; callers mkdir_p it. */
int yai_transcript_dir(char *out, size_t out_size)
{
    struct yetty_yplatform_paths_ptr_result paths_res = yetty_yplatform_paths_get_platform_paths();
    if (YETTY_IS_ERR(paths_res)) {
        yetty_ycore_error_destroy(paths_res.error);
        return -1;
    }
    int written = snprintf(out, out_size, "%s/yai/transcripts", paths_res.value->state_dir_buf);
    yetty_yplatform_paths_destroy(paths_res.value);
    return (written > 0 && (size_t)written < out_size) ? 0 : -1;
}

/* Open the session transcript under <state_dir>/yai/transcripts. The
 * transcript is a diagnostic mirror — the caller decides whether a failure
 * here is fatal. */
static struct yetty_ycore_void_result transcript_open(struct yai_app *app)
{
    char dir[PATH_MAX];
    if (yai_transcript_dir(dir, sizeof(dir)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: state dir unavailable");
    }
    yetty_yplatform_mkdir_p(dir);
    int written = snprintf(app->transcript_path, sizeof(app->transcript_path),
                           "%s/transcript-%s.jsonl", dir, app->transcript_tag);
    if (written < 0 || (size_t)written >= sizeof(app->transcript_path)) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: path truncated");
    }
    app->transcript_file = fopen(app->transcript_path, "a");
    if (!app->transcript_file) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: fopen failed");
    }
    return YETTY_OK_VOID();
}

/* The fd the child's stderr is redirected to (stderr-<tag>.log, alongside
 * the transcript). */
static struct yetty_ycore_int_result child_stderr_log_open(const char *file_tag)
{
    char dir[PATH_MAX];
    if (yai_transcript_dir(dir, sizeof(dir)) != 0) {
        return YETTY_ERR(yetty_ycore_int, "child_stderr_log_open: state dir unavailable");
    }
    yetty_yplatform_mkdir_p(dir);
    char stderr_log_path[PATH_MAX];
    int written =
        snprintf(stderr_log_path, sizeof(stderr_log_path), "%s/stderr-%s.log", dir, file_tag);
    if (written < 0 || (size_t)written >= sizeof(stderr_log_path)) {
        return YETTY_ERR(yetty_ycore_int, "child_stderr_log_open: path truncated");
    }
    int stderr_fd = open(stderr_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd < 0) {
        return YETTY_ERR(yetty_ycore_int, "child_stderr_log_open: open failed");
    }
    return YETTY_OK(yetty_ycore_int, stderr_fd);
}

static struct yetty_ycore_void_result event_loop_setup(struct yai_app *app)
{
    if (uv_loop_init(&app->loop) != 0) {
        return YETTY_ERR(yetty_ycore_void, "event_loop_setup: uv_loop_init failed");
    }
    if (uv_timer_init(&app->loop, &app->kill_timer) != 0) {
        return YETTY_ERR(yetty_ycore_void, "event_loop_setup: uv_timer_init failed");
    }
    app->kill_timer.data = app;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result input_watchers_start(struct yai_app *app)
{
    if (uv_poll_init(&app->loop, &app->stdin_poll, STDIN_FILENO) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: uv_poll_init failed");
    }
    app->stdin_poll.data = app;
    if (uv_poll_start(&app->stdin_poll, UV_READABLE, on_stdin_readable) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: uv_poll_start failed");
    }
    if (uv_signal_init(&app->loop, &app->sigint_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigint uv_signal_init failed");
    }
    app->sigint_handle.data = app;
    if (uv_signal_start(&app->sigint_handle, on_sigint, SIGINT) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigint uv_signal_start failed");
    }
    if (uv_signal_init(&app->loop, &app->sigwinch_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigwinch uv_signal_init failed");
    }
    app->sigwinch_handle.data = app;
    if (uv_signal_start(&app->sigwinch_handle, on_sigwinch, SIGWINCH) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigwinch uv_signal_start failed");
    }
    /* SIGTERM / SIGHUP → clean shutdown, so an external kill or a closed
     * pane restores the terminal instead of leaking input subscriptions. */
    if (uv_signal_init(&app->loop, &app->sigterm_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigterm uv_signal_init failed");
    }
    app->sigterm_handle.data = app;
    if (uv_signal_start(&app->sigterm_handle, on_term_signal, SIGTERM) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigterm uv_signal_start failed");
    }
    if (uv_signal_init(&app->loop, &app->sighup_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sighup uv_signal_init failed");
    }
    app->sighup_handle.data = app;
    if (uv_signal_start(&app->sighup_handle, on_term_signal, SIGHUP) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sighup uv_signal_start failed");
    }
    /* SIGCHLD → reap the interop shell (shell.c; engine children belong
     * to libuv's own reaper). */
    if (yai_shell_sigchld_start(app) != 0) {
        return YETTY_ERR(yetty_ycore_void, "input_watchers_start: sigchld watcher failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result print_banner(const struct yai_app *app)
{
    printf(YAI_MINT YAI_BOLD
           "yai" YAI_RESET " " YAI_MUTED "engine %s · session %s" YAI_RESET "\n" YAI_DIM
           "transcript=%s  hud=%s" YAI_RESET "\n" YAI_DIM
           "Type a message. /help for keys + commands; /quit or Ctrl-D to exit; Ctrl-C "
           "interrupts a turn; ! switches to the shell." YAI_RESET "\n",
           app->engine_name, app->session_id[0] ? app->session_id : "(new)", app->transcript_path,
           app->hud ? "on" : (app->renderer.text_hud ? "text" : "off"));
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "print_banner: flush");
    return YETTY_OK_VOID();
}

/* The engine object for `name` ("claude" / "codex" / "gemini"). */
static struct yetty_yclass_object_ptr_result create_engine(const char *name)
{
    if (strcmp(name, "codex") == 0) {
        struct yetty_yclass_object_ptr_result codex_res = yetty_yai_codex_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, codex_res, "create_engine: codex");
        return codex_res;
    }
    if (strcmp(name, "gemini") == 0) {
        struct yetty_yclass_object_ptr_result gemini_res = yetty_yai_gemini_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, gemini_res, "create_engine: gemini");
        return gemini_res;
    }
    struct yetty_yclass_object_ptr_result claude_res = yetty_yai_claude_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, claude_res, "create_engine: claude");
    return claude_res;
}

/* The line-editor strategy object for `mode` ("vi" → yai:vi, else
 * yai:emacs). */
static struct yetty_yclass_object_ptr_result create_editor(const char *mode)
{
    if (mode && strcmp(mode, "vi") == 0) {
        struct yetty_yclass_object_ptr_result vi_res = yetty_yai_vi_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, vi_res, "create_editor: vi");
        return vi_res;
    }
    struct yetty_yclass_object_ptr_result emacs_res = yetty_yai_emacs_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, emacs_res, "create_editor: emacs");
    return emacs_res;
}

/* Swap the editor strategy at runtime (from /config). The line buffer
 * lives in the app, so only the strategy object is replaced; the new
 * mode's indicator is reset. No-op when already in `mode`. */
static struct yetty_ycore_void_result yai_set_edit_mode(struct yai_app *app, const char *mode)
{
    const char *target = (mode && strcmp(mode, "vi") == 0) ? "vi" : "emacs";
    if (strcmp(target, app->editor_mode_name) == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object_ptr_result editor_res = create_editor(target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, editor_res, "yai_set_edit_mode: create");
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(app->editor);
    app->editor = editor_res.value;
    app->editor_mode_name = target;
    /* The edit-mode knob edits the global default, not a per-backend override. */
    snprintf(app->config.edit_mode, sizeof(app->config.edit_mode), "%s", target);
    /* vi enters in insert; emacs has no modal indicator. The first
     * keystroke refreshes this, but set it now so the prompt is right
     * the instant the mode changes. */
    snprintf(app->edit_status, sizeof(app->edit_status), "%s",
             strcmp(target, "vi") == 0 ? "[I]" : "");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "yai_set_edit_mode: free old editor");
    return YETTY_OK_VOID();
}

/* Print CLI usage. Plain fprintf to the given stream — a --help / bad-arg
 * path that exits immediately, so no Result (matching the other argv
 * diagnostics in main()). */
static void print_usage(FILE *stream, const char *program)
{
    fprintf(
        stream,
        "yai — a hackable AI-CLI TUI for yetty (Claude Code / Codex / Gemini)\n"
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "Options (all engines):\n"
        "  --engine <claude|codex|gemini>  AI CLI to drive (default: claude)\n"
        "  --model <id>                    model override (engine-specific id)\n"
        "  --edit-mode <emacs|vi>          line-editor mode (default: emacs)\n"
        "  --submit-key <enter|alt-enter>  which key sends; the other inserts a "
        "newline (default: enter)\n"
        "  --show-thinking                 stream dim \"thinking\" text\n"
        "  --fold-lines <n>                tool-output preview cap (default: 8)\n"
        "  --no-hud                        no ygui status window; stats as plain text\n"
        "  --hud-float                     float the HUD window instead of docking it\n"
        "  --resume <id>                   resume a conversation (claude session / codex "
        "thread)\n"
        "  --markdown                      render answers as SDF/MSDF markdown figures "
        "(markdown-mode yetty; yetty host only)\n"
        "  --no-markdown                   render answers as plain text (markdown-mode text)\n"
        "  --animate                       animated activity glyph (costs ~100%% CPU; "
        "default static)\n"
        "  --rpc <port>                    msgpack-RPC control server on 127.0.0.1:<port>\n"
        "  --connect <port> <method> [a…]  client: drive another running yai's --rpc server, "
        "print reply, exit\n"
        "                                  (methods: run \"<line>\" · status)\n"
        "  -h, --help                      show this help and exit\n"
        "\n"
        "Options (claude):\n"
        "  --permission-mode <manual|acceptEdits|plan|auto|dontAsk|bypass>\n"
        "                                  (auto = the CLI's classifier; bypass = approve all)\n"
        "  --allowed-preset <curated|readonly|edit|full>\n"
        "  --allowed-tools <comma-list>    explicit allowlist (overrides the preset)\n"
        "  --claude-effort <auto|low|medium|high|xhigh|max>\n"
        "\n"
        "Options (codex):\n"
        "  --codex-sandbox <read-only|workspace-write|danger-full-access>\n"
        "  --codex-approval <never|on-request|untrusted>   (exec: only 'never' is serviceable)\n"
        "  --codex-effort <minimal|low|medium|high>\n"
        "\n"
        "Options (gemini):\n"
        "  --gemini-approval <default|auto_edit|yolo>\n"
        "\n"
        "Settings persist to the yetty config file; flags override the file for the run.\n"
        "At the prompt:\n"
        "  <text> + Enter   send a message (Alt+Enter inserts a newline;\n"
        "                   swap with --submit-key alt-enter or /config)\n"
        "  /quit, /exit     quit (or Ctrl-D)\n"
        "  /config          open the config panel (model, permissions, …)\n"
        "  !                hand the keyboard to the interop shell (zsh/bash on a PTY;\n"
        "                   your bindings work); focus returns when the command finishes\n"
        "                   (Ctrl-] to come back early)\n"
        "  /shell           drop to a separate interactive shell until it exits\n"
        "  Ctrl-G           edit the message in $EDITOR (with the last reply for reference)\n"
        "  Ctrl-C           interrupt the turn in flight\n"
        "  Tab / Up / Down  slash-command completion menu\n"
        "\n"
        "Each session's JSONL transcript is written under "
        "$XDG_STATE_HOME/yetty/yai/transcripts/ (transcript-<session>.jsonl)\n",
        program);
}

int main(int argc, char **argv)
{
    ytrace_init();
    /* Honor the user's locale for time formatting (quota reset times use the
     * locale's month names and am/pm markers). */
    setlocale(LC_TIME, "");

    /* Client mode: `yai --connect <port> <method> [args...]` makes this
     * yai a one-shot client of ANOTHER running yai's --rpc server — send
     * one request, print the response, exit. Handled before any terminal /
     * engine setup, since the client needs none of it. */
    for (int arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], "--connect") == 0) {
            if (arg_index + 2 >= argc) {
                fprintf(stderr, "yai: --connect needs <port> <method> [args...]\n");
                return 2;
            }
            int peer_port = (int)strtol(argv[arg_index + 1], NULL, 10);
            const char *method = argv[arg_index + 2];
            const char *const *params = (const char *const *)&argv[arg_index + 3];
            int param_count = argc - (arg_index + 3);
            return yai_control_client_main("127.0.0.1", peer_port, method, params, param_count);
        }
    }

    /* Settings come from three layers, lowest priority first: built-in
     * defaults, the YAML config file, then the command-line flags below.
     * yai has no configuration environment variables. */
    struct yai_app *app = calloc(1, sizeof(*app));
    if (!app) {
        return 1;
    }
    app->session_start_ms = yai_monotonic_ms();
    yai_config_defaults(app);
    yai_config_load(app);

    /* Flags that take a value and map straight onto a setting (engine and the
     * boolean flags are handled explicitly below for validation / no-value). */
    const struct {
        const char *flag;
        const char *key;
    } value_flags[] = {
        {"--model", "model"},
        {"--edit-mode", "edit-mode"},
        {"--submit-key", "submit-key"},
        {"--fold-lines", "fold-lines"},
        {"--permission-mode", "permission-mode"},
        {"--allowed-preset", "allowed-preset"},
        {"--allowed-tools", "allowed-tools"},
        {"--claude-effort", "claude-effort"},
        {"--codex-sandbox", "codex-sandbox"},
        {"--codex-approval", "codex-approval"},
        {"--codex-effort", "codex-effort"},
        {"--gemini-approval", "gemini-approval-mode"},
    };
    const char *resume_session_id = NULL;
    int animate_requested = 0;
    int rpc_port = 0; /* external control server; 0 = off (set via --rpc) */
    for (int arg_index = 1; arg_index < argc; arg_index++) {
        const char *arg = argv[arg_index];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(stdout, argv[0]);
            free(app);
            return 0;
        } else if (strcmp(arg, "--markdown") == 0) {
            yai_config_set(app, "markdown-mode", "yetty");
        } else if (strcmp(arg, "--no-markdown") == 0) {
            yai_config_set(app, "markdown-mode", "text");
        } else if (strcmp(arg, "--animate") == 0) {
            animate_requested = 1;
        } else if (strcmp(arg, "--show-thinking") == 0) {
            yai_config_set(app, "show-thinking", "1");
        } else if (strcmp(arg, "--no-hud") == 0) {
            yai_config_set(app, "hud-on", "0");
        } else if (strcmp(arg, "--hud-float") == 0) {
            yai_config_set(app, "hud-float", "1");
        } else if (strcmp(arg, "--rpc") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "yai: --rpc needs a port\n");
                free(app);
                return 2;
            }
            rpc_port = (int)strtol(argv[++arg_index], NULL, 10);
        } else if (strcmp(arg, "--resume") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "yai: --resume needs an id\n");
                free(app);
                return 2;
            }
            resume_session_id = argv[++arg_index];
        } else if (strcmp(arg, "--engine") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "yai: --engine needs a name (claude / codex / gemini)\n");
                free(app);
                return 2;
            }
            const char *value = argv[++arg_index];
            if (strcmp(value, "claude") != 0 && strcmp(value, "codex") != 0 &&
                strcmp(value, "gemini") != 0) {
                fprintf(stderr, "yai: unknown engine '%s' (claude / codex / gemini)\n", value);
                free(app);
                return 2;
            }
            /* CLI override: change the ACTIVE engine for this session only, then
             * re-resolve so the active backend's overrides take effect. The
             * persisted config->engine is left untouched so a later save does
             * not bake the override into the file. */
            app->engine_name = engine_canonical(value);
            yai_config_resolve(app);
        } else {
            int matched = 0;
            for (size_t flag_index = 0; flag_index < sizeof(value_flags) / sizeof(value_flags[0]);
                 flag_index++) {
                if (strcmp(arg, value_flags[flag_index].flag) == 0) {
                    if (arg_index + 1 >= argc) {
                        fprintf(stderr, "yai: %s needs a value\n", arg);
                        free(app);
                        return 2;
                    }
                    yai_config_set(app, value_flags[flag_index].key, argv[++arg_index]);
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                fprintf(stderr, "yai: unrecognized argument '%s'\n", arg);
                print_usage(stderr, argv[0]);
                free(app);
                return 2;
            }
        }
    }
    /* Materialize the resolved config to disk so the file always reflects the
     * current schema (per-engine sections + the global hud_format) and is
     * self-documenting from the first run — even if this session is later
     * killed rather than exited cleanly. Best-effort: the renderer isn't up
     * yet, so absorb any error here rather than routing it through the UI. */
    struct yetty_ycore_void_result materialize_res = yai_config_save(app);
    if (YETTY_IS_ERR(materialize_res)) {
        yetty_ycore_error_destroy(materialize_res.error);
    }

    const char *engine_name = app->engine_name;
    struct yetty_yclass_object_ptr_result engine_res = create_engine(engine_name);
    if (YETTY_IS_ERR(engine_res)) {
        yai_report_error(app, "engine create",
                         (struct yetty_ycore_void_result){.ok = 0, .error = engine_res.error});
        free(app);
        return 1;
    }
    app->engine = engine_res.value;

    /* The line-editor strategy: emacs (default) or vi (resolved into
     * app->editor_mode_name by defaults/file/CLI). */
    const char *edit_mode = app->editor_mode_name;
    struct yetty_yclass_object_ptr_result editor_res = create_editor(edit_mode);
    if (YETTY_IS_ERR(editor_res)) {
        yai_report_error(app, "editor create",
                         (struct yetty_ycore_void_result){.ok = 0, .error = editor_res.error});
        yai_report_error(app, "engine destroy", yetty_yclass_object_free(app->engine));
        free(app);
        return 1;
    }
    app->editor = editor_res.value;
    app->editor_mode_name = (strcmp(edit_mode, "vi") == 0) ? "vi" : "emacs";
    if (strcmp(edit_mode, "vi") == 0) {
        snprintf(app->edit_status, sizeof(app->edit_status), "[I]");
    }

    /* session_id is the engine's resume token. Claude mints it client-
     * side; codex mints it server-side (stays empty until
     * thread.started); gemini has none. The transcript/log files are
     * named by a tag that always exists. */
    char file_tag[48];
    struct yetty_ycore_void_result tag_res = generate_session_id(file_tag, sizeof(file_tag));
    if (YETTY_IS_ERR(tag_res)) {
        yai_report_error(app, "session id", tag_res);
        yai_report_error(app, "engine destroy", yetty_yclass_object_free(app->engine));
        free(app);
        return 1;
    }
    snprintf(app->transcript_tag, sizeof(app->transcript_tag), "%s", file_tag);
    if (resume_session_id) {
        if (strlen(resume_session_id) >= sizeof(app->session_id)) {
            fprintf(stderr, "yai: --resume id too long (max %zu chars)\n",
                    sizeof(app->session_id) - 1);
            yai_report_error(app, "engine destroy", yetty_yclass_object_free(app->engine));
            free(app);
            return 2;
        }
        app->resume_requested = 1;
        snprintf(app->session_id, sizeof(app->session_id), "%s", resume_session_id);
    } else if (strcmp(engine_name, "claude") == 0) {
        snprintf(app->session_id, sizeof(app->session_id), "%s", file_tag);
    }
    yai_renderer_init(&app->renderer, app->renderer.fold_lines, app->renderer.show_thinking,
                      engine_name);
    /* markdown-mode "yetty" renders answers through the SDF/MSDF markdown
     * facility (a ycat figure envelope); only the yetty host can display the
     * envelope, so require it — off-host falls back to plain text. Parallel to
     * the want_ygui_hud gate below. */
    app->renderer.render_markdown =
        strcmp(yai_effective_markdown_mode(app), "yetty") == 0 && app->renderer.pin_shader_glyphs;
    /* --animate: the animated shader glyph (default static — see render.h). */
    app->renderer.animate_glyph = animate_requested;
    yai_renderer_pin_setup(&app->renderer, app->stdin_buf, &app->stdin_len, &app->stdin_cursor);
    app->renderer.edit_status_ptr = app->edit_status;
    app->history_browse = -1;
    app->shell_master_fd = -1; /* interop shell down (0 is a real fd) */
    app->echo_input = isatty(STDIN_FILENO);
    yai_report_error(app, "command table", yai_command_table_init(&app->commands));

    /* The transcript is a diagnostic mirror — a failure is reported but
     * not fatal: the session runs without it. */
    yai_report_error(app, "transcript", transcript_open(app));

    /* No log file → the child's stderr stays on ours. Explicit,
     * reported degradation, not a silent one. */
    int stderr_fd = STDERR_FILENO;
    struct yetty_ycore_int_result stderr_log_res = child_stderr_log_open(file_tag);
    if (YETTY_IS_ERR(stderr_log_res)) {
        yai_report_error(app, "child stderr log",
                         (struct yetty_ycore_void_result){.ok = 0, .error = stderr_log_res.error});
    } else {
        stderr_fd = stderr_log_res.value;
    }
    app->child_stderr_fd = stderr_fd;

    /* Raw input BEFORE the first envelope write — otherwise the tty
     * echoes the ESC bytes back and the host renders "^[" garbage. */
    yai_report_error(app, "raw input", enter_raw_input(app));

    /* The ygui HUD window is built only when hud-on is true AND visuals.hud
     * selects it; "text" falls through to the text status bar. A ygui
     * message-search (visuals.message-search = yetty) needs the same ygui
     * framework even when the HUD itself is text/off, so it requests a
     * framework-only hud object (no status window). Off the yetty host
     * yai_hud_create returns NULL regardless and every visual degrades
     * to its text form. */
    int want_ygui_hud =
        yai_effective_hud_on(app) && strcmp(yai_effective_hud_mode(app), "yetty") == 0;
    int want_ygui_search = strcmp(yai_effective_message_search_mode(app), "yetty") == 0;
    struct yai_hud_ptr_result hud_res =
        yai_hud_create(want_ygui_hud, yai_effective_hud_float(app), want_ygui_search);
    if (YETTY_IS_ERR(hud_res)) {
        yai_report_error(app, "hud create",
                         (struct yetty_ycore_void_result){.ok = 0, .error = hud_res.error});
        app->hud = NULL; /* run without the window */
    } else {
        app->hud = hud_res.value;
    }
    /* Whenever there is no ygui HUD WINDOW (hud-mode "text", non-yetty host,
     * creation failed, or a framework-only hud that exists just for the ygui
     * message-search), drive the renderer's text status bar instead. This is
     * independent of the host: an explicit hud-mode "text" under yetty still
     * wants the text bar, so it must NOT key off pin_shader_glyphs
     * (host==yetty) — doing so wrongly suppressed the text bar whenever yai
     * ran inside yetty. Skipped only when the HUD is disabled (hud_on=false)
     * or stdout is not a tty (pin_enabled is false: no bottom row to pin). */
    if (!yai_hud_has_window(app->hud) && app->renderer.pin_enabled && yai_effective_hud_on(app)) {
        app->renderer.text_hud = 1;
    }
    /* Parse the configured HUD format (both backends read app->hud_format:
     * the ygui HUD builds labels from it, the text bar expands its row 0).
     * A bad format falls back to the built-in default so the HUD always has
     * content. */
    struct yetty_ycore_void_result format_res =
        yai_hud_format_parse(&app->hud_format, yai_effective_hud_format(app));
    if (YETTY_IS_ERR(format_res)) {
        yai_report_error(app, "hud format", format_res);
        yai_report_error(app, "hud format default",
                         yai_hud_format_parse(&app->hud_format, YAI_DEFAULT_HUD_FORMAT));
    }
    if (app->hud) {
        yai_report_error(app, "hud set format", yai_hud_set_format(app->hud, &app->hud_format));
    }
    /* Seed the HUD with the current values so it reads sensibly before the
     * first turn lands (idle state, like the legacy HUD's default). */
    snprintf(app->state_text, sizeof(app->state_text), "%s", "idle");
    yai_report_error(app, "hud stats", yai_refresh_hud_stats(app));

    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        yai_report_error(app, "input demux",
                         (struct yetty_ycore_void_result){.ok = 0, .error = yface_res.error});
        yai_report_error(app, "raw input restore", leave_raw_input(app));
        if (app->hud) {
            yai_report_error(app, "hud destroy", yai_hud_destroy(app->hud));
        }
        free(app);
        return 1;
    }
    app->yface = yface_res.value;
    yetty_yface_set_handlers(app->yface, on_yface_osc, on_yface_raw, app);

    struct yetty_ycore_void_result loop_res = event_loop_setup(app);
    if (YETTY_IS_ERR(loop_res)) {
        yai_report_error(app, "event loop", loop_res);
        yai_report_error(app, "raw input restore", leave_raw_input(app));
        if (app->hud) {
            yai_report_error(app, "hud destroy", yai_hud_destroy(app->hud));
        }
        yai_report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
        free(app);
        return 1;
    }

    /* Builtin usage proxy: interpose on the active engine's API traffic so
     * every request/response is mirrored to the proxy transcript (and, for
     * claude, live quota headers are captured). Brought up BEFORE the engine
     * starts so the routing knob is in place at spawn time: claude/gemini
     * inherit the base-URL env var the proxy sets; codex reads the bound port
     * (yai_usage_proxy_port) to build its -c chatgpt_base_url override. A
     * failure is reported but non-fatal — the session just runs unproxied. */
    yai_report_error(app, "usage proxy", yai_usage_proxy_start(app));

    struct yetty_ycore_void_result start_res = yetty_yai_start(app->engine, app);
    if (YETTY_IS_OK(start_res)) {
        start_res = input_watchers_start(app);
    }
    if (YETTY_IS_ERR(start_res)) {
        yai_report_error(app, "engine start", start_res);
        yai_report_error(app, "raw input restore", leave_raw_input(app));
        if (app->hud) {
            yai_report_error(app, "hud destroy", yai_hud_destroy(app->hud));
        }
        yai_report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
        free(app);
        return 1;
    }

    if (app->hud) {
        yai_report_error(app, "mouse subscribe", subscribe_mouse(app));
    }

    /* Optional external control server (--rpc / YAI_RPC_PORT). A bind
     * failure is reported but non-fatal — the session runs without it. */
    if (rpc_port > 0) {
        yai_report_error(app, "control server", yai_control_start(app, rpc_port));
    }

    yai_report_error(app, "hud dock", yai_apply_dock_reservation(app));
    /* Reserve the bottom row for the text HUD (non-yetty tty) before the
     * banner and prompt. At startup there is no conversation yet, so anchor
     * at the top: this clears the screen and homes the cursor, and the
     * banner + prompt below then start at the top instead of jumping to the
     * bottom of the reserved region. In yetty mode reserve is a no-op and
     * the banner just prints into the conversation as before. */
    yai_report_error(app, "text hud", yai_renderer_text_hud_reserve(&app->renderer, 1));
    /* That anchor-top reserve cleared the screen — a full-screen erase the
     * host answers by dropping every compositor figure, including the ones
     * a framework-only hud (ygui message-search over a text HUD) emitted at
     * create. Forget + re-emit so they re-materialize. */
    if (app->hud && app->renderer.text_hud) {
        yai_report_error(app, "hud re-mint", yai_hud_forget_remote(app->hud));
    }
    yai_report_error(app, "banner", print_banner(app));
    yai_report_error(app, "prompt", show_prompt(app));

    uv_run(&app->loop, UV_RUN_DEFAULT);

    /* Drain closing handles. (control was already stopped in begin_shutdown
     * for the normal paths; this covers any exit that skipped it.) The proxy
     * is stopped after the loop, by which point the child is gone and its
     * connections have dropped, so its worker threads exit promptly. */
    yai_control_stop(app);
    yai_usage_proxy_stop(app);
    yai_shell_stop(app);
    uv_close((uv_handle_t *)&app->stdin_poll, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigint_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigwinch_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigterm_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sighup_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigchld_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->kill_timer, yai_handle_closed_cb);
    if (app->child_stdout_pipe_initialized &&
        !uv_is_closing((uv_handle_t *)&app->child_stdout_pipe)) {
        uv_close((uv_handle_t *)&app->child_stdout_pipe, yai_handle_closed_cb);
    }
    uv_run(&app->loop, UV_RUN_NOWAIT);
    if (uv_loop_close(&app->loop) != 0) {
        yai_report_error(app, "event loop close",
                         YETTY_ERR(yetty_ycore_void, "main: uv_loop_close: handles still open"));
    }

    /* libuv left the tty's shared file description non-blocking for the
     * stdin poll. The loop is gone now, so restore blocking writes before
     * the teardown envelopes below: the HUD figure-clear goes out via
     * yetty_yface_emit_to_fd, which retries only EINTR (not EAGAIN) — on a
     * busy PTY a non-blocking write truncates the DCS mid-stream and the
     * tail leaks into the pane as garbage. (The session-time writers poll
     * POLLOUT themselves; this teardown path does not.) */
    int stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);
    if (stdout_flags != -1) {
        (void)fcntl(STDOUT_FILENO, F_SETFL, stdout_flags & ~O_NONBLOCK);
    }

    /* Order matters: clear the HUD figure and stop mouse/dock OSC forwarding
     * while raw mode is still on (so the host's input replies can't echo),
     * and restore cooked mode (leave_raw_input) only afterwards, below. */
    yai_report_error(app, "hud dock release", yai_release_dock_reservation(app));
    yai_report_error(app, "mouse unsubscribe", unsubscribe_mouse(app));
    if (app->hud) {
        yai_report_error(app, "hud destroy", yai_hud_destroy(app->hud));
        app->hud = NULL;
    }
    yai_report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
    yai_report_error(app, "raw input restore", leave_raw_input(app));
    /* Persist the final config on exit (covers changes made via the
     * permission "auto" shortcut and any path that didn't save inline). */
    yai_report_error(app, "config save", yai_config_save(app));
    printf("\n");
    /* Show the resume token so the conversation can be picked up later.
     * For claude this id is also the transcript filename uuid; for codex
     * it's the server thread id (differs from the file tag); gemini has
     * none, so the line is skipped. */
    if (app->session_id[0]) {
        printf(YAI_MINT "session %s" YAI_RESET YAI_DIM
                        " · resume: yai --engine %s --resume %s" YAI_RESET "\n",
               app->session_id, app->engine_name, app->session_id);
    }
    printf(YAI_DIM "transcript saved to %s" YAI_RESET "\n", app->transcript_path);
    yai_report_error(app, "final flush", yai_render_flush_stdout());

    int exit_code = app->exit_code;
    if (app->transcript_file && fclose(app->transcript_file) != 0) {
        app->transcript_file = NULL;
        yai_report_error(app, "transcript close",
                         YETTY_ERR(yetty_ycore_void, "main: transcript fclose failed"));
    }
    if (stderr_fd != STDERR_FILENO && close(stderr_fd) != 0) {
        yai_report_error(app, "stderr log close",
                         YETTY_ERR(yetty_ycore_void, "main: stderr log close failed"));
    }
    for (int queue_index = 0; queue_index < app->queue_len; queue_index++) {
        free(app->queue[queue_index]);
    }
    yai_drop_pending_permission(app);
    yai_command_table_destroy(&app->commands);
    /* The HUD (destroyed above) only borrowed this; free it now. */
    yai_hud_format_free(&app->hud_format);
    yai_renderer_destroy(&app->renderer);
    for (int history_index = 0; history_index < app->history_len; history_index++) {
        free(app->history[history_index]);
    }
    free(app->child_out_buf);
    free(app->btw_out_buf);
    free(app->pending_json);
    free(app->last_response);
    for (size_t deferred_index = 0; deferred_index < app->deferred_count; deferred_index++) {
        free(app->deferred_lines[deferred_index].bytes);
    }
    free(app->deferred_lines);
    struct yetty_ycore_void_result editor_free_res = yetty_yclass_object_free(app->editor);
    if (YETTY_IS_ERR(editor_free_res)) {
        yai_report_error(app, "editor destroy", editor_free_res);
    }
    struct yetty_ycore_void_result engine_free_res = yetty_yclass_object_free(app->engine);
    if (YETTY_IS_ERR(engine_free_res)) {
        yai_report_error(app, "engine destroy", engine_free_res);
    }
    free(app);
    return exit_code;
}
