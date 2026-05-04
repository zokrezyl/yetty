/*
 * ygui.h - YGui C API
 *
 * Self-contained widget library for yetty terminal.
 * Handles: widgets, events (libuv), input parsing, OSC output.
 * Works from any language via FFI.
 */

#ifndef YGUI_H
#define YGUI_H

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>

/* Forward declare libuv types */
typedef struct uv_loop_s uv_loop_t;

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Library Initialization
 *===========================================================================*/

/* Initialize the library (sets up raw terminal mode, signal handlers)
 * Must be called before any other ygui function.
 * Returns 0 on success, -1 on error. */
int yetty_ygui_init(void);

/* Shutdown the library (restores terminal settings)
 * Should be called before exit. */
void yetty_ygui_shutdown(void);

/*=============================================================================
 * Opaque Types
 *===========================================================================*/

struct yetty_ygui_engine;
YETTY_YRESULT_DECLARE(ygui_engine_ptr, struct yetty_ygui_engine *);
struct yetty_ygui_widget;
struct yetty_ygui_theme;

/*=============================================================================
 * Enums
 *===========================================================================*/

typedef enum {
    YETTY_YGUI_WIDGET_BUTTON,
    YETTY_YGUI_WIDGET_LABEL,
    YETTY_YGUI_WIDGET_SLIDER,
    YETTY_YGUI_WIDGET_CHECKBOX,
    YETTY_YGUI_WIDGET_TEXTINPUT,
    YETTY_YGUI_WIDGET_PANEL,
    YETTY_YGUI_WIDGET_HBOX,
    YETTY_YGUI_WIDGET_VBOX,
    YETTY_YGUI_WIDGET_DROPDOWN,
    YETTY_YGUI_WIDGET_LISTBOX,
    YETTY_YGUI_WIDGET_TABLE,
    YETTY_YGUI_WIDGET_TABBAR,
    YETTY_YGUI_WIDGET_COLORPICKER,
    YETTY_YGUI_WIDGET_SCROLLAREA,
    YETTY_YGUI_WIDGET_PROGRESS,
    YETTY_YGUI_WIDGET_SEPARATOR,
    YETTY_YGUI_WIDGET_POPUP,
    YETTY_YGUI_WIDGET_COLLAPSING_HEADER,
    YETTY_YGUI_WIDGET_TOOLTIP,
    YETTY_YGUI_WIDGET_SELECTABLE,
    YETTY_YGUI_WIDGET_CHOICEBOX,
    YETTY_YGUI_WIDGET_VSCROLLBAR,
    YETTY_YGUI_WIDGET_HSCROLLBAR,
    YETTY_YGUI_WIDGET_CUSTOM,
} ygui_widget_type_t;

typedef enum {
    YETTY_YGUI_EVENT_NONE = 0,
    YETTY_YGUI_EVENT_CLICK,
    YETTY_YGUI_EVENT_PRESS,
    YETTY_YGUI_EVENT_RELEASE,
    YETTY_YGUI_EVENT_CHANGE,
    YETTY_YGUI_EVENT_SCROLL,
    YETTY_YGUI_EVENT_FOCUS,
    YETTY_YGUI_EVENT_BLUR,
    YETTY_YGUI_EVENT_KEY,
    YETTY_YGUI_EVENT_TEXT,
} ygui_event_type_t;

typedef enum {
    YETTY_YGUI_FLAG_NONE = 0,
    YETTY_YGUI_FLAG_HOVER = 1 << 0,
    YETTY_YGUI_FLAG_PRESSED = 1 << 1,
    YETTY_YGUI_FLAG_FOCUSED = 1 << 2,
    YETTY_YGUI_FLAG_DISABLED = 1 << 3,
    YETTY_YGUI_FLAG_CHECKED = 1 << 4,
    YETTY_YGUI_FLAG_OPEN = 1 << 5,
    YETTY_YGUI_FLAG_VISIBLE = 1 << 6,
} ygui_flags_t;

/* Canvas mode: how canvas size relates to display size */
typedef enum {
    YETTY_YGUI_CANVAS_FIXED, /* Canvas size stays constant */
    YETTY_YGUI_CANVAS_FIT    /* Canvas size = card pixel size (card_cells * cell_pixels) */
} ygui_canvas_mode_t;

