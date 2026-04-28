/*
 * ygui_engine.c - YGui engine implementation with libuv event loop
 */

#include "ygui_internal.h"
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/ymgui/wire.h>
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
static void handle_resize(ygui_engine_t *engine);

/*=============================================================================
 * Terminal State (Unix only - Windows uses ConPTY via yetty)
 *===========================================================================*/

static int ygui_initialized = 0;
static volatile int ygui_resize_pending = 0;
static ygui_engine_t *ygui_active_engine = NULL; /* For resize handler */

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
    ygui_resize_pending = 1;
}
#endif /* !_WIN32 */

int ygui_init(void)
{
    if (ygui_initialized) {
        return 0;
    }

#ifndef _WIN32
    /* Save original terminal settings */
    if (tcgetattr(STDIN_FILENO, &ygui_orig_termios) < 0) {
        ygui_set_error("Failed to get terminal attributes");
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
        ygui_set_error("Failed to set raw terminal mode");
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

void ygui_shutdown(void)
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

void ygui_set_error(const char *msg)
{
    if (msg) {
        snprintf(ygui_error_msg, sizeof(ygui_error_msg), "%s", msg);
    } else {
        ygui_error_msg[0] = '\0';
    }
}

const char *ygui_get_error(void)
{
    return ygui_error_msg;
}

/*=============================================================================
 * Version
 *===========================================================================*/

const char *ygui_version(void)
{
    return "0.2.0";
}

/*=============================================================================
 * Engine Lifecycle
 *===========================================================================*/

/* Internal helper to allocate and initialize common engine state */
static ygui_engine_t *engine_alloc_init(const char *card_name, int x, int y, int cols, int rows)
{
    ygui_engine_t *engine = (ygui_engine_t *)calloc(1, sizeof(ygui_engine_t));
    if (!engine) {
        ygui_set_error("Failed to allocate engine");
        return NULL;
    }

    /* Create ypaint-core buffer — widgets accumulate SDF primitives + text
     * spans into it; the engine base64-encodes the serialization and ships
     * it via OSC 666674 every render. */
    struct yetty_ypaint_core_buffer_result br = yetty_ypaint_core_buffer_create(NULL);
    if (!YETTY_IS_OK(br)) {
        ygui_set_error("Failed to create ypaint buffer");
        free(engine);
        return NULL;
    }
    engine->buffer = br.value;

    /* Optional: open a raster font in metrics-only mode for text width
     * measurement. The env var lets users point at any TTF; absence just
     * leaves measure_font NULL and ygui falls back to its heuristic. Same
     * pattern as ypdf (pdf-renderer.c line ~359). */
    const char *ttf = getenv("YGUI_MEASURE_FONT");
    if (ttf && ttf[0]) {
        engine->measure_base_size = 32.0f;
        struct yetty_font_font_result fr = yetty_font_raster_font_create_from_file(
            ttf, /*shader*/ NULL, engine->measure_base_size);
        if (YETTY_IS_OK(fr)) {
            engine->measure_font = fr.value;
        }
    }

    /* Store card name, position, and cell dimensions */
    engine->card_name = ygui_strdup(card_name);
    engine->card_x = x;
    engine->card_y = y;
    engine->card_w = cols;
    engine->card_h = rows;

    /* Default theme */
    engine->theme = ygui_theme_create_default();
    engine->owns_theme = 1;

    /* Initial state - canvas size set after OSC 777780 */
    engine->dirty = 1;
    engine->width = 1.0f; /* Placeholder until pixel size known */
    engine->height = 1.0f;
    engine->cell_width = 0.0f;
    engine->cell_height = 0.0f;

    /* View state defaults */
    engine->view_zoom = 1.0f;
    engine->view_scroll_x = 0.0f;
    engine->view_scroll_y = 0.0f;

    /* Canvas = actual card pixels, no scaling needed */
    engine->canvas_mode = YGUI_CANVAS_FIT;
    engine->scale_mode = YGUI_SCALE_OFF;
    engine->reference_w = 0.0f;
    engine->reference_h = 0.0f;
    engine->display_pixel_w = 0.0f;
    engine->display_pixel_h = 0.0f;
    engine->have_pixel_size = 0;

    /* Default I/O file descriptors */
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
    ygui_grid_init(&engine->grid, 1.0f, 1.0f, 1.0f);

    return engine;
}

ygui_engine_t *ygui_engine_create(const char *card_name, int x, int y, int cols, int rows)
{
    return engine_alloc_init(card_name, x, y, cols, rows);
}

ygui_engine_t *ygui_engine_create_with_pixel_hint(const char *card_name, int x, int y,
                                                  float width_hint, float height_hint)
{
    /* TODO: Query cell size first to calculate cols/rows
     * For now, use reasonable defaults (10x16 cell size) */
    int cols = (int)(width_hint / 10.0f + 0.5f);
    int rows = (int)(height_hint / 16.0f + 0.5f);
    if (cols < 1) {
        cols = 1;
    }
    if (rows < 1) {
        rows = 1;
    }

    ygui_engine_t *engine = engine_alloc_init(card_name, x, y, cols, rows);
    if (engine) {
        /* Store pixel hints as reference size for widget scaling */
        engine->reference_w = width_hint;
        engine->reference_h = height_hint;
        engine->width = width_hint;
        engine->height = height_hint;
        engine->scale_mode = YGUI_SCALE_ON; /* Scale widgets from hint to actual */

        /* Initialize grid with hint size */
        ygui_grid_destroy(&engine->grid);
        ygui_grid_init(&engine->grid, width_hint, height_hint,
                       calc_grid_bucket_size(width_hint, height_hint));
    }
    return engine;
}

void ygui_engine_destroy(ygui_engine_t *engine)
{
    if (!engine) {
        return;
    }

    /* Stop running if needed */
    engine->running = 0;

    /* Unsubscribe from events */
    if (engine->clicks_subscribed) {
        ygui_osc_subscribe_clicks(0);
    }
    if (engine->moves_subscribed) {
        ygui_osc_subscribe_moves(0);
    }

    /* Kill card if shown */
    if (engine->card_shown && engine->card_name) {
        ygui_osc_kill_card(engine->card_name);
    }

    /* Drop the ymgui-layer card so the server stops routing mouse to us. */
    if (engine->card_id != 0) {
        ygui_osc_card_remove(engine->card_id);
    }

    /* Destroy yface */
    if (engine->yface_in) {
        yetty_yface_destroy(engine->yface_in);
        engine->yface_in = NULL;
    }

    /* Clean up libuv handles if we own the loop */
    if (engine->owns_loop && engine->loop) {
        uv_loop_close(engine->loop);
        free(engine->loop);
    }

    /* Destroy all widgets */
    ygui_widget_t *w = engine->first_widget;
    while (w) {
        ygui_widget_t *next = w->next_sibling;
        ygui_widget_free(w);
        w = next;
    }

    /* Destroy grid */
    ygui_grid_destroy(&engine->grid);

    /* Destroy theme if owned */
    if (engine->owns_theme && engine->theme) {
        ygui_theme_destroy(engine->theme);
    }

    /* Destroy buffer + measurement font */
    if (engine->buffer) {
        yetty_ypaint_core_buffer_destroy(engine->buffer);
    }
    if (engine->measure_font && engine->measure_font->ops && engine->measure_font->ops->destroy) {
        engine->measure_font->ops->destroy(engine->measure_font);
    }

    /* Free card name */
    free(engine->card_name);

    free(engine);
}

/*=============================================================================
 * Engine Configuration
 *===========================================================================*/

void ygui_engine_set_size(ygui_engine_t *engine, float width, float height)
{
    if (!engine) {
        return;
    }
    engine->width = width;
    engine->height = height;
    ygui_grid_destroy(&engine->grid);
    ygui_grid_init(&engine->grid, width, height, calc_grid_bucket_size(width, height));
    engine->dirty = 1;
}

void ygui_engine_set_theme(ygui_engine_t *engine, ygui_theme_t *theme)
{
    if (!engine) {
        return;
    }
    if (engine->owns_theme && engine->theme) {
        ygui_theme_destroy(engine->theme);
    }
    engine->theme = theme;
    engine->owns_theme = 0;
    engine->dirty = 1;
}

void ygui_engine_set_event_callback(ygui_engine_t *engine, ygui_event_callback_t callback,
                                    void *userdata)
{
    if (!engine) {
        return;
    }
    engine->event_callback = callback;
    engine->event_userdata = userdata;
}

void ygui_engine_on_key(ygui_engine_t *engine, ygui_key_callback_t callback, void *userdata)
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

int ygui_engine_is_dirty(const ygui_engine_t *engine)
{
    return engine ? engine->dirty : 0;
}

void ygui_engine_mark_dirty(ygui_engine_t *engine)
{
    if (engine) {
        engine->dirty = 1;
    }
}

/*=============================================================================
 * Rendering
 *===========================================================================*/

static void engine_rebuild(ygui_engine_t *engine)
{
    if (!engine || !engine->buffer) {
        return;
    }

    /* Clear the grid */
    ygui_grid_clear(&engine->grid);

    /* Create render context */
    ygui_render_ctx_t ctx;
    ygui_render_ctx_init(&ctx, engine->buffer, engine->theme);

    /* Render all top-level widgets */
    for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
        w->was_rendered = 0;
        if (w->render_all) {
            w->render_all(w, &ctx);
        } else {
            ygui_widget_render_all_default(w, &ctx);
        }
    }

    /* Rebuild spatial grid with rendered widgets */
    for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
        if (w->was_rendered) {
            ygui_grid_insert(&engine->grid, w);
        }
    }

    engine->dirty = 0;
}

