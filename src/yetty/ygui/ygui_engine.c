/*
 * ygui_engine.c - YGui engine implementation with libuv event loop
 */

#include "ygui_internal.h"
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/term.h>
#include <yetty/ytrace/ytrace.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <io.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#endif

/* Debug logging - set YGUI_C_LOG env var to enable */
static FILE *_ygui_log_file = NULL;
static int _ygui_log_checked = 0;

static void _ygui_log_init(void)
{
    if (_ygui_log_checked) {
        return;
    }
    _ygui_log_checked = 1;
    const char *log_path = getenv("YGUI_C_LOG");
    if (log_path) {
        _ygui_log_file = fopen(log_path, "w");
        if (_ygui_log_file) {
            setvbuf(_ygui_log_file, NULL, _IONBF, 0);
            fprintf(_ygui_log_file, "[YGUI-C] Logging initialized\n");
        }
    }
}

#define YGUI_LOG(...)                                                                              \
    do {                                                                                           \
        _ygui_log_init();                                                                          \
        if (_ygui_log_file) {                                                                      \
            fprintf(_ygui_log_file, "[YGUI-C] " __VA_ARGS__);                                      \
            fprintf(_ygui_log_file, "\n");                                                         \
        }                                                                                          \
    } while (0)

/* Calculate grid bucket size based on canvas dimensions.
 * Aims for ~16 buckets on the larger dimension, minimum 32.0f */
static float calc_grid_bucket_size(float width, float height)
{
    float larger = (width > height) ? width : height;
    float bucket = larger / 16.0f;
    return (bucket < 32.0f) ? 32.0f : bucket;
}

/* Forward declarations */
static void handle_resize(struct yetty_ygui_engine *engine);

/*=============================================================================
 * Terminal State (Unix only - Windows uses ConPTY via yetty)
 *===========================================================================*/

static int ygui_initialized = 0;
volatile int yetty_ygui_internal_resize_pending = 0;
struct yetty_ygui_engine *yetty_ygui_internal_active_engine = NULL; /* For resize handler */

#ifndef _WIN32
static struct termios ygui_orig_termios;
static int ygui_raw_mode = 0;

static void ygui_restore_terminal(void)
{
    if (ygui_raw_mode) {
        /* Leave alternate screen — host terminal restores its prior content. */
        const char leave_alt[] = "\033[?1049l";
        ssize_t _w = write(STDOUT_FILENO, leave_alt, sizeof(leave_alt) - 1);
        (void)_w;
        tcsetattr(STDIN_FILENO, TCSANOW, &ygui_orig_termios);
        ygui_raw_mode = 0;
    }
}

static void ygui_signal_handler(int sig)
{
    ygui_restore_terminal();
    /* Re-raise signal with default handler */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void ygui_sigwinch_handler(int sig)
{
    (void)sig;
    yetty_ygui_internal_resize_pending = 1;
}
#endif /* !_WIN32 */

int yetty_ygui_init(void)
{
    if (ygui_initialized) {
        return 0;
    }

#ifndef _WIN32
    /* Save original terminal settings */
    if (tcgetattr(STDIN_FILENO, &ygui_orig_termios) < 0) {
        yetty_ygui_set_error("Failed to get terminal attributes");
        return -1;
    }

    /* Set up raw mode */
    struct termios raw = ygui_orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag |= OPOST; /* Keep output processing */
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        yetty_ygui_set_error("Failed to set raw terminal mode");
        return -1;
    }
    ygui_raw_mode = 1;

    /* Enter alternate screen — host terminal saves its content; ygui's UI
     * draws on a fresh buffer; on shutdown the original screen is restored. */
    const char enter_alt[] = "\033[?1049h";
    ssize_t _w = write(STDOUT_FILENO, enter_alt, sizeof(enter_alt) - 1);
    (void)_w;

    /* Set up signal handlers for clean exit */
    signal(SIGINT, ygui_signal_handler);
    signal(SIGTERM, ygui_signal_handler);
    signal(SIGQUIT, ygui_signal_handler);

    /* Set up SIGWINCH handler for terminal resize */
    signal(SIGWINCH, ygui_sigwinch_handler);

    /* Register atexit handler */
    atexit(ygui_restore_terminal);
#endif /* !_WIN32 */

    ygui_initialized = 1;
    return 0;
}

void yetty_ygui_shutdown(void)
{
#ifndef _WIN32
    ygui_restore_terminal();
#endif
    ygui_initialized = 0;
}

/*=============================================================================
 * Thread-local error message
 *===========================================================================*/

static _Thread_local char ygui_error_msg[256] = {0};

void yetty_ygui_set_error(const char *msg)
{
    if (msg) {
        snprintf(ygui_error_msg, sizeof(ygui_error_msg), "%s", msg);
    } else {
        ygui_error_msg[0] = '\0';
    }
}

const char *yetty_ygui_get_error(void)
{
    return ygui_error_msg;
}

/*=============================================================================
 * Version
 *===========================================================================*/

const char *yetty_ygui_version(void)
{
    /* Pinned to the enum in include/yetty/ygui/ygui.h — if someone bumps
     * one without the other, this fails at compile time. Stringification
     * (#) is preprocessor-only, so it can't see enum constants — hence the
     * literal here plus the assert. */
    _Static_assert(YETTY_YGUI_VERSION_MAJOR == 0 && YETTY_YGUI_VERSION_MINOR == 2 &&
                       YETTY_YGUI_VERSION_PATCH == 0,
                   "ygui version string must match enum constants");
    return "0.2.0";
}

/*=============================================================================
 * Engine Lifecycle
 *===========================================================================*/

/* Allocate the engine struct and set every non-libuv, non-pty field to
 * its initial state. No I/O happens here — the libuv runtime + pty are
 * wired by engine_internal_bootstrap_runtime which runs immediately
 * after, inside engine_internal_create. */
static struct ygui_engine_ptr_result engine_alloc_init(const char *name,
                                                       struct yetty_ygui_theme *theme)
{
    struct yetty_ygui_engine *engine =
        (struct yetty_ygui_engine *)calloc(1, sizeof(struct yetty_ygui_engine));
    if (!engine) {
        yetty_ygui_set_error("Failed to allocate engine");
        return YETTY_ERR(ygui_engine_ptr, "engine_alloc_init: alloc failed");
    }

    /* Create ydraw-core buffer — widgets accumulate SDF primitives + text
     * spans into it; the engine base64-encodes the serialization and ships
     * it via OSC SCENE_BIN every render. */
    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (!YETTY_IS_OK(br)) {
        yetty_ygui_set_error("Failed to create ydraw buffer");
        free(engine);
        return YETTY_ERR(ygui_engine_ptr, "engine_alloc_init: ydraw buffer create failed", br);
    }
    engine->buffer = br.value;

    /* Optional: open a raster font in metrics-only mode for text width
     * measurement. The env var lets users point at any TTF; absence just
     * leaves measure_font NULL and ygui falls back to its heuristic. Same
     * pattern as ypdf (pdf-renderer.c line ~359). */
    const char *ttf = getenv("YGUI_MEASURE_FONT");
    if (ttf && ttf[0]) {
        engine->measure_base_size = 32.0f;
        struct yetty_font_font_result fr = yetty_yfont_raster_font_create_from_file(
            ttf, /*shader*/ NULL, engine->measure_base_size);
        if (YETTY_IS_OK(fr)) {
            engine->measure_font = fr.value;
        }
    }

    /* Identifier + initial card geometry. Cell counts come from
     * TIOCGWINSZ; the host's SC_RESIZE response replaces width/height
     * with actual pixel dims before any real render happens. */
    engine->card_name = ygui_strdup(name && name[0] ? name : "ygui");
    engine->card_x = 0;
    engine->card_y = 0;
    int cols = 80;
    int rows = 24;
    (void)yetty_yplatform_term_get_size(&cols, &rows);
    engine->card_w = cols;
    engine->card_h = rows;

    /* Theme: caller-supplied (borrowed) or built-in default (owned). */
    if (theme) {
        engine->theme = theme;
        engine->owns_theme = 0;
    } else {
        engine->theme = yetty_ygui_theme_create_default();
        engine->owns_theme = 1;
    }

    /* Initial state — actual pixel size is set by SC_RESIZE; this 1×1
     * placeholder is what the first (placeholder) frame ships. */
    engine->dirty = 1;
    engine->width = 1.0f;
    engine->height = 1.0f;
    engine->cell_width = 0.0f;
    engine->cell_height = 0.0f;

    /* Wire state — sequential u32 ids start at 1 (0 = receiver root).
     * First render is a full redraw (CMD_ZERO + entire tree); after that
     * the producer only ships DELETE+GROUP for dirty subtrees. */
    engine->next_group_id = 1;
    engine->needs_full_redraw = 1;
    engine->pending_deletes = NULL;
    engine->pending_delete_count = 0;
    engine->pending_delete_cap = 0;

    /* View state defaults */
    engine->view_zoom = 1.0f;
    engine->view_scroll_x = 0.0f;
    engine->view_scroll_y = 0.0f;