/* Widget scale mode: how widgets respond to canvas size changes.
 *
 * NOTE: After the layout-engine rewrite, SCALE_ON is currently a no-op.
 * Layout is recomputed from authored values on every resize instead of
 * destructively scaling widget geometry. A future iteration may feed a
 * scale factor back through the layout pass. */
typedef enum {
    YETTY_YGUI_SCALE_OFF, /* Widgets keep positions/sizes (may clip) */
    YETTY_YGUI_SCALE_ON   /* Widgets scale proportionally with canvas */
} ygui_scale_mode_t;

/*=============================================================================
 * Layout (flexbox-style)
 *===========================================================================*/

typedef enum {
    YETTY_YGUI_LAYOUT_MANUAL = 0, /* default: children at authored x/y/w/h */
    YETTY_YGUI_LAYOUT_FLEX        /* row/column flex container */
} ygui_layout_mode_t;

typedef enum {
    YETTY_YGUI_FLEX_ROW = 0,
    YETTY_YGUI_FLEX_COLUMN
} ygui_flex_direction_t;

typedef enum {
    YETTY_YGUI_JUSTIFY_START = 0,
    YETTY_YGUI_JUSTIFY_CENTER,
    YETTY_YGUI_JUSTIFY_END,
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN,
    YETTY_YGUI_JUSTIFY_SPACE_AROUND,
    YETTY_YGUI_JUSTIFY_SPACE_EVENLY
} ygui_justify_t;

typedef enum {
    /* AUTO is the calloc-zero default. As align_items it behaves like START.
     * As align_self it means "inherit parent's align_items". */
    YETTY_YGUI_ALIGN_AUTO = 0,
    YETTY_YGUI_ALIGN_START,
    YETTY_YGUI_ALIGN_CENTER,
    YETTY_YGUI_ALIGN_END,
    YETTY_YGUI_ALIGN_STRETCH,
    YETTY_YGUI_ALIGN_BASELINE   /* cross-axis: line up children by text baseline */
} ygui_align_t;

typedef enum {
    YETTY_YGUI_FLEX_NOWRAP = 0,
    YETTY_YGUI_FLEX_WRAP        /* break to new line when children overflow */
} ygui_flex_wrap_t;

typedef enum {
    YETTY_YGUI_POSITION_RELATIVE = 0, /* default — participates in flex flow */
    YETTY_YGUI_POSITION_ABSOLUTE      /* skipped by flex; positioned at authored x/y */
} ygui_position_t;

struct yetty_ygui_layout {
    ygui_layout_mode_t    mode;
    ygui_flex_direction_t direction;
    ygui_flex_wrap_t      wrap;
    ygui_justify_t        justify_content;
    ygui_align_t          align_items;
    ygui_align_t          align_self;
    ygui_align_t          align_content;   /* multi-line cross-axis alignment */
    ygui_position_t       position;

    float flex_grow;
    float flex_shrink;
    float flex_basis;           /* <= 0: use authored size on main axis */
    float flex_basis_percent;   /* > 0: percent of parent's main content size (overrides flex_basis) */

    float gap;
    float padding_top, padding_right, padding_bottom, padding_left;
    float margin_top, margin_right, margin_bottom, margin_left;

    float min_w, min_h;             /* 0: unset (absolute pixels) */
    float max_w, max_h;
    float min_w_percent, min_h_percent; /* 0: unset (% of parent content) */
    float max_w_percent, max_h_percent;

    /* Optional explicit width/height as percent of parent's content box.
     * 0 = unset. Applies on the *cross axis* for flex children, and on
     * both axes in MANUAL mode. */
    float width_percent, height_percent;
};

/*=============================================================================
 * Event Structure (for legacy callback)
 *===========================================================================*/

typedef struct {
    const char *widget_id;
    ygui_event_type_t type;
    union {
        float float_value;
        int32_t int_value;
        int bool_value;
        const char *string_value;
        struct {
            float r, g, b, a;
        } color;
        struct {
            float x, y;
        } scroll;
        struct {
            uint32_t key;
            int mods;
        } key;
    } data;
} ygui_event_t;

/*=============================================================================
 * Callback Types
 *===========================================================================*/

/* Legacy event callback (all events) */
typedef void (*ygui_event_callback_t)(const ygui_event_t *event, void *userdata);

/* Keyboard callback */
typedef void (*ygui_key_callback_t)(struct yetty_ygui_engine *engine, uint32_t key, int mods, void *userdata);