void ygui_engine_render(ygui_engine_t *engine)
{
    if (!engine || !engine->buffer) {
        return;
    }

    /* 0. Handle pending resize BEFORE rendering - keeps visual and hit-test in sync */
    if (engine->needs_resize) {
        handle_resize(engine);
        engine->needs_resize = 0;
    }

    /* 1. Clear buffer */
    yetty_ypaint_core_buffer_clear(engine->buffer);

    /* 2. Set explicit scene bounds to match full canvas */
    yetty_ypaint_core_buffer_set_scene_bounds(engine->buffer, 0, 0, engine->width, engine->height);

    /* 3. Rebuild UI */
    engine_rebuild(engine);

    /* 4. Serialize (framed: prims + text_spans + scene_bounds) */
    const uint8_t *data = NULL;
    uint32_t size = (uint32_t)yetty_ypaint_core_buffer_serialize(engine->buffer, &data);
    if (size == 0 || !data) {
        return;
    }

    /* 5. Send OSC */
    if (!engine->card_shown) {
        ygui_osc_create_card(engine->card_name, engine->card_x, engine->card_y, engine->card_w,
                             engine->card_h, data, size);
        engine->card_shown = 1;
    } else {
        ygui_osc_update_card(engine->card_name, data, size);
    }
}

void ygui_engine_show(ygui_engine_t *engine)
{
    if (!engine) {
        return;
    }

    /* card_x, card_y, card_w, card_h already set in ygui_engine_create */

    /* Query cell size (kept for future use) */
    ygui_osc_query_cell_size();

    /* Subscribe to click AND move events (move needed for slider drag) */
    ygui_osc_subscribe_clicks(1);
    engine->clicks_subscribed = 1;
    ygui_osc_subscribe_moves(1);
    engine->moves_subscribed = 1;

    /* Register our card with the ymgui-layer so the server hit-tests the
     * cursor against our rect and emits YMGUI_OSC_SC_MOUSE with card-local
     * coordinates. Without this, mouse events have nowhere to go. */
    ygui_osc_card_place(engine->card_id, engine->card_x, engine->card_y, (uint32_t)engine->card_w,
                        (uint32_t)engine->card_h);

    /* In CANVAS_FIT mode, create card with minimal data first to trigger OSC 777780.
     * The real render happens after we receive the actual pixel size.
     * This prevents the visual "zoom jump" when canvas resizes to match display. */
    if (engine->canvas_mode == YGUI_CANVAS_FIT && !engine->have_pixel_size) {
        /* Establish the card with an empty buffer so the receiver responds
         * with the pixel size we need to drive the first real render. */
        yetty_ypaint_core_buffer_clear(engine->buffer);
        yetty_ypaint_core_buffer_set_scene_bounds(engine->buffer, 0, 0, 1, 1);
        const uint8_t *data = NULL;
        uint32_t size = (uint32_t)yetty_ypaint_core_buffer_serialize(engine->buffer, &data);
        if (size > 0 && data) {
            ygui_osc_create_card(engine->card_name, engine->card_x, engine->card_y, engine->card_w,
                                 engine->card_h, data, size);
            engine->card_shown = 1;
        }
        engine->dirty = 1; /* Real render after OSC 777780 */
        YGUI_LOG("CANVAS_FIT: created placeholder card, waiting for OSC 777780");
    } else {
        /* CANVAS_FIXED or already have pixel size: render immediately */
        ygui_engine_render(engine);
    }
}