    /* Canvas always tracks the host's reported pixel size — no other
     * mode is supported. SCALE_OFF: widget pixels are display pixels. */
    engine->canvas_mode = YETTY_YGUI_CANVAS_FIT;
    engine->scale_mode = YETTY_YGUI_SCALE_OFF;
    engine->reference_w = 0.0f;
    engine->reference_h = 0.0f;
    engine->display_pixel_w = 0.0f;
    engine->display_pixel_h = 0.0f;
    engine->have_pixel_size = 0;

    /* I/O endpoints. STDIN for inbound OSCs, STDOUT for outbound. The
     * libuv pipe wrapping STDOUT becomes output_pty inside
     * engine_internal_bootstrap_runtime. */
    engine->input_fd = STDIN_FILENO;
    engine->output_fd = STDOUT_FILENO;

    /* Allocate a non-zero card_id (0 = "no card" sentinel on the wire).
     * One process can hold several engines so we hand out monotonically
     * increasing ids. */
    static uint32_t s_next_card_id = 1;
    engine->card_id = s_next_card_id++;

    /* Long-lived yface used to parse inbound binary OSC envelopes
     * (mouse/resize/focus/key from the ymgui-layer hit router). */
    struct yetty_yface_ptr_result fr = yetty_yface_create();
    if (YETTY_IS_OK(fr)) {
        engine->yface_in = fr.value;
    }

    /* Grid initialized after pixel size known */
    yetty_ygui_grid_init(&engine->grid, 1.0f, 1.0f, 1.0f);

    return YETTY_OK(ygui_engine_ptr, engine);
}

struct ygui_engine_ptr_result yetty_ygui_engine_internal_alloc_for_yui(
    const char *name, struct yetty_ygui_theme *theme)
{
    /* yui doesn't talk to a parent yetty over a real pty, doesn't poll
     * stdin, and is driven by yetty's render loop directly. So we just
     * allocate + initialise the engine struct here; yui then plugs in
     * its memory-pty via engine_set_output_pty and pushes the display
     * pixel size in directly. No bootstrap_runtime, no handshake. */
    return engine_alloc_init(name, theme);
}

/* Expose engine_alloc_init to engine_uv.c (which lives in the ygui
 * library and implements the public engine_create on top of this +
 * bootstrap_runtime). ygui_core stays libuv-free; the public entry
 * lives in the libuv-coupled layer. */
struct ygui_engine_ptr_result yetty_ygui_engine_internal_alloc(
    const char *name, struct yetty_ygui_theme *theme)
{
    return engine_alloc_init(name, theme);
}

struct yetty_ycore_void_result yetty_ygui_engine_destroy(struct yetty_ygui_engine *engine)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "ygui_engine_destroy: NULL engine");
    }

    /* Stop running if needed */
    engine->running = 0;

    /* Unsubscribe from events. Errors here are best-effort during
     * teardown — log and continue so the rest of cleanup still runs. */
    if (engine->clicks_subscribed) {
        struct yetty_ycore_void_result r = yetty_ygui_osc_subscribe_clicks(engine->output_pty, 0);
        if (YETTY_IS_ERR(r)) {
            yerror("ygui_engine_destroy: unsubscribe_clicks: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }
    if (engine->moves_subscribed) {
        struct yetty_ycore_void_result r = yetty_ygui_osc_subscribe_moves(engine->output_pty, 0);
        if (YETTY_IS_ERR(r)) {
            yerror("ygui_engine_destroy: unsubscribe_moves: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }

    /* Kill card (= clear the ydraw canvas) if shown — UNLESS the
     * preserve flag is set. The window widget's close button sets it
     * so the user's final view stays on screen after the app exits. */
    if (engine->card_shown && engine->card_name && !engine->preserve_canvas_on_destroy) {
        struct yetty_ycore_void_result r =
            yetty_ygui_osc_kill_card(engine->output_pty, engine->card_name);
        if (YETTY_IS_ERR(r)) {
            yerror("ygui_engine_destroy: kill_card: %s", r.error.msg);
            yetty_ycore_error_destroy(r.error);
        }
    }

    /* Drop the ymgui-layer card so the server stops routing mouse to us. */
    if (engine->card_id != 0) {
        struct yetty_ycore_void_result r =
            yetty_ygui_osc_card_remove(engine->output_pty, engine->card_id);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
    }

    /* Destroy yface */
    if (engine->yface_in) {
        yetty_yface_destroy(engine->yface_in);
        engine->yface_in = NULL;
    }

    /* Tear down libuv state if the full `ygui` library populated it
     * via yetty_ygui_engine_attach. ygui-core-only consumers have NULL
     * here and the call is skipped — no libuv dependency to satisfy. */
    if (engine->uv_state_destroy_cb) {
        engine->uv_state_destroy_cb(engine);
    }

    /* Free notification message strings. The card widgets themselves are
     * top-level engine widgets and go down with the loop below. */
    yetty_ygui_engine_notify_shutdown(engine);

    /* Destroy all widgets */
    struct yetty_ygui_widget *w = engine->first_widget;
    while (w) {
        struct yetty_ygui_widget *next = w->next_sibling;
        yetty_ygui_widget_free(w);
        w = next;
    }

    /* Destroy grid */
    yetty_ygui_grid_destroy(&engine->grid);

    /* Destroy theme if owned */
    if (engine->owns_theme && engine->theme) {
        yetty_ygui_theme_destroy(engine->theme);
    }

    /* Destroy buffer + measurement font */
    if (engine->buffer) {
        yetty_ydraw_draw_list_destroy(engine->buffer);
    }
    if (engine->measure_font && engine->measure_font->ops && engine->measure_font->ops->destroy) {
        engine->measure_font->ops->destroy(engine->measure_font);
    }

    /* Free card name */
    free(engine->pending_deletes);
    free(engine->card_name);

    /* Free dedup cache */
    free(engine->prev_emit_data);

    free(engine);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "ygui_engine_destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Engine Configuration
 *===========================================================================*/

void yetty_ygui_engine_set_size(struct yetty_ygui_engine *engine, float width, float height)
{
    if (!engine) {
        return;
    }
    engine->width = width;
    engine->height = height;
    yetty_ygui_grid_destroy(&engine->grid);
    yetty_ygui_grid_init(&engine->grid, width, height, calc_grid_bucket_size(width, height));
    engine->dirty = 1;
    /* Resize: every widget's absolute coords may shift after the next
     * layout pass — force a full redraw rather than guessing which
     * widgets moved. CMD_ZERO + full re-emit. */
    engine->needs_full_redraw = 1;
}

struct pixel_size_result yetty_ygui_engine_get_size(
    const struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return YETTY_ERR(pixel_size, "engine_get_size: NULL engine");
    }
    struct yetty_ycore_pixel_size size = {engine->width, engine->height};
    return YETTY_OK(pixel_size, size);
}

void yetty_ygui_engine_set_theme(struct yetty_ygui_engine *engine, struct yetty_ygui_theme *theme)
{
    if (!engine) {
        return;
    }
    if (engine->owns_theme && engine->theme) {
        yetty_ygui_theme_destroy(engine->theme);
    }
    engine->theme = theme;
    engine->owns_theme = 0;
    engine->dirty = 1;
    /* Theme swap touches colors / radii of every widget — full redraw. */
    engine->needs_full_redraw = 1;
}

void yetty_ygui_engine_set_event_callback(struct yetty_ygui_engine *engine,
                                          ygui_event_callback_t callback, void *userdata)
{
    if (!engine) {
        return;
    }
    engine->event_callback = callback;
    engine->event_userdata = userdata;
}

void yetty_ygui_engine_on_key(struct yetty_ygui_engine *engine, ygui_key_callback_t callback,
                              void *userdata)
{
    if (!engine) {
        return;
    }
    engine->key_callback = callback;
    engine->key_userdata = userdata;
}

/*=============================================================================
 * Engine State
 *===========================================================================*/

int yetty_ygui_engine_has_pressed_widget(const struct yetty_ygui_engine *engine)
{
    return engine && engine->pressed ? 1 : 0;
}

struct yetty_ygui_widget *yetty_ygui_engine_hovered_widget(
    const struct yetty_ygui_engine *engine)
{
    return engine ? engine->hovered : NULL;
}

struct yetty_ygui_widget *yetty_ygui_engine_pressed_widget(
    const struct yetty_ygui_engine *engine)
{
    return engine ? engine->pressed : NULL;
}

int yetty_ygui_engine_is_dirty(const struct yetty_ygui_engine *engine)
{
    return engine ? engine->dirty : 0;
}

void yetty_ygui_engine_mark_dirty(struct yetty_ygui_engine *engine)
{
    if (engine) {
        engine->dirty = 1;
        /* External callers using this entry point don't know which
         * widget changed — be safe and force a full redraw. Code that
         * does know which widget changed should call the widget's
         * setter (which dirties just that widget) instead. */
        engine->needs_full_redraw = 1;
    }
}

/*=============================================================================
 * Rendering
 *===========================================================================*/

static void reset_was_rendered_recursive(struct yetty_ygui_widget *w)
{
    if (!w) {
        return;
    }
    w->was_rendered = 0;
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        reset_was_rendered_recursive(c);
    }
}

static struct yetty_ycore_void_result engine_rebuild_with_mode(struct yetty_ygui_engine *engine,
                                                                int full_redraw)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (!engine || !engine->buffer) {
        return YETTY_ERR(yetty_ycore_void, "engine_rebuild: NULL engine or buffer");
    }

    /* Clear the grid */
    yetty_ygui_grid_clear(&engine->grid);

    /* Compute layout (resolves authored geometry into live + absolute boxes)
     * before any rendering. Render and grid both consume the resolved values. */
    {
        struct yetty_ycore_void_result lr = yetty_ygui_layout_compute_engine(engine);
        if (YETTY_IS_ERR(lr)) {
            return YETTY_ERR(yetty_ycore_void, "engine_rebuild: layout failed", lr);
        }
    }

    /* Reset was_rendered on EVERY widget (not just top-level) before
     * the render pass. */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        reset_was_rendered_recursive(w);
    }

    /* Create render context. force_full_redraw=1 makes every visible
     * widget emit its GROUP regardless of its `dirty` bit; the CMD_ZERO
     * at the envelope head means we don't need to prefix each emit with
     * a DELETE. Incremental mode (full_redraw=0) emits only widgets
     * whose `dirty` is set, each prefixed with DELETE(group_id) so the
     * receiver's existing entity is wiped before the new one lands. */
    struct yetty_ygui_render_ctx ctx;
    yetty_ygui_render_ctx_init(&ctx, engine->buffer, engine->theme);
    ctx.force_full_redraw = full_redraw;

    /* Render all top-level widgets */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        struct yetty_ycore_void_result r;
        if (w->vtable && w->vtable->render_all) {
            r = w->vtable->render_all(w, &ctx);
        } else {
            r = yetty_ygui_widget_render_all_default(w, &ctx);
        }
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    /* Rebuild spatial grid with rendered widgets */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        if (w->was_rendered) {
            yetty_ygui_grid_insert(&engine->grid, w);
        }
    }

    engine->dirty = 0;

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "engine_rebuild: widget render failed", first_err);
    }
    return YETTY_OK_VOID();
}

