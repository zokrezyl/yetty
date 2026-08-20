/*
 * yperf — a perf / flame-graph workbench built on the ygui widget engine and
 * the yflame renderer.
 *
 * The profile model (profile.c) parses folded stacks (or collapses `perf
 * script` output) into a symbol table and folded text; ui.c renders the flame
 * graph via the yflame class beside a synchronized top-symbol table. This file
 * is the yetty-client harness (libuv loop, stdout OSC transport, stdin keys,
 * SIGWINCH) plus ingestion and keyboard / mouse handling.
 *
 * Stdin is fronted by a yetty_yface: OSC envelopes carry pane mouse / resize
 * events (decoded here and routed to the flame), while raw bytes are the
 * keystrokes fed to the ygui framework. Mouse reporting is turned on the same
 * way tools/yflame does — a pane-wide input subscription plus DEC ?1500h/?1501h.
 *
 *   Record and view in one step (like tools/yflame.sh, but the full workbench):
 *       yetty -e './yperf -- ./my_program --args'   # profile a command, then show
 *       yetty -e './yperf -p 4242 -d 5'             # profile a running pid for 5s
 *       yetty -e './yperf -a -d 5'                  # whole system for 5s (needs privs)
 *       yetty -e './yperf --demo'                   # synthetic data, no perf needed
 *
 *   Or ingest an existing capture:
 *       yetty -e './yperf out.folded'               # folded stacks
 *       yetty -e './yperf --perf perf.data'         # collapse perf.data directly
 *       yetty -e './yperf --diff base.folded cur.folded'   # differential colours
 *
 *   Scrollback figure (renders inline, no dashboard):
 *       yetty -e './yperf --emit out.folded'
 *
 *   Headless (structured text, no GUI):
 *       ./yperf --print out.folded
 *
 * The capture path shells out to the user's `perf` (a separate GPL process) and
 * bundles no profiler code, exactly as tools/yflame.sh does.
 */
#define _DEFAULT_SOURCE 1

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yface/yface.h>
#include "yetty/gen/impl/yflame/flame.h"
#include <yetty/ygui/ygui.h>
#include <yetty/ymgui/wire.h>             /* YMGUI_WIRE_VERSION */
#include <yetty/yterminal/client-input.h> /* mouse / resize / subscription wire */
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
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

#include "app.h"
#include "profile.h"
#include "ui.h"

static void error_absorb(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
    }
}

/* ------------------------------------------------------------------ */
/* Ingestion                                                           */
/* ------------------------------------------------------------------ */

/* Run `perf script -i <path>` and read its stdout into a heap buffer. */
static struct yetty_ycore_void_result run_perf_script(const char *path, char **out, size_t *out_len)
{
    if (strchr(path, '\'')) {
        return YETTY_ERR(yetty_ycore_void, "perf.data path may not contain a quote");
    }
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "perf script -i '%s'", path);
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return YETTY_ERR(yetty_ycore_void, "failed to launch perf");
    }
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        pclose(pipe);
        return YETTY_ERR(yetty_ycore_void, "out of memory reading perf output");
    }
    for (;;) {
        if (len == cap) {
            size_t new_cap = cap * 2;
            char *grown = realloc(buf, new_cap);
            if (!grown) {
                free(buf);
                pclose(pipe);
                return YETTY_ERR(yetty_ycore_void, "out of memory reading perf output");
            }
            buf = grown;
            cap = new_cap;
        }
        size_t got = fread(buf + len, 1, cap - len, pipe);
        len += got;
        if (got == 0) {
            break;
        }
    }
    int status = pclose(pipe);
    if (len == 0) {
        free(buf);
        return YETTY_ERR(yetty_ycore_void,
                         "perf produced no output (is perf installed and the file valid?)");
    }
    (void)status;
    buf[len < cap ? len : cap - 1] = '\0';
    *out = buf;
    *out_len = len;
    return YETTY_OK_VOID();
}

/* Record a profile with `perf record` into a fresh temp perf.data, returning its
 * path (heap-owned) in *out_path. For a command, `record_argv` / `record_argc`
 * is the program to profile; otherwise `pid` (a running process) or `all_system`
 * selects the target and the capture runs for `duration` seconds. Call graphs
 * are DWARF-unwound unless `frame_pointer` is set. Shells out to the user's
 * `perf`; no profiler code is bundled. */
