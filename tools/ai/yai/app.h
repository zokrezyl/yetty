/*
 * app.h — yai's application state + the shared surface the engine
 * classes compile against.
 *
 * The engine is a yclass object: base class `yai:engine` (engine.c)
 * with one subclass per CLI — claude.c (Claude Code, persistent
 * stream-json child), codex.c and gemini.c (one child per turn, both
 * under the shared `yai:turn_engine` intermediate in turn-engine.c).
 * main.c owns the event loop, terminal, line editor and rendering; the
 * engines own everything protocol-specific and reach the app through
 * this header.
 *
 * Error discipline: every function that can fail returns a Result and
 * chains its causes (3-arg YETTY_ERR / YETTY_RETURN_IF_ERR). Results
 * are absorbed — printed and destroyed — ONLY by yai_report_error, at
 * the places with nowhere left to propagate: the main loop and the
 * YETTY_EXTERNAL_CALLBACK libuv boundaries.
 */
#ifndef YAI_APP_H
#define YAI_APP_H

#include "commands.h"
#include "event.h"
#include "hud.h"
#include "render.h"

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdio.h>
#include <termios.h>

#include <uv.h>
#include <yyjson.h>

#define YAI_QUEUE_MAX 16
/* Completion menu rows under the prompt while typing a /command. */
#define YAI_MENU_ROWS YAI_RENDERER_MENU_ROWS
#define YAI_HISTORY_MAX 100
/* Max line-editor undo depth (vi `u`); oldest steps drop past this. */
#define YAI_UNDO_MAX 256

/* One line-editor undo step: a heap copy of the line and its cursor as
 * they were before an edit. See struct yai_app's undo_stack. */
struct yai_undo_entry {
    char *bytes; /* malloc'd; `len` bytes, not NUL-terminated */
    size_t len;
    size_t cursor;
};
/* Tools the user marked "always allow" this session (permission prompt). */
#define YAI_ALWAYS_ALLOW_MAX 32

struct yetty_yface;
struct yai_control;

struct yai_session_usage {
    uint64_t input;
    uint64_t output;
    uint64_t cache_read;
    uint64_t cache_creation;
    double cost;
    int turns;
};

/* One in-flight can_use_tool permission request. The claude CLI blocks
 * the tool until yai answers with a control_response. The state lives
 * here because the line editor consumes the next typed y/n as the
 * verdict; the protocol answer is the engine's resolve_permission
 * slot (a no-op for engines without a permission protocol). */
struct yai_pending_permission {
    int active;
    char request_id[80];
    char tool_name[80];
    /* Deep copy of the request's `input` object — echoed back as
     * updatedInput on allow. Owned while active. */
    yyjson_mut_doc *input_doc;
};

struct yai_app {
    uv_loop_t loop;
    uv_process_t child_process;
    uv_pipe_t child_stdin_pipe;
    uv_pipe_t child_stdout_pipe;
    uv_poll_t stdin_poll;
    uv_signal_t sigint_handle;
    uv_signal_t sigwinch_handle;
    uv_signal_t sigterm_handle;
    uv_signal_t sighup_handle;
    uv_timer_t kill_timer;

    struct yai_hud *hud;
    struct yai_renderer renderer;
    struct yai_session_usage usage;

    /* Input demux + routing. */
    struct yetty_yface *yface;
    /* External msgpack-RPC control server (NULL unless --rpc/YAI_RPC_PORT
     * was given): lets an outside client inject commands and query state
     * over a localhost TCP port. */
    struct yai_control *control;
    int focus_gui; /* 1 = the HUD window owns keyboard input */
    int mouse_subscribed;
    /* Bottom band (px) currently reserved with the yetty host for the
     * docked HUD via YETTY_OSC_CS_CONTENT_INSET. Re-emitting an unchanged
     * inset makes the host re-resize the grid and re-raise SIGWINCH, whose
     * handler re-emits the inset — a reflow feedback loop. Track the last
     * emitted value and only send on a real change. 0 = nothing reserved. */
    float dock_reserved_px;
    int echo_input;   /* echo typed bytes (stdin is a tty in raw mode) */
    int escape_state; /* line-editor ESC/CSI decode: 0 none, 1 ESC, 2 CSI */
    int escape_param; /* numeric CSI parameter (last group) */
    struct termios saved_termios;
    int termios_saved;