/* Pure layout pass. Resolves authored geometry into live + absolute boxes
 * for every visible widget; does not touch resize state.
 *
 * Earlier this function also drained a pending resize via handle_resize,
 * but that put a user-visible side effect — firing the engine's
 * resize_callback — behind a name that promises only layout. Because the
 * callback is allowed to call back into engine_layout (and ygreeter does,
 * via ygreeter_tab_viewport), and because `needs_resize` was only
 * cleared after handle_resize returned, the second call would see the
 * still-set flag and run handle_resize again, recursing through
 * on_resize → tab_viewport → engine_layout → handle_resize until the
 * stack overflowed. The dispatch belongs in engine_render (and the
 * input-event helpers that want fresh hit-test geometry); engine_layout
 * is now strictly the layout phase. */
struct yetty_ycore_void_result yetty_ygui_engine_layout(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_engine_layout: NULL engine");
    }
    return yetty_ygui_layout_compute_engine(engine);
}

struct yetty_ycore_void_result yetty_ygui_engine_render(struct yetty_ygui_engine *engine)
{
    if (!engine || !engine->buffer) {
        return YETTY_ERR(yetty_ycore_void, "ygui_engine_render: NULL engine or buffer");
    }

    /* 0. Handle pending resize BEFORE rendering - keeps visual and hit-test in sync */
    if (engine->needs_resize) {
        handle_resize(engine);
        engine->needs_resize = 0;
    }

    /* 0a. Drop expired toast notifications so the about-to-be-built
     * frame already reflects the compacted stack. */
    yetty_ygui_engine_notify_tick(engine);

    /* 1. Clear buffer (the producer-side staging only — has no
     * effect on the receiver until we ship the envelope). */
    yetty_ydraw_draw_list_clear(engine->buffer);

    /* 1a. Full-redraw frames open with CMD_ZERO so the receiver wipes
     * any prior entity state before we re-emit the whole tree. After
     * the first frame we run in incremental mode: no CMD_ZERO, just a
     * batch of `DELETE(group_id)` records for widgets that died since
     * last render, followed by `DELETE+GROUP` pairs for every dirty
     * widget (emitted inside the tree walk below). */
    int full_redraw = engine->needs_full_redraw || !engine->card_shown;
    if (full_redraw) {
        struct yetty_ycore_void_result zr = yetty_ydraw_draw_list_add_cmd_zero(engine->buffer);
        if (YETTY_IS_ERR(zr)) {
            yetty_ycore_error_destroy(zr.error);
        }
        /* CMD_ZERO supersedes any queued deletes from the prior frame. */
        engine->pending_delete_count = 0;
    } else {
        /* Flush all queued DELETEs for destroyed / unparented widgets. */
        for (uint32_t i = 0; i < engine->pending_delete_count; i++) {
            struct yetty_ycore_void_result dr =
                yetty_ydraw_draw_list_add_cmd_delete(engine->buffer, engine->pending_deletes[i]);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
        }
        engine->pending_delete_count = 0;
    }

    /* 2. Set explicit scene bounds to match full canvas */
    yetty_ydraw_draw_list_set_scene_bounds(engine->buffer, 0, 0, engine->width, engine->height);

    /* 3. Rebuild UI — walks the tree; emits GROUP for dirty widgets
     * (or for every visible widget when `full_redraw` is set). */
    struct yetty_ycore_void_result rb = engine_rebuild_with_mode(engine, full_redraw);
    if (YETTY_IS_ERR(rb)) {
        yetty_ycore_error_destroy(rb.error);
    }
    engine->needs_full_redraw = 0;

    /* 4. Serialize (framed: prims + text_spans + scene_bounds) */
    const uint8_t *data = NULL;
    uint32_t size = (uint32_t)yetty_ydraw_draw_list_serialize(engine->buffer, &data);
    if (size == 0 || !data) {
        return YETTY_OK_VOID();
    }

    /* 4b. Dedup. The dirty flag fires for hover changes, mouse moves
     * (when subscribed), view-zoom ticks, etc., but many of those leave
     * the rendered bytes unchanged. Re-emitting the same envelope makes
     * the receiver tear down and re-create every complex-prim instance
     * (yimage, yplot) which is what produces the visible blink on the
     * Images tab. If the just-serialized bytes match the previously
     * sent ones, skip the OSC write. Card-creation always sends. */
    if (engine->card_shown && size == engine->prev_emit_size && engine->prev_emit_data &&
        memcmp(data, engine->prev_emit_data, size) == 0) {
        ydebug("ygui_engine_render: frame identical to previous (%u B), skipping emit", size);
        return YETTY_OK_VOID();
    }

    if (size > engine->prev_emit_cap) {
        uint8_t *grown = realloc(engine->prev_emit_data, size);
        if (grown) {
            engine->prev_emit_data = grown;
            engine->prev_emit_cap = size;
        }
    }
    if (engine->prev_emit_cap >= size && engine->prev_emit_data) {
        memcpy(engine->prev_emit_data, data, size);
        engine->prev_emit_size = size;
    } else {
        /* realloc failed — disable dedup for this frame, keep working. */
        engine->prev_emit_size = 0;
    }

    /* 5. Send OSC */
    if (!engine->card_shown) {
        struct yetty_ycore_void_result r = yetty_ygui_osc_create_card(
            engine->output_pty, engine->card_name, engine->card_x, engine->card_y, engine->card_w,
            engine->card_h, data, size);
        engine->card_shown = 1;
        return r;
    }
    return yetty_ygui_osc_update_card(engine->output_pty, engine->card_name, data, size);
}

/* Send the init OSC handshake: cell-size query, mouse subscriptions,
 * CARD_PLACE, and the CANVAS_FIT placeholder. Called from
 * engine_internal_bootstrap_runtime after the output pty has been
 * wired — the pty is guaranteed live here. There is no public
 * engine_show; the handshake is part of construction. */
struct yetty_ycore_void_result yetty_ygui_engine_internal_emit_handshake(
    struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "engine_emit_handshake: NULL engine");
    }
    if (!engine->output_pty) {
        return YETTY_ERR(yetty_ycore_void,
                         "engine_emit_handshake: output_pty not installed — "
                         "bootstrap must run before any OSC emission");
    }

    /* Cell size query — host replies via OSC and runtime stores it. */
    struct yetty_ycore_void_result qr = yetty_ygui_osc_query_cell_size(engine->output_pty);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, qr, "engine_emit_handshake: query_cell_size failed");

    /* Subscribe to click AND move events (move needed for slider drag) */
    struct yetty_ycore_void_result sc_r = yetty_ygui_osc_subscribe_clicks(engine->output_pty, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sc_r, "engine_emit_handshake: subscribe_clicks failed");
    engine->clicks_subscribed = 1;
    struct yetty_ycore_void_result sm_r = yetty_ygui_osc_subscribe_moves(engine->output_pty, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sm_r, "engine_emit_handshake: subscribe_moves failed");
    engine->moves_subscribed = 1;

    /* Register the card with the ymgui-layer so the server hit-tests
     * the cursor against our rect and emits YMGUI_OSC_SC_MOUSE with
     * card-local coordinates. Triggers the host's YMGUI_OSC_SC_RESIZE
     * reply carrying the actual pixel size. */
    struct yetty_ycore_void_result place_r =
        yetty_ygui_osc_card_place(engine->output_pty, engine->card_id, engine->card_x,
                                  engine->card_y, (uint32_t)engine->card_w,
                                  (uint32_t)engine->card_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, place_r, "engine_emit_handshake: card_place failed");

    /* Send a minimal placeholder envelope so the host has a card to
     * size against. The real render fires after SC_RESIZE updates
     * width/height — that prevents the visual "zoom jump" of doing the
     * first render at the initial 1×1 dims. */
    yetty_ydraw_draw_list_clear(engine->buffer);
    yetty_ydraw_draw_list_set_scene_bounds(engine->buffer, 0, 0, 1, 1);
    const uint8_t *data = NULL;
    uint32_t size = (uint32_t)yetty_ydraw_draw_list_serialize(engine->buffer, &data);
    if (size > 0 && data) {
        struct yetty_ycore_void_result cr = yetty_ygui_osc_create_card(
            engine->output_pty, engine->card_name, engine->card_x, engine->card_y,
            engine->card_w, engine->card_h, data, size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr,
                            "engine_emit_handshake: create_card placeholder failed");
        engine->card_shown = 1;
    }
    engine->dirty = 1;
    YGUI_LOG("ygui: handshake sent, waiting for SC_RESIZE");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Input Handling
 *===========================================================================*/

