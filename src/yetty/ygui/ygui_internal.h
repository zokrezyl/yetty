/*
 * ygui_internal.h - Internal structures for YGui-C
 */

#ifndef YGUI_INTERNAL_H
#define YGUI_INTERNAL_H

#include <yetty/ygui/ygui.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

/*=============================================================================
 * Forward Declarations
 *===========================================================================*/

struct yetty_ygui_render_ctx;
typedef struct ygui_input_event ygui_input_event_t;

/*=============================================================================
 * Render Context (drawing target = a ypaint-core buffer). The shim that used
 * to sit here (ydraw-capi.gen.*) is gone; widgets call yetty_ysdf_add_* and
 * yetty_ypaint_core_buffer_add_text directly.
 *===========================================================================*/

struct yetty_ygui_render_ctx {
    struct yetty_ypaint_core_buffer *buffer;
    const struct yetty_ygui_theme *theme;
    float offset_x, offset_y;
    float clip_x, clip_y, clip_w, clip_h;
    int has_clip;
};

/*=============================================================================
 * Theme Structure
 *===========================================================================*/

struct yetty_ygui_theme {
    /* Spacing */
    float pad_small;
    float pad_medium;
    float pad_large;

    /* Corner radius */
    float radius_small;
    float radius_medium;
    float radius_large;

    /* Sizing */
    float row_height;
    float scrollbar_size;
    float scroll_speed;
    float font_size;
    float separator_size;

    /* Colors (ABGR format) */
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_surface;
    uint32_t bg_hover;
    uint32_t bg_header;
    uint32_t bg_dropdown;
    uint32_t border;
    uint32_t border_light;
    uint32_t border_muted;
    uint32_t text_primary;
    uint32_t text_muted;
    uint32_t accent;
    uint32_t thumb_normal;
    uint32_t thumb_hover;
    uint32_t overlay_modal;
    uint32_t shadow;
    uint32_t tooltip_bg;
    uint32_t selection_bg;
};

/*=============================================================================
 * Widget Callbacks (per-widget)
 *===========================================================================*/

typedef void (*ygui_widget_click_fn)(struct yetty_ygui_widget *widget, void *userdata);
typedef void (*ygui_widget_change_fn)(struct yetty_ygui_widget *widget, float value,
                                      void *userdata);
typedef void (*ygui_widget_text_fn)(struct yetty_ygui_widget *widget, const char *text,
                                    void *userdata);
typedef void (*ygui_widget_check_fn)(struct yetty_ygui_widget *widget, int checked, void *userdata);

/*=============================================================================
 * Widget Function Pointers (internal rendering/events)
 *===========================================================================*/

typedef struct yetty_ycore_void_result (*ygui_widget_render_fn)(struct yetty_ygui_widget *self,
                                                                struct yetty_ygui_render_ctx *ctx);
typedef struct yetty_ycore_void_result (*ygui_widget_render_all_fn)(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx);
typedef int (*ygui_widget_on_press_fn)(struct yetty_ygui_widget *self, float lx, float ly,
                                       ygui_event_t *out);
typedef int (*ygui_widget_on_release_fn)(struct yetty_ygui_widget *self, float lx, float ly,
                                         ygui_event_t *out);
typedef int (*ygui_widget_on_drag_fn)(struct yetty_ygui_widget *self, float lx, float ly,
                                      ygui_event_t *out);
typedef int (*ygui_widget_on_scroll_fn)(struct yetty_ygui_widget *self, float dx, float dy,
                                        ygui_event_t *out);
typedef int (*ygui_widget_on_key_fn)(struct yetty_ygui_widget *self, uint32_t key, int mods,
                                     ygui_event_t *out);
typedef void (*ygui_widget_destroy_fn)(struct yetty_ygui_widget *self);

/*=============================================================================
 * Widget Structure
 *===========================================================================*/

struct yetty_ygui_widget {
    /* Identity */
    char *id;
    ygui_widget_type_t type;