/* Widget-specific callbacks */
typedef void (*ygui_click_callback_t)(struct yetty_ygui_widget *widget, void *userdata);
typedef void (*ygui_change_callback_t)(struct yetty_ygui_widget *widget, float value, void *userdata);
typedef void (*ygui_text_callback_t)(struct yetty_ygui_widget *widget, const char *text, void *userdata);
typedef void (*ygui_check_callback_t)(struct yetty_ygui_widget *widget, int checked, void *userdata);

/* Resize callback — called whenever the canvas size changes (terminal
 * resize, cell-pixel-size change, or yetty_ygui_engine_set_size).
 *
 * The callback receives the new canvas size and the previous canvas size,
 * so handlers can react to actual changes without having to remember state
 * across calls. On the first call (initial size set after engine_show),
 * prev_w / prev_h are 0. */
typedef void (*ygui_resize_callback_t)(struct yetty_ygui_engine *engine, float new_w, float new_h,
                                       float prev_w, float prev_h, void *userdata);

/*=============================================================================
 * Engine API
 *===========================================================================*/

/* Create engine with card name, position, and size in terminal cells.
 * x, y: card position in terminal cells
 * cols, rows: card size in terminal cells
 * After show(), queries card pixel size (OSC 777780).
 * Canvas = actual card pixels (cols * cell_width, rows * cell_height).
 * Widgets are positioned in actual pixel coordinates. */
struct ygui_engine_ptr_result yetty_ygui_engine_create(const char *card_name, int x, int y, int cols,
                                                 int rows);

/* Create engine with pixel size hints.
 * x, y: card position in terminal cells
 * width_hint, height_hint: desired pixel size (calculates closest cols/rows)
 * Then same as ygui_engine_create: canvas = actual card pixels. */
struct ygui_engine_ptr_result yetty_ygui_engine_create_with_pixel_hint(const char *card_name, int x,
                                                                 int y, float width_hint,
                                                                 float height_hint);

/* Destroy engine (kills card, frees all resources) */
struct yetty_ycore_void_result yetty_ygui_engine_destroy(struct yetty_ygui_engine *engine);

/* Show card (creates it via OSC, queries pixel size).
 * Position and size were set in ygui_engine_create. */
struct yetty_ycore_void_result yetty_ygui_engine_show(struct yetty_ygui_engine *engine);

/* Render a frame (clear buffer → rebuild → serialize → send OSC)
 * Usually not needed - engine auto-renders when dirty. */
struct yetty_ycore_void_result yetty_ygui_engine_render(struct yetty_ygui_engine *engine);

/* Run only the layout pass — no rendering, no OSC.
 * After this call, every visible widget has resolved layout_x/y/w/h available
 * through yetty_ygui_widget_get_layout_box(). Useful for tests, headless
 * inspection, and tools that want to query post-flex geometry without
 * triggering a render. */
struct yetty_ycore_void_result yetty_ygui_engine_layout(struct yetty_ygui_engine *engine);

/* Attach engine to user's libuv loop (for advanced usage) */
void yetty_ygui_engine_attach(struct yetty_ygui_engine *engine, uv_loop_t *loop);

/* Run event loop (creates libuv loop internally for simple usage)
 * Blocks until ygui_engine_stop() called or 'q' pressed. */
void yetty_ygui_engine_run(struct yetty_ygui_engine *engine);

/* Stop the event loop */
void yetty_ygui_engine_stop(struct yetty_ygui_engine *engine);

/* Configuration */
void yetty_ygui_engine_set_size(struct yetty_ygui_engine *engine, float width, float height);
void yetty_ygui_engine_get_size(const struct yetty_ygui_engine *engine, float *width, float *height);
void yetty_ygui_engine_set_theme(struct yetty_ygui_engine *engine, struct yetty_ygui_theme *theme);

/* Keyboard callback */
void yetty_ygui_engine_on_key(struct yetty_ygui_engine *engine, ygui_key_callback_t callback, void *userdata);

/* Legacy event callback (all events go through one callback) */
void yetty_ygui_engine_set_event_callback(struct yetty_ygui_engine *engine, ygui_event_callback_t callback,
                                    void *userdata);

/* State */
int yetty_ygui_engine_is_dirty(const struct yetty_ygui_engine *engine);
void yetty_ygui_engine_mark_dirty(struct yetty_ygui_engine *engine);

