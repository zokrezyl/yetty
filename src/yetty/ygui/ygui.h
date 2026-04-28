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
int ygui_init(void);

/* Shutdown the library (restores terminal settings)
 * Should be called before exit. */
void ygui_shutdown(void);

/*=============================================================================
 * Opaque Types
 *===========================================================================*/

typedef struct ygui_engine ygui_engine_t;
typedef struct ygui_widget ygui_widget_t;
typedef struct ygui_theme ygui_theme_t;

/*=============================================================================
 * Enums
 *===========================================================================*/

typedef enum {
    YGUI_WIDGET_BUTTON,
    YGUI_WIDGET_LABEL,
    YGUI_WIDGET_SLIDER,
    YGUI_WIDGET_CHECKBOX,
    YGUI_WIDGET_TEXTINPUT,
    YGUI_WIDGET_PANEL,
    YGUI_WIDGET_HBOX,
    YGUI_WIDGET_VBOX,
    YGUI_WIDGET_DROPDOWN,
    YGUI_WIDGET_LISTBOX,
    YGUI_WIDGET_TABLE,
    YGUI_WIDGET_TABBAR,
    YGUI_WIDGET_COLORPICKER,
    YGUI_WIDGET_SCROLLAREA,
    YGUI_WIDGET_PROGRESS,
    YGUI_WIDGET_SEPARATOR,
    YGUI_WIDGET_POPUP,
    YGUI_WIDGET_COLLAPSING_HEADER,
    YGUI_WIDGET_TOOLTIP,
    YGUI_WIDGET_SELECTABLE,
    YGUI_WIDGET_CHOICEBOX,
    YGUI_WIDGET_VSCROLLBAR,
    YGUI_WIDGET_HSCROLLBAR,
    YGUI_WIDGET_CUSTOM,
} ygui_widget_type_t;

typedef enum {
    YGUI_EVENT_NONE = 0,
    YGUI_EVENT_CLICK,
    YGUI_EVENT_PRESS,
    YGUI_EVENT_RELEASE,
    YGUI_EVENT_CHANGE,
    YGUI_EVENT_SCROLL,
    YGUI_EVENT_FOCUS,
    YGUI_EVENT_BLUR,
    YGUI_EVENT_KEY,
    YGUI_EVENT_TEXT,
} ygui_event_type_t;

typedef enum {
    YGUI_FLAG_NONE = 0,
    YGUI_FLAG_HOVER = 1 << 0,
    YGUI_FLAG_PRESSED = 1 << 1,
    YGUI_FLAG_FOCUSED = 1 << 2,
    YGUI_FLAG_DISABLED = 1 << 3,
    YGUI_FLAG_CHECKED = 1 << 4,
    YGUI_FLAG_OPEN = 1 << 5,
    YGUI_FLAG_VISIBLE = 1 << 6,
} ygui_flags_t;

/* Canvas mode: how canvas size relates to display size */
typedef enum {
    YGUI_CANVAS_FIXED, /* Canvas size stays constant */
    YGUI_CANVAS_FIT    /* Canvas size = card pixel size (card_cells * cell_pixels) */
} ygui_canvas_mode_t;

/* Widget scale mode: how widgets respond to canvas size changes */
typedef enum {
    YGUI_SCALE_OFF, /* Widgets keep positions/sizes (may clip) */
    YGUI_SCALE_ON   /* Widgets scale proportionally with canvas */
} ygui_scale_mode_t;

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
typedef void (*ygui_key_callback_t)(ygui_engine_t *engine, uint32_t key, int mods, void *userdata);

/* Widget-specific callbacks */
typedef void (*ygui_click_callback_t)(ygui_widget_t *widget, void *userdata);
typedef void (*ygui_change_callback_t)(ygui_widget_t *widget, float value, void *userdata);
typedef void (*ygui_text_callback_t)(ygui_widget_t *widget, const char *text, void *userdata);
typedef void (*ygui_check_callback_t)(ygui_widget_t *widget, int checked, void *userdata);