    /* Authored geometry — input to the layout pass; set by constructors and
     * by yetty_ygui_widget_set_position/set_size. Never mutated by the engine. */
    float authored_x, authored_y, authored_w, authored_h;

    /* Live geometry — output of the layout pass.
     *   x, y      : relative to immediate parent (or absolute for top-level)
     *   w, h      : resolved size after flex grow/shrink/stretch
     *   layout_*  : absolute resolved box (used by spatial grid / hit test)
     *   content_* : inner box after padding (for future scroll/clip)
     *   effective_x, effective_y: legacy alias for layout_x/layout_y. */
    float x, y, w, h;
    float layout_x, layout_y, layout_w, layout_h;
    float content_x, content_y, content_w, content_h;
    float effective_x, effective_y;
    int was_rendered;

    /* Layout (flexbox-style). Zero-initialized = MANUAL mode (default). */
    struct yetty_ygui_layout layout;

    /* State */
    uint32_t flags;

    /* Styling */
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t accent_color;

    /* Hierarchy (linked list) */
    struct yetty_ygui_engine *engine;
    struct yetty_ygui_widget *parent;
    struct yetty_ygui_widget *first_child;
    struct yetty_ygui_widget *last_child;
    struct yetty_ygui_widget *next_sibling;
    struct yetty_ygui_widget *prev_sibling;

    /* Internal virtual functions */
    ygui_widget_render_fn render;
    ygui_widget_render_all_fn render_all;
    ygui_widget_on_press_fn on_press;
    ygui_widget_on_release_fn on_release;
    ygui_widget_on_drag_fn on_drag;
    ygui_widget_on_scroll_fn on_scroll;
    ygui_widget_on_key_fn on_key;
    ygui_widget_destroy_fn destroy;

    /* User callbacks */
    ygui_widget_click_fn click_callback;
    void *click_userdata;
    ygui_widget_change_fn change_callback;
    void *change_userdata;
    ygui_widget_text_fn text_callback;
    void *text_userdata;
    ygui_widget_check_fn check_callback;
    void *check_userdata;

    /* Widget-specific data */
    union {
        struct {
            char *label;
        } button;

        struct {
            char *text;
            float font_size;
        } label;

        struct {
            float value;
            float min_val;
            float max_val;
        } slider;

        struct {
            char *label;
            int checked;
        } checkbox;

        struct {
            char *text;
            char *placeholder;
            int cursor_pos;
            int selection_start;
            int selection_end;
        } textinput;

        struct {
            float scroll_x, scroll_y;
            float content_w, content_h;
            float header_h;
            float corner_radius;
            int dragging_scrollbar;
            int dragging_window;
            float drag_start_y;
            float drag_start_scroll_y;
        } panel;

        struct {
            char **options;
            int option_count;
            int selected;
            int open;
        } dropdown;

        struct {
            float value;
        } progress;

        struct {
            float hue, sat, val, alpha;
        } colorpicker;

        struct {
            char *label;
            int modal;
            uint32_t header_color; /* 0 = use theme bg_header */
            float scene_w, scene_h;
        } popup;

        struct {
            char *label;
        } collapsing_header;

        struct {
            char *label;
        } tooltip;

        struct {
            char *label;
        } selectable;

        struct {
            char **options;
            int option_count;
            int selected;
            int hover_index;
        } choicebox;

        struct {
            float value; /* 0..1 */
        } scrollbar;
    } data;
};

/*=============================================================================
 * Spatial Grid
 *===========================================================================*/

typedef struct {
    struct yetty_ygui_widget **widgets;
    int count;
    int capacity;
} ygui_grid_cell_t;

typedef struct {
    ygui_grid_cell_t *cells;
    int cols, rows;
    float cell_size;
    float width, height;
} ygui_spatial_grid_t;

/*=============================================================================
 * Engine Structure
 *===========================================================================*/