/*=============================================================================
 * Input Handling
 *===========================================================================*/

static void emit_event(ygui_engine_t *engine, const ygui_event_t *event)
{
    if (engine->event_callback && event->type != YGUI_EVENT_NONE) {
        engine->event_callback(event, engine->event_userdata);
    }
}

void ygui_engine_mouse_move(ygui_engine_t *engine, float x, float y)
{
    if (!engine) {
        return;
    }

    ygui_widget_t *hit = ygui_grid_query(&engine->grid, x, y);

    /* Handle hover changes */
    if (hit != engine->hovered) {
        if (engine->hovered) {
            engine->hovered->flags &= ~YGUI_FLAG_HOVER;
            engine->dirty = 1;
        }
        if (hit) {
            hit->flags |= YGUI_FLAG_HOVER;
            engine->dirty = 1;
        }
        engine->hovered = hit;
    }

    /* Handle drag */
    if (engine->pressed && engine->pressed->on_drag) {
        float lx = x - engine->pressed->effective_x;
        float ly = y - engine->pressed->effective_y;
        ygui_event_t event = {0};
        if (engine->pressed->on_drag(engine->pressed, lx, ly, &event)) {
            emit_event(engine, &event);
            engine->dirty = 1;
        }
    }
}

void ygui_engine_mouse_down(ygui_engine_t *engine, float x, float y, int button)
{
    if (!engine) {
        return;
    }
    (void)button;

    YGUI_LOG("mouse_down at (%.1f, %.1f)", x, y);
    ygui_widget_t *hit = ygui_grid_query(&engine->grid, x, y);
    YGUI_LOG("  grid_query returned: %s (ptr=%p)", hit ? hit->id : "NULL", (void *)hit);

    if (hit) {
        hit->flags |= YGUI_FLAG_PRESSED;
        engine->pressed = hit;
        engine->dirty = 1;

        /* Focus change */
        if (engine->focused != hit) {
            if (engine->focused) {
                engine->focused->flags &= ~YGUI_FLAG_FOCUSED;
            }
            hit->flags |= YGUI_FLAG_FOCUSED;
            engine->focused = hit;
        }

        if (hit->on_press) {
            float lx = x - hit->effective_x;
            float ly = y - hit->effective_y;
            ygui_event_t event = {0};
            if (hit->on_press(hit, lx, ly, &event)) {
                emit_event(engine, &event);
            }
        }
    }
}