    FILE *transcript_file;
    char transcript_path[256];
    /* THE conversation id: claude session uuid / codex thread id — the
     * resume token of its CLI. Claude mints it client-side (we pass
     * --session-id); codex mints it server-side (empty until
     * thread.started); gemini has no resume token yet. */
    char session_id[128];

    /* The engine object (yai:claude, yai:codex or yai:gemini) and its
     * display name. Dispatch goes through the generated yetty_yai_*
     * stubs — local vtable dispatch, ctx NULL. */
    struct yetty_yclass_object *engine;
    const char *engine_name;

    int child_stderr_fd;
    int resume_requested; /* --resume given: session_id is an engine id */

    int child_alive;
    int child_stdin_open;
    /* The child stdout uv pipe has been uv_pipe_init'ed at least once
     * (claude: at session start; per-turn engines: at the first turn's
     * spawn). Guards the final-cleanup uv_close — uv_is_closing on a
     * never-initialized handle is undefined. */
    int child_stdout_pipe_initialized;
    /* Open uv handles of the current child (process + pipes) — the
     * per-turn engines respawn only once this drops back to zero. */
    int child_open_handles;
    /* The turn in flight ended in failure (nonzero child exit,
     * turn.failed / error event). Read and cleared at the turn
     * boundary to render a distinct failed state. */
    int turn_failed;
    /* The most recent turn ended with an error `result` event (e.g. a
     * failed --resume). If the child then exits, that exit is explained,
     * so the EOF handler skips its "exited unexpectedly" warning. */
    int saw_result_error;
    int waiting;       /* a turn is in flight */
    /* Running token estimate for the in-flight request (claude's
     * system/thinking_tokens events; cumulative, reset at each turn start).
     * Shown next to the "thinking" activity on the left of the HUD, while
     * app->usage holds the cumulative session total on the right. */
    uint64_t estimated_tokens;
    int shutting_down; /* /quit or EOF — wind the child down */
    int exit_code;

    /* child stdout line assembly (lines can be MBs — figures) */
    char *child_out_buf;
    size_t child_out_len;
    size_t child_out_cap;

    /* JSON-document reassembly across lines. Claude/codex emit JSONL —
     * one complete object per line, so this parses and clears on the
     * first line. Gemini emits ONE pretty-printed (multi-line) object
     * per turn, so its lines accumulate here until the closing brace
     * completes the document. Reset at each child EOF. */
    char *pending_json;
    size_t pending_json_len;
    size_t pending_json_cap;

    /* The assistant's most recent answer text (accumulated from text
     * deltas; reset at each message start). Ctrl-G opens the external
     * editor with the current draft plus this, for reference/quoting. */
    char *last_response;
    size_t last_response_len;
    size_t last_response_cap;

    /* line editor buffer (terminal-focused typing). The buffer lives
     * here (not in the editor object) so the renderer's pinned prompt
     * can borrow it; the editor is a polymorphic strategy that mutates
     * it. */
    char stdin_buf[8192];
    size_t stdin_len;
    size_t stdin_cursor; /* byte offset of the editing cursor */
    /* Kill ring / vi register: one slot, shared by emacs yank and vi
     * delete-into-register. */
    char kill_buf[8192];
    size_t kill_len;
    /* Multi-level undo for the line editor (vi `u`). Each text-changing
     * normal-mode command — and each whole insert session (i…ESC) — pushes
     * the pre-change line onto the stack; `u` pops and restores, so
     * repeated `u` walks back through the previous edits. Entries are heap
     * copies, freed when the line is submitted/cancelled. The "group"
     * fields hold the snapshot for the command/session in progress: a
     * normal command opens and commits it in one keystroke, while an insert
     * session keeps it open until ESC so the whole insertion is one undo. */
    struct yai_undo_entry undo_stack[YAI_UNDO_MAX];
    int undo_depth;
    char undo_group_buf[8192];
    size_t undo_group_len;
    size_t undo_group_cursor;
    int undo_group_open;
    int undo_group_dirty;
    /* Editor mode indicator drawn before the prompt ("", "[N]", "[I]"). */
    char edit_status[16];

