/*
 * ccc — a minimal, hackable `claude` CLI loop rendering into yetty.
 *
 * Drives the headless `claude` CLI over a bidirectional stream-json
 * pipe, owns its own scrollback, streams responses live, and keeps the
 * non-scrolling status (state, tokens, cost) in a floating ygui window
 * (hud.c) while the conversation scrolls like any CLI.
 *
 * Input model — ALL focus is client-side:
 *
 *   ccc is the pane's single PTY client. stdin is switched to raw input
 *   (output stays cooked) and every byte runs through a yetty_yface
 *   demux: plain bytes are keystrokes, OSC envelopes are pane-wide
 *   mouse / resize events (ccc subscribes via CLIENT_INPUT_SUB +
 *   DECSET ?1500/?1501). Mouse events are hit-tested by ccc itself: a
 *   press inside the HUD window moves the focus to the GUI (titlebar
 *   drag, ccc-side corner resize, future widgets); a press outside
 *   moves it back to the terminal, where keystrokes feed ccc's own
 *   line editor at the prompt. The host arbitrates nothing.
 *
 * Rendering paths into yetty:
 *   1. Agent-drawn figures — the sub-`claude` gets the `yetty` MCP
 *      server; under YETTY_MCP_VIA_PARENT its tools hand the OSC
 *      envelope back through a sentinel in the tool result and ccc
 *      (the single PTY writer) emits it itself.
 *   2. The ygui HUD window — a compositor figure that does NOT scroll.
 *
 * Scrollback: every stream-json event is mirrored to
 * tmp/transcript-<session>.jsonl.
 *
 * Usage:
 *     ./ccc                      # fresh session (claude backend)
 *     ./ccc --backend codex      # drive `codex exec --json` instead
 *     ./ccc --resume <id>        # resume (claude session / codex thread)
 *     CCC_BACKEND=codex ./ccc    # backend via environment
 *     CCC_SHOW_THINKING=1 ./ccc  # show dim thinking text
 *     CCC_FOLD_LINES=20 ./ccc    # tool-output preview cap (default 8)
 *     CCC_NO_HUD=1 ./ccc         # stats as plain lines, no ygui window
 *
 * Type a message at the prompt. Ctrl-D or /quit to exit; Ctrl-C
 * interrupts the turn in flight. "/shell" (or "!cmd") hands the PTY to
 * a shell; the prompt continues when it exits.
 *
 * The prompt is a pinned bottom row and stays available while a turn is
 * in flight (typed messages queue; local commands run immediately). An
 * animated shader glyph on the prompt row shows agent activity, and the
 * in-progress streamed line rides a ticker row above it; both are
 * erased before any history write (render.h: pinned zone).
 */

#include "commands.h"
#include "hud.h"
#include "render.h"

#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <uv.h>
#include <yyjson.h>

#define CCC_DEFAULT_FOLD_LINES 8
#define CCC_TOOL_NAME_MAP_SIZE 128
#define CCC_QUEUE_MAX 16
#define CCC_KILL_TIMEOUT_MS 5000
/* Completion menu rows under the prompt while typing a /command. */
#define CCC_MENU_ROWS CCC_RENDERER_MENU_ROWS
#define CCC_HISTORY_MAX 100
#define CCC_INIT_REQUEST_ID "ccc-init"

#define CCC_PROMPT "\n" CCC_MINT "you ▸ " CCC_RESET

struct ccc_app;

/* Backend polymorphism. Both backends run on the SAME ccc_app state —
 * only message/response handling differs. Claude: one long-lived child
 * with a bidirectional stream-json pipe. Codex: a fresh
 * `codex exec --json [resume <id>]` child per turn — its exit is the
 * turn boundary, and session_id carries the conversation. */
struct ccc_backend_ops {
    const char *name;
    /* Session start: claude spawns its persistent child here; codex
     * spawns per turn and starts nothing. */
    struct yetty_ycore_void_result (*start)(struct ccc_app *app);
    /* Begin a turn with the user's text. */
    struct yetty_ycore_void_result (*send_user_message)(struct ccc_app *app, const char *text);
    /* One decoded JSONL event from the child's stdout. */
    void (*handle_event)(struct ccc_app *app, yyjson_val *event);
    /* Ctrl-C during a turn. */
    struct yetty_ycore_void_result (*interrupt)(struct ccc_app *app);
    /* Child lifecycle policy: process exit / stdout EOF semantics
     * (claude: session death; codex: normal turn boundary). */
    void (*on_child_exit)(struct ccc_app *app, int64_t exit_status);
    void (*on_child_eof)(struct ccc_app *app);
};

struct ccc_tool_name_entry {
    char id[80];
    char name[80];
};

struct ccc_session_usage {
    uint64_t input;
    uint64_t output;
    uint64_t cache_read;
    uint64_t cache_creation;
    double cost;
    int turns;
};

/* One in-flight can_use_tool permission request. The CLI blocks the
 * tool until ccc answers with a control_response. */
struct ccc_pending_permission {
    int active;
    char request_id[80];
    char tool_name[80];
    /* Deep copy of the request's `input` object — echoed back as
     * updatedInput on allow. Owned while active. */
    yyjson_mut_doc *input_doc;
};

struct ccc_app {
    uv_loop_t loop;
    uv_process_t child_process;
    uv_pipe_t child_stdin_pipe;
    uv_pipe_t child_stdout_pipe;
    uv_poll_t stdin_poll;
    uv_signal_t sigint_handle;
    uv_signal_t sigwinch_handle;
    uv_timer_t kill_timer;

    struct ccc_hud *hud;
    struct ccc_renderer renderer;
    struct ccc_session_usage usage;

    /* Input demux + routing. */
    struct yetty_yface *yface;
    int focus_gui; /* 1 = the HUD window owns keyboard input */
    int mouse_subscribed;
    int echo_input;   /* echo typed bytes (stdin is a tty in raw mode) */
    int escape_state; /* line-editor ESC/CSI decode: 0 none, 1 ESC, 2 CSI */
    int escape_param; /* numeric CSI parameter (last group) */
    struct termios saved_termios;
    int termios_saved;

    FILE *transcript_file;
    char transcript_path[256];
    /* THE conversation id, both backends: claude session uuid / codex
     * thread id — each is the resume token of its CLI. Claude mints it
     * client-side (we pass --session-id); codex mints it server-side
     * (empty until thread.started arrives). */
    char session_id[48];

    const struct ccc_backend_ops *backend;

    /* Open uv handles of the current child (process + pipes). Per-turn
     * backends respawn only once this drops to zero. */
    int child_open_handles;
    int child_stderr_fd;
    int resume_requested; /* --resume given: session_id is a backend id */

    int child_alive;
    int child_stdin_open;
    int waiting;       /* a turn is in flight */
    int shutting_down; /* /quit or EOF — wind the child down */
    int exit_code;

    /* child stdout line assembly (lines can be MBs — figures) */
    char *child_out_buf;
    size_t child_out_len;
    size_t child_out_cap;

    /* line editor buffer (terminal-focused typing) */
    char stdin_buf[8192];
    size_t stdin_len;
    size_t stdin_cursor; /* byte offset of the editing cursor */

    /* input history (submitted lines, newest last; up/down browses) */
    char *history[CCC_HISTORY_MAX];
    int history_len;
    int history_browse; /* -1 = not browsing; else index into history */
    char history_stash[8192];
    size_t history_stash_len;

    /* messages typed while a turn was in flight */
    char *queue[CCC_QUEUE_MAX];
    int queue_len;

    struct ccc_tool_name_entry tool_names[CCC_TOOL_NAME_MAP_SIZE];
    int tool_name_next;

    struct ccc_pending_permission pending_permission;

    /* Slash commands: locals + the CLI's list from `initialize`. */
    struct ccc_command_table commands;
    /* Live completion menu state (drawn below the input line). */
    int menu_visible;
    size_t menu_matches[CCC_MENU_ROWS];
    size_t menu_match_count;
    size_t menu_selected;

    int interrupt_request_counter;
};

/*---------------------------------------------------------------------------
 * Error surfacing — main.c is the end consumer: print into the
 * scrollback (our UI), then destroy the chain.
 *---------------------------------------------------------------------------*/

static void report_error(struct ccc_app *app, const char *context,
                         struct yetty_ycore_void_result result)
{
    if (!YETTY_IS_ERR(result)) {
        return;
    }
    char message[512];
    yetty_ycore_error_snprint(message, sizeof(message), result.error);
    ccc_renderer_zone_suspend(&app->renderer);
    printf("\n" CCC_RED "✗ %s: %s" CCC_RESET "\n", context, message);
    fflush(stdout);
    ccc_renderer_zone_resume(&app->renderer);
    yetty_ycore_error_destroy(result.error);
}

/* Update the activity surfaces together: the animated shader glyph on
 * the pinned prompt row and, when present, the HUD state text. */
static void set_activity(struct ccc_app *app, const char *glyph_name, const char *state_text)
{
    ccc_renderer_activity_set(&app->renderer, glyph_name);
    if (app->hud) {
        report_error(app, "hud state", ccc_hud_set_state(app->hud, state_text));
        report_error(app, "hud flush", ccc_hud_flush(app->hud));
    }
}

/*---------------------------------------------------------------------------
 * Small helpers
 *---------------------------------------------------------------------------*/

static int env_flag(const char *name)
{
    const char *value = getenv(name);
    return value && strcmp(value, "") != 0 && strcmp(value, "0") != 0 && strcmp(value, "no") != 0;
}

static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    return (int)strtol(value, NULL, 10);
}