void ygui_engine_mouse_up(ygui_engine_t *engine, float x, float y, int button)
{
    if (!engine) {
        return;
    }
    (void)button;

    YGUI_LOG("mouse_up at (%.1f, %.1f) pressed=%s", x, y,
             engine->pressed ? engine->pressed->id : "NULL");

    if (engine->pressed) {
        ygui_widget_t *widget = engine->pressed;
        widget->flags &= ~YGUI_FLAG_PRESSED;
        engine->dirty = 1;

        /* Check if release is on same widget (click) */
        ygui_widget_t *hit = ygui_grid_query(&engine->grid, x, y);
        YGUI_LOG("  release hit=%s, pressed=%s, match=%d", hit ? hit->id : "NULL", widget->id,
                 hit == widget);
        if (hit == widget) {
            /* Call widget's click callback */
            if (widget->click_callback) {
                widget->click_callback(widget, widget->click_userdata);
            }

            /* Legacy event */
            ygui_event_t event = {.widget_id = widget->id, .type = YGUI_EVENT_CLICK};
            emit_event(engine, &event);
        }

        if (widget->on_release) {
            float lx = x - widget->effective_x;
            float ly = y - widget->effective_y;
            ygui_event_t event = {0};
            if (widget->on_release(widget, lx, ly, &event)) {
                emit_event(engine, &event);
            }
        }

        engine->pressed = NULL;
    }
}

void ygui_engine_mouse_scroll(ygui_engine_t *engine, float x, float y, float dx, float dy)
{
    if (!engine) {
        return;
    }

    ygui_widget_t *hit = ygui_grid_query(&engine->grid, x, y);

    if (hit && hit->on_scroll) {
        ygui_event_t event = {0};
        if (hit->on_scroll(hit, dx, dy, &event)) {
            emit_event(engine, &event);
            engine->dirty = 1;
        }
    }
}

void ygui_engine_key_down(ygui_engine_t *engine, uint32_t key, int mods)
{
    if (!engine) {
        return;
    }

    /* Call global key callback */
    if (engine->key_callback) {
        engine->key_callback(engine, key, mods, engine->key_userdata);
    }

    /* Also try focused widget */
    if (engine->focused && engine->focused->on_key) {
        ygui_event_t event = {0};
        if (engine->focused->on_key(engine->focused, key, mods, &event)) {
            emit_event(engine, &event);
            engine->dirty = 1;
        }
    }
}

void ygui_engine_key_up(ygui_engine_t *engine, uint32_t key, int mods)
{
    (void)engine;
    (void)key;
    (void)mods;
    /* Currently unused */
}

