/*
 * shell.c — the persistent interop shell behind the "!" focus switch.
 *
 * One interactive $SHELL (zsh or bash only — anything else is refused
 * with a warning) runs in parallel with yai on its own PTY, spawned
 * lazily on the first "!". Pressing "!" as the first character of an
 * empty prompt hands the keyboard to the shell: every byte is forwarded
 * raw to the PTY master, so the shell's own line editor, completion and
 * bindings (Ctrl-R → fzf, …) work exactly as in a bare terminal, and
 * the shell's output is relayed raw to the screen. Focus automatically
 * snaps back to yai when the invoked command/function finishes.
 *
 * "Finished" is detected with shell hooks injected at spawn (the user's
 * rc files load first, the hooks are appended): each prompt display
 * emits a private OSC marker (ESC ] 7771 ; precmd BEL) and each
 * accepted command line emits ESC ] 7771 ; preexec BEL — zsh via
 * precmd/preexec hooks from a ZDOTDIR shim, bash via PROMPT_COMMAND and
 * PS0 from an --rcfile shim. yai strips the markers from the stream; a
 * precmd marker while the shell has focus means "back at the prompt" →
 * focus returns to yai (the very first prompt after spawn only arms the
 * machinery). Ctrl-] is the manual escape hatch (main.c).
 *
 * While yai owns the screen, shell output (prompt paints, background
 * jobs) is buffered and replayed on the next "!", so the user always
 * enters at a live prompt; while the shell owns the screen, engine
 * output is deferred by main.c and replayed when focus returns.
 */
#include "app.h"

#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

/* Ceiling for output buffered while yai owns the screen; the front is
 * dropped beyond it (prompt paints are tiny — only a chatty background
 * job can reach this). */
#define YAI_SHELL_PENDING_MAX (1u * 1024u * 1024u)

/* OSC-7771 marker filter states. */
enum {
    YAI_SHELL_FILTER_SCAN = 0, /* copying plain bytes through */
    YAI_SHELL_FILTER_ESC,      /* held an ESC, deciding */
    YAI_SHELL_FILTER_OSC,      /* inside ESC ] …, accumulating */
};

/*---------------------------------------------------------------------------
 * Supported-shell detection
 *---------------------------------------------------------------------------*/

/* The $SHELL basename when it is a supported interop shell ("zsh" /
 * "bash"), NULL otherwise. `out_path` receives the full $SHELL value. */
static const char *shell_supported(const char **out_path)
{
    const char *shell_path = getenv("SHELL");
    *out_path = shell_path;
    if (!shell_path || !shell_path[0]) {
        return NULL;
    }
    const char *base = strrchr(shell_path, '/');
    base = base ? base + 1 : shell_path;
    if (strcmp(base, "zsh") == 0 || strcmp(base, "bash") == 0) {
        return base;
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Hook shims — written once per session under the transcript dir
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_void_result shell_write_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: fopen failed");
    }
    int failed = fputs(content, file) == EOF;
    failed |= fclose(file) != 0;
    if (failed) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: write failed");
    }
    return YETTY_OK_VOID();
}

/* Fill `out` with `hex_chars` lowercase-hex random characters + NUL (out
 * must hold hex_chars + 1). Returns 0 on success. Used for the marker
 * nonce — needs only to be unguessable enough to avoid accidental /
 * casual collisions, not cryptographic secrecy. */
static int shell_random_hex(char *out, size_t hex_chars)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    size_t bytes_needed = (hex_chars + 1) / 2;
    unsigned char raw[16];
    if (bytes_needed > sizeof(raw)) {
        bytes_needed = sizeof(raw);
    }
    ssize_t got = read(fd, raw, bytes_needed);
    close(fd);
    if (got <= 0) {
        return -1;
    }
    static const char hex_digits[] = "0123456789abcdef";
    size_t written = 0;
    for (ssize_t index = 0; index < got && written < hex_chars; index++) {
        out[written++] = hex_digits[raw[index] >> 4];
        if (written < hex_chars) {
            out[written++] = hex_digits[raw[index] & 0x0f];
        }
    }
    out[written] = '\0';
    return written == hex_chars ? 0 : -1;
}