/* Resize handling */
void yetty_ygui_engine_set_canvas_mode(struct yetty_ygui_engine *engine, ygui_canvas_mode_t mode);
void yetty_ygui_engine_set_scale_mode(struct yetty_ygui_engine *engine, ygui_scale_mode_t mode);
void yetty_ygui_engine_on_resize(struct yetty_ygui_engine *engine, ygui_resize_callback_t callback, void *userdata);

/* View state (read-only, updated from OSC 777779 events) */
float yetty_ygui_engine_get_zoom(const struct yetty_ygui_engine *engine);
float yetty_ygui_engine_get_scroll_x(const struct yetty_ygui_engine *engine);
float yetty_ygui_engine_get_scroll_y(const struct yetty_ygui_engine *engine);

/* Subscribe to view change events (zoom/scroll by user) */
void yetty_ygui_engine_subscribe_view_changes(struct yetty_ygui_engine *engine, int enable);

/* Control card view (app → yetty) */
void yetty_ygui_engine_set_zoom(struct yetty_ygui_engine *engine, float level);
void yetty_ygui_engine_scroll_to(struct yetty_ygui_engine *engine, float x, float y);
void yetty_ygui_engine_scroll_by(struct yetty_ygui_engine *engine, float dx, float dy);

/*=============================================================================
 * Widget Creation
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_button(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                           float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_label(struct yetty_ygui_engine *engine, const char *id, float x, float y,
                          const char *text);

struct yetty_ygui_widget *yetty_ygui_engine_slider(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                           float h, float min_val, float max_val, float value);

struct yetty_ygui_widget *yetty_ygui_engine_checkbox(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                             float h, const char *label, int checked);

struct yetty_ygui_widget *yetty_ygui_engine_textinput(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                              float h, const char *placeholder);

struct yetty_ygui_widget *yetty_ygui_engine_panel(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                          float h);

struct yetty_ygui_widget *yetty_ygui_engine_hbox(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_vbox(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_dropdown(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                             float h, const char **options, int option_count);

struct yetty_ygui_widget *yetty_ygui_engine_progress(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                             float h, float value);

struct yetty_ygui_widget *yetty_ygui_engine_separator(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                              float h);

struct yetty_ygui_widget *yetty_ygui_engine_colorpicker(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                                float h);

struct yetty_ygui_widget *yetty_ygui_engine_popup(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w, float h,
                          const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_collapsing_header(struct yetty_ygui_engine *engine, const char *id, float x, float y,
                                      float w, float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_tooltip(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                            float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_selectable(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                               float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_choicebox(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                              float h, const char **options, int option_count);

struct yetty_ygui_widget *yetty_ygui_engine_vscrollbar(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                               float h);

struct yetty_ygui_widget *yetty_ygui_engine_hscrollbar(struct yetty_ygui_engine *engine, const char *id, float x, float y, float w,
                               float h);

/*=============================================================================
 * Widget Callbacks
 *===========================================================================*/

/* Button */
void yetty_ygui_widget_button_on_click(struct yetty_ygui_widget *button, ygui_click_callback_t callback, void *userdata);

/* Slider */
void yetty_ygui_widget_slider_on_change(struct yetty_ygui_widget *slider, ygui_change_callback_t callback, void *userdata);

/* Checkbox */
void yetty_ygui_widget_checkbox_on_change(struct yetty_ygui_widget *checkbox, ygui_check_callback_t callback,
                             void *userdata);

/* TextInput */
void yetty_ygui_widget_textinput_on_change(struct yetty_ygui_widget *input, ygui_text_callback_t callback, void *userdata);

/*=============================================================================
 * Widget Hierarchy
 *===========================================================================*/

void yetty_ygui_widget_add_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *child);
void yetty_ygui_widget_remove_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *child);
void yetty_ygui_widget_remove(struct yetty_ygui_widget *widget);
struct yetty_ygui_widget *yetty_ygui_widget_parent(struct yetty_ygui_widget *widget);
struct yetty_ygui_widget *yetty_ygui_widget_first_child(struct yetty_ygui_widget *widget);
struct yetty_ygui_widget *yetty_ygui_widget_next_sibling(struct yetty_ygui_widget *widget);

/*=============================================================================
 * Widget Properties (Generic)
 *===========================================================================*/