void ygui_engine_text_input(ygui_engine_t *engine, const char *text)
{
    if (!engine || !engine->focused) {
        return;
    }

    /* Only textinput handles text input */
    if (engine->focused->type == YGUI_WIDGET_TEXTINPUT) {
        /* Append text to input */
        char *old_text = engine->focused->data.textinput.text;
        size_t old_len = old_text ? strlen(old_text) : 0;
        size_t add_len = strlen(text);
        char *new_text = (char *)malloc(old_len + add_len + 1);
        if (new_text) {
            if (old_text) {
                memcpy(new_text, old_text, old_len);
            }
            memcpy(new_text + old_len, text, add_len + 1);
            free(old_text);
            engine->focused->data.textinput.text = new_text;
            engine->focused->data.textinput.cursor_pos = (int)(old_len + add_len);
            engine->dirty = 1;

            /* Call widget's text callback */
            if (engine->focused->text_callback) {
                engine->focused->text_callback(engine->focused, new_text,
                                               engine->focused->text_userdata);
            }

            /* Legacy event */
            ygui_event_t event = {.widget_id = engine->focused->id,
                                  .type = YGUI_EVENT_CHANGE,
                                  .data.string_value = new_text};
            emit_event(engine, &event);
        }
    }
}

/*=============================================================================
 * Widget Lookup
 *===========================================================================*/

