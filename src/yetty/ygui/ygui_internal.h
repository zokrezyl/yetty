/*
 * ygui_internal.h - Internal structures for YGui-C
 */

#ifndef YGUI_INTERNAL_H
#define YGUI_INTERNAL_H

#include <yetty/ygui/ygui.h>
#include <yetty/ydraw-core/buffer.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <stdlib.h>
#include <string.h>

/* No <uv.h> here — the libuv-driven event-loop integration lives in
 * ygui_engine_uv.c (the `ygui` static library, layered on top of
 * `ygui-core`). The engine struct carries an opaque uv_state pointer
 * + a destroy hook so ygui-core can still build/own/destroy an engine
 * without pulling libuv into its translation units. Targets that need
 * the libuv-driven engine_run/attach/poll API link `ygui` (full); the
 * webasm pipeline only consumes ygui-core. */

/*=============================================================================
 * Forward Declarations
 *===========================================================================*/

struct yetty_ygui_render_ctx;
typedef struct ygui_input_event ygui_input_event_t;

/* Forward-declare to avoid pulling yplatform/pty.h into widget-heavy includes. */
struct yetty_platform_pty;

/*=============================================================================
 * Render Context (drawing target = a ydraw-core buffer). The shim that used
 * to sit here (ydraw-capi.gen.*) is gone; widgets call yetty_ysdf_add_* and
 * yetty_ydraw_core_buffer_add_text directly.
 *===========================================================================*/

struct yetty_ygui_render_ctx {
    struct yetty_ydraw_core_buffer *buffer;
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

    /* Elevation (Material-inspired soft drop shadow).
     *
     * Each level is the vertical offset of the cast shadow in pixels;
     * shadow_alpha is its base opacity. Set elevation_* to 0 to fall back
     * to the legacy flat look. The actual shadow color is `shadow` —
     * elevation_alpha multiplies its alpha channel. */
    float elevation_low;    /* default 1.5px — buttons, list rows */
    float elevation_medium; /* default 4.0px — dropdowns, tooltips */
    float elevation_high;   /* default 8.0px — popups, modals */
    float elevation_alpha;  /* default 0.55 — global multiplier on shadow.alpha */
    int enable_gradient;    /* 0 = flat surface, 1 = subtle vertical gradient on buttons */

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
 * Widget vtable — per-type behavior, shared across all instances of a type.
 *
 * Every field is optional (NULL = "no handler"). Each widget's `vtable`
 * pointer is `NULL` for trivial types (e.g. hbox / vbox carry no custom
 * behavior beyond the layout pass) or points to a single static
 * `<type>_vtable` defined alongside the type's render functions in
 * ygui_widgets.c.
 *===========================================================================*/

/* Distance from the widget's top edge to its first text baseline, in
 * resolved pixels. Used by ALIGN_BASELINE in the flex layout pass.
 * Widgets without text return their height (or anything sensible) — the
 * layout falls back to start-alignment when only some children expose a
 * baseline. */
typedef float (*ygui_widget_baseline_fn)(const struct yetty_ygui_widget *self,
                                         const struct yetty_ygui_theme *theme);

struct yetty_ygui_widget_vtable {
    ygui_widget_render_fn render;
    ygui_widget_render_all_fn render_all;
    ygui_widget_on_press_fn on_press;
    ygui_widget_on_release_fn on_release;
    ygui_widget_on_drag_fn on_drag;
    ygui_widget_on_scroll_fn on_scroll;
    ygui_widget_on_key_fn on_key;
    ygui_widget_destroy_fn destroy;
    /* Optional. NULL = widget has no meaningful baseline; layout falls
     * back to ALIGN_START. */
    ygui_widget_baseline_fn baseline_offset;
};

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

    /* Per-type behavior — points at one of the static <type>_vtable
     * structs in ygui_widgets.c. NULL is allowed (e.g. layout-only
     * containers like hbox / vbox). All eight type-level fn pointers used
     * to live inline in the widget; folding them into a per-type vtable
     * saved 56 bytes per instance and matches the project design rule of
     * "vtable pattern with structural embedding". */
    const struct yetty_ygui_widget_vtable *vtable;

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