const char *yetty_ygui_widget_id(const struct yetty_ygui_widget *widget);
ygui_widget_type_t yetty_ygui_widget_type(const struct yetty_ygui_widget *widget);

void yetty_ygui_widget_set_position(struct yetty_ygui_widget *widget, float x, float y);
void yetty_ygui_widget_get_position(const struct yetty_ygui_widget *widget, float *x, float *y);

void yetty_ygui_widget_set_size(struct yetty_ygui_widget *widget, float w, float h);
void yetty_ygui_widget_get_size(const struct yetty_ygui_widget *widget, float *w, float *h);

/* Resolved (post-layout) absolute box. Valid after engine_layout() or
 * engine_render() has run. Any of x/y/w/h may be NULL. */
void yetty_ygui_widget_get_layout_box(const struct yetty_ygui_widget *widget, float *x, float *y,
                                      float *w, float *h);

void yetty_ygui_widget_set_visible(struct yetty_ygui_widget *widget, int visible);
int yetty_ygui_widget_is_visible(const struct yetty_ygui_widget *widget);

void yetty_ygui_widget_set_enabled(struct yetty_ygui_widget *widget, int enabled);
int yetty_ygui_widget_is_enabled(const struct yetty_ygui_widget *widget);

uint32_t yetty_ygui_widget_get_flags(const struct yetty_ygui_widget *widget);

/* Styling */
void yetty_ygui_widget_set_bg_color(struct yetty_ygui_widget *widget, uint32_t color);
void yetty_ygui_widget_set_fg_color(struct yetty_ygui_widget *widget, uint32_t color);
void yetty_ygui_widget_set_accent_color(struct yetty_ygui_widget *widget, uint32_t color);

/* Layout (flexbox) — see ygui_layout_mode_t et al. */
void yetty_ygui_widget_set_layout_mode(struct yetty_ygui_widget *widget, ygui_layout_mode_t mode);
void yetty_ygui_widget_set_flex_direction(struct yetty_ygui_widget *widget,
                                          ygui_flex_direction_t direction);
void yetty_ygui_widget_set_justify_content(struct yetty_ygui_widget *widget,
                                           ygui_justify_t justify);
void yetty_ygui_widget_set_align_items(struct yetty_ygui_widget *widget, ygui_align_t align);
void yetty_ygui_widget_set_align_self(struct yetty_ygui_widget *widget, ygui_align_t align);
void yetty_ygui_widget_set_flex(struct yetty_ygui_widget *widget, float grow, float shrink,
                                float basis);
void yetty_ygui_widget_set_gap(struct yetty_ygui_widget *widget, float gap);
void yetty_ygui_widget_set_padding(struct yetty_ygui_widget *widget, float top, float right,
                                   float bottom, float left);
void yetty_ygui_widget_set_margin(struct yetty_ygui_widget *widget, float top, float right,
                                  float bottom, float left);
void yetty_ygui_widget_set_min_size(struct yetty_ygui_widget *widget, float min_w, float min_h);
void yetty_ygui_widget_set_max_size(struct yetty_ygui_widget *widget, float max_w, float max_h);
void yetty_ygui_widget_set_flex_wrap(struct yetty_ygui_widget *widget, ygui_flex_wrap_t wrap);
void yetty_ygui_widget_set_align_content(struct yetty_ygui_widget *widget, ygui_align_t align);
void yetty_ygui_widget_set_position_mode(struct yetty_ygui_widget *widget, ygui_position_t position);
void yetty_ygui_widget_set_flex_basis_percent(struct yetty_ygui_widget *widget, float pct);
void yetty_ygui_widget_set_size_percent(struct yetty_ygui_widget *widget, float w_pct, float h_pct);
void yetty_ygui_widget_set_min_size_percent(struct yetty_ygui_widget *widget,
                                            float min_w_pct, float min_h_pct);
void yetty_ygui_widget_set_max_size_percent(struct yetty_ygui_widget *widget,
                                            float max_w_pct, float max_h_pct);