/* Create the shim directory and write the hook files for `base` ("zsh" /
 * "bash"). The precmd/preexec hooks embed app->shell_marker_nonce so only
 * yai's own shell can emit markers yai will honor. Fills `shim_dir` with
 * the directory path. */
static struct yetty_ycore_void_result shell_write_shims(struct yai_app *app, const char *base,
                                                        char *shim_dir, size_t shim_dir_size)
{
    const char *nonce = app->shell_marker_nonce;
    char transcripts[PATH_MAX];
    if (yai_transcript_dir(transcripts, sizeof(transcripts)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: transcript dir too long");
    }
    int written = snprintf(shim_dir, shim_dir_size, "%s/shell-shim-%s", transcripts,
                           app->transcript_tag);
    if (written < 0 || (size_t)written >= shim_dir_size) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: path too long");
    }
    if (mkdir(shim_dir, 0700) != 0 && errno != EEXIST) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: mkdir failed");
    }

    char path[PATH_MAX];
    if (strcmp(base, "zsh") == 0) {
        /* ZDOTDIR shim: .zshenv and .zshrc chain to the user's own (from
         * the original ZDOTDIR, default $HOME), then .zshrc appends the
         * marker hooks. Caveat: a user .zshenv that re-sets ZDOTDIR wins
         * and the hooks are lost — then "!" simply never returns focus
         * automatically (Ctrl-] still does). */
        snprintf(path, sizeof(path), "%s/.zshenv", shim_dir);
        struct yetty_ycore_void_result env_res = shell_write_file(
            path, "yai_zdot=${YAI_ORIG_ZDOTDIR:-$HOME}\n"
                  "[[ -f $yai_zdot/.zshenv ]] && source $yai_zdot/.zshenv\n"
                  "unset yai_zdot\n");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, env_res, "shell shim: .zshenv");
        snprintf(path, sizeof(path), "%s/.zshrc", shim_dir);
        char zshrc[512];
        int zwritten = snprintf(
            zshrc, sizeof(zshrc),
            "yai_zdot=${YAI_ORIG_ZDOTDIR:-$HOME}\n"
            "[[ -f $yai_zdot/.zshrc ]] && source $yai_zdot/.zshrc\n"
            "unset yai_zdot\n"
            "yai_hook_preexec() { printf '\\033]7771;%s;preexec\\007'; }\n"
            "yai_hook_precmd() { printf '\\033]7771;%s;precmd\\007'; }\n"
            "autoload -Uz add-zsh-hook\n"
            "add-zsh-hook preexec yai_hook_preexec\n"
            "add-zsh-hook precmd yai_hook_precmd\n",
            nonce, nonce);
        if (zwritten < 0 || (size_t)zwritten >= sizeof(zshrc)) {
            return YETTY_ERR(yetty_ycore_void, "shell shim: .zshrc content too long");
        }
        struct yetty_ycore_void_result rc_res = shell_write_file(path, zshrc);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rc_res, "shell shim: .zshrc");
        return YETTY_OK_VOID();
    }
    /* bash: --rcfile shim. PS0 (bash ≥ 4.4) is the "command accepted"
     * marker; older bash just loses that nicety — focus return only
     * needs the PROMPT_COMMAND marker. */
    snprintf(path, sizeof(path), "%s/bashrc", shim_dir);
    char bashrc[512];
    int bwritten = snprintf(
        bashrc, sizeof(bashrc),
        "[[ -f ~/.bashrc ]] && source ~/.bashrc\n"
        "yai_hook_precmd() { printf '\\033]7771;%s;precmd\\007'; }\n"
        "PROMPT_COMMAND=\"yai_hook_precmd${PROMPT_COMMAND:+;$PROMPT_COMMAND}\"\n"
        "if ((BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 4)));"
        " then\n"
        "  PS0='\\e]7771;%s;preexec\\a'\"${PS0}\"\n"
        "fi\n",
        nonce, nonce);
    if (bwritten < 0 || (size_t)bwritten >= sizeof(bashrc)) {
        return YETTY_ERR(yetty_ycore_void, "shell shim: bashrc content too long");
    }
    struct yetty_ycore_void_result rc_res = shell_write_file(path, bashrc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rc_res, "shell shim: bashrc");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Output relay — marker filter + focus-dependent routing
 *---------------------------------------------------------------------------*/