/* Resize callback - called when terminal resizes and new cell size is received */
typedef void (*ygui_resize_callback_t)(ygui_engine_t *engine, void *userdata);

/*=============================================================================
 * Engine API
 *===========================================================================*/

/* Create engine with card name, position, and size in terminal cells.
 * x, y: card position in terminal cells
 * cols, rows: card size in terminal cells
 * After show(), queries card pixel size (OSC 777780).
 * Canvas = actual card pixels (cols * cell_width, rows * cell_height).
 * Widgets are positioned in actual pixel coordinates. */
ygui_engine_t *ygui_engine_create(const char *card_name, int x, int y, int cols, int rows);

/* Create engine with pixel size hints.
 * x, y: card position in terminal cells
 * width_hint, height_hint: desired pixel size (calculates closest cols/rows)
 * Then same as ygui_engine_create: canvas = actual card pixels. */
ygui_engine_t *ygui_engine_create_with_pixel_hint(const char *card_name, int x, int y,
                                                  float width_hint, float height_hint);

/* Destroy engine (kills card, frees all resources) */
void ygui_engine_destroy(ygui_engine_t *engine);

/* Show card (creates it via OSC, queries pixel size).
 * Position and size were set in ygui_engine_create. */
void ygui_engine_show(ygui_engine_t *engine);

/* Render a frame (clear buffer → rebuild → serialize → send OSC)
 * Usually not needed - engine auto-renders when dirty. */
void ygui_engine_render(ygui_engine_t *engine);

/* Attach engine to user's libuv loop (for advanced usage) */
void ygui_engine_attach(ygui_engine_t *engine, uv_loop_t *loop);

/* Run event loop (creates libuv loop internally for simple usage)
 * Blocks until ygui_engine_stop() called or 'q' pressed. */
void ygui_engine_run(ygui_engine_t *engine);

/* Stop the event loop */
void ygui_engine_stop(ygui_engine_t *engine);

/* Configuration */
void ygui_engine_set_size(ygui_engine_t *engine, float width, float height);
void ygui_engine_set_theme(ygui_engine_t *engine, ygui_theme_t *theme);

/* Keyboard callback */
void ygui_engine_on_key(ygui_engine_t *engine, ygui_key_callback_t callback, void *userdata);

/* Legacy event callback (all events go through one callback) */
void ygui_engine_set_event_callback(ygui_engine_t *engine, ygui_event_callback_t callback,
                                    void *userdata);

/* State */
int ygui_engine_is_dirty(const ygui_engine_t *engine);
void ygui_engine_mark_dirty(ygui_engine_t *engine);

/* Resize handling */
void ygui_engine_set_canvas_mode(ygui_engine_t *engine, ygui_canvas_mode_t mode);
void ygui_engine_set_scale_mode(ygui_engine_t *engine, ygui_scale_mode_t mode);
void ygui_engine_on_resize(ygui_engine_t *engine, ygui_resize_callback_t callback, void *userdata);

/* View state (read-only, updated from OSC 777779 events) */
float ygui_engine_get_zoom(const ygui_engine_t *engine);
float ygui_engine_get_scroll_x(const ygui_engine_t *engine);
float ygui_engine_get_scroll_y(const ygui_engine_t *engine);

/* Subscribe to view change events (zoom/scroll by user) */
void ygui_engine_subscribe_view_changes(ygui_engine_t *engine, int enable);

/* Control card view (app → yetty) */
void ygui_engine_set_zoom(ygui_engine_t *engine, float level);
void ygui_engine_scroll_to(ygui_engine_t *engine, float x, float y);
void ygui_engine_scroll_by(ygui_engine_t *engine, float dx, float dy);

/*=============================================================================
 * Widget Creation
 *===========================================================================*/

ygui_widget_t *ygui_button(ygui_engine_t *engine, const char *id, float x, float y, float w,
                           float h, const char *label);

ygui_widget_t *ygui_label(ygui_engine_t *engine, const char *id, float x, float y,
                          const char *text);