static struct yetty_ycore_void_result perf_record_capture(char **record_argv, int record_argc,
                                                          const char *pid, int all_system,
                                                          int duration, int freq, int frame_pointer,
                                                          char **out_path)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !tmpdir[0]) {
        tmpdir = "/tmp";
    }
    char path_template[4096];
    snprintf(path_template, sizeof(path_template), "%s/yperf-XXXXXX", tmpdir);
    int fd = mkstemp(path_template);
    if (fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "cannot create a temporary perf.data");
    }
    close(fd);

    char freq_buf[16], duration_buf[16];
    snprintf(freq_buf, sizeof(freq_buf), "%d", freq);
    snprintf(duration_buf, sizeof(duration_buf), "%d", duration);
    const char *call_graph = frame_pointer ? "fp" : "dwarf";

    /* perf record -F <freq> --call-graph <cg> -o <tmp> <target> */
    size_t max_args = 16 + (size_t)(record_argc > 0 ? record_argc : 0);
    char **argv = calloc(max_args, sizeof(char *));
    if (!argv) {
        unlink(path_template);
        return YETTY_ERR(yetty_ycore_void, "out of memory building perf command");
    }
    size_t argn = 0;
    argv[argn++] = "perf";
    argv[argn++] = "record";
    argv[argn++] = "-F";
    argv[argn++] = freq_buf;
    argv[argn++] = "--call-graph";
    argv[argn++] = (char *)(uintptr_t)call_graph;
    argv[argn++] = "-o";
    argv[argn++] = path_template;
    if (record_argc > 0) {
        argv[argn++] = "--";
        for (int i = 0; i < record_argc; i++) {
            argv[argn++] = record_argv[i];
        }
    } else if (all_system) {
        argv[argn++] = "-a";
        argv[argn++] = "--";
        argv[argn++] = "sleep";
        argv[argn++] = duration_buf;
    } else {
        argv[argn++] = "-p";
        argv[argn++] = (char *)(uintptr_t)pid;
        argv[argn++] = "--";
        argv[argn++] = "sleep";
        argv[argn++] = duration_buf;
    }
    argv[argn] = NULL;

    fprintf(stderr, "yperf: recording with perf (-F %s, --call-graph %s)…\n", freq_buf, call_graph);
    pid_t child = fork();
    if (child < 0) {
        free(argv);
        unlink(path_template);
        return YETTY_ERR(yetty_ycore_void, "fork failed for perf record");
    }
    if (child == 0) {
        execvp("perf", argv);
        fprintf(stderr, "yperf: cannot run perf (install linux-perf / linux-tools)\n");
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        /* retry on interrupt */
    }
    free(argv);

    /* perf record itself exits non-zero when the profiled program does (or is
     * Ctrl-C'd) — the capture is still valid. Only bail when perf could not be
     * started at all, or when nothing was captured. */
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        unlink(path_template);
        return YETTY_ERR(yetty_ycore_void,
                         "perf could not be started (install linux-perf / linux-tools)");
    }
    struct stat st;
    if (stat(path_template, &st) != 0 || st.st_size == 0) {
        unlink(path_template);
        return YETTY_ERR(
            yetty_ycore_void,
            "perf captured no data (try sudo, or sysctl kernel.perf_event_paranoid=1)");
    }
    char *path = strdup(path_template);
    if (!path) {
        unlink(path_template);
        return YETTY_ERR(yetty_ycore_void, "out of memory");
    }
    *out_path = path;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_profile(const char *path, int perf_data, int perf_script,
                                                   struct yperf_profile **out)
{
    char *input = NULL;
    size_t input_len = 0;
    struct yetty_ycore_void_result read_res;
    if (perf_data) {
        read_res = run_perf_script(path, &input, &input_len);
    } else {
        read_res = yperf_read_all(path, &input, &input_len);
    }
    if (YETTY_IS_ERR(read_res)) {
        return read_res; /* surface the specific cause (bad path, perf failure, …) */
    }

    struct yetty_ycore_void_result build_res;
    if (perf_data || perf_script) {
        build_res = yperf_profile_from_perf_script(input, input_len, out);
    } else {
        build_res = yperf_profile_from_folded(input, input_len, out);
    }
    free(input);
    return build_res;
}

/* ------------------------------------------------------------------ */
/* Headless text mode (consumption mode 3, and a test path)            */
/* ------------------------------------------------------------------ */

static void yperf_print(struct yperf_profile *profile)
{
    printf("%llu stacks  %llu samples  %zu symbols  max-depth %u\n\n",
           (unsigned long long)profile->stack_count, (unsigned long long)profile->total_samples,
           profile->n_symbols, profile->max_depth);
    yperf_profile_sort(profile, YPERF_SORT_SELF);
    printf("%-40s %10s %8s %10s\n", "SYMBOL", "SELF", "SELF%", "TOTAL");
    double denom = profile->total_samples ? (double)profile->total_samples : 1.0;
    size_t rows = profile->n_symbols < 25 ? profile->n_symbols : 25;
    for (size_t i = 0; i < rows; i++) {
        const struct yperf_symbol *sym = &profile->symbols[i];
        char self[16], total[16];
        yperf_fmt_count(sym->self, self, sizeof(self));
        yperf_fmt_count(sym->total, total, sizeof(total));
        printf("%-40.40s %10s %7.1f%% %10s\n", sym->name, self, 100.0 * (double)sym->self / denom,
               total);
    }
}

