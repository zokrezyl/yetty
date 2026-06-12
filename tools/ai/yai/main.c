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
 * tmp/transcript-<session>.jsonl.
 *
 * Usage:
 *     ./yai                      # fresh session (claude engine)
 *     ./yai --engine codex       # drive `codex exec --json` instead
 *     ./yai --engine gemini      # drive `gemini -o json` per turn
 *     ./yai --resume <id>        # resume (claude session / codex thread)
 *     YAI_ENGINE=codex ./yai     # engine via environment
 *     YAI_SHOW_THINKING=1 ./yai  # show dim thinking text
 *     YAI_FOLD_LINES=20 ./yai    # tool-output preview cap (default 8)
 *     YAI_NO_HUD=1 ./yai         # stats as plain lines, no ygui window
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
 *
 * Error discipline: everything returns Results with chained causes;
 * yai_report_error is the single end consumer, called only from the
 * places with nowhere left to propagate (libuv / yface callbacks,
 * main itself, and UI flows that recover and continue).
 */

#include "app.h"
#include "editor-ops.h"

#include <yetty/yai/claude.h>
#include <yetty/yai/codex.h>
#include <yetty/yai/editor-emacs.h>
#include <yetty/yai/editor-vi.h>
#include <yetty/yai/editor.h>
#include <yetty/yai/engine.h>
#include <yetty/yai/gemini.h>

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
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <uv.h>
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
 * the pinned prompt row and, when present, the HUD state text. */
struct yetty_ycore_void_result yai_set_activity(struct yai_app *app, const char *glyph_name,
                                                const char *state_text)
{
    struct yetty_ycore_void_result glyph_res =
        yai_renderer_activity_set(&app->renderer, glyph_name);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, glyph_res, "yai_set_activity: glyph");
    if (app->hud) {
        struct yetty_ycore_void_result state_res = yai_hud_set_state(app->hud, state_text);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "yai_set_activity: hud state");
        struct yetty_ycore_void_result flush_res = yai_hud_flush(app->hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_set_activity: hud flush");
    }
    return YETTY_OK_VOID();
}

void yai_drop_pending_permission(struct yai_app *app)
{
    if (app->pending_permission.input_doc) {
        yyjson_mut_doc_free(app->pending_permission.input_doc);
    }
    memset(&app->pending_permission, 0, sizeof(app->pending_permission));
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

static struct yetty_ycore_void_result emit_mouse_sub(uint32_t flags)
{
    struct yetty_client_input_sub sub = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
    };
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "emit_mouse_sub: flush");
    struct yetty_ycore_void_result emit_res = yetty_yface_emit_to_fd(
        STDOUT_FILENO, YETTY_OSC_CS_CLIENT_INPUT_SUB, 0, NULL, 0, &sub, sizeof(sub));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "emit_mouse_sub: emit_to_fd");
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
    }
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
    if (app->hud) {
        struct yetty_ycore_void_result state_res = yai_hud_set_state(app->hud, "⌨ shell");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "run_shell: hud state");
        struct yetty_ycore_void_result flush_res = yai_hud_flush(app->hud);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "run_shell: hud flush");
    }
    printf(YAI_DIM "(entering %s — exit to return to yai%s)" YAI_RESET "\n",
           command ? command : shell,
           app->waiting ? "; the turn continues in the background" : "");
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
    } else if (app->hud) {
        teardown = yetty_ycore_void_chain(teardown, yai_hud_set_state(app->hud, "idle"));
        teardown = yetty_ycore_void_chain(teardown, yai_hud_flush(app->hud));
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, teardown, "run_shell: resume");
    return YETTY_OK_VOID();
}

/* Bounce an event yai did not consume back to the host for its default
 * handling (wheel → terminal scrollback, …). Subscribing made the host
 * forward everything; this is the return path for the rest. */