/* Route clean (marker-stripped) output: to the screen while the shell
 * has focus, into the pending buffer otherwise (replayed on the next
 * "!"). */
static struct yetty_ycore_void_result shell_route_bytes(struct yai_app *app, const char *bytes,
                                                        size_t len)
{
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (app->shell_focus) {
        if (fwrite(bytes, 1, len, stdout) != len) {
            return YETTY_ERR(yetty_ycore_void, "shell route: fwrite failed");
        }
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "shell route: flush");
        return YETTY_OK_VOID();
    }
    if (app->shell_pending_len + len > YAI_SHELL_PENDING_MAX) {
        /* Drop from the front — the tail (the live prompt) is what the
         * next "!" needs. */
        size_t excess = app->shell_pending_len + len - YAI_SHELL_PENDING_MAX;
        if (excess >= app->shell_pending_len) {
            app->shell_pending_len = 0;
        } else {
            memmove(app->shell_pending, app->shell_pending + excess,
                    app->shell_pending_len - excess);
            app->shell_pending_len -= excess;
        }
        if (len > YAI_SHELL_PENDING_MAX) {
            bytes += len - YAI_SHELL_PENDING_MAX;
            len = YAI_SHELL_PENDING_MAX;
        }
    }
    if (app->shell_pending_len + len > app->shell_pending_cap) {
        size_t new_cap = app->shell_pending_cap ? app->shell_pending_cap : 4096;
        while (new_cap < app->shell_pending_len + len) {
            new_cap *= 2;
        }
        char *grown = realloc(app->shell_pending, new_cap);
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "shell route: realloc failed");
        }
        app->shell_pending = grown;
        app->shell_pending_cap = new_cap;
    }
    memcpy(app->shell_pending + app->shell_pending_len, bytes, len);
    app->shell_pending_len += len;
    return YETTY_OK_VOID();
}

/* One recognized marker. `payload` is the text after "7771;". */
static struct yetty_ycore_void_result shell_handle_marker(struct yai_app *app,
                                                          const char *payload)
{
    if (strcmp(payload, "precmd") == 0) {
        if (!app->shell_saw_first_prompt) {
            /* The spawn-time prompt — the machinery is armed now. */
            app->shell_saw_first_prompt = 1;
            return YETTY_OK_VOID();
        }
        if (app->shell_focus) {
            /* Back at the prompt: the invocation is done. */
            struct yetty_ycore_void_result end_res = yai_shell_focus_end(app, NULL);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, end_res, "shell marker: focus end");
        }
        return YETTY_OK_VOID();
    }
    if (strcmp(payload, "preexec") == 0 && app->shell_focus) {
        struct yetty_ycore_void_result state_res = yai_set_state(app, "⌨ shell · running");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "shell marker: state");
    }
    return YETTY_OK_VOID();
}

/* Push one chunk of PTY output through the OSC-7771 filter. Markers may
 * split across chunks, so partial candidates are held in
 * shell_filter_hold between calls; anything that turns out not to be a
 * marker is re-emitted verbatim. Other escape sequences pass through
 * untouched. */