static void yperf_usage(void)
{
    printf("yperf — perf / flame-graph workbench\n\n"
           "usage: yperf [options] [file | -- <command>]\n\n"
           "capture (shells out to perf):\n"
           "  -- <command> [args] record <command> until it exits, then show it\n"
           "  -p, --pid <pid>     record a running process for --duration seconds\n"
           "  -a, --all           record the whole system for --duration seconds\n"
           "  -d, --duration <s>  capture duration for --pid / --all (default 10)\n"
           "      --freq <hz>     sampling frequency (default 997)\n"
           "      --fp            frame-pointer stacks (default: DWARF call-graph)\n"
           "      --demo          render built-in synthetic data (no perf needed)\n\n"
           "ingest an existing capture:\n"
           "  file                folded-stack file (default: read stdin)\n"
           "  --perf <data>       run `perf script -i <data>` and collapse it\n"
           "  --perf-script       treat the input as raw `perf script` output\n"
           "  --diff <base> <cur> colour <cur> by its delta vs folded <base>\n\n"
           "view:\n"
           "  --focus <symbol>    open zoomed to <symbol> (also applies to --emit)\n"
           "  -e, --emit          emit the flame as a scrollback figure, then exit\n"
           "  -i, --icicle        icicle orientation (root at top)\n"
           "      --print         print the top-symbol table, then exit (no GUI)\n"
           "  -h, --help          show this help\n\n"
           "interactive keys: [j/k] move  [/] search  [enter] zoom  [f] filter  "
           "[F] clear  [s] sort  [i] flame/icicle  [r] reset  [q] quit\n");
}

/* ------------------------------------------------------------------ */
/* Symbol-table navigation                                             */
/* ------------------------------------------------------------------ */

/* Cross-highlight: after the selection moves, the flame highlights the selected
 * symbol's frames, so re-emit it alongside the cheap table refresh. */
static void after_select(struct yperf_app *app)
{
    yperf_ui_refresh_table(app);
    yperf_ui_render_flame(app);
}

static void nav_move(struct yperf_app *app, int delta)
{
    if (!app->profile || app->profile->n_symbols == 0) {
        return;
    }
    int count = (int)app->profile->n_symbols;
    app->selected += delta;
    if (app->selected < 0) {
        app->selected = 0;
    }
    if (app->selected >= count) {
        app->selected = count - 1;
    }
    after_select(app);
}

static void nav_cycle_sort(struct yperf_app *app)
{
    app->sort_mode = (enum yperf_sort_mode)((app->sort_mode + 1) % YPERF_SORT_MODE_COUNT);
    yperf_profile_sort(app->profile, app->sort_mode);
    app->selected = 0;
    after_select(app);
}

static void nav_toggle_icicle(struct yperf_app *app)
{
    app->icicle = !app->icicle;
    yperf_ui_refresh(app); /* re-renders the flame with the new orientation */
}

static void focus_selected(struct yperf_app *app)
{
    if (!app->profile || app->profile->n_symbols == 0 || app->selected < 0 ||
        (size_t)app->selected >= app->profile->n_symbols) {
        return;
    }
    const char *name = app->profile->symbols[app->selected].name;
    error_absorb(yetty_yflame_focus_name(app->flame, name, strlen(name)));
    yperf_ui_render_flame(app);
}

static void reset_view(struct yperf_app *app)
{
    error_absorb(yetty_yflame_reset(app->flame));
    app->search_active = 0;
    app->search_len = 0;
    app->search[0] = '\0';
    after_select(app);
}

/* Rebuild `profile` from the original capture, keeping only stacks that contain
 * `symbol` (NULL restores the full capture, timeline and all). */