ygui_widget_t *ygui_slider(ygui_engine_t *engine, const char *id, float x, float y, float w,
                           float h, float min_val, float max_val, float value);

ygui_widget_t *ygui_checkbox(ygui_engine_t *engine, const char *id, float x, float y, float w,
                             float h, const char *label, int checked);

ygui_widget_t *ygui_textinput(ygui_engine_t *engine, const char *id, float x, float y, float w,
                              float h, const char *placeholder);

ygui_widget_t *ygui_panel(ygui_engine_t *engine, const char *id, float x, float y, float w,
                          float h);

ygui_widget_t *ygui_hbox(ygui_engine_t *engine, const char *id, float x, float y, float w, float h);

ygui_widget_t *ygui_vbox(ygui_engine_t *engine, const char *id, float x, float y, float w, float h);

ygui_widget_t *ygui_dropdown(ygui_engine_t *engine, const char *id, float x, float y, float w,
                             float h, const char **options, int option_count);

ygui_widget_t *ygui_progress(ygui_engine_t *engine, const char *id, float x, float y, float w,
                             float h, float value);

ygui_widget_t *ygui_separator(ygui_engine_t *engine, const char *id, float x, float y, float w,
                              float h);

ygui_widget_t *ygui_colorpicker(ygui_engine_t *engine, const char *id, float x, float y, float w,
                                float h);

ygui_widget_t *ygui_popup(ygui_engine_t *engine, const char *id, float x, float y, float w, float h,
                          const char *label);

ygui_widget_t *ygui_collapsing_header(ygui_engine_t *engine, const char *id, float x, float y,
                                      float w, float h, const char *label);

ygui_widget_t *ygui_tooltip(ygui_engine_t *engine, const char *id, float x, float y, float w,
                            float h, const char *label);

ygui_widget_t *ygui_selectable(ygui_engine_t *engine, const char *id, float x, float y, float w,
                               float h, const char *label);

ygui_widget_t *ygui_choicebox(ygui_engine_t *engine, const char *id, float x, float y, float w,
                              float h, const char **options, int option_count);

ygui_widget_t *ygui_vscrollbar(ygui_engine_t *engine, const char *id, float x, float y, float w,
                               float h);

ygui_widget_t *ygui_hscrollbar(ygui_engine_t *engine, const char *id, float x, float y, float w,
                               float h);

/*=============================================================================
 * Widget Callbacks
 *===========================================================================*/

/* Button */
void ygui_button_on_click(ygui_widget_t *button, ygui_click_callback_t callback, void *userdata);

/* Slider */
void ygui_slider_on_change(ygui_widget_t *slider, ygui_change_callback_t callback, void *userdata);

/* Checkbox */
void ygui_checkbox_on_change(ygui_widget_t *checkbox, ygui_check_callback_t callback,
                             void *userdata);

/* TextInput */
void ygui_textinput_on_change(ygui_widget_t *input, ygui_text_callback_t callback, void *userdata);

/*=============================================================================
 * Widget Hierarchy
 *===========================================================================*/

void ygui_widget_add_child(ygui_widget_t *parent, ygui_widget_t *child);
void ygui_widget_remove_child(ygui_widget_t *parent, ygui_widget_t *child);
void ygui_widget_remove(ygui_widget_t *widget);
ygui_widget_t *ygui_widget_parent(ygui_widget_t *widget);
ygui_widget_t *ygui_widget_first_child(ygui_widget_t *widget);
ygui_widget_t *ygui_widget_next_sibling(ygui_widget_t *widget);

/*=============================================================================
 * Widget Properties (Generic)
 *===========================================================================*/

const char *ygui_widget_id(const ygui_widget_t *widget);
ygui_widget_type_t ygui_widget_type(const ygui_widget_t *widget);

void ygui_widget_set_position(ygui_widget_t *widget, float x, float y);
void ygui_widget_get_position(const ygui_widget_t *widget, float *x, float *y);