        struct {
            /* selected child (pointer; not an index, so it survives
             * insertion/removal of other children). NULL = no selection. */
            struct yetty_ygui_widget *selected;
            ygui_widget_click_fn on_select_internal; /* unused for now */
            ygui_widget_click_fn on_select;
            void *on_select_userdata;
        } list;

        struct {
            char *label;
            int expanded;
            /* The auto-allocated children list. Always present (created
             * in the constructor); rendered only when `expanded` AND it
             * has visible children. Owned via the normal child-widget
             * hierarchy so destroy/free propagates. */
            struct yetty_ygui_widget *children_list;
            ygui_widget_check_fn on_toggle;
            void *on_toggle_userdata;
        } tree_node;

        struct {
            int n_columns;
            char **column_names;  /* size n_columns */
            float *column_widths; /* size n_columns; 0 = stretch */
            int n_rows;
            int row_capacity;
            char ***rows;     /* rows[i][j] (owned strings) */
            int selected_row; /* -1 = none */
            float row_height; /* 0 = use theme->row_height */
            void (*on_select)(struct yetty_ygui_widget *table, int row, void *userdata);
            void *on_select_userdata;
        } table;

        struct {
            /* Owned ydraw-core buffer. NULL while empty. Authors fill the
             * primitives in widget-local coordinates (0..w, 0..h); the
             * widget's render translates them to absolute canvas coords. */
            struct yetty_ydraw_core_buffer *buffer;
        } rich;

        struct {
            /* Title text (owned, NUL-terminated, NULL when chromeless). */
            char *title;
            /* Title-bar height in pixels. 0 = derive from theme. */
            float title_h;
            /* Auto-allocated inner body container (a vbox). Lives in
             * the regular child hierarchy, so destroy/free propagates. */
            struct yetty_ygui_widget *body;
            /* Optional notify-only callback fired on close click. */
            ygui_widget_click_fn on_close;
            void *on_close_userdata;
            /* Optional popup menu opened by the hamburger button. When
             * NULL the hamburger acts as a direct close button (legacy
             * behaviour). The menu is OWNED by the engine via the
             * regular top-level widget list; the window only holds a
             * reference. */
            struct yetty_ygui_widget *menu;
        } window;

        struct {
            /* Per-row label (owned). NULL slot = separator row (drawn
             * as a thin divider, not clickable). */
            char **item_labels;
            /* Per-row click callback. NULL = item is disabled / a
             * pure separator. */
            ygui_widget_click_fn *item_callbacks;
            void **item_userdata;
            int n_items;
            int capacity;
            /* Row height in pixels. 0 = derive from theme. */
            float item_h;
            int modal;
            /* Index of the currently-hovered row (for highlight). -1
             * when the cursor isn't over any clickable row. */
            int hover_index;
        } popup_menu;

        struct {
            /* Per-tab header label strings (owned, NUL-terminated). */
            char **labels;
            /* Per-tab content panel widgets (owned via the normal child
             * hierarchy — destroy/free propagates). The active panel is
             * the only one rendered; others are made invisible. */
            struct yetty_ygui_widget **panels;
            int n_tabs;
            int capacity;
            int active;
            /* Header strip height in pixels. 0 = derive from theme. */
            float header_h;
            /* Fired when a tab's close 'x' button is clicked, BEFORE
             * the default remove_tab path runs. `value` carries the
             * tab index. Signature reuses change_callback_t for
             * convenience — close-events deliver an index as a float
             * the same way tab-switch events do. */
            ygui_widget_change_fn on_tab_close;
            void *on_tab_close_userdata;
        } tabbar;
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
    /* ydraw-core buffer (created and owned by engine). Widgets add primitives
     * via yetty_ysdf_* and text via yetty_ydraw_core_buffer_add_text. */
    struct yetty_ydraw_core_buffer *buffer;