static void apply_filter(struct yperf_app *app, const char *symbol)
{
    if (!app->orig_profile) {
        return;
    }
    if (!symbol) {
        if (app->profile != app->orig_profile) {
            yperf_profile_destroy(app->profile);
            app->profile = app->orig_profile;
        }
        app->filter[0] = '\0';
        yperf_profile_sort(app->profile, app->sort_mode);
        app->selected = 0;
        yperf_ui_relayout(app);
        yperf_ui_refresh(app);
        return;
    }

    char *folded = NULL;
    size_t folded_len = 0;
    struct yetty_ycore_void_result filtered = yperf_folded_filter(
        app->orig_profile->folded, app->orig_profile->folded_len, symbol, &folded, &folded_len);
    if (YETTY_IS_ERR(filtered)) {
        yetty_ycore_error_destroy(filtered.error);
        return;
    }
    struct yperf_profile *built = NULL;
    struct yetty_ycore_void_result built_res =
        yperf_profile_from_folded(folded, folded_len, &built);
    free(folded);
    if (YETTY_IS_ERR(built_res)) {
        yetty_ycore_error_destroy(built_res.error);
        return;
    }
    if (built->n_symbols == 0) {
        yperf_profile_destroy(built); /* no match — leave the current view alone */
        return;
    }
    if (app->profile != app->orig_profile) {
        yperf_profile_destroy(app->profile);
    }
    app->profile = built;
    yperf_profile_sort(app->profile, app->sort_mode);
    app->selected = 0;
    snprintf(app->filter, sizeof(app->filter), "%s", symbol);
    yperf_ui_relayout(app);
    yperf_ui_refresh(app);
}

static void filter_selected(struct yperf_app *app)
{
    if (!app->profile || app->profile->n_symbols == 0 || app->selected < 0 ||
        (size_t)app->selected >= app->profile->n_symbols) {
        return;
    }
    apply_filter(app, app->profile->symbols[app->selected].name);
}

/* ------------------------------------------------------------------ */
/* Client-mode harness                                                 */
/* ------------------------------------------------------------------ */

struct yperf_client {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    struct yetty_yface *yface;
    struct yperf_app *app;
};

/* Keystrokes while the search field is open build the query and re-highlight
 * the flame live. Returns 1 (always consumed while searching). */
static int search_key(struct yperf_app *app, uint32_t key)
{
    if (key == 0x0d || key == 0x0a) { /* Enter — keep the highlight, leave search mode */
        app->search_active = 0;
        yperf_ui_refresh_table(app);
        return 1;
    }
    if (key == 0x1b || key == 0x07) { /* Esc / Ctrl-G — cancel and clear the query */
        app->search_active = 0;
        app->search_len = 0;
        app->search[0] = '\0';
        after_select(app);
        return 1;
    }
    if (key == 0x7f || key == 0x08) { /* Backspace */
        if (app->search_len > 0) {
            app->search[--app->search_len] = '\0';
        }
        yperf_ui_render_flame(app);
        yperf_ui_refresh_table(app);
        return 1;
    }
    if (key >= 0x20 && key < 0x7f) { /* printable ASCII */
        if (app->search_len + 1 < sizeof(app->search)) {
            app->search[app->search_len++] = (char)key;
            app->search[app->search_len] = '\0';
        }
        yperf_ui_render_flame(app);
        yperf_ui_refresh_table(app);
        return 1;
    }
    return 1;
}

static int on_key(struct yetty_yclass_object *engine, uint32_t key, int mods, void *user)
{
    (void)engine;
    (void)mods;
    struct yperf_client *client = user;
    struct yperf_app *app = client->app;

    if (app->search_active) {
        return search_key(app, key);
    }

    switch (key) {
    case 'q':
    case 'Q':
    case 0x03: /* Ctrl-C */
    case 0x04: /* Ctrl-D */
        app->running = 0;
        return 1;
    case 'j':
    case 'J':
        nav_move(app, 1);
        return 1;
    case 'k':
    case 'K':
        nav_move(app, -1);
        return 1;
    case 's':
    case 'S':
        nav_cycle_sort(app);
        return 1;
    case 'i':
    case 'I':
        nav_toggle_icicle(app);
        return 1;
    case '/':
        app->search_active = 1;
        app->search_len = 0;
        app->search[0] = '\0';
        yperf_ui_render_flame(app); /* clears any selected-symbol highlight */
        yperf_ui_refresh_table(app);
        return 1;
    case 0x0d: /* Enter — zoom the flame to the selected symbol */
    case 0x0a:
        focus_selected(app);
        return 1;
    case 'f':
        filter_selected(app);
        return 1;
    case 'F':
        apply_filter(app, NULL);
        return 1;
    case 'r':
    case 'R':
    case 0x1b: /* Esc */
        reset_view(app);
        return 1;
    case 'g':
        app->selected = 0;
        after_select(app);
        return 1;
    case 'G':
        if (app->profile && app->profile->n_symbols) {
            app->selected = (int)app->profile->n_symbols - 1;
            after_select(app);
        }
        return 1;
    default:
        return 0;
    }
}

/* Pane input arrives as OSC envelopes on stdin (mouse, resize). Raw bytes are
 * keystrokes handed to the ygui framework's decoder. A ygui pane-app is hosted
 * as a full-pane figure, so the terminal forwards mouse / resize as the
 * figure-tagged variants (coords already local to the figure = pane origin);
 * the pane-wide variants are handled too for completeness. */