/* RFC 4122 v4 UUID from /dev/urandom; falls back to pid/time salt. */
static void generate_session_id(char *out, size_t out_size)
{
    unsigned char bytes[16];
    int filled = 0;
    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd >= 0) {
        filled = read(urandom_fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes);
        close(urandom_fd);
    }
    if (!filled) {
        uint64_t salt = (uint64_t)getpid() * 2654435761u ^ (uint64_t)uv_hrtime();
        for (size_t index = 0; index < sizeof(bytes); index++) {
            salt = salt * 6364136223846793005ULL + 1442695040888963407ULL;
            bytes[index] = (unsigned char)(salt >> 33);
        }
    }
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);
    snprintf(out, out_size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static void show_prompt(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    if (app->renderer.pin_enabled) {
        /* The prompt is the pinned bottom row — always there, busy or
         * not, so commands can be typed/queued mid-turn. */
        ccc_renderer_pin_show(&app->renderer);
        return;
    }
    if (app->waiting) {
        return; /* legacy (non-tty) mode: no prompt while a turn runs */
    }
    fputs(CCC_PROMPT, stdout);
    fflush(stdout);
}

/*---------------------------------------------------------------------------
 * Raw input mode (output stays cooked — OPOST survives, so "\n" still
 * renders as CRLF in the scrollback).
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result enter_raw_input(struct ccc_app *app)
{
    if (!isatty(STDIN_FILENO)) {
        return YETTY_OK_VOID(); /* pipes need no tty discipline */
    }
    if (tcgetattr(STDIN_FILENO, &app->saved_termios) != 0) {
        return YETTY_ERR(yetty_ycore_void, "enter_raw_input: tcgetattr");
    }
    struct termios raw = app->saved_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | INLCR);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return YETTY_ERR(yetty_ycore_void, "enter_raw_input: tcsetattr");
    }
    app->termios_saved = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result leave_raw_input(struct ccc_app *app)
{
    if (!app->termios_saved) {
        return YETTY_OK_VOID();
    }
    app->termios_saved = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &app->saved_termios) != 0) {
        return YETTY_ERR(yetty_ycore_void, "leave_raw_input: tcsetattr");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Pane-wide mouse subscription (client-input OSC envelopes on stdin)
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result emit_mouse_sub(uint32_t flags)
{
    struct yetty_client_input_sub sub = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
    };
    fflush(stdout);
    struct yetty_ycore_void_result res = yetty_yface_emit_to_fd(
        STDOUT_FILENO, YETTY_OSC_CS_CLIENT_INPUT_SUB, 0, NULL, 0, &sub, sizeof(sub));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "emit_mouse_sub: emit_to_fd");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result subscribe_mouse(struct ccc_app *app)
{
    struct yetty_ycore_void_result sub_res =
        emit_mouse_sub(YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK | YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                       YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_res, "subscribe_mouse: sub envelope");
    fputs("\x1b[?1500h\x1b[?1501h", stdout);
    fflush(stdout);
    app->mouse_subscribed = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result unsubscribe_mouse(struct ccc_app *app)
{
    if (!app->mouse_subscribed) {
        return YETTY_OK_VOID();
    }
    app->mouse_subscribed = 0;
    fputs("\x1b[?1500l\x1b[?1501l", stdout);
    fflush(stdout);
    struct yetty_ycore_void_result res = emit_mouse_sub(0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "unsubscribe_mouse: sub envelope");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Shell escape — /shell [cmd…] or !cmd. ccc hands the PTY to a shell
 * (cooked tty, no mouse envelopes, no stdin polling) and blocks until
 * it exits; then it re-takes ownership and the prompt continues.
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_stdin_readable(uv_poll_t *poll_handle, int status, int events);
YETTY_EXTERNAL_CALLBACK
static void on_sigint(uv_signal_t *signal_handle, int signum);

/* Give the terminal back to plain processes. Mirror of resume below. */
static struct yetty_ycore_void_result suspend_terminal_ownership(struct ccc_app *app)
{
    struct yetty_ycore_void_result unsub_res = unsubscribe_mouse(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, unsub_res, "suspend_terminal: unsubscribe mouse");
    struct yetty_ycore_void_result cooked_res = leave_raw_input(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cooked_res, "suspend_terminal: restore tty");
    uv_poll_stop(&app->stdin_poll);
    uv_signal_stop(&app->sigint_handle);
    return YETTY_OK_VOID();
}

/* Re-take the terminal. Best-effort: every step runs even if an
 * earlier one failed — a half-resumed ccc is unusable. */
static struct yetty_ycore_void_result resume_terminal_ownership(struct ccc_app *app)
{
    struct yetty_ycore_void_result resume = YETTY_OK_VOID();
    resume = yetty_ycore_void_chain(resume, enter_raw_input(app));
    if (app->hud) {
        resume = yetty_ycore_void_chain(resume, subscribe_mouse(app));
    }
    uv_poll_start(&app->stdin_poll, UV_READABLE, on_stdin_readable);
    uv_signal_start(&app->sigint_handle, on_sigint, SIGINT);
    return resume;
}

/* Run `command` (NULL = interactive $SHELL) as the foreground process
 * group of the tty — real job control inside, Ctrl-C hits the shell,
 * not ccc. Blocks the event loop on purpose: while the shell owns the
 * screen, ccc must process nothing. Only callable between turns. */
static struct yetty_ycore_void_result run_shell(struct ccc_app *app, const char *command)
{
    if (app->waiting) {
        return YETTY_ERR(yetty_ycore_void, "run_shell: a turn is in flight — try again after it");
    }
    /* The shell owns the screen: drop the pinned prompt zone until the
     * caller re-shows it after the handoff ends. */
    ccc_renderer_pin_hide(&app->renderer);
    const char *shell = getenv("SHELL");
    if (!shell || !shell[0]) {
        shell = "/bin/sh";
    }
    struct yetty_ycore_void_result suspend_res = suspend_terminal_ownership(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "run_shell: suspend");
    if (app->hud) {
        report_error(app, "hud state", ccc_hud_set_state(app->hud, "⌨ shell"));
        report_error(app, "hud flush", ccc_hud_flush(app->hud));
    }
    printf(CCC_DIM "(entering %s — exit to return to ccc)" CCC_RESET "\n",
           command ? command : shell);
    fflush(stdout);

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
        printf(CCC_DIM "(shell exited %d)" CCC_RESET "\n", WEXITSTATUS(wait_status));
    } else if (WIFSIGNALED(wait_status)) {
        printf(CCC_DIM "(shell killed by signal %d)" CCC_RESET "\n", WTERMSIG(wait_status));
    }
    fflush(stdout);

    if (app->hud) {
        report_error(app, "hud state", ccc_hud_set_state(app->hud, "idle"));
        /* No flush yet — resume re-subscribes first so the emit lands
         * after the tty is ours again. */
    }
    struct yetty_ycore_void_result resume_res = resume_terminal_ownership(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "run_shell: resume");
    if (app->hud) {
        report_error(app, "hud flush", ccc_hud_flush(app->hud));
    }
    return YETTY_OK_VOID();
}

/* Bounce an event ccc did not consume back to the host for its default
 * handling (wheel → terminal scrollback, …). Subscribing made the host
 * forward everything; this is the return path for the rest. */
static struct yetty_ycore_void_result reinject_mouse(const struct yetty_client_input_mouse *mouse)
{
    fflush(stdout);
    struct yetty_ycore_void_result res = yetty_yface_emit_to_fd(
        STDOUT_FILENO, YETTY_OSC_CS_CLIENT_INPUT_REINJECT, 0, NULL, 0, mouse, sizeof(*mouse));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "reinject_mouse: emit_to_fd");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Child stdin writes
 *---------------------------------------------------------------------------*/

struct ccc_child_write {
    uv_write_t request;
    char *data;
};

YETTY_EXTERNAL_CALLBACK
static void on_child_write_done(uv_write_t *request, int status)
{
    struct ccc_child_write *write_request = (struct ccc_child_write *)request;
    (void)status;
    free(write_request->data);
    free(write_request);
}

/* Take ownership of `line` (heap, newline-terminated) and ship it. */
static struct yetty_ycore_void_result write_to_child(struct ccc_app *app, char *line, size_t len)
{
    if (!app->child_alive || !app->child_stdin_open) {
        free(line);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: child not running");
    }
    struct ccc_child_write *write_request = calloc(1, sizeof(*write_request));
    if (!write_request) {
        free(line);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: calloc");
    }
    write_request->data = line;
    uv_buf_t buffer = uv_buf_init(line, (unsigned)len);
    if (uv_write(&write_request->request, (uv_stream_t *)&app->child_stdin_pipe, &buffer, 1,
                 on_child_write_done) != 0) {
        free(write_request->data);
        free(write_request);
        return YETTY_ERR(yetty_ycore_void, "write_to_child: uv_write");
    }
    return YETTY_OK_VOID();
}

/* Serialize `doc` to a newline-terminated heap line and ship it to the
 * child. Frees the doc. */
static struct yetty_ycore_void_result write_json_to_child(struct ccc_app *app, yyjson_mut_doc *doc)
{
    size_t json_len = 0;
    char *json_text = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (!json_text) {
        return YETTY_ERR(yetty_ycore_void, "write_json_to_child: yyjson_mut_write");
    }
    char *line = malloc(json_len + 2);
    if (!line) {
        free(json_text);
        return YETTY_ERR(yetty_ycore_void, "write_json_to_child: malloc");
    }
    memcpy(line, json_text, json_len);
    line[json_len] = '\n';
    line[json_len + 1] = '\0';
    free(json_text);
    struct yetty_ycore_void_result res = write_to_child(app, line, json_len + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "write_json_to_child: write");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result claude_send_user_message(struct ccc_app *app,
                                                               const char *text)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "claude_send_user_message: yyjson_mut_doc_new");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "user");
    yyjson_mut_val *message = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, message, "role", "user");
    yyjson_mut_obj_add_str(doc, message, "content", text);
    yyjson_mut_obj_add_val(doc, envelope, "message", message);
    struct yetty_ycore_void_result res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "claude_send_user_message");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result send_interrupt_request(struct ccc_app *app)
{
    char request_id[32];
    snprintf(request_id, sizeof(request_id), "ccc-%d", ++app->interrupt_request_counter);

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "send_interrupt_request: yyjson_mut_doc_new");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", request_id);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "interrupt");
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "send_interrupt_request");
    return YETTY_OK_VOID();
}

/* The SDK-style handshake: its response carries the CLI's slash-command
 * list (name + description + argumentHint) — the source the completion
 * menu mirrors. */
static struct yetty_ycore_void_result send_initialize_request(struct ccc_app *app)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "send_initialize_request: yyjson_mut_doc_new");
    }
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", CCC_INIT_REQUEST_ID);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "initialize");
    yyjson_mut_obj_add_val(doc, request, "hooks", yyjson_mut_obj(doc));
    yyjson_mut_obj_add_val(doc, envelope, "request", request);
    struct yetty_ycore_void_result res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "send_initialize_request");
    return YETTY_OK_VOID();
}