static ygui_widget_t *find_recursive(ygui_widget_t *w, const char *id)
{
    if (!w || !id) {
        return NULL;
    }
    if (w->id && strcmp(w->id, id) == 0) {
        return w;
    }

    for (ygui_widget_t *child = w->first_child; child; child = child->next_sibling) {
        ygui_widget_t *found = find_recursive(child, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

ygui_widget_t *ygui_engine_find(ygui_engine_t *engine, const char *id)
{
    if (!engine || !id) {
        return NULL;
    }

    for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
        ygui_widget_t *found = find_recursive(w, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}

ygui_widget_t *ygui_engine_widget_at(ygui_engine_t *engine, float x, float y)
{
    if (!engine) {
        return NULL;
    }
    return ygui_grid_query(&engine->grid, x, y);
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
static void scale_coords(ygui_engine_t *engine, float *x, float *y)
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
static void handle_resize(ygui_engine_t *engine)
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

    if (engine->canvas_mode == YGUI_CANVAS_FIT) {
        /* Canvas size matches display size */
        float old_canvas_w = engine->width;
        float old_canvas_h = engine->height;
        engine->width = new_display_w;
        engine->height = new_display_h;

        /* First resize AND subsequent resizes: scale widgets if SCALE_ON.
         * Widgets were created at initial canvas coords, need to scale to match display. */
        if (!engine->had_first_resize) {
            engine->had_first_resize = 1;
            if (engine->scale_mode == YGUI_SCALE_ON && old_canvas_w > 0 && old_canvas_h > 0) {
                float scale_x = new_display_w / old_canvas_w;
                float scale_y = new_display_h / old_canvas_h;
                YGUI_LOG("First resize: canvas %.0fx%.0f -> %.0fx%.0f scale=(%.4f,%.4f)",
                         old_canvas_w, old_canvas_h, new_display_w, new_display_h, scale_x,
                         scale_y);
                for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
                    w->x *= scale_x;
                    w->y *= scale_y;
                    w->w *= scale_x;
                    w->h *= scale_y;
                    w->effective_x = w->x;
                    w->effective_y = w->y;
                }
            } else {
                YGUI_LOG("First resize: canvas %.0fx%.0f -> %.0fx%.0f (no scaling)", old_canvas_w,
                         old_canvas_h, new_display_w, new_display_h);
                for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
                    w->effective_x = w->x;
                    w->effective_y = w->y;
                }
            }
        } else if (engine->scale_mode == YGUI_SCALE_ON && old_canvas_w > 0 && old_canvas_h > 0) {
            /* Scale all widgets proportionally using FLOAT precision */
            float scale_x = new_display_w / old_canvas_w;
            float scale_y = new_display_h / old_canvas_h;

            YGUI_LOG("Scaling widgets by %.4fx%.4f", scale_x, scale_y);

            for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
                /* Scale positions and sizes (all FLOAT) */
                w->x *= scale_x;
                w->y *= scale_y;
                w->w *= scale_x;
                w->h *= scale_y;

                /* Update effective positions for hit testing */
                w->effective_x = w->x;
                w->effective_y = w->y;
            }
        }

        /* Rebuild spatial grid with new canvas size */
        ygui_grid_destroy(&engine->grid);
        ygui_grid_init(&engine->grid, engine->width, engine->height,
                       calc_grid_bucket_size(engine->width, engine->height));

        /* Re-insert all widgets - they now have correct effective positions */
        for (ygui_widget_t *w = engine->first_widget; w; w = w->next_sibling) {
            if (w->was_rendered) {
                ygui_grid_insert(&engine->grid, w);
            }
        }

        engine->dirty = 1;
    }
    /* YGUI_CANVAS_FIXED: canvas size unchanged, ydraw card handles zoom/scroll */

    /* Call user's resize callback */
    if (engine->resize_callback) {
        engine->resize_callback(engine, engine->resize_userdata);
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

static void process_input(ygui_engine_t *engine, const char *data, int len)
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
                    ygui_engine_mouse_down(engine, x, y, buttons & 0x7);
                } else {
                    ygui_engine_mouse_up(engine, x, y, buttons & 0x7);
                }
            } else {
                /* Move event */
                ygui_engine_mouse_move(engine, x, y);
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

static void yface_on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    ygui_engine_t *engine = (ygui_engine_t *)user;
    if (!engine) {
        return;
    }

    switch (osc_code) {
    case YMGUI_OSC_SC_MOUSE: {
        if (payload_len < sizeof(struct ymgui_wire_input_mouse)) {
            return;
        }
        const struct ymgui_wire_input_mouse *m = (const struct ymgui_wire_input_mouse *)payload;
        if (m->magic != YMGUI_WIRE_MAGIC_INPUT_MOUSE) {
            return;
        }
        if (m->card_id != engine->card_id) {
            return; /* not ours */
        }

        switch (m->kind) {
        case YMGUI_INPUT_MOUSE_POS:
            ygui_engine_mouse_move(engine, m->x, m->y);
            break;
        case YMGUI_INPUT_MOUSE_BUTTON:
            if (m->pressed) {
                ygui_engine_mouse_down(engine, m->x, m->y, m->button);
            } else {
                ygui_engine_mouse_up(engine, m->x, m->y, m->button);
            }
            break;
        case YMGUI_INPUT_MOUSE_WHEEL:
            ygui_engine_mouse_scroll(engine, m->x, m->y, 0.0f, m->wheel_dy);
            break;
        }
        break;
    }
    case YMGUI_OSC_SC_RESIZE: {
        if (payload_len < sizeof(struct ymgui_wire_input_resize)) {
            return;
        }
        const struct ymgui_wire_input_resize *r = (const struct ymgui_wire_input_resize *)payload;
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
        if (engine->resize_callback) {
            engine->resize_callback(engine, engine->resize_userdata);
        }
        break;
    }
    case YMGUI_OSC_SC_FOCUS: {
        /* Focus tracking is internal to the engine for now — no public API
         * surface. The server tells us when our card gains/loses focus;
         * widget-level focus stays driven by mouse/key events. */
        if (payload_len < sizeof(struct ymgui_wire_input_focus)) {
            return;
        }
        const struct ymgui_wire_input_focus *f = (const struct ymgui_wire_input_focus *)payload;
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
        if (payload_len < sizeof(struct ymgui_wire_input_key)) {
            return;
        }
        const struct ymgui_wire_input_key *k = (const struct ymgui_wire_input_key *)payload;
        if (k->magic != YMGUI_WIRE_MAGIC_INPUT_KEY) {
            return;
        }
        if (k->card_id != engine->card_id) {
            return;
        }

        if (k->kind == YMGUI_INPUT_KEY_CHAR) {
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
            ygui_engine_text_input(engine, utf8);
        } else if (k->kind == YMGUI_INPUT_KEY_DOWN) {
            ygui_engine_key_down(engine, (uint32_t)k->key, k->mods);
        } else if (k->kind == YMGUI_INPUT_KEY_UP) {
            ygui_engine_key_up(engine, (uint32_t)k->key, k->mods);
        }
        break;
    }
    default:
        break;
    }
}

static void yface_on_raw(void *user, const char *bytes, size_t n)
{
    ygui_engine_t *engine = (ygui_engine_t *)user;
    if (!engine || n == 0) {
        return;
    }
    /* Non-OSC bytes (CSI replies, raw keystrokes) — feed the legacy
     * parser, which still handles cell-size CSI / view-change OSC and
     * direct keyboard chars. */
    process_input(engine, bytes, (int)n);
}

/*=============================================================================
 * libuv Event Loop
 *===========================================================================*/

static void stdin_poll_cb(uv_poll_t *handle, int status, int events)
{
    ygui_engine_t *engine = (ygui_engine_t *)handle->data;
    if (status < 0) {
        return;
    }

    if (events & UV_READABLE) {
        char buf[1024];
        ssize_t n = read(engine->input_fd, buf, sizeof(buf));
        if (n > 0) {
            if (engine->yface_in) {
                /* yface scans for \e]…\e\\ envelopes, calls on_osc per
                 * complete envelope, and on_raw for the bytes in between. */
                yetty_yface_feed_bytes(engine->yface_in, buf, (size_t)n);
            } else {
                /* No yface available — fall back to legacy text parser. */
                process_input(engine, buf, (int)n);
            }
        } else if (n == 0) {
            /* EOF */
            engine->running = 0;
        }
    }
}

static void prepare_cb(uv_prepare_t *handle)
{
    ygui_engine_t *engine = (ygui_engine_t *)handle->data;

    /* Check for terminal resize */
    if (ygui_resize_pending) {
        ygui_resize_pending = 0;
        YGUI_LOG("SIGWINCH received, re-querying cell size");
        ygui_osc_query_cell_size();
        /* handle_resize will be called when CSI response arrives */
    }

    /* Auto-render if dirty */
    if (engine->dirty) {
        ygui_engine_render(engine);
    }

    /* Check if we should stop */
    if (!engine->running) {
        uv_stop(engine->loop);
    }
}

void ygui_engine_attach(ygui_engine_t *engine, uv_loop_t *loop)
{
    if (!engine || !loop) {
        return;
    }

    engine->loop = loop;
    engine->owns_loop = 0;

    /* Wire up the yface handlers so feed_bytes can dispatch into us. */
    if (engine->yface_in) {
        yetty_yface_set_handlers(engine->yface_in, yface_on_osc, yface_on_raw, engine);
    }

    /* Set up stdin poll */
    uv_poll_init(loop, &engine->stdin_poll, engine->input_fd);
    engine->stdin_poll.data = engine;
    uv_poll_start(&engine->stdin_poll, UV_READABLE, stdin_poll_cb);

    /* Set up prepare handle for auto-render */
    uv_prepare_init(loop, &engine->prepare_handle);
    engine->prepare_handle.data = engine;
    uv_prepare_start(&engine->prepare_handle, prepare_cb);
}

void ygui_engine_run(ygui_engine_t *engine)
{
    if (!engine) {
        return;
    }

    /* Create loop if needed */
    if (!engine->loop) {
        engine->loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
        if (!engine->loop) {
            return;
        }
        uv_loop_init(engine->loop);
        engine->owns_loop = 1;

        /* Attach to the loop */
        ygui_engine_attach(engine, engine->loop);
    }

    engine->running = 1;

    /* Run the loop */
    uv_run(engine->loop, UV_RUN_DEFAULT);

    /* Cleanup handles */
    uv_poll_stop(&engine->stdin_poll);
    uv_prepare_stop(&engine->prepare_handle);
}

void ygui_engine_stop(ygui_engine_t *engine)
{
    if (engine) {
        engine->running = 0;
    }
}

/*=============================================================================
 * Testing API
 *===========================================================================*/

void ygui_engine_set_input_fd(ygui_engine_t *engine, int fd)
{
    if (engine) {
        engine->input_fd = fd;
    }
}

void ygui_engine_set_output_fd(ygui_engine_t *engine, int fd)
{
    if (engine) {
        engine->output_fd = fd;
    }
}

void ygui_engine_set_card_size(ygui_engine_t *engine, int card_w, int card_h)
{
    if (engine) {
        engine->card_w = card_w;
        engine->card_h = card_h;
    }
}

void ygui_engine_set_display_pixel_size(ygui_engine_t *engine, float width, float height)
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
    ygui_grid_destroy(&engine->grid);
    ygui_grid_init(&engine->grid, width, height, calc_grid_bucket_size(width, height));
}