static struct yetty_ycore_void_result shell_filter_output(struct yai_app *app, const char *bytes,
                                                          size_t len)
{
    for (size_t index = 0; index < len; index++) {
        char byte = bytes[index];
        switch (app->shell_filter_state) {
        case YAI_SHELL_FILTER_SCAN: {
            if (byte != 0x1b) {
                /* Fast path: emit the whole plain run at once. */
                size_t run_start = index;
                while (index + 1 < len && bytes[index + 1] != 0x1b) {
                    index++;
                }
                struct yetty_ycore_void_result route_res =
                    shell_route_bytes(app, bytes + run_start, index - run_start + 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, route_res, "shell filter: run");
                break;
            }
            app->shell_filter_state = YAI_SHELL_FILTER_ESC;
            app->shell_filter_hold[0] = byte;
            app->shell_filter_hold_len = 1;
            break;
        }
        case YAI_SHELL_FILTER_ESC: {
            if (byte == ']') {
                app->shell_filter_state = YAI_SHELL_FILTER_OSC;
                app->shell_filter_hold[app->shell_filter_hold_len++] = byte;
                break;
            }
            /* Not an OSC — release the ESC and reprocess this byte. */
            app->shell_filter_state = YAI_SHELL_FILTER_SCAN;
            struct yetty_ycore_void_result esc_res =
                shell_route_bytes(app, app->shell_filter_hold, app->shell_filter_hold_len);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, esc_res, "shell filter: esc release");
            app->shell_filter_hold_len = 0;
            index--; /* reprocess in SCAN */
            break;
        }
        case YAI_SHELL_FILTER_OSC: {
            /* Accumulate up to BEL / ESC-\ / capacity. Only exact
             * "7771;…" sequences are consumed; everything else (titles,
             * OSC 133, …) is released verbatim. */
            int terminated = (byte == 0x07);
            int overlong =
                app->shell_filter_hold_len + 1 >= sizeof(app->shell_filter_hold) - 1;
            app->shell_filter_hold[app->shell_filter_hold_len++] = byte;
            if (byte == '\\' && app->shell_filter_hold_len >= 2 &&
                app->shell_filter_hold[app->shell_filter_hold_len - 2] == 0x1b) {
                terminated = 1;
            }
            if (!terminated && !overlong) {
                break;
            }
            app->shell_filter_state = YAI_SHELL_FILTER_SCAN;
            app->shell_filter_hold[app->shell_filter_hold_len] = '\0';
            /* Honor only "\x1b]7771;<nonce>;<payload>" where <nonce> is this
             * shell's own token — an unnonced or wrong-nonced sequence
             * (ordinary output, a spoof) is released verbatim, never acted
             * on. hold[0]=ESC hold[1]=']' hold[2..6]="7771;" hold[7..]=nonce. */
            size_t nonce_len = strlen(app->shell_marker_nonce);
            if (terminated && nonce_len > 0 &&
                strncmp(app->shell_filter_hold + 2, "7771;", 5) == 0 &&
                strncmp(app->shell_filter_hold + 7, app->shell_marker_nonce, nonce_len) == 0 &&
                app->shell_filter_hold[7 + nonce_len] == ';') {
                /* Strip the terminator (BEL or ESC-\) off the payload. */
                size_t payload_end = app->shell_filter_hold_len - 1;
                if (app->shell_filter_hold[payload_end] == '\\') {
                    payload_end--; /* the ESC of ESC-\ */
                }
                app->shell_filter_hold[payload_end] = '\0';
                struct yetty_ycore_void_result marker_res =
                    shell_handle_marker(app, app->shell_filter_hold + 7 + nonce_len + 1);
                app->shell_filter_hold_len = 0;
                YETTY_RETURN_IF_ERR(yetty_ycore_void, marker_res, "shell filter: marker");
                break;
            }
            struct yetty_ycore_void_result release_res =
                shell_route_bytes(app, app->shell_filter_hold, app->shell_filter_hold_len);
            app->shell_filter_hold_len = 0;
            YETTY_RETURN_IF_ERR(yetty_ycore_void, release_res, "shell filter: osc release");
            break;
        }
        default:
            return YETTY_ERR(yetty_ycore_void, "shell filter: bad state");
        }
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Child lifecycle
 *---------------------------------------------------------------------------*/

/* The shell died (EOF/EIO on the master, or SIGCHLD): tear the fd/poll
 * down, reap, and give the keyboard back to yai if it was focused. */
static void shell_gone(struct yai_app *app)
{
    if (app->shell_pid == 0 && app->shell_master_fd < 0) {
        return;
    }
    if (app->shell_poll_initialized && !uv_is_closing((uv_handle_t *)&app->shell_poll)) {
        uv_poll_stop(&app->shell_poll);
        uv_close((uv_handle_t *)&app->shell_poll, NULL);
    }
    if (app->shell_master_fd >= 0) {
        close(app->shell_master_fd);
        app->shell_master_fd = -1;
    }
    if (app->shell_pid > 0) {
        waitpid(app->shell_pid, NULL, WNOHANG);
        app->shell_pid = 0;
    }
    app->shell_saw_first_prompt = 0;
    app->shell_filter_state = YAI_SHELL_FILTER_SCAN;
    app->shell_filter_hold_len = 0;
    app->shell_pending_len = 0;
    if (app->shell_focus && !app->shutting_down) {
        yai_report_error(app, "shell focus",
                         yai_shell_focus_end(app, "(interop shell exited — ! restarts it)"));
    }
}

YETTY_EXTERNAL_CALLBACK
static void shell_poll_cb(uv_poll_t *poll_handle, int status, int events)
{
    struct yai_app *app = poll_handle->data;
    (void)events;
    if (status < 0) {
        shell_gone(app);
        return;
    }
    char chunk[4096];
    for (;;) {
        ssize_t nread = read(app->shell_master_fd, chunk, sizeof(chunk));
        if (nread > 0) {
            yai_report_error(app, "shell output",
                             shell_filter_output(app, chunk, (size_t)nread));
            continue;
        }
        if (nread < 0 && errno == EINTR) {
            continue;
        }
        if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; /* drained */
        }
        /* EOF or EIO — the slave side is gone. */
        shell_gone(app);
        return;
    }
}