static struct yetty_ycore_void_result reinject_mouse(const struct yetty_client_input_mouse *mouse)
{
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "reinject_mouse: flush");
    struct yetty_ycore_void_result emit_res = yetty_yface_emit_to_fd(
        STDOUT_FILENO, YETTY_OSC_CS_CLIENT_INPUT_REINJECT, 0, NULL, 0, mouse, sizeof(*mouse));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "reinject_mouse: emit_to_fd");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Child stdout pump — shared by all engines
 *---------------------------------------------------------------------------*/

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
            transcript_res = YETTY_ERR(
                yetty_ycore_void, "yai_handle_child_line: transcript write failed — disabled");
        } else if (fflush(app->transcript_file) != 0) {
            fclose(app->transcript_file);
            app->transcript_file = NULL;
            transcript_res = YETTY_ERR(
                yetty_ycore_void, "yai_handle_child_line: transcript flush failed — disabled");
        }
    }
    yyjson_doc *doc = yyjson_read(line, len, 0);
    if (doc) {
        yyjson_val *root = yyjson_doc_get_root(doc);
        if (yyjson_is_obj(root)) {
            struct yetty_ycore_void_result dispatch_res =
                yetty_yai_handle_event(NULL, app->engine, app, root);
            if (YETTY_IS_ERR(dispatch_res)) {
                yyjson_doc_free(doc);
                if (YETTY_IS_ERR(transcript_res)) {
                    yetty_ycore_error_destroy(transcript_res.error);
                }
                return YETTY_ERR(yetty_ycore_void, "yai_handle_child_line: handle_event",
                                 dispatch_res);
            }
        }
        yyjson_doc_free(doc);
    }
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
        yai_report_error(app, "engine eof", yetty_yai_on_child_eof(NULL, app->engine, app));
        return;
    }
    if (nread == 0) {
        free(buffer->base);
        return;
    }
    /* A single line (e.g. a base64 figure) can be large, but a child
     * that never emits a newline must not grow this without bound and
     * wrap the capacity math. Cap it. */
    if ((size_t)nread > YAI_CHILD_OUT_MAX || app->child_out_len > YAI_CHILD_OUT_MAX - (size_t)nread) {
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
    yai_report_error(app, "engine exit",
                     yetty_yai_on_child_exit(NULL, app->engine, app, exit_status));
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
        /* Stash the in-progress line; up enters browsing at the end. */
        memcpy(app->history_stash, app->stdin_buf, app->stdin_len);
        app->history_stash_len = app->stdin_len;
        app->history_browse = app->history_len - 1;
    } else {
        int next = app->history_browse + delta;
        if (next < 0) {
            return YETTY_OK_VOID(); /* already at the oldest entry */
        }
        if (next >= app->history_len) {
            /* Walked past the newest entry: restore the stashed line. */
            app->history_browse = -1;
            history_load_entry(app, app->history_stash, app->history_stash_len);
            struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "history_browse_move: redraw");
            struct yetty_ycore_void_result menu_res = menu_update(app);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, menu_res, "history_browse_move: menu");
            return YETTY_OK_VOID();
        }
        app->history_browse = next;
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

/* Reserve the bottom rows for the docked HUD via the terminal scroll
 * region (DECSTBM), so conversation text never scrolls under the bar.
 * Re-applied on every resize; reset with yai_release_dock_reservation.
 * A no-op when the HUD floats or stdout is not a tty. */