uv_loop_t *ygui_engine_get_loop(ygui_engine_t *engine)
{
    return engine ? engine->loop : NULL;
}

int ygui_engine_poll(ygui_engine_t *engine)
{
    if (!engine || !engine->loop) {
        return 0;
    }
    return uv_run(engine->loop, UV_RUN_NOWAIT);
}

/*=============================================================================
 * Legacy API compatibility (ygui_engine_create with buffer)
 *===========================================================================*/

/* Keep old function signature working via macro or wrapper if needed */

/*=============================================================================
 * Widget Callbacks
 *===========================================================================*/

void ygui_button_on_click(ygui_widget_t *button, ygui_click_callback_t callback, void *userdata)
{
    if (!button || button->type != YGUI_WIDGET_BUTTON) {
        return;
    }
    button->click_callback = callback;
    button->click_userdata = userdata;
}

void ygui_slider_on_change(ygui_widget_t *slider, ygui_change_callback_t callback, void *userdata)
{
    if (!slider || slider->type != YGUI_WIDGET_SLIDER) {
        return;
    }
    slider->change_callback = callback;
    slider->change_userdata = userdata;
}

void ygui_checkbox_on_change(ygui_widget_t *checkbox, ygui_check_callback_t callback,
                             void *userdata)
{
    if (!checkbox || checkbox->type != YGUI_WIDGET_CHECKBOX) {
        return;
    }
    checkbox->check_callback = callback;
    checkbox->check_userdata = userdata;
}