static void handle_control_response(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *response = yyjson_obj_get(event, "response");
    const char *request_id = yyjson_get_str(yyjson_obj_get(response, "request_id"));
    if (!request_id || strcmp(request_id, CCC_INIT_REQUEST_ID) != 0) {
        return; /* interrupt acks etc. — nothing to do */
    }
    yyjson_val *payload = yyjson_obj_get(response, "response");
    yyjson_val *commands_array = payload ? yyjson_obj_get(payload, "commands") : NULL;
    if (commands_array) {
        report_error(app, "command list", ccc_command_table_load(&app->commands, commands_array));
        ydebug("ccc: command table loaded, %zu entries", app->commands.count);
    }
}

/*---------------------------------------------------------------------------
 * Permission prompts — the CLI runs with `--permission-prompt-tool
 * stdio`: tools outside the allowlist arrive as can_use_tool
 * control_requests and block until ccc answers. The answer comes from
 * a click on the HUD window's Allow/Deny buttons, or — without a HUD —
 * from a y/n line at the prompt.
 *---------------------------------------------------------------------------*/

static void drop_pending_permission(struct ccc_app *app)
{
    if (app->pending_permission.input_doc) {
        yyjson_mut_doc_free(app->pending_permission.input_doc);
    }
    memset(&app->pending_permission, 0, sizeof(app->pending_permission));
}

/* Ship one can_use_tool control_response. `input_doc` (may be NULL) is
 * echoed back as updatedInput on allow; borrowed, not freed here. */
static struct yetty_ycore_void_result send_permission_response(struct ccc_app *app,
                                                               const char *request_id, int allowed,
                                                               yyjson_mut_doc *input_doc)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc) {
        return YETTY_ERR(yetty_ycore_void, "send_permission_response: yyjson_mut_doc_new");
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
        yyjson_mut_obj_add_val(doc, verdict, "updatedInput",
                               input_copy ? input_copy : yyjson_mut_obj(doc));
    } else {
        yyjson_mut_obj_add_str(doc, verdict, "behavior", "deny");
        yyjson_mut_obj_add_str(doc, verdict, "message", "denied from ccc");
    }
    yyjson_mut_obj_add_val(doc, response, "response", verdict);
    yyjson_mut_obj_add_val(doc, envelope, "response", response);
    struct yetty_ycore_void_result send_res = write_json_to_child(app, doc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "send_permission_response: send");
    return YETTY_OK_VOID();
}

/* Answer the pending request. `allowed`: 1 = allow (echoes the input
 * back as updatedInput), 0 = deny. */
static struct yetty_ycore_void_result resolve_pending_permission(struct ccc_app *app, int allowed)
{
    if (!app->pending_permission.active) {
        return YETTY_OK_VOID();
    }
    ccc_renderer_zone_suspend(&app->renderer);
    printf("\n%s%s" CCC_RESET "\n", allowed ? CCC_MINT "✓ allowed: " : CCC_RED "✗ denied: ",
           app->pending_permission.tool_name);
    fflush(stdout);
    struct yetty_ycore_void_result send_res = send_permission_response(
        app, app->pending_permission.request_id, allowed, app->pending_permission.input_doc);
    drop_pending_permission(app);
    set_activity(app, "typing-dots", "… thinking");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "resolve_pending_permission");
    return YETTY_OK_VOID();
}

static void handle_control_request(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *request = yyjson_obj_get(event, "request");
    const char *subtype = yyjson_get_str(yyjson_obj_get(request, "subtype"));
    const char *request_id = yyjson_get_str(yyjson_obj_get(event, "request_id"));
    if (!subtype || !request_id || strcmp(subtype, "can_use_tool") != 0) {
        return; /* only the feature we enabled can arrive */
    }
    if (app->pending_permission.active) {
        /* One at a time; a second concurrent request would deadlock the
         * UI — answer it deny-busy immediately, pending stays intact. */
        report_error(app, "permission", send_permission_response(app, request_id, 0, NULL));
        return;
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
    ccc_summarize_tool_input(input, summary, sizeof(summary));

    ccc_renderer_zone_suspend(&app->renderer);
    printf("\n" CCC_MINT "? permission: " CCC_RESET CCC_BOLD "%s" CCC_RESET " " CCC_DIM
           "%s" CCC_RESET "\n" CCC_DIM "  allow? type y / n" CCC_RESET "\n",
           tool_name, summary);
    fflush(stdout);
    /* The status line + HUD mirror the pending question (display-only —
     * the verdict is typed at the prompt). */
    char state_line[96];
    snprintf(state_line, sizeof(state_line), "? %s %s", tool_name, summary);
    set_activity(app, "hourglass", state_line);
}

static void handle_control_cancel(struct ccc_app *app, yyjson_val *event)
{
    const char *request_id = yyjson_get_str(yyjson_obj_get(event, "request_id"));
    if (!app->pending_permission.active || !request_id ||
        strcmp(request_id, app->pending_permission.request_id) != 0) {
        return;
    }
    ccc_renderer_zone_suspend(&app->renderer);
    printf("\n" CCC_DIM "(permission request cancelled)" CCC_RESET "\n");
    fflush(stdout);
    drop_pending_permission(app);
    /* The turn is still in flight — the result event ends it. */
    set_activity(app, "typing-dots", "… thinking");
}

/*---------------------------------------------------------------------------
 * Tool-name map (tool_use_id -> name, to label results)
 *---------------------------------------------------------------------------*/

static void remember_tool_name(struct ccc_app *app, const char *tool_use_id, const char *name)
{
    struct ccc_tool_name_entry *entry =
        &app->tool_names[app->tool_name_next % CCC_TOOL_NAME_MAP_SIZE];
    app->tool_name_next++;
    snprintf(entry->id, sizeof(entry->id), "%s", tool_use_id);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static const char *lookup_tool_name(struct ccc_app *app, const char *tool_use_id)
{
    if (!tool_use_id) {
        return NULL;
    }
    for (int index = 0; index < CCC_TOOL_NAME_MAP_SIZE; index++) {
        if (strcmp(app->tool_names[index].id, tool_use_id) == 0) {
            return app->tool_names[index].name;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Usage accounting
 *---------------------------------------------------------------------------*/

static uint64_t usage_field(yyjson_val *usage, const char *key)
{
    yyjson_val *value = usage ? yyjson_obj_get(usage, key) : NULL;
    return value ? (uint64_t)yyjson_get_uint(value) : 0;
}

static void render_turn_usage(struct ccc_app *app, yyjson_val *result_event)
{
    yyjson_val *usage = yyjson_obj_get(result_event, "usage");
    yyjson_val *cost_value = yyjson_obj_get(result_event, "total_cost_usd");
    yyjson_val *duration_value = yyjson_obj_get(result_event, "duration_ms");

    uint64_t turn_input = usage_field(usage, "input_tokens");
    uint64_t turn_output = usage_field(usage, "output_tokens");
    uint64_t cache_read = usage_field(usage, "cache_read_input_tokens");
    uint64_t cache_creation = usage_field(usage, "cache_creation_input_tokens");
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
    ccc_format_tokens(turn_input, input_text, sizeof(input_text));
    ccc_format_tokens(turn_output, output_text, sizeof(output_text));
    ccc_format_tokens(cache_read, cached_text, sizeof(cached_text));
    ccc_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));

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
        report_error(app, "hud turn line", ccc_hud_set_turn(app->hud, turn_line));
        report_error(app, "hud session line", ccc_hud_set_session(app->hud, session_line));
        report_error(app, "hud state", ccc_hud_set_state(app->hud, "idle"));
        report_error(app, "hud flush", ccc_hud_flush(app->hud));
        return;
    }
    ccc_renderer_zone_suspend(&app->renderer);
    printf(CCC_MUTED "  %s" CCC_RESET "\n" CCC_DIM "  %s" CCC_RESET "\n", turn_line, session_line);
    fflush(stdout);
}

/*---------------------------------------------------------------------------
 * Stream-json event handling
 *---------------------------------------------------------------------------*/

static void begin_shutdown(struct ccc_app *app);
static void handle_input_line(struct ccc_app *app, const char *line, size_t len);

static void handle_stream_event(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *inner = yyjson_obj_get(event, "event");
    const char *inner_type = yyjson_get_str(yyjson_obj_get(inner, "type"));
    if (!inner_type) {
        return;
    }
    if (strcmp(inner_type, "message_start") == 0) {
        ccc_renderer_begin_message(&app->renderer);
        return;
    }
    if (strcmp(inner_type, "content_block_delta") == 0) {
        yyjson_val *delta = yyjson_obj_get(inner, "delta");
        const char *delta_type = yyjson_get_str(yyjson_obj_get(delta, "type"));
        if (!delta_type) {
            return;
        }
        if (strcmp(delta_type, "text_delta") == 0) {
            yyjson_val *text = yyjson_obj_get(delta, "text");
            if (yyjson_is_str(text)) {
                ccc_renderer_end_thinking(&app->renderer);
                ccc_renderer_text_delta(&app->renderer, yyjson_get_str(text), yyjson_get_len(text));
            }
        } else if (strcmp(delta_type, "thinking_delta") == 0) {
            yyjson_val *thinking = yyjson_obj_get(delta, "thinking");
            if (yyjson_is_str(thinking)) {
                ccc_renderer_thinking_delta(&app->renderer, yyjson_get_str(thinking),
                                            yyjson_get_len(thinking));
            }
        }
        return;
    }
    if (strcmp(inner_type, "content_block_stop") == 0) {
        ccc_renderer_end_thinking(&app->renderer);
    }
}

static void handle_assistant_event(struct ccc_app *app, yyjson_val *event)
{
    ccc_renderer_end_thinking(&app->renderer);
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return;
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
            remember_tool_name(app, tool_use_id, name);
        }
        ccc_render_tool_call(&app->renderer, name, yyjson_obj_get(block, "input"));
        char state_line[96];
        snprintf(state_line, sizeof(state_line), "⚙ %s", name);
        set_activity(app, "orbit-dots", state_line);
    }
}

static void handle_user_event(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return;
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
        report_error(app, "render tool result",
                     ccc_render_tool_result(&app->renderer, yyjson_obj_get(block, "content"),
                                            is_error, lookup_tool_name(app, tool_use_id)));
    }
}