static struct yetty_ycore_void_result yai_apply_dock_reservation(struct yai_app *app)
{
    if (!app->hud || !app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    float dock_px = yai_hud_dock_height(app->hud);
    if (dock_px <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_row == 0) {
        return YETTY_OK_VOID();
    }
    float cell_height =
        (size.ws_ypixel && size.ws_row) ? (float)size.ws_ypixel / (float)size.ws_row : 19.0f;
    int dock_rows = (int)((dock_px + cell_height - 1.0f) / cell_height); /* ceil */
    if (dock_rows < 1) {
        dock_rows = 1;
    }
    if (dock_rows >= size.ws_row) {
        dock_rows = size.ws_row - 1;
    }
    int region_bottom = size.ws_row - dock_rows;
    /* Scroll region rows 1..region_bottom; park the cursor at the
     * region's bottom so the pinned prompt sits just above the bar. */
    printf("\033[1;%dr\033[%d;1H", region_bottom, region_bottom);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_apply_dock_reservation: flush");
    return YETTY_OK_VOID();
}

/* Restore the full-screen scroll region (drop the HUD reservation). */
static struct yetty_ycore_void_result yai_release_dock_reservation(struct yai_app *app)
{
    if (!app->renderer.pin_enabled) {
        return YETTY_OK_VOID();
    }
    fputs("\033[r", stdout);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_release_dock_reservation: flush");
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
    if (app->pending_permission.active) {
        teardown = yetty_ycore_void_chain(teardown,
                                          yetty_yai_resolve_permission(NULL, app->engine, app, 0));
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
    app->shutting_down = 1;
    uv_poll_stop(&app->stdin_poll);
    uv_signal_stop(&app->sigint_handle);
    uv_signal_stop(&app->sigwinch_handle);
    uv_signal_stop(&app->sigterm_handle);
    uv_signal_stop(&app->sighup_handle);
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
    struct yetty_ycore_void_result clear_res = yai_renderer_activity_clear(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "yai_turn_finished: activity clear");
    if (app->turn_failed) {
        app->turn_failed = 0;
        /* The scrollback already carries the red ✗ line(s); the
         * persistent state surface must say failed too, not "idle". */
        if (app->hud) {
            struct yetty_ycore_void_result state_res =
                yai_hud_set_state(app->hud, "✗ turn failed");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "yai_turn_finished: hud state");
            struct yetty_ycore_void_result flush_res = yai_hud_flush(app->hud);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "yai_turn_finished: hud flush");
        }
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
        const char *option_end = memchr(option_cursor, ',', (size_t)(current_start - option_cursor));
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

/* Fill knob 0: the yai edit-mode (emacs / vi) radio group. */
static void build_editmode_knob(struct yai_app *app, struct yai_hud_config_knob *out)
{
    app->config_knobs[0].is_edit_mode = 1;
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
}

/* /config — an EDITABLE floating ygui dialog when the HUD is up (a
 * second /config toggles it closed): read-only info rows, a
 * show-thinking checkbox, a fold-lines slider, the yai edit-mode knob
 * (emacs / vi), and the engine's knob as a radio group (codex sandbox,
 * claude permission mode, gemini approval mode). Plain scrollback text
 * without a HUD. Runnable any time, mid-turn included. */
static struct yetty_ycore_void_result show_config(struct yai_app *app)
{
    char engine_rows[1024];
    struct yetty_ycore_void_result describe_res =
        yetty_yai_describe_config(NULL, app->engine, app, engine_rows, sizeof(engine_rows));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, describe_res, "show_config: engine rows");

    if (app->hud) {
        char knob_spec[256];
        struct yetty_ycore_void_result knob_res =
            yetty_yai_config_knob(NULL, app->engine, app, knob_spec, sizeof(knob_spec));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "show_config: knob spec");

        char info_text[2048];
        int written = snprintf(info_text, sizeof(info_text),
                               "## yai\n"
                               "engine: %s · session: %s\n"
                               "transcript: %s\n"
                               "edit mode: %s · turn in flight: %s · queued %d/%d\n"
                               "## %s\n"
                               "%s",
                               app->engine_name,
                               app->session_id[0] ? app->session_id : "(none yet)",
                               app->transcript_file ? app->transcript_path : "(disabled)",
                               app->editor_mode_name, app->waiting ? "yes" : "no", app->queue_len,
                               YAI_QUEUE_MAX, app->engine_name, engine_rows);
        if (written < 0 || (size_t)written >= sizeof(info_text)) {
            return YETTY_ERR(yetty_ycore_void, "show_config: info text truncated");
        }

        struct yai_hud_config_setup setup = {0};
        setup.info_text = info_text;
        setup.show_thinking = app->renderer.show_thinking;
        setup.fold_lines = (float)app->renderer.fold_lines;
        /* knob 0: yai edit mode; knob 1: the engine's knob (if any). */
        build_editmode_knob(app, &setup.knobs[0]);
        char engine_knob_label[64];
        struct yetty_ycore_int_result engine_knob_res = parse_engine_knob(
            app, 1, knob_spec, engine_knob_label, sizeof(engine_knob_label), &setup.knobs[1]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, engine_knob_res, "show_config: engine knob");
        app->config_knob_count = 1 + engine_knob_res.value;
        setup.knob_count = app->config_knob_count;
        app->config_show_thinking_applied = app->renderer.show_thinking;
        app->config_fold_lines_applied = app->renderer.fold_lines;