void ygui_textinput_on_change(ygui_widget_t *input, ygui_text_callback_t callback, void *userdata)
{
    if (!input || input->type != YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    input->text_callback = callback;
    input->text_userdata = userdata;
}

/*=============================================================================
 * Engine Clear (removes all widgets)
 *===========================================================================*/

void ygui_engine_clear(ygui_engine_t *engine)
{
    if (!engine) {
        return;
    }

    /* Free all widgets */
    ygui_widget_t *w = engine->first_widget;
    while (w) {
        ygui_widget_t *next = w->next_sibling;
        ygui_widget_free(w);
        w = next;
    }

    engine->first_widget = NULL;
    engine->last_widget = NULL;
    engine->widget_count = 0;
    engine->hovered = NULL;
    engine->pressed = NULL;
    engine->focused = NULL;

    ygui_grid_clear(&engine->grid);
    engine->dirty = 1;
}

/*=============================================================================
 * Deprecated/Legacy Functions
 *===========================================================================*/

void ygui_engine_subscribe_clicks(ygui_engine_t *engine, int enable)
{
    if (!engine) {
        return;
    }
    if (enable && !engine->clicks_subscribed) {
        ygui_osc_subscribe_clicks(1);
        engine->clicks_subscribed = 1;
    } else if (!enable && engine->clicks_subscribed) {
        ygui_osc_subscribe_clicks(0);
        engine->clicks_subscribed = 0;
    }
}

void ygui_engine_subscribe_moves(ygui_engine_t *engine, int enable)
{
    if (!engine) {
        return;
    }
    if (enable && !engine->moves_subscribed) {
        ygui_osc_subscribe_moves(1);
        engine->moves_subscribed = 1;
    } else if (!enable && engine->moves_subscribed) {
        ygui_osc_subscribe_moves(0);
        engine->moves_subscribed = 0;
    }
}

/* Rebuild without render (for internal use) */
void ygui_engine_rebuild(ygui_engine_t *engine)
{
    engine_rebuild(engine);
}

/*=============================================================================
 * Resize Handling API
 *===========================================================================*/

void ygui_engine_set_canvas_mode(ygui_engine_t *engine, ygui_canvas_mode_t mode)
{
    if (engine) {
        engine->canvas_mode = mode;
    }
}

void ygui_engine_set_scale_mode(ygui_engine_t *engine, ygui_scale_mode_t mode)
{
    if (engine) {
        engine->scale_mode = mode;
    }
}

void ygui_engine_on_resize(ygui_engine_t *engine, ygui_resize_callback_t callback, void *userdata)
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

float ygui_engine_get_zoom(const ygui_engine_t *engine)
{
    return engine ? engine->view_zoom : 1.0f;
}

float ygui_engine_get_scroll_x(const ygui_engine_t *engine)
{
    return engine ? engine->view_scroll_x : 0.0f;
}

float ygui_engine_get_scroll_y(const ygui_engine_t *engine)
{
    return engine ? engine->view_scroll_y : 0.0f;
}

/*=============================================================================
 * View Change Subscription
 *===========================================================================*/

void ygui_engine_subscribe_view_changes(ygui_engine_t *engine, int enable)
{
    if (!engine) {
        return;
    }
    if (enable && !engine->view_subscribed) {
        ygui_osc_subscribe_view_changes(1);
        engine->view_subscribed = 1;
    } else if (!enable && engine->view_subscribed) {
        ygui_osc_subscribe_view_changes(0);
        engine->view_subscribed = 0;
    }
}

/*=============================================================================
 * View Control API (app → yetty)
 *===========================================================================*/

void ygui_engine_set_zoom(ygui_engine_t *engine, float level)
{
    if (!engine || !engine->card_name) {
        return;
    }
    ygui_osc_zoom_card(engine->card_name, level);
}

void ygui_engine_scroll_to(ygui_engine_t *engine, float x, float y)
{
    if (!engine || !engine->card_name) {
        return;
    }
    ygui_osc_scroll_card(engine->card_name, x, y, 1);
}

void ygui_engine_scroll_by(ygui_engine_t *engine, float dx, float dy)
{
    if (!engine || !engine->card_name) {
        return;
    }
    ygui_osc_scroll_card(engine->card_name, dx, dy, 0);
}