void ygui_widget_set_size(ygui_widget_t *widget, float w, float h);
void ygui_widget_get_size(const ygui_widget_t *widget, float *w, float *h);

void ygui_widget_set_visible(ygui_widget_t *widget, int visible);
int ygui_widget_is_visible(const ygui_widget_t *widget);

void ygui_widget_set_enabled(ygui_widget_t *widget, int enabled);
int ygui_widget_is_enabled(const ygui_widget_t *widget);

uint32_t ygui_widget_get_flags(const ygui_widget_t *widget);

/* Styling */
void ygui_widget_set_bg_color(ygui_widget_t *widget, uint32_t color);
void ygui_widget_set_fg_color(ygui_widget_t *widget, uint32_t color);
void ygui_widget_set_accent_color(ygui_widget_t *widget, uint32_t color);

/*=============================================================================
 * Widget-Specific Properties
 *===========================================================================*/

/* Button */
void ygui_button_set_label(ygui_widget_t *widget, const char *label);
const char *ygui_button_get_label(const ygui_widget_t *widget);

/* Label */
void ygui_label_set_text(ygui_widget_t *widget, const char *text);
const char *ygui_label_get_text(const ygui_widget_t *widget);
void ygui_label_set_font_size(ygui_widget_t *widget, float size);

/* Slider */
void ygui_slider_set_value(ygui_widget_t *widget, float value);
float ygui_slider_get_value(const ygui_widget_t *widget);
void ygui_slider_set_range(ygui_widget_t *widget, float min_val, float max_val);

/* Checkbox */
void ygui_checkbox_set_checked(ygui_widget_t *widget, int checked);
int ygui_checkbox_get_checked(const ygui_widget_t *widget);
void ygui_checkbox_set_label(ygui_widget_t *widget, const char *label);

/* TextInput */
void ygui_textinput_set_text(ygui_widget_t *widget, const char *text);
const char *ygui_textinput_get_text(const ygui_widget_t *widget);
void ygui_textinput_set_placeholder(ygui_widget_t *widget, const char *text);

/* Panel */
void ygui_panel_set_scroll(ygui_widget_t *widget, float x, float y);
void ygui_panel_get_scroll(const ygui_widget_t *widget, float *x, float *y);
void ygui_panel_set_content_size(ygui_widget_t *widget, float w, float h);
void ygui_panel_set_header_height(ygui_widget_t *widget, float h);

/* Progress */
void ygui_progress_set_value(ygui_widget_t *widget, float value);
float ygui_progress_get_value(const ygui_widget_t *widget);

/* Dropdown */
void ygui_dropdown_set_options(ygui_widget_t *widget, const char **options, int count);
void ygui_dropdown_set_selected(ygui_widget_t *widget, int index);
int ygui_dropdown_get_selected(const ygui_widget_t *widget);

/* ColorPicker */
void ygui_colorpicker_set_color(ygui_widget_t *widget, float r, float g, float b, float a);
void ygui_colorpicker_get_color(const ygui_widget_t *widget, float *r, float *g, float *b,
                                float *a);

/* Popup */
void ygui_popup_set_label(ygui_widget_t *widget, const char *label);
const char *ygui_popup_get_label(const ygui_widget_t *widget);
void ygui_popup_set_modal(ygui_widget_t *widget, int modal);
int ygui_popup_is_modal(const ygui_widget_t *widget);
void ygui_popup_set_open(ygui_widget_t *widget, int open);
int ygui_popup_is_open(const ygui_widget_t *widget);
void ygui_popup_set_scene_size(ygui_widget_t *widget, float w, float h);
void ygui_popup_set_header_color(ygui_widget_t *widget, uint32_t color);

/* CollapsingHeader */
void ygui_collapsing_header_set_label(ygui_widget_t *widget, const char *label);
const char *ygui_collapsing_header_get_label(const ygui_widget_t *widget);
void ygui_collapsing_header_set_open(ygui_widget_t *widget, int open);
int ygui_collapsing_header_is_open(const ygui_widget_t *widget);