/* Backend-neutral turn boundary: pump the queue or re-prompt. */
static void turn_finished(struct ccc_app *app)
{
    app->waiting = 0;
    ccc_renderer_activity_clear(&app->renderer);
    if (app->queue_len > 0) {
        /* Pump the next queued message instead of prompting. */
        char *queued = app->queue[0];
        memmove(&app->queue[0], &app->queue[1],
                sizeof(app->queue[0]) * (size_t)(app->queue_len - 1));
        app->queue_len--;
        ccc_renderer_zone_suspend(&app->renderer);
        printf("\n" CCC_DIM "(sending queued message)" CCC_RESET "\n");
        fflush(stdout);
        handle_input_line(app, queued, strlen(queued));
        free(queued);
        return;
    }
    show_prompt(app);
}

static void handle_result_event(struct ccc_app *app, yyjson_val *event)
{
    ccc_renderer_activity_clear(&app->renderer);
    ccc_renderer_finish_turn(&app->renderer);
    render_turn_usage(app, event);
    turn_finished(app);
}

static void claude_handle_event(struct ccc_app *app, yyjson_val *event)
{
    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));

    if (kind && strcmp(kind, "system") == 0) {
        const char *subtype = yyjson_get_str(yyjson_obj_get(event, "subtype"));
        if (subtype && strcmp(subtype, "init") == 0) {
            const char *session_id = yyjson_get_str(yyjson_obj_get(event, "session_id"));
            if (session_id) {
                snprintf(app->session_id, sizeof(app->session_id), "%s", session_id);
            }
        }
        return;
    }
    if (kind && strcmp(kind, "stream_event") == 0) {
        handle_stream_event(app, event);
        return;
    }
    if (kind && strcmp(kind, "assistant") == 0) {
        handle_assistant_event(app, event);
        return;
    }
    if (kind && strcmp(kind, "user") == 0) {
        handle_user_event(app, event);
        return;
    }
    if (kind && strcmp(kind, "control_request") == 0) {
        handle_control_request(app, event);
        return;
    }
    if (kind && strcmp(kind, "control_response") == 0) {
        handle_control_response(app, event);
        return;
    }
    if (kind && strcmp(kind, "control_cancel_request") == 0) {
        handle_control_cancel(app, event);
        return;
    }
    if ((kind && strstr(kind, "hook")) || yyjson_obj_get(event, "hook_event_name")) {
        ccc_render_hook(&app->renderer, event);
        return;
    }
    if (kind && strcmp(kind, "result") == 0) {
        handle_result_event(app, event);
    }
}

/*---------------------------------------------------------------------------
 * Child stdout pump
 *---------------------------------------------------------------------------*/

static void handle_child_line(struct ccc_app *app, const char *line, size_t len)
{
    if (len == 0) {
        return;
    }
    if (app->transcript_file) {
        fwrite(line, 1, len, app->transcript_file);
        fputc('\n', app->transcript_file);
        fflush(app->transcript_file);
    }
    yyjson_doc *doc = yyjson_read(line, len, 0);
    if (!doc) {
        return; /* not JSON — ignore */
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        app->backend->handle_event(app, root);
    }
    yyjson_doc_free(doc);
}

YETTY_EXTERNAL_CALLBACK
static void on_child_stdout_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
    (void)handle;
    buffer->base = malloc(suggested_size);
    buffer->len = buffer->base ? suggested_size : 0;
}

static void child_exited_unexpectedly(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    ccc_renderer_pin_hide(&app->renderer);
    printf("\n" CCC_RED "✗ %s exited unexpectedly — see stderr log under tmp/" CCC_RESET "\n",
           app->backend->name);
    fflush(stdout);
    app->exit_code = 1;
    begin_shutdown(app);
}