        struct yetty_ycore_void_result toggle_res = yai_hud_toggle_config(app->hud, &setup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_res, "show_config: dialog");
        return YETTY_OK_VOID();
    }

    char config_text[2048];
    int written = snprintf(config_text, sizeof(config_text),
                           "## yai\n"
                           "engine: %s  [--engine / YAI_ENGINE]\n"
                           "session: %s\n"
                           "transcript: %s\n"
                           "fold lines: %d  [YAI_FOLD_LINES]\n"
                           "show thinking: %s  [YAI_SHOW_THINKING]\n"
                           "hud: off  [YAI_NO_HUD]\n"
                           "turn in flight: %s · queued %d/%d\n"
                           "## %s\n"
                           "%s",
                           app->engine_name, app->session_id[0] ? app->session_id : "(none yet)",
                           app->transcript_file ? app->transcript_path : "(disabled)",
                           app->renderer.fold_lines, app->renderer.show_thinking ? "on" : "off",
                           app->waiting ? "yes" : "no", app->queue_len, YAI_QUEUE_MAX,
                           app->engine_name, engine_rows);
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
        if (len == 1 && (line[0] == 'y' || line[0] == 'Y')) {
            struct yetty_ycore_void_result allow_res =
                yetty_yai_resolve_permission(NULL, app->engine, app, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, allow_res, "handle_input_line: allow");
        } else if (len == 1 && (line[0] == 'n' || line[0] == 'N')) {
            struct yetty_ycore_void_result deny_res =
                yetty_yai_resolve_permission(NULL, app->engine, app, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, deny_res, "handle_input_line: deny");
        } else {
            struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "handle_input_line: suspend");
            printf(YAI_DIM "  pending permission — answer y or n" YAI_RESET "\n");
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
                struct yetty_ycore_void_result prompt_res = show_prompt(app);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, prompt_res,
                                    "handle_input_line: prompt after config");
                return YETTY_OK_VOID();
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
        struct yetty_ycore_void_result queue_res = YETTY_OK_VOID();
        if (app->queue_len < YAI_QUEUE_MAX) {
            char *copy = strndup(line, len);
            if (!copy) {
                return YETTY_ERR(yetty_ycore_void, "handle_input_line: queue strndup failed");
            }
            app->queue[app->queue_len++] = copy;
            printf(YAI_DIM "(queued — sends when this turn finishes)" YAI_RESET "\n");
        } else {
            printf(YAI_RED "queue full — message dropped" YAI_RESET "\n");
            queue_res = YETTY_ERR(yetty_ycore_void, "handle_input_line: queue full");
        }
        /* Best-effort: the zone must come back whatever happened; the
         * first error wins, the rest still run. */
        struct yetty_ycore_void_result restore_res = yai_render_flush_stdout();
        restore_res = yetty_ycore_void_chain(restore_res, yai_renderer_zone_resume(&app->renderer));
        queue_res = yetty_ycore_void_chain(queue_res, restore_res);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, queue_res, "handle_input_line: queue");
        return YETTY_OK_VOID();
    }
    char *copy = strndup(line, len);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "handle_input_line: strndup failed");
    }
    app->waiting = 1;
    struct yetty_ycore_void_result activity_res =
        yai_set_activity(app, "typing-dots", "… thinking");
    if (YETTY_IS_ERR(activity_res)) {
        yai_report_error(app, "activity", activity_res);
    }
    struct yetty_ycore_void_result send_res =
        yetty_yai_send_user_message(NULL, app->engine, app, copy);
    free(copy);
    if (YETTY_IS_ERR(send_res)) {
        /* Recoverable UI flow: the prompt comes back, the user retries.
         * This is an end-consumer point for the send error. */
        app->waiting = 0;
        yai_report_error(app, "activity clear",
                         yai_renderer_activity_clear(&app->renderer));
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

/* Ctrl-C: interrupt the in-flight turn, else begin shutdown. */
static struct yetty_ycore_void_result editor_interrupt(struct yai_app *app)
{
    if (app->waiting && app->child_alive) {
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "editor_interrupt: suspend");
        printf("\n" YAI_MUTED "(interrupt requested)" YAI_RESET "\n");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "editor_interrupt: flush");
        struct yetty_ycore_void_result interrupt_res = yetty_yai_interrupt(NULL, app->engine, app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, interrupt_res, "editor_interrupt: interrupt");
        struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "editor_interrupt: resume");
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result close_res = menu_close(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "editor_interrupt: menu close");
    struct yetty_ycore_void_result shutdown_res = yai_begin_shutdown(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shutdown_res, "editor_interrupt: shutdown");
    return YETTY_OK_VOID();
}