struct yetty_ygui_engine {
    /* ypaint-core buffer (created and owned by engine). Widgets add primitives
     * via yetty_ysdf_* and text via yetty_ypaint_core_buffer_add_text. */
    struct yetty_ypaint_core_buffer *buffer;

    /* Raster font in metrics-only mode — used for ygui_text_width() and widget
     * layout. Opened in engine_alloc_init and reused for every render. See
     * ypdf's pdf-renderer.c for the pattern (raster_font_create_from_file with
     * shader_path=NULL). Lazily reopened if the path changes. */
    struct yetty_ypaint_font *measure_font;
    float measure_base_size; /* The font's base_size; measurement scales from here. */

    /* Spatial grid for hit testing */
    ygui_spatial_grid_t grid;

    /* Theme */
    struct yetty_ygui_theme *theme;
    int owns_theme;

    /* Widget storage */
    struct yetty_ygui_widget *first_widget;
    struct yetty_ygui_widget *last_widget;
    int widget_count;

    /* Interaction state */
    struct yetty_ygui_widget *hovered;
    struct yetty_ygui_widget *pressed;
    struct yetty_ygui_widget *focused;

    /* Legacy event callback (for backwards compat) */
    ygui_event_callback_t event_callback;
    void *event_userdata;

    /* Keyboard callback */
    ygui_key_callback_t key_callback;
    void *key_userdata;

    /* Size in pixels (widget coordinate system).
     * prev_width / prev_height carry the canvas size from before the most
     * recent change so the resize callback can pass both old and new
     * dimensions. They're 0 until the first resize lands. */
    float width, height;
    float prev_width, prev_height;
    float cell_width, cell_height;

    /* Card info for OSC output */
    char *card_name;
    int card_x, card_y, card_w, card_h;
    int card_shown;   /* 0 = not shown yet, 1 = shown (use update) */
    uint32_t card_id; /* ymgui-layer card id (for CARD_PLACE / hit routing) */

    /* Long-lived yface for parsing inbound binary OSC envelopes
     * (YMGUI_OSC_SC_MOUSE / RESIZE / FOCUS / KEY). */
    struct yetty_yface *yface_in;

    /* State */
    int dirty;
    int running;

    /* libuv event loop */
    uv_loop_t *loop;
    int owns_loop; /* 1 if we created the loop */
    int input_fd;  /* Input file descriptor (default: STDIN_FILENO) */
    int output_fd; /* Output file descriptor (default: STDOUT_FILENO) */
    uv_poll_t stdin_poll;
    uv_prepare_t prepare_handle; /* For auto-render before polling */

    /* Input buffer for parsing */
    char input_buffer[4096];
    int input_len;

    /* Event subscriptions */
    int clicks_subscribed;
    int moves_subscribed;
    int view_subscribed;

    /* View state (from OSC 777779) */
    float view_zoom;     /* Current zoom level (1.0 = 100%) */
    float view_scroll_x; /* Current scroll offset in canvas pixels */
    float view_scroll_y;

    /* Resize handling */
    ygui_canvas_mode_t canvas_mode;
    ygui_scale_mode_t scale_mode;
    float reference_w; /* Initial display size for scaling */
    float reference_h;
    float display_pixel_w; /* Direct pixel dimensions from OSC 777780 */
    float display_pixel_h;
    int have_pixel_size;  /* 1 if we received OSC 777780 */
    int needs_resize;     /* 1 if resize should happen before next render */
    int had_first_resize; /* 1 after first resize in CANVAS_FIT mode */

    /* Resize callback */
    ygui_resize_callback_t resize_callback;
    void *resize_userdata;
};

/*=============================================================================
 * Internal Functions
 *===========================================================================*/

/* Memory helpers */
static inline char *ygui_strdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