static void emit_event(struct yetty_ygui_engine *engine, const ygui_event_t *event)
{
    if (engine->event_callback && event->type != YETTY_YGUI_EVENT_NONE) {
        engine->event_callback(event, engine->event_userdata);
    }
}

void yetty_ygui_engine_mouse_move(struct yetty_ygui_engine *engine, float x, float y)
{
    if (!engine) {
        return;
    }

    struct yetty_ygui_widget *hit = yetty_ygui_grid_query(&engine->grid, x, y);

    /* Handle hover changes — only the two affected widgets need re-emitting. */
    if (hit != engine->hovered) {
        if (engine->hovered) {
            engine->hovered->flags &= ~YETTY_YGUI_FLAG_HOVER;
            engine->hovered->dirty = 1;
            engine->dirty = 1;
        }
        if (hit) {
            hit->flags |= YETTY_YGUI_FLAG_HOVER;
            hit->dirty = 1;
            engine->dirty = 1;
        }
        engine->hovered = hit;
    }

    /* Handle drag — the pressed widget may visibly change. */
    if (engine->pressed && engine->pressed->vtable && engine->pressed->vtable->on_drag) {
        float lx = x - engine->pressed->effective_x;
        float ly = y - engine->pressed->effective_y;
        ygui_event_t event = {0};
        if (engine->pressed->vtable->on_drag(engine->pressed, lx, ly, &event)) {
            emit_event(engine, &event);
            engine->pressed->dirty = 1;
            engine->dirty = 1;
        }
    }
}

/* Click-outside-to-close for open popups and popup-menus.
 *
 * The spatial grid routes the click to a single widget — typically
 * whatever is under the cursor. If that widget is not the open popup
 * / popup_menu (or one of its descendants), the user intent is
 * usually "dismiss the popup". This pass walks the engine's top-level
 * widget list, finds any non-modal popup / popup_menu whose OPEN flag
 * is set, and closes it when the click lands OUTSIDE its layout box
 * and outside its descendants.
 *
 * Modal popups (set_modal=1) are skipped — their whole point is to
 * force the user to use the in-popup buttons.
 *
 * The click is NOT consumed here: closing the popup is silent, and
 * the click proceeds to whatever was under it (matches GTK / macOS /
 * web menu UX where clicking outside closes the menu AND triggers
 * the underlying widget). */
static int point_in_subtree(struct yetty_ygui_widget *w, float x, float y)
{
    if (!w || !(w->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return 0;
    }
    if (x >= w->layout_x && x < w->layout_x + w->layout_w &&
        y >= w->layout_y && y < w->layout_y + w->layout_h) {
        return 1;
    }
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        if (point_in_subtree(c, x, y)) return 1;
    }
    return 0;
}

static void close_open_overlays_outside(struct yetty_ygui_engine *engine, float x, float y)
{
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        if (!(w->flags & YETTY_YGUI_FLAG_OPEN)) {
            continue;
        }
        if (w->type == YETTY_YGUI_WIDGET_POPUP) {
            if (w->data.popup.modal) continue;
            if (!point_in_subtree(w, x, y)) {
                yetty_ygui_widget_popup_set_open(w, 0);
            }
        } else if (w->type == YETTY_YGUI_WIDGET_POPUP_MENU) {
            if (w->data.popup_menu.modal) continue;
            if (!point_in_subtree(w, x, y)) {
                yetty_ygui_widget_popup_menu_close(w);
            }
        }
    }
}

void yetty_ygui_engine_mouse_down(struct yetty_ygui_engine *engine, float x, float y, int button)
{
    ydebug("mouse_down at (%.1f, %.1f) btn=%d", x, y, button);
    if (!engine) {
        return;
    }

    /* Dismiss any open non-modal overlays before normal hit-test
     * dispatch. This must happen BEFORE bringing-to-front / spatial
     * grid lookup so we don't accidentally treat the click as
     * targeting the overlay we're about to close. */
    close_open_overlays_outside(engine, x, y);

    YGUI_LOG("mouse_down at (%.1f, %.1f)", x, y);
    struct yetty_ygui_widget *hit = yetty_ygui_grid_query(&engine->grid, x, y);

    /* Right-click context menu — walk up the widget chain looking for
     * an attached menu. Two button conventions in use across the
     * codebase: bit-flag (0x4 = right) and ordinal (2 = right).
     * Accept either. */
    int is_right = (button == 2) || (button & 0x4);
    if (is_right && hit) {
        for (struct yetty_ygui_widget *w = hit; w; w = w->parent) {
            if (w->context_menu) {
                yetty_ygui_widget_popup_menu_open_at(w->context_menu, x, y);
                engine->dirty = 1;
                return;
            }
        }
    }
    YGUI_LOG("  grid_query returned: %s (ptr=%p)", hit ? hit->id : "NULL", (void *)hit);
    ydebug("mouse_down: hit=%s ptr=%p has_on_press=%d",
           hit ? (hit->id ? hit->id : "?") : "NULL", (void *)hit,
           (hit && hit->vtable && hit->vtable->on_press) ? 1 : 0);

    if (hit) {
        hit->flags |= YETTY_YGUI_FLAG_PRESSED;
        engine->pressed = hit;
        hit->dirty = 1;
        engine->dirty = 1;

        /* Focus change — old focused widget must be marked dirty so
         * its focus ring goes away on the receiver. Without this,
         * clearing FOCUSED is invisible to incremental rendering and
         * the focus ring sticks around on the previously-focused
         * widget. */
        if (engine->focused != hit) {
            if (engine->focused) {
                engine->focused->flags &= ~YETTY_YGUI_FLAG_FOCUSED;
                engine->focused->dirty = 1;
            }
            hit->flags |= YETTY_YGUI_FLAG_FOCUSED;
            engine->focused = hit;
        }

        if (hit->vtable && hit->vtable->on_press) {
            float lx = x - hit->effective_x;
            float ly = y - hit->effective_y;
            ygui_event_t event = {0};
            int handled = hit->vtable->on_press(hit, lx, ly, &event);
            ydebug("mouse_down: on_press(%s, lx=%.1f, ly=%.1f) -> %d",
                   hit->id ? hit->id : "?", lx, ly, handled);
            if (handled) {
                emit_event(engine, &event);
            } else if (!hit->vtable->on_drag) {
                /* on_press declined AND the widget has no on_drag — it's
                 * not interested in this gesture at all. Release the
                 * implicit press grab so MOVE/UP fall through to the
                 * host (e.g. the tabbar widget's empty area surrenders
                 * clicks to yui's titlebar window-drag handler).
                 *
                 * Widgets that return 0 from on_press but DO define
                 * on_drag (notably the splitter — "Press alone doesn't
                 * move; drag will pick up from here") still need the
                 * pressed grab so subsequent MOUSE_MOVE events fire
                 * their on_drag — those keep the grab. */
                hit->flags &= ~YETTY_YGUI_FLAG_PRESSED;
                engine->pressed = NULL;
            }
        }
    }
}

void yetty_ygui_engine_mouse_up(struct yetty_ygui_engine *engine, float x, float y, int button)
{
    if (!engine) {
        return;
    }
    (void)button;

    YGUI_LOG("mouse_up at (%.1f, %.1f) pressed=%s", x, y,
             engine->pressed ? engine->pressed->id : "NULL");

    if (engine->pressed) {
        struct yetty_ygui_widget *widget = engine->pressed;
        widget->flags &= ~YETTY_YGUI_FLAG_PRESSED;
        widget->dirty = 1;
        engine->dirty = 1;

        /* Check if release is on same widget (click) */
        struct yetty_ygui_widget *hit = yetty_ygui_grid_query(&engine->grid, x, y);
        YGUI_LOG("  release hit=%s, pressed=%s, match=%d", hit ? hit->id : "NULL", widget->id,
                 hit == widget);
        if (hit == widget) {
            /* Call widget's click callback */
            if (widget->click_callback) {
                widget->click_callback(widget, widget->click_userdata);
            }

            /* Legacy event */
            ygui_event_t event = {.widget_id = widget->id, .type = YETTY_YGUI_EVENT_CLICK};
            emit_event(engine, &event);
        }

        if (widget->vtable && widget->vtable->on_release) {
            float lx = x - widget->effective_x;
            float ly = y - widget->effective_y;
            ygui_event_t event = {0};
            if (widget->vtable->on_release(widget, lx, ly, &event)) {
                emit_event(engine, &event);
            }
        }

        engine->pressed = NULL;
    }
}