/* Ctrl-D on an empty line: quit. */
static struct yetty_ycore_void_result editor_eof(struct yai_app *app)
{
    struct yetty_ycore_void_result close_res = menu_close(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "editor_eof: menu close");
    struct yetty_ycore_void_result shutdown_res = yai_begin_shutdown(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shutdown_res, "editor_eof: shutdown");
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
    default:
        return YETTY_OK_VOID();
    }
}

static struct yetty_ycore_void_result keyboard_input(struct yai_app *app, const char *bytes,
                                                     size_t len)
{
    ydebug("yai: keys len=%zu first=0x%02x -> %s", len, len ? (unsigned char)bytes[0] : 0,
           (app->focus_gui && app->hud) ? "gui" : "editor");
    if (app->focus_gui && app->hud) {
        struct yetty_ycore_void_result feed_res = yai_hud_feed_keys(app->hud, bytes, len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed_res, "keyboard_input: gui keys");
        return YETTY_OK_VOID();
    }
    for (size_t index = 0; index < len; index++) {
        struct yetty_ycore_int_result action_res =
            yetty_yai_feed_byte(NULL, app->editor, app, (unsigned char)bytes[index]);
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
        app->renderer.show_thinking = values.show_thinking;
    }
    int fold_lines = (int)(values.fold_lines + 0.5f);
    if (fold_lines != app->config_fold_lines_applied) {
        app->config_fold_lines_applied = fold_lines;
        app->renderer.fold_lines = fold_lines;
    }
    for (int knob = 0; knob < app->config_knob_count && knob < YAI_HUD_CONFIG_MAX_KNOBS; knob++) {
        int chosen = values.knob_selected[knob];
        if (chosen < 0 || chosen >= app->config_knobs[knob].option_count ||
            chosen == app->config_knobs[knob].selected) {
            continue;
        }
        app->config_knobs[knob].selected = chosen;
        const char *value = app->config_knobs[knob].options[chosen];
        if (app->config_knobs[knob].is_edit_mode) {
            struct yetty_ycore_void_result mode_res = yai_set_edit_mode(app, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mode_res, "config_dialog_sync: edit mode");
            struct yetty_ycore_void_result note_res = config_note(app, "edit mode", value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, note_res, "config_dialog_sync: note");
        } else {
            struct yetty_ycore_void_result apply_res = yetty_yai_apply_config(
                NULL, app->engine, app, app->config_knobs[knob].key, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, apply_res, "config_dialog_sync: apply");
            struct yetty_ycore_void_result note_res =
                config_note(app, app->config_knobs[knob].key, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, note_res, "config_dialog_sync: note");
        }
    }
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
            /* Client-side focus: the press decides who owns input. */
            app->focus_gui = press_res.value;
            ydebug("yai: focus -> %s", app->focus_gui ? "gui" : "terminal");
        }
        /* Press AND release: a slider drag commits its value on the
         * release, a checkbox/radio flips on the press. */
        struct yetty_ycore_void_result sync_res = config_dialog_sync(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sync_res, "mouse envelope: config sync");
        return YETTY_OK_VOID();
    }
    case YETTY_YMGUI_INPUT_MOUSE_POS: {
        struct yetty_ycore_void_result motion_res =
            yai_hud_mouse_motion(app->hud, mouse.x, mouse.y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, motion_res, "mouse envelope: motion");
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
        yai_report_error(app, "resize envelope",
                         handle_resize_envelope(app, payload, payload_len));
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
    if (app->waiting && app->child_alive) {
        yai_report_error(app, "interrupt suspend", yai_renderer_zone_suspend(&app->renderer));
        printf("\n" YAI_MUTED "(interrupt requested)" YAI_RESET "\n");
        yai_report_error(app, "interrupt flush", yai_render_flush_stdout());
        yai_report_error(app, "interrupt", yetty_yai_interrupt(NULL, app->engine, app));
        yai_report_error(app, "interrupt resume", yai_renderer_zone_resume(&app->renderer));
        return;
    }
    yai_report_error(app, "shutdown", yai_begin_shutdown(app));
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
    yai_report_error(app, "zone re-clip", yai_renderer_pin_redraw(&app->renderer));
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

/* tmp/ + the transcript file. The transcript is a diagnostic mirror —
 * the caller decides whether a failure here is fatal. */
static struct yetty_ycore_void_result transcript_open(struct yai_app *app, const char *file_tag)
{
    if (mkdir("tmp", 0777) != 0 && errno != EEXIST) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: mkdir tmp failed");
    }
    int written = snprintf(app->transcript_path, sizeof(app->transcript_path),
                           "tmp/transcript-%s.jsonl", file_tag);
    if (written < 0 || (size_t)written >= sizeof(app->transcript_path)) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: path truncated");
    }
    app->transcript_file = fopen(app->transcript_path, "a");
    if (!app->transcript_file) {
        return YETTY_ERR(yetty_ycore_void, "transcript_open: fopen failed");
    }
    return YETTY_OK_VOID();
}