/* Tooltip */
void ygui_tooltip_set_label(ygui_widget_t *widget, const char *label);
const char *ygui_tooltip_get_label(const ygui_widget_t *widget);

/* Selectable */
void ygui_selectable_set_label(ygui_widget_t *widget, const char *label);
const char *ygui_selectable_get_label(const ygui_widget_t *widget);
void ygui_selectable_set_checked(ygui_widget_t *widget, int checked);
int ygui_selectable_is_checked(const ygui_widget_t *widget);

/* ChoiceBox */
void ygui_choicebox_set_options(ygui_widget_t *widget, const char **options, int count);
void ygui_choicebox_set_selected(ygui_widget_t *widget, int index);
int ygui_choicebox_get_selected(const ygui_widget_t *widget);

/* Scrollbars (V/H share the same value 0..1) */
void ygui_scrollbar_set_value(ygui_widget_t *widget, float value);
float ygui_scrollbar_get_value(const ygui_widget_t *widget);

/*=============================================================================
 * Widget Lookup
 *===========================================================================*/

ygui_widget_t *ygui_engine_find(ygui_engine_t *engine, const char *id);
ygui_widget_t *ygui_engine_widget_at(ygui_engine_t *engine, float x, float y);

/*=============================================================================
 * Theme API
 *===========================================================================*/

ygui_theme_t *ygui_theme_create(void);
ygui_theme_t *ygui_theme_create_default(void);
void ygui_theme_destroy(ygui_theme_t *theme);

void ygui_theme_set_padding(ygui_theme_t *theme, float sm, float med, float lg);
void ygui_theme_set_radius(ygui_theme_t *theme, float sm, float med, float lg);
void ygui_theme_set_row_height(ygui_theme_t *theme, float height);
void ygui_theme_set_font_size(ygui_theme_t *theme, float size);
void ygui_theme_set_scrollbar_size(ygui_theme_t *theme, float size);

void ygui_theme_set_bg_primary(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_bg_surface(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_bg_hover(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_text_primary(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_text_muted(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_accent(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_border(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_border_muted(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_bg_dropdown(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_overlay_modal(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_shadow(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_tooltip_bg(ygui_theme_t *theme, uint32_t color);
void ygui_theme_set_selection_bg(ygui_theme_t *theme, uint32_t color);

/*=============================================================================
 * Testing API
 *===========================================================================*/

/* Set custom input/output file descriptors (must be called before attach/run)
 * Useful for PTY-based testing. Default is STDIN_FILENO/STDOUT_FILENO. */
void ygui_engine_set_input_fd(ygui_engine_t *engine, int fd);
void ygui_engine_set_output_fd(ygui_engine_t *engine, int fd);

/* Set card dimensions for testing coordinate scaling
 * These are normally set by ygui_engine_create() but can be overridden for tests */
void ygui_engine_set_card_size(ygui_engine_t *engine, int card_w, int card_h);

/* Set display pixel size directly for testing.
 * Normally this comes from OSC 777780 after show().
 * For tests, call this to set canvas size before creating widgets. */
void ygui_engine_set_display_pixel_size(ygui_engine_t *engine, float width, float height);

/* Get access to engine's libuv loop (after attach/run) */
uv_loop_t *ygui_engine_get_loop(ygui_engine_t *engine);

/* Process pending events without blocking (run one loop iteration)
 * Returns 0 if no more events, 1 if there are still pending events */
int ygui_engine_poll(ygui_engine_t *engine);

/*=============================================================================
 * Error Handling
 *===========================================================================*/

const char *ygui_get_error(void);

/*=============================================================================
 * Version
 *===========================================================================*/

#define YGUI_VERSION_MAJOR 0
#define YGUI_VERSION_MINOR 2
#define YGUI_VERSION_PATCH 0

const char *ygui_version(void);

#ifdef __cplusplus
}
#endif

#endif /* YGUI_H */