YETTY_EXTERNAL_CALLBACK
static void on_child_stdout_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
    struct ccc_app *app = stream->data;
    if (nread < 0) {
        free(buffer->base);
        uv_read_stop(stream);
        app->backend->on_child_eof(app);
        return;
    }
    if (nread == 0) {
        free(buffer->base);
        return;
    }
    if (app->child_out_len + (size_t)nread + 1 > app->child_out_cap) {
        size_t new_cap = app->child_out_cap ? app->child_out_cap : 64 * 1024;
        while (new_cap < app->child_out_len + (size_t)nread + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(app->child_out_buf, new_cap);
        if (!grown) {
            free(buffer->base);
            report_error(app, "child stdout",
                         YETTY_ERR(yetty_ycore_void, "on_child_stdout_read: realloc"));
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
        handle_child_line(app, app->child_out_buf + line_start, index - line_start);
        line_start = index + 1;
    }
    if (line_start > 0) {
        memmove(app->child_out_buf, app->child_out_buf + line_start,
                app->child_out_len - line_start);
        app->child_out_len -= line_start;
    }
}

/*---------------------------------------------------------------------------
 * Slash-command completion menu — matches live here; the rows render as
 * part of the pinned zone (render.c), so asynchronous history writes
 * repaint them safely even while a turn is in flight.
 *---------------------------------------------------------------------------*/

/* Hand the current matches to the renderer as prerendered rows. */
static void menu_render(struct ccc_app *app)
{
    char row_storage[CCC_MENU_ROWS][CCC_RENDERER_MENU_ROW_BYTES];
    const char *row_pointers[CCC_MENU_ROWS];
    for (size_t row = 0; row < app->menu_match_count; row++) {
        const struct ccc_command *command = &app->commands.items[app->menu_matches[row]];
        char detail[96];
        snprintf(detail, sizeof(detail), "%s%s%.56s", command->argument_hint,
                 command->argument_hint[0] ? "  " : "", command->description);
        if (row == app->menu_selected) {
            snprintf(row_storage[row], sizeof(row_storage[row]),
                     CCC_MINT "▸ /%s" CCC_RESET " " CCC_DIM "%s" CCC_RESET, command->name, detail);
        } else {
            snprintf(row_storage[row], sizeof(row_storage[row]),
                     "  " CCC_BOLD "/%s" CCC_RESET " " CCC_DIM "%s" CCC_RESET, command->name,
                     detail);
        }
        row_pointers[row] = row_storage[row];
    }
    ccc_renderer_menu_set(&app->renderer, row_pointers, app->menu_match_count);
}

static void menu_close(struct ccc_app *app)
{
    if (!app->menu_visible) {
        return;
    }
    app->menu_visible = 0;
    app->menu_match_count = 0;
    ccc_renderer_menu_clear(&app->renderer);
}

/* Recompute matches + redraw after every edit of the input line. */
static void menu_update(struct ccc_app *app)
{
    if (!app->echo_input || !app->renderer.pin_enabled || app->stdin_len == 0 ||
        app->stdin_buf[0] != '/' || memchr(app->stdin_buf, ' ', app->stdin_len)) {
        menu_close(app);
        return;
    }
    size_t match_count = ccc_command_table_match(
        &app->commands, app->stdin_buf + 1, app->stdin_len - 1, app->menu_matches, CCC_MENU_ROWS);
    if (match_count == 0) {
        menu_close(app);
        return;
    }
    app->menu_match_count = match_count;
    if (app->menu_selected >= match_count) {
        app->menu_selected = 0;
    }
    app->menu_visible = 1;
    menu_render(app);
}

static void menu_move_selection(struct ccc_app *app, int delta)
{
    if (!app->menu_visible || app->menu_match_count == 0) {
        return;
    }
    app->menu_selected =
        (app->menu_selected + (size_t)(delta + (int)app->menu_match_count)) % app->menu_match_count;
    menu_render(app);
}

/* Replace the input line with the selected command. */
static void menu_adopt_selection(struct ccc_app *app, int trailing_space)
{
    if (!app->menu_visible || app->menu_match_count == 0) {
        return;
    }
    const struct ccc_command *command = &app->commands.items[app->menu_matches[app->menu_selected]];
    int written = snprintf(app->stdin_buf, sizeof(app->stdin_buf), "/%s%s", command->name,
                           (trailing_space && command->argument_hint[0]) ? " " : "");
    app->stdin_len = (written > 0) ? (size_t)written : 0;
    app->stdin_cursor = app->stdin_len;
    ccc_renderer_pin_redraw(&app->renderer);
}

/*---------------------------------------------------------------------------
 * Input history — submitted lines, browsed with up/down when the
 * completion menu is closed.
 *---------------------------------------------------------------------------*/

static void history_add(struct ccc_app *app, const char *line, size_t len)
{
    if (len == 0) {
        return;
    }
    if (app->history_len > 0) {
        const char *last = app->history[app->history_len - 1];
        if (strlen(last) == len && memcmp(last, line, len) == 0) {
            return; /* immediate duplicate */
        }
    }
    char *copy = strndup(line, len);
    if (!copy) {
        return; /* history is best-effort */
    }
    if (app->history_len == CCC_HISTORY_MAX) {
        free(app->history[0]);
        memmove(&app->history[0], &app->history[1],
                sizeof(app->history[0]) * (CCC_HISTORY_MAX - 1));
        app->history_len--;
    }
    app->history[app->history_len++] = copy;
}

static void history_load_entry(struct ccc_app *app, const char *text, size_t len)
{
    if (len >= sizeof(app->stdin_buf)) {
        len = sizeof(app->stdin_buf) - 1;
    }
    memcpy(app->stdin_buf, text, len);
    app->stdin_len = len;
    app->stdin_cursor = len;
}

static void history_browse_move(struct ccc_app *app, int delta)
{
    if (app->history_len == 0) {
        return;
    }
    if (app->history_browse < 0) {
        if (delta > 0) {
            return; /* not browsing; down does nothing */
        }
        /* Stash the in-progress line; up enters browsing at the end. */
        memcpy(app->history_stash, app->stdin_buf, app->stdin_len);
        app->history_stash_len = app->stdin_len;
        app->history_browse = app->history_len - 1;
    } else {
        int next = app->history_browse + delta;
        if (next < 0) {
            return; /* already at the oldest entry */
        }
        if (next >= app->history_len) {
            /* Walked past the newest entry: restore the stashed line. */
            app->history_browse = -1;
            history_load_entry(app, app->history_stash, app->history_stash_len);
            ccc_renderer_pin_redraw(&app->renderer);
            menu_update(app);
            return;
        }
        app->history_browse = next;
    }
    const char *entry = app->history[app->history_browse];
    history_load_entry(app, entry, strlen(entry));
    ccc_renderer_pin_redraw(&app->renderer);
    menu_update(app);
}

/*---------------------------------------------------------------------------
 * Line editor (terminal-focused typing). stdin is raw: ccc echoes,
 * erases, and dispatches lines itself.
 *---------------------------------------------------------------------------*/

static void handle_input_line(struct ccc_app *app, const char *line, size_t len)
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
            show_prompt(app);
        } else {
            ccc_renderer_zone_resume(&app->renderer);
        }
        return;
    }
    /* A pending permission consumes the next typed line as its verdict
     * — BEFORE the queue: "y" must never be shipped as a message. */
    if (app->pending_permission.active) {
        if (len == 1 && (line[0] == 'y' || line[0] == 'Y')) {
            report_error(app, "permission", resolve_pending_permission(app, 1));
        } else if (len == 1 && (line[0] == 'n' || line[0] == 'N')) {
            report_error(app, "permission", resolve_pending_permission(app, 0));
        } else {
            ccc_renderer_zone_suspend(&app->renderer);
            printf(CCC_DIM "  pending permission — answer y or n (or click)" CCC_RESET "\n");
            fflush(stdout);
            ccc_renderer_zone_resume(&app->renderer);
        }
        return;
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
        const struct ccc_command *command =
            ccc_command_table_find(&app->commands, line + 1, word_len);
        if (command && command->local) {
            if (strcmp(command->name, "quit") == 0 || strcmp(command->name, "exit") == 0) {
                begin_shutdown(app);
                return;
            }
            if (strcmp(command->name, "shell") == 0) {
                is_shell = 1;
                shell_args = line + 1 + word_len;
                shell_args_len = len - 1 - word_len;
            }
        }
    } else if (line[0] == '!') {
        /* Bang escape: "!cmd" ("!" alone = interactive shell). */
        is_shell = 1;
        shell_args = line + 1;
        shell_args_len = len - 1;
    }
    if (is_shell) {
        while (shell_args_len > 0 && shell_args[0] == ' ') {
            shell_args++;
            shell_args_len--;
        }
        char *command = (shell_args_len > 0) ? strndup(shell_args, shell_args_len) : NULL;
        if (shell_args_len > 0 && !command) {
            report_error(app, "shell", YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup"));
            return;
        }
        report_error(app, "shell", run_shell(app, command));
        free(command);
        show_prompt(app);
        return;
    }
    if (app->waiting) {
        ccc_renderer_zone_suspend(&app->renderer);
        if (app->queue_len < CCC_QUEUE_MAX) {
            char *copy = strndup(line, len);
            if (!copy) {
                report_error(app, "queue", YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup"));
                return;
            }
            app->queue[app->queue_len++] = copy;
            printf(CCC_DIM "(queued — sends when this turn finishes)" CCC_RESET "\n");
            fflush(stdout);
        } else {
            printf(CCC_RED "queue full — message dropped" CCC_RESET "\n");
            fflush(stdout);
        }
        ccc_renderer_zone_resume(&app->renderer);
        return;
    }
    char *copy = strndup(line, len);
    if (!copy) {
        report_error(app, "send", YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup"));
        return;
    }
    app->waiting = 1;
    set_activity(app, "typing-dots", "… thinking");
    struct yetty_ycore_void_result send_res = app->backend->send_user_message(app, copy);
    free(copy);
    if (YETTY_IS_ERR(send_res)) {
        app->waiting = 0;
        ccc_renderer_activity_clear(&app->renderer);
        report_error(app, "send", send_res);
        show_prompt(app);
    }
}

/* Pinned mode repaints the whole prompt row per edit; legacy (non-tty)
 * mode echoes bytes in place. */
static void editor_render(struct ccc_app *app)
{
    if (app->renderer.pin_enabled) {
        ccc_renderer_pin_redraw(&app->renderer);
    }
}

static void editor_echo(struct ccc_app *app, const char *bytes, size_t len)
{
    if (!app->echo_input || app->renderer.pin_enabled) {
        return;
    }
    fwrite(bytes, 1, len, stdout);
    fflush(stdout);
}

/* Byte offset of the codepoint start before `position`. */
static size_t editor_prev_char(const struct ccc_app *app, size_t position)
{
    while (position > 0 && (app->stdin_buf[position - 1] & 0xC0) == 0x80) {
        position--;
    }
    return position > 0 ? position - 1 : 0;
}

/* Byte offset just past the codepoint starting at `position`. */
static size_t editor_next_char(const struct ccc_app *app, size_t position)
{
    if (position >= app->stdin_len) {
        return app->stdin_len;
    }
    position++;
    while (position < app->stdin_len && (app->stdin_buf[position] & 0xC0) == 0x80) {
        position++;
    }
    return position;
}

/* Remove the bytes [from, to) and pull the cursor to `from`. */
static void editor_delete_range(struct ccc_app *app, size_t from, size_t to)
{
    if (to <= from) {
        return;
    }
    memmove(app->stdin_buf + from, app->stdin_buf + to, app->stdin_len - to);
    app->stdin_len -= to - from;
    app->stdin_cursor = from;
}

static void editor_erase_one(struct ccc_app *app)
{
    if (app->stdin_cursor == 0) {
        return;
    }
    editor_delete_range(app, editor_prev_char(app, app->stdin_cursor), app->stdin_cursor);
    editor_echo(app, "\b \b", 3);
}

/* Delete the word before the cursor (spaces, then the word). */
static void editor_erase_word(struct ccc_app *app)
{
    size_t from = app->stdin_cursor;
    while (from > 0 && app->stdin_buf[from - 1] == ' ') {
        from--;
    }
    while (from > 0 && app->stdin_buf[from - 1] != ' ') {
        from--;
    }
    editor_delete_range(app, from, app->stdin_cursor);
}

/* Every edit invalidates a history-browse position (the edited text
 * becomes the new in-progress line). */
static void editor_edited(struct ccc_app *app)
{
    app->history_browse = -1;
    editor_render(app);
    menu_update(app);
}

static void editor_handle_csi(struct ccc_app *app, char final_byte, int parameter)
{
    switch (final_byte) {
    case 'A': /* up: menu selection, else history */
        if (app->menu_visible) {
            menu_move_selection(app, -1);
        } else {
            history_browse_move(app, -1);
        }
        return;
    case 'B': /* down */
        if (app->menu_visible) {
            menu_move_selection(app, 1);
        } else {
            history_browse_move(app, 1);
        }
        return;
    case 'C': /* right */
        app->stdin_cursor = editor_next_char(app, app->stdin_cursor);
        editor_render(app);
        return;
    case 'D': /* left */
        if (app->stdin_cursor > 0) {
            app->stdin_cursor = editor_prev_char(app, app->stdin_cursor);
        }
        editor_render(app);
        return;
    case 'H': /* home */
        app->stdin_cursor = 0;
        editor_render(app);
        return;
    case 'F': /* end */
        app->stdin_cursor = app->stdin_len;
        editor_render(app);
        return;
    case '~':
        if (parameter == 3) { /* delete: the codepoint under the cursor */
            editor_delete_range(app, app->stdin_cursor,
                                editor_next_char(app, app->stdin_cursor));
            editor_edited(app);
        } else if (parameter == 1 || parameter == 7) { /* home variants */
            app->stdin_cursor = 0;
            editor_render(app);
        } else if (parameter == 4 || parameter == 8) { /* end variants */
            app->stdin_cursor = app->stdin_len;
            editor_render(app);
        }
        return;
    default:
        return; /* other sequences: swallow */
    }
}

static void editor_feed_byte(struct ccc_app *app, char ch)
{
    if (app->escape_state == 1) {
        app->escape_state = (ch == '[') ? 2 : 0;
        app->escape_param = 0;
        return;
    }
    if (app->escape_state == 2) {
        if (ch >= '0' && ch <= '9') {
            app->escape_param = app->escape_param * 10 + (ch - '0');
            return;
        }
        if (ch == ';') {
            app->escape_param = 0; /* keep only the last parameter group */
            return;
        }
        if ((unsigned char)ch >= 0x40 && (unsigned char)ch <= 0x7E) {
            app->escape_state = 0;
            editor_handle_csi(app, ch, app->escape_param);
        }
        return;
    }
    switch ((unsigned char)ch) {
    case 0x1B:
        app->escape_state = 1;
        return;
    case '\r':
    case '\n': {
        if (app->menu_visible) {
            /* Enter accepts the highlighted command. */
            menu_adopt_selection(app, /*trailing_space=*/0);
            menu_close(app);
        }
        app->stdin_buf[app->stdin_len] = '\0';
        size_t submitted_len = app->stdin_len;
        history_add(app, app->stdin_buf, submitted_len);
        app->history_browse = -1;
        /* Reset BEFORE dispatch so zone redraws show an empty prompt;
         * the bytes stay valid for handle_input_line. */
        app->stdin_len = 0;
        app->stdin_cursor = 0;
        if (app->renderer.pin_enabled) {
            /* Commit the pinned prompt row into history as a plain
             * line (no animated glyph may enter the scrollback). */
            ccc_renderer_zone_suspend(&app->renderer);
            if (submitted_len > 0) {
                printf(CCC_MINT "you ▸ " CCC_RESET "%s\n", app->stdin_buf);
                fflush(stdout);
            }
        } else {
            editor_echo(app, "\n", 1);
        }
        handle_input_line(app, app->stdin_buf, submitted_len);
        return;
    }
    case '\t': /* Tab fills the highlighted command (plus arg space) */
        if (app->menu_visible) {
            menu_adopt_selection(app, /*trailing_space=*/1);
            menu_update(app);
        }
        return;
    case 0x7F:
    case 0x08:
        editor_erase_one(app);
        editor_edited(app);
        return;
    case 0x01: /* Ctrl-A: start of line */
        app->stdin_cursor = 0;
        editor_render(app);
        return;
    case 0x05: /* Ctrl-E: end of line */
        app->stdin_cursor = app->stdin_len;
        editor_render(app);
        return;
    case 0x0B: /* Ctrl-K: kill to end of line */
        editor_delete_range(app, app->stdin_cursor, app->stdin_len);
        editor_edited(app);
        return;
    case 0x17: /* Ctrl-W: kill the word before the cursor */
        editor_erase_word(app);
        editor_edited(app);
        return;
    case 0x15: /* Ctrl-U: kill the line */
        app->stdin_len = 0;
        app->stdin_cursor = 0;
        editor_edited(app);
        return;
    case 0x04: /* Ctrl-D: EOF on an empty line */
        if (app->stdin_len == 0) {
            menu_close(app);
            begin_shutdown(app);
        }
        return;
    case 0x03: /* Ctrl-C (ISIG is off in raw input mode) */
        if (app->waiting && app->child_alive) {
            ccc_renderer_zone_suspend(&app->renderer);
            printf("\n" CCC_MUTED "(interrupt requested)" CCC_RESET "\n");
            fflush(stdout);
            report_error(app, "interrupt", app->backend->interrupt(app));
            ccc_renderer_zone_resume(&app->renderer);
        } else {
            menu_close(app);
            begin_shutdown(app);
        }
        return;
    default:
        break;
    }
    if ((unsigned char)ch < 0x20) {
        return; /* other control bytes: ignore */
    }
    if (app->stdin_len + 1 < sizeof(app->stdin_buf)) {
        memmove(app->stdin_buf + app->stdin_cursor + 1, app->stdin_buf + app->stdin_cursor,
                app->stdin_len - app->stdin_cursor);
        app->stdin_buf[app->stdin_cursor] = ch;
        app->stdin_len++;
        app->stdin_cursor++;
        editor_echo(app, &ch, 1);
        editor_edited(app);
    }
}

/* One normalized keyboard stream — raw PTY bytes and decoded key
 * envelopes both land here; ccc's own focus decides who consumes. */
static void keyboard_input(struct ccc_app *app, const char *bytes, size_t len)
{
    ydebug("ccc: keys len=%zu first=0x%02x -> %s", len, len ? (unsigned char)bytes[0] : 0,
           (app->focus_gui && app->hud) ? "gui" : "editor");
    if (app->focus_gui && app->hud) {
        report_error(app, "gui keys", ccc_hud_feed_keys(app->hud, bytes, len));
        return;
    }
    for (size_t index = 0; index < len; index++) {
        editor_feed_byte(app, bytes[index]);
    }
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

static void handle_mouse_envelope(struct ccc_app *app, const uint8_t *payload, size_t payload_len)
{
    if (!app->hud || payload_len < sizeof(struct yetty_client_input_mouse)) {
        return;
    }
    struct yetty_client_input_mouse mouse;
    memcpy(&mouse, payload, sizeof(mouse));
    if (mouse.magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return;
    }
    switch (mouse.kind) {
    case YETTY_YMGUI_INPUT_MOUSE_BUTTON: {
        struct yetty_ycore_int_result press_res =
            ccc_hud_mouse_button(app->hud, mouse.x, mouse.y, mouse.button, mouse.pressed);
        if (YETTY_IS_ERR(press_res)) {
            report_error(app, "gui mouse",
                         (struct yetty_ycore_void_result){.ok = 0, .error = press_res.error});
            return;
        }
        if (mouse.pressed) {
            /* Client-side focus: the press decides who owns input. */
            app->focus_gui = press_res.value;
            ydebug("ccc: focus -> %s", app->focus_gui ? "gui" : "terminal");
        }
        return;
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS:
        report_error(app, "gui motion", ccc_hud_mouse_motion(app->hud, mouse.x, mouse.y));
        return;
    case YETTY_YMGUI_INPUT_MOUSE_WHEEL: {
        /* Client-side decision: wheel over the window scrolls the GUI;
         * anywhere else it belongs to the terminal — bounce it back. */
        struct yetty_ycore_int_result inside_res =
            ccc_hud_contains_point(app->hud, mouse.x, mouse.y);
        if (YETTY_IS_ERR(inside_res)) {
            report_error(app, "gui wheel hit-test",
                         (struct yetty_ycore_void_result){.ok = 0, .error = inside_res.error});
            return;
        }
        ydebug("ccc: wheel at (%.0f,%.0f) dy=%.1f -> %s", mouse.x, mouse.y, mouse.wheel_dy,
               inside_res.value ? "gui" : "reinject");
        if (inside_res.value) {
            report_error(app, "gui wheel",
                         ccc_hud_mouse_wheel(app->hud, mouse.x, mouse.y, mouse.wheel_dy));
        } else {
            report_error(app, "wheel reinject", reinject_mouse(&mouse));
        }
        return;
    }
    default:
        return;
    }
}

/* Authoritative pane pixel size — sent on the mouse-subscribe rising
 * edge and on every pane resize (the tty winsize often carries no
 * pixel fields, so this envelope is the only real size cue). */
static void handle_resize_envelope(struct ccc_app *app, const uint8_t *payload, size_t payload_len)
{
    if (!app->hud || payload_len < sizeof(struct yetty_client_input_resize)) {
        return;
    }
    struct yetty_client_input_resize resize_event;
    memcpy(&resize_event, payload, sizeof(resize_event));
    if (resize_event.magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC) {
        return;
    }
    report_error(app, "hud viewport",
                 ccc_hud_set_viewport(app->hud, resize_event.width, resize_event.height));
}

static void handle_key_envelope(struct ccc_app *app, const uint8_t *payload, size_t payload_len)
{
    if (payload_len < sizeof(struct yetty_client_input_key)) {
        return;
    }
    struct yetty_client_input_key key_event;
    memcpy(&key_event, payload, sizeof(key_event));
    if (key_event.magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
        return;
    }
    char bytes[8];
    size_t len = key_event_to_bytes(key_event.kind, key_event.key, key_event.mods,
                                    key_event.codepoint, bytes, sizeof(bytes));
    if (len > 0) {
        keyboard_input(app, bytes, len);
    }
}

YETTY_EXTERNAL_CALLBACK
static void on_yface_osc(void *user, int wire_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t payload_len)
{
    struct ccc_app *app = user;
    (void)args;
    (void)args_len;
    switch (wire_code) {
    case YETTY_OSC_SC_CLIENT_INPUT_MOUSE:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE:
        handle_mouse_envelope(app, payload, payload_len);
        return;
    case YETTY_OSC_SC_CLIENT_INPUT_KEY:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY:
        handle_key_envelope(app, payload, payload_len);
        return;
    case YETTY_OSC_SC_CLIENT_INPUT_RESIZE:
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE:
        handle_resize_envelope(app, payload, payload_len);
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
    keyboard_input(user, bytes, n);
}

YETTY_EXTERNAL_CALLBACK
static void on_stdin_readable(uv_poll_t *poll_handle, int status, int events)
{
    struct ccc_app *app = poll_handle->data;
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
            begin_shutdown(app);
        }
        return;
    }
    report_error(app, "input demux", yetty_yface_feed_bytes(app->yface, chunk, (size_t)nread));
}

/*---------------------------------------------------------------------------
 * Signals
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_sigint(uv_signal_t *signal_handle, int signum)
{
    struct ccc_app *app = signal_handle->data;
    (void)signum;
    if (app->waiting && app->child_alive) {
        ccc_renderer_zone_suspend(&app->renderer);
        printf("\n" CCC_MUTED "(interrupt requested)" CCC_RESET "\n");
        fflush(stdout);
        report_error(app, "interrupt", app->backend->interrupt(app));
        ccc_renderer_zone_resume(&app->renderer);
        return;
    }
    begin_shutdown(app);
}

YETTY_EXTERNAL_CALLBACK
static void on_sigwinch(uv_signal_t *signal_handle, int signum)
{
    struct ccc_app *app = signal_handle->data;
    (void)signum;
    if (app->hud) {
        report_error(app, "hud viewport", ccc_hud_viewport_changed(app->hud));
    }
    /* Re-clip the pinned zone to the new width. */
    ccc_renderer_pin_redraw(&app->renderer);
}

/*---------------------------------------------------------------------------
 * Child lifecycle / shutdown
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_handle_closed(uv_handle_t *handle)
{
    (void)handle;
}

YETTY_EXTERNAL_CALLBACK
static void on_kill_timer(uv_timer_t *timer)
{
    struct ccc_app *app = timer->data;
    if (app->child_alive) {
        uv_process_kill(&app->child_process, SIGKILL);
    }
}

static void begin_shutdown(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    /* Never leave the CLI blocked on an unanswered permission. */
    if (app->pending_permission.active) {
        report_error(app, "permission", resolve_pending_permission(app, 0));
    }
    /* No animated glyph may survive the session — drop the pinned zone
     * for good before the teardown output. */
    ccc_renderer_pin_hide(&app->renderer);
    app->shutting_down = 1;
    uv_poll_stop(&app->stdin_poll);
    uv_signal_stop(&app->sigint_handle);
    uv_signal_stop(&app->sigwinch_handle);
    if (app->child_stdin_open) {
        app->child_stdin_open = 0;
        uv_close((uv_handle_t *)&app->child_stdin_pipe, on_handle_closed);
    }
    if (app->child_alive) {
        /* Closing stdin asks claude to exit; SIGKILL backstop after 5 s. */
        uv_timer_start(&app->kill_timer, on_kill_timer, CCC_KILL_TIMEOUT_MS, 0);
    } else {
        uv_stop(&app->loop);
    }
}

/* Shared uv exit callback — semantics are backend policy. */
YETTY_EXTERNAL_CALLBACK
static void on_child_exit(uv_process_t *process, int64_t exit_status, int term_signal)
{
    struct ccc_app *app = process->data;
    (void)term_signal;
    app->child_alive = 0;
    uv_timer_stop(&app->kill_timer);
    app->backend->on_child_exit(app, exit_status);
}

/*---------------------------------------------------------------------------
 * Claude backend — one persistent child, bidirectional stream-json.
 *---------------------------------------------------------------------------*/

static void claude_on_child_exit(struct ccc_app *app, int64_t exit_status)
{
    if (!app->shutting_down) {
        if (exit_status != 0) {
            ccc_renderer_pin_hide(&app->renderer);
            printf("\n" CCC_RED "✗ claude exited with status %lld — see %s and tmp/" CCC_RESET "\n",
                   (long long)exit_status, app->transcript_path);
            app->exit_code = 1;
        }
        begin_shutdown(app);
    }
    uv_close((uv_handle_t *)&app->child_process, on_handle_closed);
    uv_stop(&app->loop);
}

static void claude_on_child_eof(struct ccc_app *app)
{
    /* The persistent pipe closed: the session is dead — the turn (if
     * any) will never finish; never hang waiting for it. */
    if (app->child_alive) {
        child_exited_unexpectedly(app);
    }
}

static struct yetty_ycore_void_result claude_start(struct ccc_app *app)
{
    /* The loop owns the terminal: the yetty MCP server must hand figures
     * back via the sentinel in the tool result instead of racing us to
     * the PTY. Claude-only — the codex backend lets the server write
     * /dev/tty directly (its items don't carry tool output back). */
    setenv("YETTY_MCP_VIA_PARENT", "1", 1);

    const char *permission_mode = getenv("CCC_PERMISSION_MODE");
    if (!permission_mode || !permission_mode[0]) {
        /* "default" makes un-allowlisted tools prompt — the whole point
         * of the clickable permission flow. */
        permission_mode = "default";
    }
    const char *allowed_tools = getenv("CCC_ALLOWED_TOOLS");
    if (!allowed_tools || !allowed_tools[0]) {
        /* mcp__yetty allows the whole yetty MCP server (all draw_* tools). */
        allowed_tools = "Read,Bash(git *),mcp__yetty";
    }
    const char *model = getenv("CCC_MODEL");

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

    uv_pipe_init(&app->loop, &app->child_stdin_pipe, 0);
    uv_pipe_init(&app->loop, &app->child_stdout_pipe, 0);
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
    options.exit_cb = on_child_exit;
    options.file = "claude";
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;
    /* env = NULL inherits ours — including YETTY_MCP_VIA_PARENT. */

    app->child_process.data = app;
    int spawn_status = uv_spawn(&app->loop, &app->child_process, &options);
    if (spawn_status != 0) {
        return YETTY_ERR(yetty_ycore_void, "claude_start: uv_spawn failed (claude not in PATH?)");
    }
    app->child_alive = 1;
    app->child_stdin_open = 1;
    uv_read_start((uv_stream_t *)&app->child_stdout_pipe, on_child_stdout_alloc,
                  on_child_stdout_read);
    /* Fetch the CLI's slash-command list for the completion menu. */
    struct yetty_ycore_void_result init_res = send_initialize_request(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "claude_start: initialize");
    return YETTY_OK_VOID();
}

static const struct ccc_backend_ops *ccc_backend_claude(void)
{
    static const struct ccc_backend_ops ops = {
        .name = "claude",
        .start = claude_start,
        .send_user_message = claude_send_user_message,
        .handle_event = claude_handle_event,
        .interrupt = send_interrupt_request,
        .on_child_exit = claude_on_child_exit,
        .on_child_eof = claude_on_child_eof,
    };
    return &ops;
}

/*---------------------------------------------------------------------------
 * Codex backend — a fresh `codex exec --json [resume <id>]` child per
 * turn. The turn is over when BOTH child handles (process + stdout
 * pipe) have fully closed; only then is a respawn safe.
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void on_codex_handle_closed(uv_handle_t *handle)
{
    struct ccc_app *app = handle->data;
    app->child_open_handles--;
    if (app->child_open_handles > 0) {
        return;
    }
    if (app->shutting_down) {
        uv_stop(&app->loop);
        return;
    }
    ccc_renderer_finish_turn(&app->renderer);
    turn_finished(app);
}

static void codex_on_child_exit(struct ccc_app *app, int64_t exit_status)
{
    if (!app->shutting_down && exit_status != 0) {
        ccc_renderer_zone_suspend(&app->renderer);
        printf("\n" CCC_RED "✗ codex exited with status %lld — see stderr log under tmp/" CCC_RESET
               "\n",
               (long long)exit_status);
    }
    uv_close((uv_handle_t *)&app->child_process, on_codex_handle_closed);
}

static void codex_on_child_eof(struct ccc_app *app)
{
    /* Normal end of a per-turn stream. Flush any unterminated tail. */
    if (app->child_out_len > 0) {
        app->child_out_buf[app->child_out_len] = '\0';
        handle_child_line(app, app->child_out_buf, app->child_out_len);
        app->child_out_len = 0;
    }
    uv_close((uv_handle_t *)&app->child_stdout_pipe, on_codex_handle_closed);
}