/* The fd the child's stderr is redirected to (a log under tmp/). */
static struct yetty_ycore_int_result child_stderr_log_open(const char *engine_name,
                                                           const char *file_tag)
{
    char stderr_log_path[256];
    int written = snprintf(stderr_log_path, sizeof(stderr_log_path), "tmp/%s-stderr-%s.log",
                           engine_name, file_tag);
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
        return YETTY_ERR(yetty_ycore_void,
                         "input_watchers_start: sigwinch uv_signal_start failed");
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
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result print_banner(const struct yai_app *app)
{
    printf(YAI_MINT YAI_BOLD
           "yai" YAI_RESET " " YAI_MUTED "engine %s · session %s" YAI_RESET "\n" YAI_DIM
           "transcript=%s  hud=%s" YAI_RESET "\n" YAI_DIM
           "Type a message. /quit or Ctrl-D to exit; Ctrl-C interrupts a turn; /shell or !cmd "
           "for a shell." YAI_RESET "\n",
           app->engine_name, app->session_id[0] ? app->session_id : "(new)", app->transcript_path,
           app->hud ? "on" : "off");
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
    /* vi enters in insert; emacs has no modal indicator. The first
     * keystroke refreshes this, but set it now so the prompt is right
     * the instant the mode changes. */
    snprintf(app->edit_status, sizeof(app->edit_status), "%s",
             strcmp(target, "vi") == 0 ? "[I]" : "");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "yai_set_edit_mode: free old editor");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    ytrace_init();

    const char *resume_session_id = NULL;
    const char *engine_name = getenv("YAI_ENGINE");
    for (int arg_index = 1; arg_index < argc; arg_index++) {
        if (strcmp(argv[arg_index], "--resume") == 0 && arg_index + 1 < argc) {
            resume_session_id = argv[++arg_index];
        } else if (strcmp(argv[arg_index], "--engine") == 0 && arg_index + 1 < argc) {
            engine_name = argv[++arg_index];
        }
    }
    if (!engine_name || !engine_name[0]) {
        engine_name = "claude";
    }
    if (strcmp(engine_name, "claude") != 0 && strcmp(engine_name, "codex") != 0 &&
        strcmp(engine_name, "gemini") != 0) {
        fprintf(stderr, "yai: unknown engine '%s' (claude / codex / gemini)\n", engine_name);
        return 2;
    }

    struct yai_app *app = calloc(1, sizeof(*app));
    if (!app) {
        return 1;
    }
    app->engine_name = engine_name;
    struct yetty_yclass_object_ptr_result engine_res = create_engine(engine_name);
    if (YETTY_IS_ERR(engine_res)) {
        yai_report_error(app, "engine create",
                         (struct yetty_ycore_void_result){.ok = 0, .error = engine_res.error});
        free(app);
        return 1;
    }
    app->engine = engine_res.value;

    /* The line-editor strategy: emacs (default) or vi. */
    const char *edit_mode = getenv("YAI_EDIT_MODE");
    if (!edit_mode || (strcmp(edit_mode, "vi") != 0 && strcmp(edit_mode, "emacs") != 0)) {
        edit_mode = "emacs";
    }
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
    yai_renderer_init(&app->renderer, env_int("YAI_FOLD_LINES", YAI_DEFAULT_FOLD_LINES),
                      env_flag("YAI_SHOW_THINKING"), engine_name);
    yai_renderer_pin_setup(&app->renderer, app->stdin_buf, &app->stdin_len, &app->stdin_cursor);
    app->renderer.edit_status_ptr = app->edit_status;
    app->history_browse = -1;
    app->echo_input = isatty(STDIN_FILENO);
    yai_report_error(app, "command table", yai_command_table_init(&app->commands));

    /* The transcript is a diagnostic mirror — a failure is reported but
     * not fatal: the session runs without it. */
    yai_report_error(app, "transcript", transcript_open(app, file_tag));

    /* No log file → the child's stderr stays on ours. Explicit,
     * reported degradation, not a silent one. */
    int stderr_fd = STDERR_FILENO;
    struct yetty_ycore_int_result stderr_log_res = child_stderr_log_open(engine_name, file_tag);
    if (YETTY_IS_ERR(stderr_log_res)) {
        yai_report_error(
            app, "child stderr log",
            (struct yetty_ycore_void_result){.ok = 0, .error = stderr_log_res.error});
    } else {
        stderr_fd = stderr_log_res.value;
    }
    app->child_stderr_fd = stderr_fd;

    /* Raw input BEFORE the first envelope write — otherwise the tty
     * echoes the ESC bytes back and the host renders "^[" garbage. */
    yai_report_error(app, "raw input", enter_raw_input(app));

    struct yai_hud_ptr_result hud_res = yai_hud_create();
    if (YETTY_IS_ERR(hud_res)) {
        yai_report_error(app, "hud create",
                         (struct yetty_ycore_void_result){.ok = 0, .error = hud_res.error});
        app->hud = NULL; /* run without the window */
    } else {
        app->hud = hud_res.value;
    }

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

    struct yetty_ycore_void_result start_res = yetty_yai_start(NULL, app->engine, app);
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

    yai_report_error(app, "banner", print_banner(app));
    yai_report_error(app, "hud dock", yai_apply_dock_reservation(app));
    yai_report_error(app, "prompt", show_prompt(app));

    uv_run(&app->loop, UV_RUN_DEFAULT);

    /* Drain closing handles. */
    uv_close((uv_handle_t *)&app->stdin_poll, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigint_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigwinch_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sigterm_handle, yai_handle_closed_cb);
    uv_close((uv_handle_t *)&app->sighup_handle, yai_handle_closed_cb);
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

    yai_report_error(app, "hud dock release", yai_release_dock_reservation(app));
    yai_report_error(app, "mouse unsubscribe", unsubscribe_mouse(app));
    if (app->hud) {
        yai_report_error(app, "hud destroy", yai_hud_destroy(app->hud));
        app->hud = NULL;
    }
    yai_report_error(app, "input demux destroy", yetty_yface_destroy(app->yface));
    yai_report_error(app, "raw input restore", leave_raw_input(app));
    printf("\n" YAI_DIM "transcript saved to %s" YAI_RESET "\n", app->transcript_path);
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
    yai_renderer_destroy(&app->renderer);
    for (int history_index = 0; history_index < app->history_len; history_index++) {
        free(app->history[history_index]);
    }
    free(app->child_out_buf);
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