    /* Raster font in metrics-only mode — used for ygui_text_width() and widget
     * layout. Opened in engine_alloc_init and reused for every render. See
     * ypdf's pdf-renderer.c for the pattern (raster_font_create_from_file with
     * shader_path=NULL). Lazily reopened if the path changes. */
    struct yetty_ydraw_font *measure_font;
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

    /* When set, engine_destroy skips the YDRAW_CLEAR OSC so the last
     * rendered frame stays painted on the canvas (the ydraw primitives
     * remain in the scrolling layer's buffer). The ymgui card-remove
     * still fires — input routing detaches, but visuals persist. Used
     * by the WINDOW widget's close button so "X" gives users a graceful
     * exit that leaves their final view in place. */
    int preserve_canvas_on_destroy;

    /* Long-lived yface for parsing inbound binary OSC envelopes
     * (YMGUI_OSC_SC_MOUSE / RESIZE / FOCUS / KEY). */
    struct yetty_yface *yface_in;

    /* State */
    int dirty;
    int running;

    /* Opaque libuv-side state. NULL on ygui-core-only builds (webasm,
     * non-interactive embedders). ygui_engine_uv.c — compiled into the
     * full `ygui` static library — allocates a `struct ygui_uv_state`
     * (uv_loop_t, owns_loop, uv_poll_t, uv_prepare_t) and stashes the
     * pointer here in yetty_ygui_engine_attach. uv_state_destroy_cb is
     * the corresponding teardown hook called from engine_destroy when
     * non-NULL. */
    void *uv_state;
    void (*uv_state_destroy_cb)(struct yetty_ygui_engine *engine);

    int input_fd;  /* Input file descriptor (default: STDIN_FILENO) */
    int output_fd; /* Output file descriptor (default: STDOUT_FILENO) */

    /* In-process sink override. When non-NULL, ydraw frame envelopes
     * go through pty->ops->write instead of write(output_fd). Used by
     * yetty's own yui chrome to avoid a stdout round-trip when ygui
     * lives in the same process as the renderer. NULL = fall back to
     * `output_fd` (default client-mode behaviour). */
    struct yetty_platform_pty *output_pty;

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

    /* Last-emitted OSC frame cache for dedup. The dirty flag fires on
     * mouse-hover updates, view-changes, focus, etc. — many of those
     * leave the rendered byte stream unchanged, but we'd still re-emit a
     * multi-MB envelope (e.g. an Images-tab frame carrying a 2.5 MB
     * yimage prim) and the receiver would tear down + re-create the
     * complex-prim instance, causing a visible blink. Cache the bytes
     * of the most recently sent envelope; if the next render produces
     * an identical byte stream, skip the OSC write entirely. */
    uint8_t *prev_emit_data;
    uint32_t prev_emit_size;
    uint32_t prev_emit_cap;