/* Apply a CSS-like one-shot string to a widget's layout. Recognized
 * properties (each terminated by ';' or end-of-string):
 *
 *   display:        flex | manual
 *   flex-direction: row | column
 *   flex-wrap:      nowrap | wrap
 *   justify-content: start | center | end | space-between | space-around | space-evenly
 *   align-items:    auto | start | center | end | stretch | baseline
 *   align-self:     auto | start | center | end | stretch | baseline
 *   align-content:  auto | start | center | end | stretch | space-between | space-around | space-evenly
 *   position:       relative | absolute
 *   flex:           <grow> [<shrink> [<basis>]]
 *   flex-grow:      <number>
 *   flex-shrink:    <number>
 *   flex-basis:     <number>[px|%] | auto
 *   gap:            <number>[px]
 *   padding:        <t> [<r> [<b> [<l>]]]   (each <number>[px])
 *   margin:         same shorthand as padding
 *   width:          <number>%
 *   height:         <number>%
 *   min-width / min-height / max-width / max-height: <number>[px|%]
 *
 * Returns ok or an error result. Unknown properties are reported but do
 * not abort parsing of the rest of the string. */
struct yetty_ycore_void_result yetty_ygui_widget_apply_css(struct yetty_ygui_widget *widget,
                                                            const char *css);

/*=============================================================================
 * Widget-Specific Properties
 *===========================================================================*/

/* Button */
void yetty_ygui_widget_button_set_label(struct yetty_ygui_widget *widget, const char *label);
const char *yetty_ygui_widget_button_get_label(const struct yetty_ygui_widget *widget);