YETTY_EXTERNAL_CALLBACK
static void on_sigchld(uv_signal_t *signal_handle, int signum)
{
    struct yai_app *app = signal_handle->data;
    (void)signum;
    /* Only reap OUR shell; engine children belong to libuv, and the
     * interactive /shell handoff waits synchronously on its own pid. */
    if (app->shell_pid > 0 && waitpid(app->shell_pid, NULL, WNOHANG) == app->shell_pid) {
        app->shell_pid = 0;
        shell_gone(app);
    }
}

void yai_shell_resize(struct yai_app *app)
{
    if (app->shell_master_fd < 0) {
        return;
    }
    struct winsize window_size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0) {
        ioctl(app->shell_master_fd, TIOCSWINSZ, &window_size);
    }
}

static struct yetty_ycore_void_result shell_spawn(struct yai_app *app, const char *shell_path,
                                                  const char *base)
{
    /* Fresh marker nonce for this shell so only its own hooks can move
     * focus (see shell_marker_nonce). 16 hex chars fit the marker well
     * inside shell_filter_hold. */
    if (shell_random_hex(app->shell_marker_nonce, 16) != 0) {
        return YETTY_ERR(yetty_ycore_void, "shell spawn: marker nonce");
    }

    char shim_dir[PATH_MAX];
    struct yetty_ycore_void_result shim_res =
        shell_write_shims(app, base, shim_dir, sizeof(shim_dir));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shim_res, "shell spawn: shims");

    /* forkpty (the ypty convention): openpty + fork + login_tty — the
     * child is a session leader with the slave as controlling terminal,
     * so job control works. */
    struct winsize window_size = {0};
    int have_size = ioctl(STDIN_FILENO, TIOCGWINSZ, &window_size) == 0;
    int master = -1;
    pid_t child = forkpty(&master, NULL, NULL, have_size ? &window_size : NULL);
    if (child < 0) {
        return YETTY_ERR(yetty_ycore_void, "shell spawn: forkpty failed");
    }
    if (child == 0) {
        if (strcmp(base, "zsh") == 0) {
            const char *original_zdotdir = getenv("ZDOTDIR");
            if (original_zdotdir && original_zdotdir[0]) {
                setenv("YAI_ORIG_ZDOTDIR", original_zdotdir, 1);
            }
            setenv("ZDOTDIR", shim_dir, 1);
            execlp(shell_path, shell_path, (char *)NULL);
        } else {
            char rcfile[PATH_MAX];
            snprintf(rcfile, sizeof(rcfile), "%s/bashrc", shim_dir);
            execlp(shell_path, shell_path, "--rcfile", rcfile, (char *)NULL);
        }
        _exit(127);
    }

    int flags = fcntl(master, F_GETFL, 0);
    if (flags < 0 || fcntl(master, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(master, F_SETFD, FD_CLOEXEC) < 0) {
        close(master);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        return YETTY_ERR(yetty_ycore_void, "shell spawn: master fcntl failed");
    }
    if (uv_poll_init(&app->loop, &app->shell_poll, master) != 0) {
        close(master);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        return YETTY_ERR(yetty_ycore_void, "shell spawn: uv_poll_init failed");
    }
    app->shell_poll.data = app;
    app->shell_poll_initialized = 1;
    if (uv_poll_start(&app->shell_poll, UV_READABLE, shell_poll_cb) != 0) {
        uv_close((uv_handle_t *)&app->shell_poll, NULL);
        close(master);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        return YETTY_ERR(yetty_ycore_void, "shell spawn: uv_poll_start failed");
    }
    app->shell_master_fd = master;
    app->shell_pid = child;
    app->shell_saw_first_prompt = 0;
    ydebug("yai shell: started %s (pid %d)", shell_path, (int)child);
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Public surface
 *---------------------------------------------------------------------------*/

struct yetty_ycore_void_result yai_shell_focus_begin(struct yai_app *app)
{
    const char *shell_path = NULL;
    const char *base = shell_supported(&shell_path);
    if (!base) {
        /* Refuse anything but zsh/bash — the hook shims are shell-
         * specific and silent focus loss would be worse than no shell. */
        struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(&app->renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "shell focus: suspend");
        printf(YAI_RED "! interop shell needs zsh or bash; $SHELL is %s" YAI_RESET "\n",
               shell_path && shell_path[0] ? shell_path : "(unset)");
        struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
        flush_res = yetty_ycore_void_chain(flush_res, yai_renderer_zone_resume(&app->renderer));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "shell focus: refuse flush");
        return YETTY_OK_VOID(); /* refused — app->shell_focus stays 0 */
    }
    if (app->shell_pid == 0) {
        if (app->shell_poll_initialized &&
            !uv_is_closing((uv_handle_t *)&app->shell_poll)) {
            /* Previous instance still winding down — one loop tick away. */
            return YETTY_ERR(yetty_ycore_void, "shell focus: shell restarting, retry");
        }
        struct yetty_ycore_void_result spawn_res = shell_spawn(app, shell_path, base);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, spawn_res, "shell focus: spawn");
    }
    /* The shell owns the screen now: drop the pinned prompt row, mark
     * the HUD, and replay whatever the shell printed since last time
     * (its current prompt paint at minimum). */
    struct yetty_ycore_void_result hide_res = yai_renderer_pin_hide(&app->renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hide_res, "shell focus: pin hide");
    app->shell_focus = 1;
    struct yetty_ycore_void_result state_res = yai_set_state(app, "⌨ shell");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, state_res, "shell focus: state");
    if (app->shell_pending_len > 0) {
        size_t pending_len = app->shell_pending_len;
        app->shell_pending_len = 0;
        if (fwrite(app->shell_pending, 1, pending_len, stdout) != pending_len) {
            return YETTY_ERR(yetty_ycore_void, "shell focus: pending replay failed");
        }
    }
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "shell focus: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_shell_forward(struct yai_app *app, const char *bytes,
                                                 size_t len)
{
    if (app->shell_master_fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "shell forward: shell not running");
    }
    size_t sent = 0;
    int stalls = 0;
    while (sent < len) {
        ssize_t wrote = write(app->shell_master_fd, bytes + sent, len - sent);
        if (wrote > 0) {
            sent += (size_t)wrote;
            continue;
        }
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* Keystroke volumes never fill the PTY buffer unless the
             * shell stopped reading (^S flow control); give it a few
             * short waits, then drop rather than hang the loop. */
            if (++stalls > 20) {
                return YETTY_ERR(yetty_ycore_void, "shell forward: pty stalled, input dropped");
            }
            struct pollfd waiter = {.fd = app->shell_master_fd, .events = POLLOUT};
            poll(&waiter, 1, 50);
            continue;
        }
        return YETTY_ERR(yetty_ycore_void, "shell forward: write failed");
    }
    return YETTY_OK_VOID();
}

int yai_shell_sigchld_start(struct yai_app *app)
{
    if (uv_signal_init(&app->loop, &app->sigchld_handle) != 0) {
        return -1;
    }
    app->sigchld_handle.data = app;
    if (uv_signal_start(&app->sigchld_handle, on_sigchld, SIGCHLD) != 0) {
        return -1;
    }
    return 0;
}

void yai_shell_stop(struct yai_app *app)
{
    if (app->shell_pid > 0) {
        /* A SIGHUP'd interactive shell exits and hangs up its jobs. */
        kill(app->shell_pid, SIGHUP);
    }
    shell_gone(app);
    free(app->shell_pending);
    app->shell_pending = NULL;
    app->shell_pending_cap = 0;
}