/* Spatial grid */
void yetty_ygui_grid_init(ygui_spatial_grid_t *grid, float width, float height, float cell_size);
void yetty_ygui_grid_destroy(ygui_spatial_grid_t *grid);
void yetty_ygui_grid_clear(ygui_spatial_grid_t *grid);
void yetty_ygui_grid_insert(ygui_spatial_grid_t *grid, struct yetty_ygui_widget *widget);
void yetty_ygui_grid_remove(ygui_spatial_grid_t *grid, struct yetty_ygui_widget *widget);
struct yetty_ygui_widget *yetty_ygui_grid_query(const ygui_spatial_grid_t *grid, float x, float y);

/* Widget helpers */
struct yetty_ygui_widget *yetty_ygui_engine_widget_alloc(struct yetty_ygui_engine *engine,
                                                         ygui_widget_type_t type, const char *id);
void yetty_ygui_widget_free(struct yetty_ygui_widget *widget);
void yetty_ygui_widget_init_base(struct yetty_ygui_widget *widget, float x, float y, float w,
                                 float h);

/* Render context */
void yetty_ygui_render_ctx_init(struct yetty_ygui_render_ctx *ctx,
                                struct yetty_ypaint_core_buffer *buffer,
                                const struct yetty_ygui_theme *theme);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box(struct yetty_ygui_render_ctx *ctx,
                                                                float x, float y, float w, float h,
                                                                uint32_t color, float radius);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_outline(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, uint32_t color,
    float radius, float stroke_width);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_text(struct yetty_ygui_render_ctx *ctx,
                                                                 const char *text, float x, float y,
                                                                 uint32_t color, float font_size);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_circle(
    struct yetty_ygui_render_ctx *ctx, float cx, float cy, float r, uint32_t color);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_circle_outline(
    struct yetty_ygui_render_ctx *ctx, float cx, float cy, float r, uint32_t color,
    float stroke_width);
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_triangle(
    struct yetty_ygui_render_ctx *ctx, float x0, float y0, float x1, float y1, float x2, float y2,
    uint32_t color);

/* Default widget functions */
struct yetty_ycore_void_result yetty_ygui_widget_render_all_default(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx);

/* Layout pass — runs before rendering inside engine_rebuild. Resolves
 * authored geometry into live geometry (x/y/w/h) plus absolute layout_*
 * boxes that the spatial grid uses for hit testing. See ygui_layout.c. */
struct yetty_ycore_void_result yetty_ygui_layout_compute_engine(struct yetty_ygui_engine *engine);

/* OSC output (ygui_osc.c) */
struct yetty_ycore_void_result yetty_ygui_osc_create_card(const char *name, int x, int y, int w,
                                                          int h, const uint8_t *data,
                                                          uint32_t size);
struct yetty_ycore_void_result yetty_ygui_osc_update_card(const char *name, const uint8_t *data,
                                                          uint32_t size);
void yetty_ygui_osc_kill_card(const char *name);
void yetty_ygui_osc_subscribe_clicks(int enable);
void yetty_ygui_osc_subscribe_moves(int enable);
void yetty_ygui_osc_subscribe_view_changes(int enable);
void yetty_ygui_osc_query_cell_size(void);
struct yetty_ycore_void_result yetty_ygui_osc_card_place(uint32_t card_id, int col, int row,
                                                         uint32_t w_cells, uint32_t h_cells);
struct yetty_ycore_void_result yetty_ygui_osc_card_remove(uint32_t card_id);
void yetty_ygui_osc_zoom_card(const char *name, float level);
void yetty_ygui_osc_scroll_card(const char *name, float x, float y, int absolute);
void yetty_ygui_osc_scroll_card_delta(const char *name, float dx, float dy);

/* Error */
void yetty_ygui_set_error(const char *msg);

/* Math helpers */
static inline float ygui_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float ygui_max(float a, float b)
{
    return a > b ? a : b;
}

static inline float ygui_min(float a, float b)
{
    return a < b ? a : b;
}

#endif /* YGUI_INTERNAL_H */