static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yperf_client *client = user;
    struct yperf_app *app = client->app;

    if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_MOUSE ||
         osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) &&
        payload_len >= sizeof(struct yetty_client_input_mouse)) {
        const struct yetty_client_input_mouse *mouse =
            (const struct yetty_client_input_mouse *)payload;
        /* Pointer arrives in FRAMEBUFFER px; the widget tree hit-tests in
         * LOGICAL px — same divide the viewport gets below. */
        const float scale = app->content_scale > 0.0f ? app->content_scale : 1.0f;
        yperf_ui_flame_mouse(app, mouse->kind, mouse->x / scale, mouse->y / scale, mouse->button,
                             mouse->pressed, mouse->wheel_dy);
    } else if ((osc_code == YETTY_OSC_SC_CLIENT_INPUT_RESIZE ||
                osc_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) &&
               payload_len >= sizeof(struct yetty_client_input_resize)) {
        const struct yetty_client_input_resize *resize =
            (const struct yetty_client_input_resize *)payload;
        if (resize->content_scale > 0.0f) {
            app->content_scale = resize->content_scale;
        }
        if (resize->width > 0.0f && resize->height > 0.0f) {
            const float scale = app->content_scale > 0.0f ? app->content_scale : 1.0f;
            error_absorb(yetty_ygui_framework_set_viewport(app->engine, resize->width / scale,
                                                           resize->height / scale));
            yperf_ui_relayout(app);
            yperf_ui_refresh(app);
        }
    }
}

static void on_raw(void *user, const char *bytes, size_t n)
{
    struct yperf_client *client = user;
    error_absorb(yetty_ygui_framework_feed_input(client->app->engine, bytes, n));
}

/* Subscribe to pane-wide mouse + keyboard events and enable the terminal's card
 * input reporting — the same handshake tools/yflame uses, plus the keyboard
 * mode (DEC ?1502h). The keyboard subscription matters because click-focusing
 * the pane figure would otherwise make the terminal fan keystrokes out to the
 * (focused) figure as structured envelopes instead of the PTY; with ?1502h the
 * app keeps consuming keys from its own PTY, so keyboard nav survives a click. */
static void subscribe_mouse(int enable)
{
    struct yetty_client_input_sub msg = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = enable ? (YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK | YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE |
                           YETTY_CLIENT_INPUT_SUB_KEY)
                        : 0u,
        ._pad0 = 0,
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emitted = yetty_yface_emit(
        YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0, NULL, 0, &msg, sizeof(msg), &envelope);
    if (YETTY_IS_OK(emitted) && envelope.size > 0) {
        ssize_t written = write(STDOUT_FILENO, envelope.data, envelope.size);
        (void)written;
    } else if (YETTY_IS_ERR(emitted)) {
        yetty_ycore_error_destroy(emitted.error);
    }
    yetty_ycore_buffer_destroy(&envelope);

    static const char input_on[] = "\x1b[?1500h\x1b[?1501h\x1b[?1502h";
    static const char input_off[] = "\x1b[?1500l\x1b[?1501l\x1b[?1502l";
    const char *seq = enable ? input_on : input_off;
    size_t seq_len = enable ? sizeof(input_on) - 1 : sizeof(input_off) - 1;
    ssize_t written = write(STDOUT_FILENO, seq, seq_len);
    (void)written;
}

static void yperf_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct yperf_client *client = handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[4096];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        yetty_yface_feed_bytes(client->yface, buf, (size_t)n);
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        client->app->running = 0;
    }
}

static void yperf_pickup_winsz(struct yperf_client *client)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        /* TIOCGWINSZ pixels are FRAMEBUFFER px (cols x cell, cell stride already
         * carries content_scale) — divide like the RESIZE envelope. */
        const float scale = client->app->content_scale > 0.0f ? client->app->content_scale : 1.0f;
        error_absorb(yetty_ygui_framework_set_viewport(
            client->app->engine, (float)ws.ws_xpixel / scale, (float)ws.ws_ypixel / scale));
        yperf_ui_relayout(client->app);
        yperf_ui_refresh(client->app);
    }
}

static void yperf_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    yperf_pickup_winsz((struct yperf_client *)handle->data);
}

static void yperf_prep_cb(uv_prepare_t *handle)
{
    struct yperf_client *client = handle->data;
    if (yetty_ygui_framework_is_dirty(client->app->engine)) {
        error_absorb(yetty_ygui_framework_emit(client->app->engine));
    }
    if (!client->app->running) {
        uv_stop(handle->loop);
    }
}

static void yperf_close_cb(uv_handle_t *handle)
{
    (void)handle;
}