static struct yetty_ycore_void_result codex_send_user_message(struct ccc_app *app, const char *text)
{
    if (app->child_open_handles > 0 || app->child_alive) {
        return YETTY_ERR(yetty_ycore_void, "codex_send_user_message: previous turn still open");
    }
    const char *model = getenv("CCC_MODEL");
    char model_override[128] = "";
    if (model && model[0]) {
        snprintf(model_override, sizeof(model_override), "model=%s", model);
    }

    /* MCP tool calls are auto-cancelled by codex exec unless the
     * sandbox allows them ("user cancelled MCP tool call"); empirically
     * only danger-full-access lets them run non-interactively. */
    const char *sandbox_mode = getenv("CCC_CODEX_SANDBOX");
    if (!sandbox_mode || !sandbox_mode[0]) {
        sandbox_mode = "danger-full-access";
    }

    const char *args[16];
    int arg_count = 0;
    args[arg_count++] = "codex";
    args[arg_count++] = "exec";
    args[arg_count++] = "--json";
    args[arg_count++] = "--skip-git-repo-check";
    args[arg_count++] = "--sandbox";
    args[arg_count++] = sandbox_mode;
    if (model_override[0]) {
        args[arg_count++] = "-c";
        args[arg_count++] = model_override;
    }
    if (app->session_id[0]) {
        args[arg_count++] = "resume";
        args[arg_count++] = app->session_id;
    }
    args[arg_count++] = text;
    args[arg_count] = NULL;