    /* The line-editor strategy object: yai:emacs or yai:vi (both
     * subclass yai:editor). Dispatch goes through yetty_yai_feed_byte;
     * switching mode recreates this as the other class. */
    struct yetty_yclass_object *editor;
    const char *editor_mode_name; /* "emacs" / "vi", for /config */

    /* input history (submitted lines, newest last; up/down browses) */
    char *history[YAI_HISTORY_MAX];
    int history_len;
    int history_browse; /* -1 = not browsing; else index into history */
    char history_stash[8192];
    size_t history_stash_len;

    /* messages typed while a turn was in flight */
    char *queue[YAI_QUEUE_MAX];
    int queue_len;

    struct yai_pending_permission pending_permission;

    /* Tools the user chose "always allow" for this session — the engine's
     * can_use_tool requests for these auto-approve without prompting
     * (claude only; the other engines have no interactive prompt). */
    char always_allow_tools[YAI_ALWAYS_ALLOW_MAX][80];
    int always_allow_count;

    /* /config dialog knob mapping — assembled when the dialog opens
     * (knob 0 = yai edit mode; the rest = the engine's config_knob).
     * The click sync diffs each against `selected` and applies: the
     * edit-mode knob swaps the editor, an engine knob goes through the
     * engine's apply_config slot. */
    struct {
        int is_edit_mode; /* 1 = yai edit mode (swap editor object) */
        int is_model;     /* 1 = model knob (setenv YAI_MODEL) */
        char key[64];     /* env knob key (engine + model knobs) */
        char options[YAI_HUD_CONFIG_KNOB_MAX_OPTIONS][48];
        int option_count;
        int selected;
    } config_knobs[YAI_HUD_CONFIG_MAX_KNOBS];
    int config_knob_count;
    int config_show_thinking_applied;
    int config_fold_lines_applied;

    /* Text settings TUI: a non-yetty alt-screen modal over config_knobs
     * (the ygui dialog's text-terminal counterpart). active = on screen;
     * selected = highlighted knob row; initial = each knob's selection
     * when the TUI opened, so only changed knobs are applied on close. */
    int config_tui_active;
    int config_tui_selected;
    int config_tui_escape; /* arrow-key decode: 0 none, 1 ESC, 2 ESC[ */
    int config_tui_initial[YAI_HUD_CONFIG_MAX_KNOBS];

    /* Slash commands: locals + the CLI's list from `initialize`. */
    struct yai_command_table commands;
    /* Live completion menu state (drawn below the input line). */
    int menu_visible;
    size_t menu_matches[YAI_MENU_ROWS];
    size_t menu_match_count;
    size_t menu_selected;
};

/*---------------------------------------------------------------------------
 * Shared surface (defined in main.c, called by the engine classes)
 *---------------------------------------------------------------------------*/

/* Error surfacing — THE end consumer of error chains: print into the
 * scrollback (our UI), then destroy the chain. Cannot fail (its own
 * stdio problems have nowhere to go). Call it only where a Result has
 * no caller left to propagate to. */
void yai_report_error(struct yai_app *app, const char *context,
                      struct yetty_ycore_void_result result);

/* Update the activity surfaces together: the animated shader glyph on
 * the pinned prompt row and, when present, the HUD state text. */
struct yetty_ycore_void_result yai_set_activity(struct yai_app *app, const char *glyph_name,
                                                const char *state_text);