static int yperf_run_client(struct yperf_app *app)
{
    /* Wipe the pane (and scrollback) so any capture-time perf progress / warning
     * chatter printed before we attach doesn't bleed through the dashboard. */
    {
        static const char clear_screen[] = "\x1b[2J\x1b[3J\x1b[H";
        ssize_t written = write(STDOUT_FILENO, clear_screen, sizeof(clear_screen) - 1);
        (void)written;
    }

    struct termios saved;
    int tty_raw_ok = 0;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw = saved;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            tty_raw_ok = 1;
        }
    }

    struct yperf_client client = {0};
    client.app = app;
    if (uv_loop_init(&client.loop) != 0) {
        fprintf(stderr, "yperf: uv_loop_init failed\n");
        goto cleanup;
    }

    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(framework_res)) {
        fprintf(stderr, "yperf: framework_create failed: %s\n", framework_res.error.msg);
        yetty_ycore_error_destroy(framework_res.error);
        goto cleanup_loop;
    }
    app->engine = framework_res.value;
    yetty_ygui_framework_set_key_cb(app->engine, on_key, &client);

    struct yetty_ycore_void_result attach_res =
        yetty_ygui_framework_attach(app->engine, STDIN_FILENO, STDOUT_FILENO, /*compressed=*/1);
    if (YETTY_IS_ERR(attach_res)) {
        fprintf(stderr, "yperf: framework_attach failed: %s\n", attach_res.error.msg);
        yetty_ycore_error_destroy(attach_res.error);
        goto cleanup_loop;
    }

    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    if (YETTY_IS_ERR(yface_res)) {
        fprintf(stderr, "yperf: yface_create failed: %s\n", yface_res.error.msg);
        yetty_ycore_error_destroy(yface_res.error);
        goto cleanup_loop;
    }
    client.yface = yface_res.value;
    yetty_yface_set_handlers(client.yface, on_osc, on_raw, &client);

    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_OK(root_res)) {
        app->root_widget = root_res.value;
        error_absorb(yetty_ygui_framework_set_root(app->engine, app->root_widget));
        error_absorb(yperf_ui_build(app));
        yperf_ui_refresh(app);
    } else {
        yetty_ycore_error_destroy(root_res.error);
    }

    subscribe_mouse(1);

    if (uv_poll_init(&client.loop, &client.stdin_poll, STDIN_FILENO) == 0) {
        client.stdin_poll.data = &client;
        uv_poll_start(&client.stdin_poll, UV_READABLE, yperf_stdin_cb);
    }
    if (uv_signal_init(&client.loop, &client.sigwinch) == 0) {
        client.sigwinch.data = &client;
        uv_signal_start(&client.sigwinch, yperf_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&client.loop, &client.prep) == 0) {
        client.prep.data = &client;
        uv_prepare_start(&client.prep, yperf_prep_cb);
    }

    yperf_pickup_winsz(&client);

    /* --focus: zoom to the requested symbol once the flame has been parsed. */
    if (app->pending_focus[0]) {
        error_absorb(
            yetty_yflame_focus_name(app->flame, app->pending_focus, strlen(app->pending_focus)));
        yperf_ui_render_flame(app);
        app->pending_focus[0] = '\0';
    }

    uv_run(&client.loop, UV_RUN_DEFAULT);

    subscribe_mouse(0);

    uv_poll_stop(&client.stdin_poll);
    uv_signal_stop(&client.sigwinch);
    uv_prepare_stop(&client.prep);
    uv_close((uv_handle_t *)&client.stdin_poll, yperf_close_cb);
    uv_close((uv_handle_t *)&client.sigwinch, yperf_close_cb);
    uv_close((uv_handle_t *)&client.prep, yperf_close_cb);
    uv_run(&client.loop, UV_RUN_NOWAIT);

    error_absorb(yetty_ygui_framework_clear(app->engine));
    yperf_ui_free(app);
    if (client.yface) {
        yetty_yface_destroy(client.yface);
    }
    error_absorb(yetty_ygui_framework_destroy(app->engine));

cleanup_loop:
    uv_loop_close(&client.loop);