void yetty_ygui_engine_mouse_scroll(struct yetty_ygui_engine *engine, float x, float y, float dx,
                                    float dy)
{
    if (!engine) {
        return;
    }

    struct yetty_ygui_widget *hit = yetty_ygui_grid_query(&engine->grid, x, y);

    if (hit && hit->vtable && hit->vtable->on_scroll) {
        ygui_event_t event = {0};
        if (hit->vtable->on_scroll(hit, dx, dy, &event)) {
            emit_event(engine, &event);
            hit->dirty = 1;
            engine->dirty = 1;
        }
    }
}

void yetty_ygui_engine_key_down(struct yetty_ygui_engine *engine, uint32_t key, int mods)
{
    if (!engine) {
        return;
    }

    /* Call global key callback */
    if (engine->key_callback) {
        engine->key_callback(engine, key, mods, engine->key_userdata);
    }

    /* Also try focused widget */
    if (engine->focused && engine->focused->vtable && engine->focused->vtable->on_key) {
        ygui_event_t event = {0};
        if (engine->focused->vtable->on_key(engine->focused, key, mods, &event)) {
            emit_event(engine, &event);
            engine->focused->dirty = 1;
            engine->dirty = 1;
        }
    }
}

void yetty_ygui_engine_key_up(struct yetty_ygui_engine *engine, uint32_t key, int mods)
{
    (void)engine;
    (void)key;
    (void)mods;
    /* Currently unused */
}

void yetty_ygui_engine_text_input(struct yetty_ygui_engine *engine, const char *text)
{
    if (!engine || !engine->focused) {
        return;
    }

    /* Textarea — multi-line. The textarea owns its insertion logic in
     * ygui_widgets.c so we don't re-implement byte/cursor splicing
     * here too. */
    if (engine->focused->type == YETTY_YGUI_WIDGET_TEXTAREA) {
        extern void yetty_ygui_internal_textarea_insert(struct yetty_ygui_widget *w,
                                                        const char *text);
        yetty_ygui_internal_textarea_insert(engine->focused, text);
        return;
    }

    /* Only textinput handles text input */
    if (engine->focused->type == YETTY_YGUI_WIDGET_TEXTINPUT) {
        /* Insert the new chunk AT the current cursor position rather
         * than at the end — without this, the emacs-style cursor
         * moves (Ctrl+A/B/E/F, arrows) only affect deletes; typing
         * always pushed characters to the right edge of the buffer. */
        char *old_text = engine->focused->data.textinput.text;
        size_t old_len = old_text ? strlen(old_text) : 0;
        size_t add_len = strlen(text);
        int cursor = engine->focused->data.textinput.cursor_pos;
        if (cursor < 0) cursor = 0;
        if ((size_t)cursor > old_len) cursor = (int)old_len;

        char *new_text = (char *)malloc(old_len + add_len + 1);
        if (new_text) {
            if (cursor > 0 && old_text) {
                memcpy(new_text, old_text, (size_t)cursor);
            }
            memcpy(new_text + cursor, text, add_len);
            if ((size_t)cursor < old_len && old_text) {
                memcpy(new_text + cursor + add_len, old_text + cursor,
                       old_len - (size_t)cursor);
            }
            new_text[old_len + add_len] = '\0';
            free(old_text);
            engine->focused->data.textinput.text = new_text;
            engine->focused->data.textinput.cursor_pos = cursor + (int)add_len;
            engine->focused->dirty = 1;
            engine->dirty = 1;

            /* Call widget's text callback */
            if (engine->focused->text_callback) {
                engine->focused->text_callback(engine->focused, new_text,
                                               engine->focused->text_userdata);
            }

            /* Legacy event */
            ygui_event_t event = {.widget_id = engine->focused->id,
                                  .type = YETTY_YGUI_EVENT_CHANGE,
                                  .data.string_value = new_text};
            emit_event(engine, &event);
        }
    }
}

/*=============================================================================
 * Widget Lookup
 *===========================================================================*/

static struct yetty_ygui_widget *find_recursive(struct yetty_ygui_widget *w, const char *id)
{
    if (!w || !id) {
        return NULL;
    }
    if (w->id && strcmp(w->id, id) == 0) {
        return w;
    }