/* Refresh the HUD's right column — the active model line and the
 * cumulative session statistics (app->usage). Cheap; nothing is written
 * until yai_hud_flush. A no-op when the HUD is absent. Call it after
 * usage changes (each turn) and whenever the model changes. */
struct yetty_ycore_void_result yai_refresh_hud_stats(struct yai_app *app);

/* Engine-neutral turn boundary: render the failed state if the turn
 * failed, then pump the queue or re-prompt. */
struct yetty_ycore_void_result yai_turn_finished(struct yai_app *app);

struct yetty_ycore_void_result yai_begin_shutdown(struct yai_app *app);

/* Free the pending permission's owned state and mark it inactive.
 * Pure teardown — cannot fail. */
void yai_drop_pending_permission(struct yai_app *app);

/* Session "always allow" policy for the permission prompt: an engine's
 * tool-permission request for a remembered tool is auto-approved without
 * prompting. yai_tool_remember_allow records one (idempotent, silently
 * caps at YAI_ALWAYS_ALLOW_MAX); yai_tool_always_allowed queries. */
int yai_tool_always_allowed(const struct yai_app *app, const char *tool_name);
void yai_tool_remember_allow(struct yai_app *app, const char *tool_name);

/* One decoded child stdout line: transcript mirror + JSON parse +
 * engine handle_event dispatch. Non-JSON lines are not an error;
 * dispatch failures propagate. */
struct yetty_ycore_void_result yai_handle_child_line(struct yai_app *app, const char *line,
                                                     size_t len);

/* Inject one line as if the user had typed it and pressed Enter (echoes
 * the prompt, then dispatches through the normal input path: slash
 * commands, shell escapes, or a message to the engine). Used by the
 * external control server. Defined in main.c. */
struct yetty_ycore_void_result yai_control_inject(struct yai_app *app, const char *line,
                                                  size_t len);

/* External control server (control.c): a localhost msgpack-RPC TCP server
 * for injecting commands / querying state from outside. start binds and
 * listens (storing the server on app->control); stop tears it down. Both
 * are no-ops / safe when control is absent. */
struct yetty_ycore_void_result yai_control_start(struct yai_app *app, int port);
void yai_control_stop(struct yai_app *app);

/* Client mode: connect to another running yai's --rpc control server, send
 * one request (method + string args), print the response, and return a
 * process exit code. This is how `yai --connect <port> <method> [args...]`
 * makes one yai drive another. Defined in control.c. */
int yai_control_client_main(const char *host, int port, const char *method,
                            const char *const *args, int arg_count);

/* uv callbacks shared by the engines' child spawns. Signatures are
 * dictated by libuv (YETTY_EXTERNAL_CALLBACK) — inner Results are
 * absorbed at this boundary via yai_report_error. */
void yai_child_exit_cb(uv_process_t *process, int64_t exit_status, int term_signal);
void yai_child_stdout_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);
void yai_child_stdout_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer);
void yai_handle_closed_cb(uv_handle_t *handle);

/* Spawn one per-turn child (stdin ignored, stdout piped, stderr → log)
 * and start reading its stdout. Shared by the per-turn engines (codex,
 * gemini) so the post-spawn cleanup path is identical: a uv_read_start
 * failure terminates the child and tears the turn down as a failed
 * turn rather than leaking a live process. Defined in turn-engine.c. */
struct yetty_ycore_void_result yai_turn_engine_spawn(struct yai_app *app, const char *file,
                                                     const char *const *args);

/* Arm the SIGKILL backstop timer against the current child (used after
 * an interrupt SIGINT, and at shutdown). Defined in main.c. */
void yai_arm_child_kill_timer(struct yai_app *app);

static inline uint64_t yai_usage_field(yyjson_val *usage, const char *key)
{
    yyjson_val *value = usage ? yyjson_obj_get(usage, key) : NULL;
    return value ? (uint64_t)yyjson_get_uint(value) : 0;
}

#endif /* YAI_APP_H */