    uv_pipe_init(&app->loop, &app->child_stdout_pipe, 0);
    app->child_stdout_pipe.data = app;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE; /* prompt rides argv, not stdin */
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&app->child_stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = app->child_stderr_fd;

    uv_process_options_t options = {0};
    options.exit_cb = on_child_exit;
    options.file = "codex";
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;

    app->child_process.data = app;
    int spawn_status = uv_spawn(&app->loop, &app->child_process, &options);
    if (spawn_status != 0) {
        uv_close((uv_handle_t *)&app->child_stdout_pipe, on_handle_closed);
        return YETTY_ERR(yetty_ycore_void,
                         "codex_send_user_message: uv_spawn failed (codex not in PATH?)");
    }
    app->child_alive = 1;
    app->child_open_handles = 2; /* process + stdout pipe */
    uv_read_start((uv_stream_t *)&app->child_stdout_pipe, on_child_stdout_alloc,
                  on_child_stdout_read);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result codex_start(struct ccc_app *app)
{
    (void)app; /* nothing runs until the first turn spawns */
    /* Codex items carry no tool output back to ccc, so the yetty MCP
     * server must write its figure envelope to /dev/tty itself. */
    unsetenv("YETTY_MCP_VIA_PARENT");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result codex_interrupt(struct ccc_app *app)
{
    if (!app->child_alive) {
        return YETTY_OK_VOID();
    }
    if (uv_process_kill(&app->child_process, SIGINT) != 0) {
        return YETTY_ERR(yetty_ycore_void, "codex_interrupt: uv_process_kill");
    }
    return YETTY_OK_VOID();
}

/* Render one completed thread item. Items arrive whole (exec mode has
 * no deltas), so each maps to one scrollback block. */
static void codex_render_item(struct ccc_app *app, yyjson_val *item)
{
    const char *item_type = yyjson_get_str(yyjson_obj_get(item, "type"));
    if (!item_type) {
        return;
    }
    ccc_renderer_zone_suspend(&app->renderer);
    if (strcmp(item_type, "agent_message") == 0) {
        const char *text = yyjson_get_str(yyjson_obj_get(item, "text"));
        if (text && text[0]) {
            printf("\n" CCC_MINT CCC_BOLD "codex" CCC_RESET " %s\n", text);
            fflush(stdout);
        }
        ccc_renderer_zone_resume(&app->renderer);
        return;
    }
    if (strcmp(item_type, "reasoning") == 0) {
        if (app->renderer.show_thinking) {
            const char *text = yyjson_get_str(yyjson_obj_get(item, "text"));
            if (text && text[0]) {
                printf("\n" CCC_MUTED "thinking " CCC_RESET CCC_DIM "%s" CCC_RESET "\n", text);
                fflush(stdout);
            }
        }
        ccc_renderer_zone_resume(&app->renderer);
        return;
    }
    if (strcmp(item_type, "command_execution") == 0) {
        const char *command = yyjson_get_str(yyjson_obj_get(item, "command"));
        yyjson_val *exit_code_value = yyjson_obj_get(item, "exit_code");
        int is_error = exit_code_value && yyjson_get_int(exit_code_value) != 0;
        printf("\n" CCC_MUTED "⚙ " CCC_BOLD "shell" CCC_RESET " " CCC_DIM "%s" CCC_RESET "\n",
               command ? command : "?");
        report_error(app, "render tool result",
                     ccc_render_tool_result(&app->renderer,
                                            yyjson_obj_get(item, "aggregated_output"), is_error,
                                            "shell"));
        return;
    }
    if (strcmp(item_type, "mcp_tool_call") == 0 || strcmp(item_type, "file_change") == 0 ||
        strcmp(item_type, "todo_list") == 0 || strcmp(item_type, "web_search") == 0) {
        char summary[100];
        ccc_summarize_tool_input(item, summary, sizeof(summary));
        printf("\n" CCC_MUTED "⚙ " CCC_BOLD "%s" CCC_RESET " " CCC_DIM "%s" CCC_RESET "\n",
               item_type, summary);
        /* Figure envelopes from the yetty MCP server ride tool output;
         * the generic result renderer decodes the sentinel. */
        report_error(app, "render tool result",
                     ccc_render_tool_result(&app->renderer, item, 0, item_type));
        return;
    }
    if (strcmp(item_type, "error") == 0) {
        const char *message = yyjson_get_str(yyjson_obj_get(item, "message"));
        printf("\n" CCC_RED "✗ %s" CCC_RESET "\n", message ? message : "error");
        fflush(stdout);
        ccc_renderer_zone_resume(&app->renderer);
        return;
    }
    char summary[100];
    ccc_summarize_tool_input(item, summary, sizeof(summary));
    printf(CCC_DIM "  (%s %s)" CCC_RESET "\n", item_type, summary);
    fflush(stdout);
    ccc_renderer_zone_resume(&app->renderer);
}

static void codex_render_usage(struct ccc_app *app, yyjson_val *usage)
{
    uint64_t turn_input = usage_field(usage, "input_tokens");
    uint64_t cached = usage_field(usage, "cached_input_tokens");
    uint64_t turn_output = usage_field(usage, "output_tokens");
    app->usage.input += turn_input;
    app->usage.output += turn_output;
    app->usage.cache_read += cached;
    app->usage.turns++;

    char input_text[16];
    char output_text[16];
    char cached_text[16];
    char session_output_text[16];
    ccc_format_tokens(turn_input, input_text, sizeof(input_text));
    ccc_format_tokens(turn_output, output_text, sizeof(output_text));
    ccc_format_tokens(cached, cached_text, sizeof(cached_text));
    ccc_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));
    char turn_line[192];
    snprintf(turn_line, sizeof(turn_line), "↑%s in · %s cached · ↓%s out", input_text, cached_text,
             output_text);
    char session_line[128];
    snprintf(session_line, sizeof(session_line), "session: ↓%s out · %d turn(s)",
             session_output_text, app->usage.turns);
    if (app->hud) {
        report_error(app, "hud turn line", ccc_hud_set_turn(app->hud, turn_line));
        report_error(app, "hud session line", ccc_hud_set_session(app->hud, session_line));
        report_error(app, "hud state", ccc_hud_set_state(app->hud, "idle"));
        report_error(app, "hud flush", ccc_hud_flush(app->hud));
        return;
    }
    ccc_renderer_zone_suspend(&app->renderer);
    printf(CCC_MUTED "  %s" CCC_RESET "\n" CCC_DIM "  %s" CCC_RESET "\n", turn_line, session_line);
    fflush(stdout);
}