    for (struct yetty_ygui_widget *child = w->first_child; child; child = child->next_sibling) {
        struct yetty_ygui_widget *found = find_recursive(child, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

struct yetty_ygui_widget *yetty_ygui_engine_find(struct yetty_ygui_engine *engine, const char *id)
{
    if (!engine || !id) {
        return NULL;
    }

    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        struct yetty_ygui_widget *found = find_recursive(w, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

struct yetty_ygui_widget *yetty_ygui_engine_widget_at(struct yetty_ygui_engine *engine, float x,
                                                      float y)
{
    if (!engine) {
        return NULL;
    }
    return yetty_ygui_grid_query(&engine->grid, x, y);
}

/*=============================================================================
 * Input Parsing (OSC 777777/777778 and keyboard)
 *===========================================================================*/

/* Parse OSC 777777 (click) or 777778 (move) sequence
 * Format: ESC ] CODE ; card-name ; buttons ; [press ;] x ; y ESC \
 * Returns 1 on success, 0 if not a matching sequence
 */
static int parse_card_mouse_osc(const char *buf, int len, int *osc_code, char *card_name,
                                int name_max, int *buttons, int *press, float *x, float *y,
                                int *consumed)
{
    if (len < 10) {
        return 0;
    }
    if (buf[0] != '\033' || buf[1] != ']') {
        return 0;
    }

    int i = 2;
    int code = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        code = code * 10 + (buf[i] - '0');
        i++;
    }
    if (code != 777777 && code != 777778) {
        return 0;
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse card name */
    int name_start = i;
    while (i < len && buf[i] != ';') {
        i++;
    }
    if (i >= len) {
        return 0;
    }
    int name_len = i - name_start;
    if (name_len >= name_max) {
        name_len = name_max - 1;
    }
    memcpy(card_name, buf + name_start, name_len);
    card_name[name_len] = '\0';
    i++;

    /* Parse buttons */
    int btn = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        btn = btn * 10 + (buf[i] - '0');
        i++;
    }
    *buttons = btn;
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* For OSC 777777, parse press */
    if (code == 777777) {
        int p = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            p = p * 10 + (buf[i] - '0');
            i++;
        }
        *press = p;
        if (i >= len || buf[i] != ';') {
            return 0;
        }
        i++;
    } else {
        *press = -1; /* N/A for move */
    }

    /* Parse x (float, e.g., "123.45" or "123") */
    float fx = 0.0f;
    int neg = 0;
    if (i < len && buf[i] == '-') {
        neg = 1;
        i++;
    }
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        fx = fx * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            fx += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    *x = neg ? -fx : fx;
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse y (float, e.g., "123.45" or "123") */
    float fy = 0.0f;
    neg = 0;
    if (i < len && buf[i] == '-') {
        neg = 1;
        i++;
    }
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        fy = fy * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            fy += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    *y = neg ? -fy : fy;

    /* Expect ST: ESC \ */
    if (i + 1 >= len || buf[i] != '\033' || buf[i + 1] != '\\') {
        return 0;
    }

    *osc_code = code;
    *consumed = i + 2;
    return 1;
}

/* Parse OSC 777780 (card pixel size report)
 * Format: ESC ] 777780 ; card-name ; pixel-width ; pixel-height ESC \
 * Returns 1 on success, 0 if not a matching sequence
 */
static int parse_card_pixel_size_osc(const char *buf, int len, char *card_name, int name_max,
                                     float *pixel_w, float *pixel_h, int *consumed)
{
    if (len < 15) {
        return 0;
    }
    if (buf[0] != '\033' || buf[1] != ']') {
        return 0;
    }

    int i = 2;
    /* Parse OSC code */
    int code = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        code = code * 10 + (buf[i] - '0');
        i++;
    }
    if (code != 777780) {
        return 0;
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse card name */
    int name_len = 0;
    while (i < len && buf[i] != ';' && name_len < name_max - 1) {
        card_name[name_len++] = buf[i++];
    }
    card_name[name_len] = '\0';
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse pixel width (float) */
    float w = 0.0f;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        w = w * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            w += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse pixel height (float) */
    float h = 0.0f;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        h = h * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            h += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }

    /* Expect ST: ESC \ */
    if (i + 1 >= len || buf[i] != '\033' || buf[i + 1] != '\\') {
        return 0;
    }

    *pixel_w = w;
    *pixel_h = h;
    *consumed = i + 2;
    return 1;
}

/* Parse CSI 6 ; h ; w t (cell size report)
 * Format: ESC [ 6 ; height ; width t
 * Height and width can be floats (e.g., "9.60") for sub-pixel precision
 * Returns 1 on success, 0 if not a matching sequence
 */
static int parse_cell_size_csi(const char *buf, int len, float *cell_height, float *cell_width,
                               int *consumed)
{
    if (len < 8) {
        return 0; /* Minimum: ESC [ 6 ; h ; w t */
    }
    if (buf[0] != '\033' || buf[1] != '[') {
        return 0;
    }

    int i = 2;
    /* Parse first number (should be 6) */
    int n1 = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        n1 = n1 * 10 + (buf[i] - '0');
        i++;
    }
    if (n1 != 6) {
        return 0; /* Not a cell size report */
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse height (may be float like "16.00") */
    float h = 0.0f;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        h = h * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            h += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse width (may be float like "9.60") */
    float w = 0.0f;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        w = w * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            w += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    if (i >= len || buf[i] != 't') {
        return 0;
    }
    i++;

    *cell_height = h;
    *cell_width = w;
    *consumed = i;
    return 1;
}

/* Scale coordinates from display space to internal canvas space */
static void scale_coords(struct yetty_ygui_engine *engine, float *x, float *y)
{
    float orig_x = *x, orig_y = *y;

    /* MUST have pixel size from OSC 777780 - no fallback */
    if (!engine->have_pixel_size) {
        YGUI_LOG("scale_coords: SKIPPED - waiting for OSC 777780");
        return;
    }

    float display_w = engine->display_pixel_w;
    float display_h = engine->display_pixel_h;

    /* When zoomed, display shows only a portion of the canvas */
    float visible_w = engine->width / engine->view_zoom;
    float visible_h = engine->height / engine->view_zoom;

    /* Transform: display → visible canvas portion → canvas coords */
    *x = engine->view_scroll_x + (*x / display_w) * visible_w;
    *y = engine->view_scroll_y + (*y / display_h) * visible_h;

    YGUI_LOG("scale_coords: in=(%.1f,%.1f) out=(%.1f,%.1f) disp=%.2fx%.2f canvas=%.0fx%.0f", orig_x,
             orig_y, *x, *y, display_w, display_h, engine->width, engine->height);
}

/* Handle terminal resize based on canvas_mode and scale_mode */
static void handle_resize(struct yetty_ygui_engine *engine)
{
    if (!engine || engine->reference_w == 0.0f) {
        return;
    }

    /* MUST have pixel size from OSC 777780 - no fallback */
    if (!engine->have_pixel_size) {
        YGUI_LOG("handle_resize: SKIPPED - waiting for OSC 777780");
        return;
    }

    float new_display_w = engine->display_pixel_w;
    float new_display_h = engine->display_pixel_h;

    YGUI_LOG("Resize: new display %.2fx%.2f", new_display_w, new_display_h);

    /* Snapshot the size before mutation so the callback can report the
     * previous canvas dims. */
    float prev_w = engine->width;
    float prev_h = engine->height;

    if (engine->canvas_mode == YETTY_YGUI_CANVAS_FIT) {
        /* Canvas size now tracks display size. Widget *authored* geometry is
         * left untouched on purpose — the layout pass will recompute resolved
         * boxes from authored values during the next render.
         *
         * NOTE: YETTY_YGUI_SCALE_ON is currently a no-op. Issue #41 calls out
         * the old in-place scaling as drift-prone; layout-driven scaling is a
         * follow-up. */
        engine->width = new_display_w;
        engine->height = new_display_h;
        engine->had_first_resize = 1;

        /* Rebuild spatial grid for the new canvas size. The next engine_rebuild
         * will re-insert widgets after running the layout pass. */
        yetty_ygui_grid_destroy(&engine->grid);
        yetty_ygui_grid_init(&engine->grid, engine->width, engine->height,
                             calc_grid_bucket_size(engine->width, engine->height));

        engine->dirty = 1;
        engine->needs_full_redraw = 1;
    }
    /* YGUI_CANVAS_FIXED: canvas size unchanged, ydraw card handles zoom/scroll */

    engine->prev_width = prev_w;
    engine->prev_height = prev_h;

    /* Re-anchor any live toast notifications to the new right edge before
     * the user callback sees the new size — the callback might trigger
     * additional layout work and we want the stack already in place. */
    yetty_ygui_engine_notify_on_resize(engine);

    /* Call user's resize callback */
    if (engine->resize_callback) {
        engine->resize_callback(engine, engine->width, engine->height, prev_w, prev_h,
                                engine->resize_userdata);
    }
}

/* Parse OSC 777779 (view change) sequence
 * Format: ESC ] 777779 ; card-name ; zoom ; scroll-x ; scroll-y ESC \
 * Returns 1 on success, 0 if not a matching sequence
 */
static int parse_view_change_osc(const char *buf, int len, char *card_name, int name_max,
                                 float *zoom, float *scroll_x, float *scroll_y, int *consumed)
{
    if (len < 12) {
        return 0;
    }
    if (buf[0] != '\033' || buf[1] != ']') {
        return 0;
    }

    int i = 2;
    int code = 0;
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        code = code * 10 + (buf[i] - '0');
        i++;
    }
    if (code != 777779) {
        return 0;
    }
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse card name */
    int name_start = i;
    while (i < len && buf[i] != ';') {
        i++;
    }
    if (i >= len) {
        return 0;
    }
    int name_len = i - name_start;
    if (name_len >= name_max) {
        name_len = name_max - 1;
    }
    memcpy(card_name, buf + name_start, name_len);
    card_name[name_len] = '\0';
    i++;

    /* Parse zoom */
    float z = 0.0f;
    int neg = 0;
    if (i < len && buf[i] == '-') {
        neg = 1;
        i++;
    }
    while (i < len && (buf[i] >= '0' && buf[i] <= '9')) {
        z = z * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            z += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    *zoom = neg ? -z : z;
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse scroll_x */
    float sx = 0.0f;
    neg = 0;
    if (i < len && buf[i] == '-') {
        neg = 1;
        i++;
    }
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        sx = sx * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            sx += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    *scroll_x = neg ? -sx : sx;
    if (i >= len || buf[i] != ';') {
        return 0;
    }
    i++;

    /* Parse scroll_y */
    float sy = 0.0f;
    neg = 0;
    if (i < len && buf[i] == '-') {
        neg = 1;
        i++;
    }
    while (i < len && buf[i] >= '0' && buf[i] <= '9') {
        sy = sy * 10.0f + (float)(buf[i] - '0');
        i++;
    }
    if (i < len && buf[i] == '.') {
        i++;
        float frac = 0.1f;
        while (i < len && buf[i] >= '0' && buf[i] <= '9') {
            sy += (float)(buf[i] - '0') * frac;
            frac *= 0.1f;
            i++;
        }
    }
    *scroll_y = neg ? -sy : sy;

    /* Expect ST: ESC \ */
    if (i + 1 >= len || buf[i] != '\033' || buf[i + 1] != '\\') {
        return 0;
    }

    *consumed = i + 2;
    return 1;
}

void yetty_ygui_internal_process_input(struct yetty_ygui_engine *engine, const char *data, int len)
{
    /* Append to input buffer */
    if (engine->input_len + len > (int)sizeof(engine->input_buffer) - 1) {
        engine->input_len = 0; /* Reset on overflow */
    }
    memcpy(engine->input_buffer + engine->input_len, data, len);
    engine->input_len += len;

    /* Process input buffer */
    int i = 0;
    while (i < engine->input_len) {
        int osc_code;
        char card_name[128];
        int buttons, press;
        float x, y;
        int consumed;
        float cell_h, cell_w;

        /* Try to parse OSC 777780 (card pixel size) first - this is the most accurate */
        float pixel_w, pixel_h;
        if (parse_card_pixel_size_osc(engine->input_buffer + i, engine->input_len - i, card_name,
                                      sizeof(card_name), &pixel_w, &pixel_h, &consumed)) {
            /* Only use if this is our card */
            if (engine->card_name && strcmp(card_name, engine->card_name) == 0) {
                YGUI_LOG("Got card pixel size for '%s': %.2fx%.2f", card_name, pixel_w, pixel_h);
                engine->display_pixel_w = pixel_w;
                engine->display_pixel_h = pixel_h;
                engine->have_pixel_size = 1;

                /* Store reference size on first pixel size */
                if (engine->reference_w == 0.0f) {
                    engine->reference_w = pixel_w;
                    engine->reference_h = pixel_h;
                    YGUI_LOG("Reference size set from pixel: %.0fx%.0f", engine->reference_w,
                             engine->reference_h);
                }

                /* Defer resize to render time - keeps visual and hit-test in sync */
                engine->needs_resize = 1;
                engine->dirty = 1;
                engine->needs_full_redraw = 1;
            }
            i += consumed;
        }
        /* Try to parse CSI cell size response */
        else if (parse_cell_size_csi(engine->input_buffer + i, engine->input_len - i, &cell_h,
                                     &cell_w, &consumed)) {
            YGUI_LOG("Got cell size: %.2fx%.2f", cell_w, cell_h);
            float old_w = engine->cell_width;
            float old_h = engine->cell_height;
            engine->cell_width = cell_w;
            engine->cell_height = cell_h;

            /* Store reference size on first cell size query (fallback if no pixel size) */
            /* Cell size is stored but not used for coordinate mapping
             * (OSC 777780 provides direct pixel size) */
            i += consumed;
        } else if (parse_card_mouse_osc(engine->input_buffer + i, engine->input_len - i, &osc_code,
                                        card_name, sizeof(card_name), &buttons, &press, &x, &y,
                                        &consumed)) {
            /* If resize is pending, apply it NOW before processing mouse events.
             * This ensures canvas size and widget positions are correct for hit testing. */
            if (engine->needs_resize) {
                handle_resize(engine);
                engine->needs_resize = 0;
            }

            /* Scale coordinates from display to internal space */
            YGUI_LOG("OSC mouse: display=(%.1f,%.1f) cell=(%.1f,%.1f) card=(%d,%d) "
                     "canvas=(%.0f,%.0f) zoom=%.2f scroll=(%.1f,%.1f)",
                     x, y, engine->cell_width, engine->cell_height, engine->card_w, engine->card_h,
                     engine->width, engine->height, engine->view_zoom, engine->view_scroll_x,
                     engine->view_scroll_y);
            scale_coords(engine, &x, &y);
            YGUI_LOG("  -> canvas coords: (%.1f, %.1f)", x, y);

            /* Dispatch mouse event */
            if (osc_code == 777777) {
                /* Click event */
                if (press == 1) {
                    yetty_ygui_engine_mouse_down(engine, x, y, buttons & 0x7);
                } else {
                    yetty_ygui_engine_mouse_up(engine, x, y, buttons & 0x7);
                }
            } else {
                /* Move event */
                yetty_ygui_engine_mouse_move(engine, x, y);
            }
            i += consumed;
        } else {
            /* Try to parse OSC 777779 (view change) */
            float view_zoom, view_sx, view_sy;
            if (parse_view_change_osc(engine->input_buffer + i, engine->input_len - i, card_name,
                                      sizeof(card_name), &view_zoom, &view_sx, &view_sy,
                                      &consumed)) {
                YGUI_LOG("View change: zoom=%.2f scroll=(%.1f,%.1f)", view_zoom, view_sx, view_sy);
                engine->view_zoom = view_zoom;
                engine->view_scroll_x = view_sx;
                engine->view_scroll_y = view_sy;
                i += consumed;
            } else if (engine->input_buffer[i] == '\033') {
                /* Incomplete escape sequence - wait for more data */
                break;
            } else {
                /* Regular character - keyboard input */
                char ch = engine->input_buffer[i];

                if (ch == 'q' || ch == 'Q') {
                    /* Quit on 'q' */
                    engine->running = 0;
                } else if (engine->key_callback) {
                    engine->key_callback(engine, (uint32_t)ch, 0, engine->key_userdata);
                }
                i++;
            }
        }
    }

    /* Compact buffer */
    if (i > 0 && i < engine->input_len) {
        memmove(engine->input_buffer, engine->input_buffer + i, engine->input_len - i);
        engine->input_len -= i;
    } else if (i >= engine->input_len) {
        engine->input_len = 0;
    }
}

/*=============================================================================
 * yface input — decode binary OSC envelopes from the ymgui-layer hit router
 *
 * The server (yetty/src/yterm/terminal.c) hit-tests the cursor against live
 * cards and emits YMGUI_OSC_SC_MOUSE / RESIZE / FOCUS / KEY with
 * card-local coordinates. We only react to events tagged with our own
 * card_id. Bytes that don't form an OSC envelope (CSI replies, plain
 * keystrokes) are forwarded through on_raw to the existing parser.
 *===========================================================================*/

void yetty_ygui_internal_yface_on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yetty_ygui_engine *engine = (struct yetty_ygui_engine *)user;
    if (!engine) {
        return;
    }

    switch (osc_code) {
    case YMGUI_OSC_SC_MOUSE: {
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_mouse)) {
            return;
        }
        const struct yetty_ymgui_wire_input_mouse *m =
            (const struct yetty_ymgui_wire_input_mouse *)payload;
        if (m->magic != YMGUI_WIRE_MAGIC_INPUT_MOUSE) {
            return;
        }
        if (m->card_id != engine->card_id) {
            return; /* not ours */
        }

        switch (m->kind) {
        case YETTY_YMGUI_INPUT_MOUSE_POS:
            yetty_ygui_engine_mouse_move(engine, m->x, m->y);
            break;
        case YETTY_YMGUI_INPUT_MOUSE_BUTTON:
            if (m->pressed) {
                yetty_ygui_engine_mouse_down(engine, m->x, m->y, m->button);
            } else {
                yetty_ygui_engine_mouse_up(engine, m->x, m->y, m->button);
            }
            break;
        case YETTY_YMGUI_INPUT_MOUSE_WHEEL:
            yetty_ygui_engine_mouse_scroll(engine, m->x, m->y, 0.0f, m->wheel_dy);
            break;
        }
        break;
    }
    case YMGUI_OSC_SC_RESIZE: {
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_resize)) {
            return;
        }
        const struct yetty_ymgui_wire_input_resize *r =
            (const struct yetty_ymgui_wire_input_resize *)payload;
        if (r->magic != YMGUI_WIRE_MAGIC_INPUT_RESIZE) {
            return;
        }
        if (r->card_id != engine->card_id) {
            return;
        }

        engine->display_pixel_w = r->width;
        engine->display_pixel_h = r->height;
        engine->have_pixel_size = 1;
        if (engine->reference_w == 0.0f) {
            engine->reference_w = r->width;
            engine->reference_h = r->height;
        }
        engine->needs_resize = 1;
        engine->dirty = 1;
        engine->needs_full_redraw = 1;
        /* Don't fire resize_callback here — engine->width/height haven't
         * been updated yet. handle_resize() runs on the next render (driven
         * by needs_resize) and emits the callback once the canvas dims
         * actually change. */
        break;
    }
    case YMGUI_OSC_SC_FOCUS: {
        /* Focus tracking is internal to the engine for now — no public API
         * surface. The server tells us when our card gains/loses focus;
         * widget-level focus stays driven by mouse/key events. */
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_focus)) {
            return;
        }
        const struct yetty_ymgui_wire_input_focus *f =
            (const struct yetty_ymgui_wire_input_focus *)payload;
        if (f->magic != YMGUI_WIRE_MAGIC_INPUT_FOCUS) {
            return;
        }
        if (f->card_id != engine->card_id) {
            return;
        }
        /* Currently unused — wire it through if/when we add a focus API. */
        break;
    }
    case YMGUI_OSC_SC_KEY: {
        if (payload_len < sizeof(struct yetty_ymgui_wire_input_key)) {
            return;
        }
        const struct yetty_ymgui_wire_input_key *k =
            (const struct yetty_ymgui_wire_input_key *)payload;
        if (k->magic != YMGUI_WIRE_MAGIC_INPUT_KEY) {
            return;
        }
        if (k->card_id != engine->card_id) {
            return;
        }

        if (k->kind == YETTY_YMGUI_INPUT_KEY_CHAR) {
            char utf8[8];
            uint32_t cp = k->codepoint;
            int n = 0;
            /* Quick UTF-32 → UTF-8 (ASCII-fast-path good enough here). */
            if (cp < 0x80) {
                utf8[n++] = (char)cp;
            } else if (cp < 0x800) {
                utf8[n++] = (char)(0xC0 | (cp >> 6));
                utf8[n++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                utf8[n++] = (char)(0xE0 | (cp >> 12));
                utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[n++] = (char)(0x80 | (cp & 0x3F));
            } else {
                utf8[n++] = (char)(0xF0 | (cp >> 18));
                utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[n++] = (char)(0x80 | (cp & 0x3F));
            }
            utf8[n] = '\0';
            yetty_ygui_engine_text_input(engine, utf8);
        } else if (k->kind == YETTY_YMGUI_INPUT_KEY_DOWN) {
            yetty_ygui_engine_key_down(engine, (uint32_t)k->key, k->mods);
        } else if (k->kind == YETTY_YMGUI_INPUT_KEY_UP) {
            yetty_ygui_engine_key_up(engine, (uint32_t)k->key, k->mods);
        }
        break;
    }
    default:
        break;
    }
}