cleanup:
    if (tty_raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scrollback-figure emit (consumption mode 2)                         */
/* ------------------------------------------------------------------ */

static int yperf_emit_figure(struct yperf_app *app, const char *focus_symbol)
{
    struct yetty_ycore_void_result reg_res = yetty_yflame_register();
    if (YETTY_IS_ERR(reg_res)) {
        fprintf(stderr, "yperf: yflame register failed: %s\n", reg_res.error.msg);
        yetty_ycore_error_destroy(reg_res.error);
        return 1;
    }
    struct yetty_yclass_object_ptr_result flame_res = yetty_yflame_flame_create(NULL);
    if (YETTY_IS_ERR(flame_res)) {
        fprintf(stderr, "yperf: yflame create failed: %s\n", flame_res.error.msg);
        yetty_ycore_error_destroy(flame_res.error);
        return 1;
    }
    struct yetty_yclass_object *flame = flame_res.value;
    uint32_t flags = YETTY_YFLAME_FLAG_LABELS | (app->icicle ? YETTY_YFLAME_FLAG_ICICLE : 0u);
    error_absorb(yetty_yflame_configure(flame, 0.0f, 24.0f, 0.0f, flags));
    error_absorb(yetty_yflame_parse(flame, app->profile->folded, app->profile->folded_len));
    if (app->baseline_folded) {
        error_absorb(
            yetty_yflame_set_baseline(flame, app->baseline_folded, app->baseline_folded_len));
    }
    if (focus_symbol) {
        error_absorb(yetty_yflame_focus_name(flame, focus_symbol, strlen(focus_symbol)));
    }

    int rc = 0;
    struct yetty_ydraw_drawable_list_result list_res = yetty_yflame_render(flame);
    if (YETTY_IS_ERR(list_res)) {
        fprintf(stderr, "yperf: flame render failed: %s\n", list_res.error.msg);
        yetty_ycore_error_destroy(list_res.error);
        rc = 1;
    } else {
        struct yetty_ycore_void_result emitted =
            yetty_yflame_emit_osc(list_res.value, STDOUT_FILENO);
        if (YETTY_IS_ERR(emitted)) {
            fprintf(stderr, "yperf: emit failed: %s\n", emitted.error.msg);
            yetty_ycore_error_destroy(emitted.error);
            rc = 1;
        }
        yetty_ydraw_drawable_list_destroy(list_res.value);
    }
    error_absorb(yetty_yflame_destroy(flame));
    return rc;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    const char *perf_data_path = NULL;
    const char *diff_base = NULL, *diff_cur = NULL;
    const char *focus_symbol = NULL;
    int print_mode = 0, perf_script = 0, icicle = 0, emit_mode = 0;
    /* Capture options. */
    char **record_argv = NULL;
    int record_argc = 0;
    const char *pid = NULL;
    int all_system = 0, demo = 0, duration = 10, freq = 997, frame_pointer = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            /* Everything after `--` is the command to profile with perf. */
            record_argv = &argv[i + 1];
            record_argc = argc - (i + 1);
            break;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            yperf_usage();
            return 0;
        } else if (strcmp(arg, "--print") == 0) {
            print_mode = 1;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--emit") == 0) {
            emit_mode = 1;
        } else if (strcmp(arg, "--perf-script") == 0) {
            perf_script = 1;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--icicle") == 0) {
            icicle = 1;
        } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--all") == 0) {
            all_system = 1;
        } else if (strcmp(arg, "--demo") == 0) {
            demo = 1;
        } else if (strcmp(arg, "--fp") == 0) {
            frame_pointer = 1;
        } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--pid") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "yperf: %s requires a pid\n", arg);
                return 1;
            }
            pid = argv[++i];
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--duration") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "yperf: %s requires seconds\n", arg);
                return 1;
            }
            duration = atoi(argv[++i]);
            if (duration <= 0) {
                duration = 10;
            }
        } else if (strcmp(arg, "--freq") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "yperf: --freq requires a frequency in hz\n");
                return 1;
            }
            freq = atoi(argv[++i]);
            if (freq <= 0) {
                freq = 997;
            }
        } else if (strcmp(arg, "--perf") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "yperf: --perf requires a perf.data path\n");
                return 1;
            }
            perf_data_path = argv[++i];
        } else if (strcmp(arg, "--focus") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "yperf: --focus requires a symbol name\n");
                return 1;
            }
            focus_symbol = argv[++i];
        } else if (strcmp(arg, "--diff") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "yperf: --diff requires <base> <cur> folded files\n");
                return 1;
            }
            diff_base = argv[++i];
            diff_cur = argv[++i];
        } else if (arg[0] != '-') {
            path = arg;
        }
    }

    ytrace_init();

    /* A capture (record a command / pid / system) produces a temp perf.data that
     * flows through the same perf-script collapse path as --perf. */
    int record_mode = (record_argc > 0) || pid || all_system;
    char *captured = NULL;
    char source_label[1024] = {0};
    if (record_mode) {
        struct yetty_ycore_void_result cap = perf_record_capture(
            record_argv, record_argc, pid, all_system, duration, freq, frame_pointer, &captured);
        if (YETTY_IS_ERR(cap)) {
            fprintf(stderr, "yperf: %s\n", cap.error.msg);
            yetty_ycore_error_destroy(cap.error);
            return 1;
        }
        perf_data_path = captured;
        if (record_argc > 0) {
            snprintf(source_label, sizeof(source_label), "%s", record_argv[0]);
        } else if (all_system) {
            snprintf(source_label, sizeof(source_label), "system-wide (%ds)", duration);
        } else {
            snprintf(source_label, sizeof(source_label), "pid %s (%ds)", pid, duration);
        }
    }

    /* --diff loads the current capture as the profile and the base capture as
     * the flame baseline; a capture sets perf_data_path to its temp file;
     * otherwise the usual single-capture path runs. */
    const char *load_path = diff_cur ? diff_cur : (perf_data_path ? perf_data_path : path);
    struct yperf_profile *profile = NULL;
    struct yetty_ycore_void_result load_res;
    if (demo) {
        static const char demo_folded[] = "main;parse;lex 60\n"
                                          "main;parse;expand 18\n"
                                          "main;parse;intern 9\n"
                                          "main;typecheck;unify 40\n"
                                          "main;typecheck;solve 25\n"
                                          "main;codegen;lower 55\n"
                                          "main;codegen;regalloc;spill 30\n"
                                          "main;codegen;regalloc;color 22\n"
                                          "main;codegen;emit 14\n"
                                          "main;link;gc 12\n"
                                          "main;link;reloc 8\n"
                                          "main 6\n";
        load_res = yperf_profile_from_folded(demo_folded, sizeof(demo_folded) - 1, &profile);
        snprintf(source_label, sizeof(source_label), "demo");
    } else {
        load_res = load_profile(load_path, perf_data_path != NULL, perf_script, &profile);
    }
    /* The temp perf.data has been consumed by the collapser; drop it now. */
    if (captured) {
        unlink(captured);
        free(captured);
        captured = NULL;
    }
    if (YETTY_IS_ERR(load_res)) {
        fprintf(stderr, "yperf: %s\n", load_res.error.msg);
        yetty_ycore_error_destroy(load_res.error);
        return 1;
    }
    if (profile->n_symbols == 0) {
        fprintf(stderr, "yperf: no samples found in input\n");
        yperf_profile_destroy(profile);
        return 1;
    }
    if (source_label[0] == '\0') {
        snprintf(source_label, sizeof(source_label), "%s", load_path ? load_path : "stdin");
    }

    if (print_mode) {
        yperf_print(profile);
        yperf_profile_destroy(profile);
        return 0;
    }

    struct yperf_app *app = calloc(1, sizeof(*app));
    if (!app) {
        yperf_profile_destroy(profile);
        return 1;
    }
    app->running = 1;
    app->sort_mode = YPERF_SORT_SELF;
    app->icicle = icicle;
    app->profile = profile;
    app->orig_profile = profile;
    snprintf(app->source, sizeof(app->source), "%s", source_label);
    yperf_profile_sort(app->profile, app->sort_mode);

    /* Load the diff baseline text (folded); the flame applies it after parse. */
    if (diff_base) {
        struct yetty_ycore_void_result base_res =
            yperf_read_all(diff_base, &app->baseline_folded, &app->baseline_folded_len);
        if (YETTY_IS_ERR(base_res)) {
            fprintf(stderr, "yperf: reading diff baseline: %s\n", base_res.error.msg);
            yetty_ycore_error_destroy(base_res.error);
            yperf_profile_destroy(app->orig_profile);
            free(app);
            return 1;
        }
        char merged[1152];
        snprintf(merged, sizeof(merged), "%s (diff vs %s)", app->source, diff_base);
        snprintf(app->source, sizeof(app->source), "%s", merged);
    }

    int rc;
    if (emit_mode) {
        rc = yperf_emit_figure(app, focus_symbol);
    } else {
        struct yetty_ycore_void_result reg_res = yetty_yflame_register();
        if (YETTY_IS_ERR(reg_res)) {
            fprintf(stderr, "yperf: yflame register failed: %s\n", reg_res.error.msg);
            yetty_ycore_error_destroy(reg_res.error);
            rc = 1;
        } else {
            struct yetty_yclass_object_ptr_result flame_res = yetty_yflame_flame_create(NULL);
            if (YETTY_IS_ERR(flame_res)) {
                fprintf(stderr, "yperf: yflame create failed: %s\n", flame_res.error.msg);
                yetty_ycore_error_destroy(flame_res.error);
                rc = 1;
            } else {
                app->flame = flame_res.value;
                if (focus_symbol) {
                    snprintf(app->pending_focus, sizeof(app->pending_focus), "%s", focus_symbol);
                }
                rc = yperf_run_client(app);
                error_absorb(yetty_yflame_destroy(app->flame));
            }
        }
    }

    if (app->profile != app->orig_profile) {
        yperf_profile_destroy(app->profile);
    }
    yperf_profile_destroy(app->orig_profile);
    free(app->baseline_folded);
    free(app);
    return rc;
}