static void codex_handle_event(struct ccc_app *app, yyjson_val *event)
{
    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));
    if (!kind) {
        return;
    }
    if (strcmp(kind, "thread.started") == 0) {
        const char *thread_id = yyjson_get_str(yyjson_obj_get(event, "thread_id"));
        if (thread_id && strcmp(thread_id, app->session_id) != 0) {
            snprintf(app->session_id, sizeof(app->session_id), "%s", thread_id);
            ccc_renderer_zone_suspend(&app->renderer);
            printf(CCC_DIM "(codex thread %s)" CCC_RESET "\n", app->session_id);
            fflush(stdout);
            ccc_renderer_zone_resume(&app->renderer);
        }
        return;
    }
    if (strcmp(kind, "item.completed") == 0) {
        codex_render_item(app, yyjson_obj_get(event, "item"));
        return;
    }
    if (strcmp(kind, "turn.completed") == 0) {
        codex_render_usage(app, yyjson_obj_get(event, "usage"));
        return;
    }
    if (strcmp(kind, "turn.failed") == 0 || strcmp(kind, "error") == 0) {
        char summary[100];
        ccc_summarize_tool_input(event, summary, sizeof(summary));
        ccc_renderer_zone_suspend(&app->renderer);
        printf("\n" CCC_RED "✗ %s %s" CCC_RESET "\n", kind, summary);
        fflush(stdout);
        return;
    }
    /* turn.started, item.started, item.updated, …: nothing to draw. */
}

static const struct ccc_backend_ops *ccc_backend_codex(void)
{
    static const struct ccc_backend_ops ops = {
        .name = "codex",
        .start = codex_start,
        .send_user_message = codex_send_user_message,
        .handle_event = codex_handle_event,
        .interrupt = codex_interrupt,
        .on_child_exit = codex_on_child_exit,
        .on_child_eof = codex_on_child_eof,
    };
    return &ops;
}

/*---------------------------------------------------------------------------
 * main
 *---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    ytrace_init();

    const char *resume_session_id = NULL;
    const char *backend_name = getenv("CCC_BACKEND");
    for (int arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], "--resume") == 0 && arg_index + 1 < argc) {
            resume_session_id = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--backend") == 0 && arg_index + 1 < argc) {
            backend_name = argv[++arg_index];
        }
    }

    struct ccc_app *app = calloc(1, sizeof(*app));
    if (!app) {
        return 1;
    }
    app->backend = (backend_name && strcmp(backend_name, "codex") == 0) ? ccc_backend_codex()
                                                                        : ccc_backend_claude();

    /* session_id is the backend's resume token. Claude mints it client-
     * side; codex mints it server-side (stays empty until
     * thread.started). The transcript/log files are named by a tag that
     * always exists. */
    char file_tag[48];
    generate_session_id(file_tag, sizeof(file_tag));
    if (resume_session_id) {
        app->resume_requested = 1;
        snprintf(app->session_id, sizeof(app->session_id), "%s", resume_session_id);
    } else if (app->backend == ccc_backend_claude()) {
        snprintf(app->session_id, sizeof(app->session_id), "%s", file_tag);
    }
    ccc_renderer_init(&app->renderer, env_int("CCC_FOLD_LINES", CCC_DEFAULT_FOLD_LINES),
                      env_flag("CCC_SHOW_THINKING"));
    ccc_renderer_pin_setup(&app->renderer, app->stdin_buf, &app->stdin_len, &app->stdin_cursor);
    app->history_browse = -1;
    app->echo_input = isatty(STDIN_FILENO);
    report_error(app, "command table", ccc_command_table_init(&app->commands));

    mkdir("tmp", 0777);
    snprintf(app->transcript_path, sizeof(app->transcript_path), "tmp/transcript-%s.jsonl",
             file_tag);
    app->transcript_file = fopen(app->transcript_path, "a");

    char stderr_log_path[256];
    snprintf(stderr_log_path, sizeof(stderr_log_path), "tmp/%s-stderr-%s.log", app->backend->name,
             file_tag);
    int stderr_fd = open(stderr_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd < 0) {
        stderr_fd = STDERR_FILENO;
    }
    app->child_stderr_fd = stderr_fd;

    /* Raw input BEFORE the first envelope write — otherwise the tty
     * echoes the ESC bytes back and the host renders "^[" garbage. */
    report_error(app, "raw input", enter_raw_input(app));

    struct ccc_hud_ptr_result hud_res = ccc_hud_create();
    if (YETTY_IS_ERR(hud_res)) {
        report_error(app, "hud create",
                     (struct yetty_ycore_void_result){.ok = 0, .error = hud_res.error});
        app->hud = NULL; /* run without the window */
    } else {
        app->hud = hud_res.value;
    }

    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        report_error(app, "input demux",
                     (struct yetty_ycore_void_result){.ok = 0, .error = yface_res.error});
        report_error(app, "raw input restore", leave_raw_input(app));
        if (app->hud) {
            report_error(app, "hud destroy", ccc_hud_destroy(app->hud));
        }
        free(app);
        return 1;
    }
    app->yface = yface_res.value;
    yetty_yface_set_handlers(app->yface, on_yface_osc, on_yface_raw, app);

    uv_loop_init(&app->loop);
    uv_timer_init(&app->loop, &app->kill_timer);
    app->kill_timer.data = app;

    struct yetty_ycore_void_result spawn_res = app->backend->start(app);
    if (YETTY_IS_ERR(spawn_res)) {
        report_error(app, "spawn", spawn_res);
        report_error(app, "raw input restore", leave_raw_input(app));
        if (app->hud) {
            report_error(app, "hud destroy", ccc_hud_destroy(app->hud));
        }
        report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
        free(app);
        return 1;
    }

    uv_poll_init(&app->loop, &app->stdin_poll, STDIN_FILENO);
    app->stdin_poll.data = app;
    uv_poll_start(&app->stdin_poll, UV_READABLE, on_stdin_readable);

    uv_signal_init(&app->loop, &app->sigint_handle);
    app->sigint_handle.data = app;
    uv_signal_start(&app->sigint_handle, on_sigint, SIGINT);
    uv_signal_init(&app->loop, &app->sigwinch_handle);
    app->sigwinch_handle.data = app;
    uv_signal_start(&app->sigwinch_handle, on_sigwinch, SIGWINCH);

    if (app->hud) {
        report_error(app, "mouse subscribe", subscribe_mouse(app));
    }

    printf(CCC_MINT CCC_BOLD
           "ccc" CCC_RESET " " CCC_MUTED "backend %s · session %s" CCC_RESET "\n" CCC_DIM
           "transcript=%s  hud=%s" CCC_RESET "\n" CCC_DIM
           "Type a message. /quit or Ctrl-D to exit; Ctrl-C interrupts a turn; /shell or !cmd "
           "for a shell." CCC_RESET "\n",
           app->backend->name, app->session_id[0] ? app->session_id : "(new)", app->transcript_path,
           app->hud ? "on" : "off");
    show_prompt(app);

    uv_run(&app->loop, UV_RUN_DEFAULT);

    /* Drain closing handles. */
    uv_close((uv_handle_t *)&app->stdin_poll, on_handle_closed);
    uv_close((uv_handle_t *)&app->sigint_handle, on_handle_closed);
    uv_close((uv_handle_t *)&app->sigwinch_handle, on_handle_closed);
    uv_close((uv_handle_t *)&app->kill_timer, on_handle_closed);
    if (!uv_is_closing((uv_handle_t *)&app->child_stdout_pipe)) {
        uv_close((uv_handle_t *)&app->child_stdout_pipe, on_handle_closed);
    }
    uv_run(&app->loop, UV_RUN_NOWAIT);
    uv_loop_close(&app->loop);

    report_error(app, "mouse unsubscribe", unsubscribe_mouse(app));
    if (app->hud) {
        report_error(app, "hud destroy", ccc_hud_destroy(app->hud));
        app->hud = NULL;
    }
    report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
    report_error(app, "raw input restore", leave_raw_input(app));
    printf("\n" CCC_DIM "transcript saved to %s" CCC_RESET "\n", app->transcript_path);

    int exit_code = app->exit_code;
    if (app->transcript_file) {
        fclose(app->transcript_file);
    }
    if (stderr_fd != STDERR_FILENO) {
        close(stderr_fd);
    }
    for (int queue_index = 0; queue_index < app->queue_len; queue_index++) {
        free(app->queue[queue_index]);
    }
    drop_pending_permission(app);
    ccc_command_table_destroy(&app->commands);
    ccc_renderer_destroy(&app->renderer);
    for (int history_index = 0; history_index < app->history_len; history_index++) {
        free(app->history[history_index]);
    }
    free(app->child_out_buf);
    free(app);
    return exit_code;
}