void yetty_ygui_internal_yface_on_raw(void *user, const char *bytes, size_t n)
{
    struct yetty_ygui_engine *engine = (struct yetty_ygui_engine *)user;
    if (!engine || n == 0) {
        return;
    }
    /* Non-OSC bytes (CSI replies, raw keystrokes) — feed the legacy
     * parser, which still handles cell-size CSI / view-change OSC and
     * direct keyboard chars. */
    yetty_ygui_internal_process_input(engine, bytes, (int)n);
}

/*=============================================================================
 * Event-loop integration is implemented in ygui_engine_uv.c (compiled
 * into the `ygui` static library, layered on top of `ygui-core`).
 * yetty_ygui_engine_attach / _run / _get_loop / _poll all live there;
 * engine_stop is libuv-free and stays here.
 *===========================================================================*/

void yetty_ygui_engine_stop(struct yetty_ygui_engine *engine)
{
    if (engine) {
        engine->running = 0;
    }
}

/*=============================================================================
 * Testing API
 *===========================================================================*/

void yetty_ygui_engine_set_input_fd(struct yetty_ygui_engine *engine, int fd)
{
    if (engine) {
        engine->input_fd = fd;
    }
}

void yetty_ygui_engine_set_output_fd(struct yetty_ygui_engine *engine, int fd)
{
    if (engine) {
        engine->output_fd = fd;
    }
}