    /* Toast-style notification stack — top-right of the canvas. See
     * yetty_ygui_engine_notify in the public header for semantics. The
     * array is a small fixed cap; pushes past the cap drop the oldest
     * notification to make room (last-in-wins is the standard pattern
     * for transient toast stacks). Each entry owns its message string
     * and a `card` widget tree on the engine; dismiss frees both. */
    struct ygui_notification {
        char    *message;            /* owned (strdup) */
        int      severity;           /* enum yetty_ygui_severity */
        uint64_t created_ms;         /* clock_gettime(CLOCK_MONOTONIC) */
        uint32_t ttl_ms;             /* 0 = sticky */
        struct yetty_ygui_widget *card;
        struct yetty_ygui_widget *close_btn;
    } notifications[8];
    int notification_count;
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
                                struct yetty_ydraw_core_buffer *buffer,
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

/* Soft drop shadow: stacks three slightly larger, semi-transparent rounded
 * boxes behind the surface to fake gaussian falloff using only the existing
 * SDF box primitive. `elevation` is the vertical offset of the deepest
 * shadow layer (in pixels) — use one of theme->elevation_low/medium/high.
 * `alpha_mul` multiplies theme->shadow's alpha channel (typically
 * theme->elevation_alpha; pass 0 to disable the shadow entirely). */
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_shadow(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius,
    float elevation, uint32_t shadow_color, float alpha_mul);

/* Linear-gradient rounded box. The gradient axis is defined in the same
 * (x,y) coordinate system as the box itself: (gx0, gy0) → (gx1, gy1).
 * For a vertical top→bottom gradient on a button, pass
 * (gx0, gy0) = (0, y) and (gx1, gy1) = (0, y + h). color0 is sampled at
 * the start of the axis, color1 at the end; pixels off-axis are
 * projected. Both colors must be ABGR (same packing as fill_color). */
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_linear_gradient(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius, float gx0,
    float gy0, float gx1, float gy1, uint32_t color0, uint32_t color1);

/* Radial gradient inside a rounded box. The gradient origin is at
 * (cx, cy) and fades over `gradient_radius` pixels. `color_inner` is
 * sampled at the origin, `color_outer` at the radius; both clamped. */
struct yetty_ycore_void_result yetty_ygui_render_ctx_render_box_radial_gradient(
    struct yetty_ygui_render_ctx *ctx, float x, float y, float w, float h, float radius, float cx,
    float cy, float gradient_radius, uint32_t color_inner, uint32_t color_outer);

/* Default widget functions */
struct yetty_ycore_void_result yetty_ygui_widget_render_all_default(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx);

/* Layout pass — runs before rendering inside engine_rebuild. Resolves
 * authored geometry into live geometry (x/y/w/h) plus absolute layout_*
 * boxes that the spatial grid uses for hit testing. See ygui_layout.c. */
struct yetty_ycore_void_result yetty_ygui_layout_compute_engine(struct yetty_ygui_engine *engine);

/* OSC output (ygui_osc.c).
 *
 * `output_pty` is optional — when non-NULL, the binary OSC envelope is
 * written through pty->ops->write (zero-copy, no fd, used by yetty's
 * own in-process yui chrome). When NULL, falls back to writing the
 * full envelope to STDOUT_FILENO (default client-mode path). */
struct yetty_ycore_void_result yetty_ygui_osc_create_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, int x, int y, int w,
                                                          int h, const uint8_t *data,
                                                          uint32_t size);
struct yetty_ycore_void_result yetty_ygui_osc_update_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, const uint8_t *data,
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

/* Internal helpers shared between the ygui-core (libuv-free) and the
 * libuv-driven runtime in ygui_engine_uv.c. Not part of the public API
 * — `yetty_ygui_internal_` prefix makes the intent obvious. */
void yetty_ygui_internal_process_input(struct yetty_ygui_engine *engine,
                                       const char *data, int len);
void yetty_ygui_internal_yface_on_osc(void *user, int osc_code,
                                      const uint8_t *args, size_t args_len,
                                      const uint8_t *payload, size_t payload_len);
void yetty_ygui_internal_yface_on_raw(void *user, const char *bytes, size_t n);
extern volatile int yetty_ygui_internal_resize_pending;
extern struct yetty_ygui_engine *yetty_ygui_internal_active_engine;

/* Notification lifecycle hooks (ygui_notify.c).
 *   _tick      — drop expired toasts; called at the top of engine_render
 *                so the resulting frame already reflects the compacted stack.
 *   _on_resize — re-anchor the stack to the new right edge.
 *   _shutdown  — free message strings on engine destroy (card widget
 *                trees go down with the engine's widget list). */
void yetty_ygui_engine_notify_tick(struct yetty_ygui_engine *engine);
void yetty_ygui_engine_notify_on_resize(struct yetty_ygui_engine *engine);
void yetty_ygui_engine_notify_shutdown(struct yetty_ygui_engine *engine);

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