/* Label */
void yetty_ygui_widget_label_set_text(struct yetty_ygui_widget *widget, const char *text);
const char *yetty_ygui_widget_label_get_text(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_label_set_font_size(struct yetty_ygui_widget *widget, float size);

/* Slider */
void yetty_ygui_widget_slider_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_slider_get_value(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_slider_set_range(struct yetty_ygui_widget *widget, float min_val, float max_val);

/* Checkbox */
void yetty_ygui_widget_checkbox_set_checked(struct yetty_ygui_widget *widget, int checked);
int yetty_ygui_widget_checkbox_get_checked(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_checkbox_set_label(struct yetty_ygui_widget *widget, const char *label);

/* TextInput */
void yetty_ygui_widget_textinput_set_text(struct yetty_ygui_widget *widget, const char *text);
const char *yetty_ygui_widget_textinput_get_text(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_textinput_set_placeholder(struct yetty_ygui_widget *widget, const char *text);

/* Panel */
void yetty_ygui_widget_panel_set_scroll(struct yetty_ygui_widget *widget, float x, float y);
void yetty_ygui_widget_panel_get_scroll(const struct yetty_ygui_widget *widget, float *x, float *y);
void yetty_ygui_widget_panel_set_content_size(struct yetty_ygui_widget *widget, float w, float h);
void yetty_ygui_widget_panel_set_header_height(struct yetty_ygui_widget *widget, float h);

/* Progress */
void yetty_ygui_widget_progress_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_progress_get_value(const struct yetty_ygui_widget *widget);

/* Dropdown */
void yetty_ygui_widget_dropdown_set_options(struct yetty_ygui_widget *widget, const char **options, int count);
void yetty_ygui_widget_dropdown_set_selected(struct yetty_ygui_widget *widget, int index);
int yetty_ygui_widget_dropdown_get_selected(const struct yetty_ygui_widget *widget);

/* ColorPicker */
void yetty_ygui_widget_colorpicker_set_color(struct yetty_ygui_widget *widget, float r, float g, float b, float a);
void yetty_ygui_widget_colorpicker_get_color(const struct yetty_ygui_widget *widget, float *r, float *g, float *b,
                                float *a);

/* Popup */
void yetty_ygui_widget_popup_set_label(struct yetty_ygui_widget *widget, const char *label);
const char *yetty_ygui_widget_popup_get_label(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_popup_set_modal(struct yetty_ygui_widget *widget, int modal);
int yetty_ygui_widget_popup_is_modal(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_popup_set_open(struct yetty_ygui_widget *widget, int open);
int yetty_ygui_widget_popup_is_open(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_popup_set_scene_size(struct yetty_ygui_widget *widget, float w, float h);
void yetty_ygui_widget_popup_set_header_color(struct yetty_ygui_widget *widget, uint32_t color);

/* CollapsingHeader */
void yetty_ygui_widget_collapsing_header_set_label(struct yetty_ygui_widget *widget, const char *label);
const char *yetty_ygui_widget_collapsing_header_get_label(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_collapsing_header_set_open(struct yetty_ygui_widget *widget, int open);
int yetty_ygui_widget_collapsing_header_is_open(const struct yetty_ygui_widget *widget);

/* Tooltip */
void yetty_ygui_widget_tooltip_set_label(struct yetty_ygui_widget *widget, const char *label);
const char *yetty_ygui_widget_tooltip_get_label(const struct yetty_ygui_widget *widget);

/* Selectable */
void yetty_ygui_widget_selectable_set_label(struct yetty_ygui_widget *widget, const char *label);
const char *yetty_ygui_widget_selectable_get_label(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_selectable_set_checked(struct yetty_ygui_widget *widget, int checked);
int yetty_ygui_widget_selectable_is_checked(const struct yetty_ygui_widget *widget);

/* ChoiceBox */
void yetty_ygui_widget_choicebox_set_options(struct yetty_ygui_widget *widget, const char **options, int count);
void yetty_ygui_widget_choicebox_set_selected(struct yetty_ygui_widget *widget, int index);
int yetty_ygui_widget_choicebox_get_selected(const struct yetty_ygui_widget *widget);

/* Scrollbars (V/H share the same value 0..1) */
void yetty_ygui_widget_scrollbar_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_scrollbar_get_value(const struct yetty_ygui_widget *widget);

/*=============================================================================
 * Widget Lookup
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_find(struct yetty_ygui_engine *engine, const char *id);
struct yetty_ygui_widget *yetty_ygui_engine_widget_at(struct yetty_ygui_engine *engine, float x, float y);

/*=============================================================================
 * Theme API
 *===========================================================================*/

struct yetty_ygui_theme *yetty_ygui_theme_create(void);
struct yetty_ygui_theme *yetty_ygui_theme_create_default(void);
void yetty_ygui_theme_destroy(struct yetty_ygui_theme *theme);

void yetty_ygui_theme_set_padding(struct yetty_ygui_theme *theme, float sm, float med, float lg);
void yetty_ygui_theme_set_radius(struct yetty_ygui_theme *theme, float sm, float med, float lg);
void yetty_ygui_theme_set_row_height(struct yetty_ygui_theme *theme, float height);
void yetty_ygui_theme_set_font_size(struct yetty_ygui_theme *theme, float size);
void yetty_ygui_theme_set_scrollbar_size(struct yetty_ygui_theme *theme, float size);

void yetty_ygui_theme_set_bg_primary(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_bg_surface(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_bg_hover(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_text_primary(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_text_muted(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_accent(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_border(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_border_muted(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_bg_dropdown(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_overlay_modal(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_shadow(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_tooltip_bg(struct yetty_ygui_theme *theme, uint32_t color);
void yetty_ygui_theme_set_selection_bg(struct yetty_ygui_theme *theme, uint32_t color);

/*=============================================================================
 * Testing API
 *===========================================================================*/

/* Set custom input/output file descriptors (must be called before attach/run)
 * Useful for PTY-based testing. Default is STDIN_FILENO/STDOUT_FILENO. */
void yetty_ygui_engine_set_input_fd(struct yetty_ygui_engine *engine, int fd);
void yetty_ygui_engine_set_output_fd(struct yetty_ygui_engine *engine, int fd);

/* Set card dimensions for testing coordinate scaling
 * These are normally set by ygui_engine_create() but can be overridden for tests */
void yetty_ygui_engine_set_card_size(struct yetty_ygui_engine *engine, int card_w, int card_h);

/* Set display pixel size directly for testing.
 * Normally this comes from OSC 777780 after show().
 * For tests, call this to set canvas size before creating widgets. */
void yetty_ygui_engine_set_display_pixel_size(struct yetty_ygui_engine *engine, float width, float height);

/* Get access to engine's libuv loop (after attach/run) */
uv_loop_t *yetty_ygui_engine_get_loop(struct yetty_ygui_engine *engine);

/* Process pending events without blocking (run one loop iteration)
 * Returns 0 if no more events, 1 if there are still pending events */
int yetty_ygui_engine_poll(struct yetty_ygui_engine *engine);

/*=============================================================================
 * Error Handling
 *===========================================================================*/

const char *yetty_ygui_get_error(void);

/*=============================================================================
 * Version
 *===========================================================================*/

#define YGUI_VERSION_MAJOR 0
#define YGUI_VERSION_MINOR 2
#define YGUI_VERSION_PATCH 0

const char *yetty_ygui_version(void);

#ifdef __cplusplus
}
#endif

#endif /* YGUI_H */