void yetty_ygui_engine_set_output_pty(struct yetty_ygui_engine *engine,
                                      struct yetty_platform_pty *pty)
{
    if (engine) {
        engine->output_pty = pty;
    }
}

void yetty_ygui_engine_set_card_size(struct yetty_ygui_engine *engine, int card_w, int card_h)
{
    if (engine) {
        engine->card_w = card_w;
        engine->card_h = card_h;
    }
}

void yetty_ygui_engine_set_display_pixel_size(struct yetty_ygui_engine *engine, float width,
                                              float height)
{
    if (!engine) {
        return;
    }

    engine->display_pixel_w = width;
    engine->display_pixel_h = height;
    engine->have_pixel_size = 1;

    /* Set canvas size to match display pixels */
    engine->width = width;
    engine->height = height;

    /* Reinitialize grid with actual size */
    yetty_ygui_grid_destroy(&engine->grid);
    yetty_ygui_grid_init(&engine->grid, width, height, calc_grid_bucket_size(width, height));

    /* Unconditionally force a re-emit. The yui transport calls
     * set_display_pixel_size right before invoking
     * layer->ops->resize_grid, and the scene-canvas's
     * scene_set_grid_size wipes every entity regardless of whether the
     * new grid dims differ from the old ones. Without this, GLFW's
     * initial RESIZE-after-first-frame (same dimensions as the first
     * frame) silently destroys the just-emitted widget tree on the
     * consumer side while our dedup cache keeps us from sending it
     * again — bottom bar, popups, etc. all disappear until the next
     * dimension-changing resize. The "changed" guard was unsafe
     * precisely because the consumer wipe is unconditional. */
    engine->dirty = 1;
    engine->needs_full_redraw = 1;
    engine->prev_emit_size = 0;
}

/* yetty_ygui_engine_get_loop / yetty_ygui_engine_poll are libuv-coupled —
 * implementations live in ygui_engine_uv.c (the full `ygui` library). */

/*=============================================================================
 * Legacy API compatibility (ygui_engine_create with buffer)
 *===========================================================================*/

/* Keep old function signature working via macro or wrapper if needed */

/*=============================================================================
 * Widget Callbacks
 *===========================================================================*/

void yetty_ygui_widget_button_on_click(struct yetty_ygui_widget *button,
                                       ygui_click_callback_t callback, void *userdata)
{
    if (!button || button->type != YETTY_YGUI_WIDGET_BUTTON) {
        return;
    }
    button->click_callback = callback;
    button->click_userdata = userdata;
}

void yetty_ygui_widget_slider_on_change(struct yetty_ygui_widget *slider,
                                        ygui_change_callback_t callback, void *userdata)
{
    if (!slider || slider->type != YETTY_YGUI_WIDGET_SLIDER) {
        return;
    }
    slider->change_callback = callback;
    slider->change_userdata = userdata;
}

void yetty_ygui_widget_checkbox_on_change(struct yetty_ygui_widget *checkbox,
                                          ygui_check_callback_t callback, void *userdata)
{
    if (!checkbox || checkbox->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return;
    }
    checkbox->check_callback = callback;
    checkbox->check_userdata = userdata;
}

void yetty_ygui_widget_textinput_on_change(struct yetty_ygui_widget *input,
                                           ygui_text_callback_t callback, void *userdata)
{
    if (!input || input->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    input->text_callback = callback;
    input->text_userdata = userdata;
}

/*=============================================================================
 * Engine Clear (removes all widgets)
 *===========================================================================*/

void yetty_ygui_engine_clear(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return;
    }

    /* Free all widgets */
    struct yetty_ygui_widget *w = engine->first_widget;
    while (w) {
        struct yetty_ygui_widget *next = w->next_sibling;
        yetty_ygui_widget_free(w);
        w = next;
    }

    engine->first_widget = NULL;
    engine->last_widget = NULL;
    engine->widget_count = 0;
    engine->hovered = NULL;
    engine->pressed = NULL;
    engine->focused = NULL;

    yetty_ygui_grid_clear(&engine->grid);
    engine->dirty = 1;
    /* All widgets gone — next render is a full redraw with CMD_ZERO. */
    engine->needs_full_redraw = 1;
    engine->pending_delete_count = 0;
}

/*=============================================================================
 * Deprecated/Legacy Functions
 *===========================================================================*/

void yetty_ygui_engine_subscribe_clicks(struct yetty_ygui_engine *engine, int enable)
{
    if (!engine) {
        return;
    }
    int want = enable ? 1 : 0;
    if (want == engine->clicks_subscribed) {
        return;
    }
    struct yetty_ycore_void_result r = yetty_ygui_osc_subscribe_clicks(engine->output_pty, want);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_subscribe_clicks(%d): %s", want, r.error.msg);
        yetty_ycore_error_destroy(r.error);
        return;
    }
    engine->clicks_subscribed = want;
}

void yetty_ygui_engine_subscribe_moves(struct yetty_ygui_engine *engine, int enable)
{
    if (!engine) {
        return;
    }
    int want = enable ? 1 : 0;
    if (want == engine->moves_subscribed) {
        return;
    }
    struct yetty_ycore_void_result r = yetty_ygui_osc_subscribe_moves(engine->output_pty, want);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_subscribe_moves(%d): %s", want, r.error.msg);
        yetty_ycore_error_destroy(r.error);
        return;
    }
    engine->moves_subscribed = want;
}

/* Rebuild without render (for internal use). Forces a full redraw
 * mode — this entry point is used by tests / off-screen consumers
 * that want to populate the buffer from scratch. */
struct yetty_ycore_void_result yetty_ygui_engine_rebuild(struct yetty_ygui_engine *engine)
{
    return engine_rebuild_with_mode(engine, /*full_redraw=*/1);
}

/*=============================================================================
 * Resize Handling API
 *===========================================================================*/

void yetty_ygui_engine_set_canvas_mode(struct yetty_ygui_engine *engine, ygui_canvas_mode_t mode)
{
    if (engine) {
        engine->canvas_mode = mode;
    }
}

void yetty_ygui_engine_set_scale_mode(struct yetty_ygui_engine *engine, ygui_scale_mode_t mode)
{
    if (engine) {
        engine->scale_mode = mode;
    }
}

void yetty_ygui_engine_on_resize(struct yetty_ygui_engine *engine, ygui_resize_callback_t callback,
                                 void *userdata)
{
    if (!engine) {
        return;
    }
    engine->resize_callback = callback;
    engine->resize_userdata = userdata;
}

/*=============================================================================
 * View State API (read-only)
 *===========================================================================*/

float yetty_ygui_engine_get_zoom(const struct yetty_ygui_engine *engine)
{
    return engine ? engine->view_zoom : 1.0f;
}

float yetty_ygui_engine_get_scroll_x(const struct yetty_ygui_engine *engine)
{
    return engine ? engine->view_scroll_x : 0.0f;
}

float yetty_ygui_engine_get_scroll_y(const struct yetty_ygui_engine *engine)
{
    return engine ? engine->view_scroll_y : 0.0f;
}

/*=============================================================================
 * View Change Subscription
 *===========================================================================*/

void yetty_ygui_engine_subscribe_view_changes(struct yetty_ygui_engine *engine, int enable)
{
    if (!engine) {
        return;
    }
    int want = enable ? 1 : 0;
    if (want == engine->view_subscribed) {
        return;
    }
    struct yetty_ycore_void_result r =
        yetty_ygui_osc_subscribe_view_changes(engine->output_pty, want);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_subscribe_view_changes(%d): %s", want, r.error.msg);
        yetty_ycore_error_destroy(r.error);
        return;
    }
    engine->view_subscribed = want;
}

/*=============================================================================
 * View Control API (app → yetty)
 *===========================================================================*/

void yetty_ygui_engine_set_zoom(struct yetty_ygui_engine *engine, float level)
{
    if (!engine || !engine->card_name) {
        return;
    }
    struct yetty_ycore_void_result r =
        yetty_ygui_osc_zoom_card(engine->output_pty, engine->card_name, level);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_set_zoom: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}

void yetty_ygui_engine_scroll_to(struct yetty_ygui_engine *engine, float x, float y)
{
    if (!engine || !engine->card_name) {
        return;
    }
    struct yetty_ycore_void_result r =
        yetty_ygui_osc_scroll_card(engine->output_pty, engine->card_name, x, y, 1);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_scroll_to: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}

void yetty_ygui_engine_scroll_by(struct yetty_ygui_engine *engine, float dx, float dy)
{
    if (!engine || !engine->card_name) {
        return;
    }
    struct yetty_ycore_void_result r =
        yetty_ygui_osc_scroll_card(engine->output_pty, engine->card_name, dx, dy, 0);
    if (YETTY_IS_ERR(r)) {
        yerror("ygui_engine_scroll_by: %s", r.error.msg);
        yetty_ycore_error_destroy(r.error);
    }
}
