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
#include <yetty/ycore/types.h>

/* Forward declare libuv types */
typedef struct uv_loop_s uv_loop_t;

/* Forward declare ydraw-core buffer (rich widget hands one of these to ygui;
 * we don't pull in the full ydraw-core header from this public surface). */
struct yetty_ydraw_draw_list;

/* Forward declare config — kept opaque so the public ygui surface doesn't
 * require pulling yconfig headers into every client app. */
struct yetty_yconfig_config;

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
    /* Generic vertical container with row-aware semantics: tracks a
     * selected child and fires on_select when a row is clicked. Children
     * can be anything — buttons, hboxes, tree_nodes, custom composites.
     * CSS controls layout (gap, padding). */
    YETTY_YGUI_WIDGET_LIST,
    /* Tree-style row: chevron + label header, plus an auto-allocated
     * children list that's only visible when expanded. A tree_node with
     * zero children renders without a chevron and acts as a leaf row
     * (covers ImGui's Selectable + TreeNode duality with a single
     * widget). Indent is controlled by CSS padding-left on the children
     * list returned by yetty_ygui_widget_tree_node_children(). */
    YETTY_YGUI_WIDGET_TREE_NODE,
    /* Rich content surface: holds a ydraw-core buffer of pre-built
     * primitives (TEXT_SPAN, SDF shapes, yplot, yimage, ...). The widget
     * reserves a flex/layout box and, at render time, translates every
     * primitive in its buffer by the box's resolved (x, y). Authors
     * compose content in widget-local coordinates 0..w x 0..h. */
    YETTY_YGUI_WIDGET_RICH,
    /* Top-level frame: title bar + close 'x' affordance + a body
     * container all other widgets sit inside. The close button stops
     * the engine while leaving the last painted frame on the ydraw
     * canvas, so apps can exit gracefully without wiping the user's
     * view. See yetty_ygui_engine_window in this file. */
    YETTY_YGUI_WIDGET_WINDOW,
    /* Floating menu of clickable items. Inherits the visuals of
     * YETTY_YGUI_WIDGET_POPUP (shadow + rounded body + optional modal
     * overlay) and specialises it for vertically stacked rows. Items
     * are stored as label/callback pairs inside the widget — no
     * sub-widgets needed. See yetty_ygui_engine_popup_menu. */
    YETTY_YGUI_WIDGET_POPUP_MENU,
    /* PDF viewer: owns N per-page draw_lists; emits font header + only
     * visible pages translated by (widget_origin + page_y - scroll_y).
     * See yetty/ygui/ygui_ypdf.h for construction + scroll API. */
    YETTY_YGUI_WIDGET_YPDF,
    /* Image surface — own widget type; owns the source path and
     * rebuilds the yimage prim from the current resolved layout box
     * on every render where the box changed. No piggyback on rich:
     * the widget fits its client area because the producer is called
     * with the size layout actually resolved. */
    YETTY_YGUI_WIDGET_YIMAGE,
    /* Plot surface — same shape as YIMAGE: owns expression source +
     * data buffers, rebuilds the yplot prim per render at the
     * current resolved box. */
    YETTY_YGUI_WIDGET_YPLOT,
    /* Video surface — owns the H.264 NAL stream + render config,
     * rebuilds the yvideo prim per render at the current resolved
     * box. */
    YETTY_YGUI_WIDGET_YVIDEO,
    /* Yzoo / yjungle showcase widgets — own their producer instance
     * + per-instance state, drive a render-time rebuild against the
     * current resolved box. */
    YETTY_YGUI_WIDGET_YZOO,
    YETTY_YGUI_WIDGET_YJUNGLE,
    /* Single-select group of radio buttons. Tracks which child radio
     * is currently selected; clicking another radio in the group
     * deselects the prior one. Fires on_change(group, index) when
     * selection moves. */
    YETTY_YGUI_WIDGET_RADIO_GROUP,
    /* One radio inside a RADIO_GROUP. Renders an outlined circle with
     * a filled inner dot when selected, plus a label to the right. */
    YETTY_YGUI_WIDGET_RADIO,
    /* Numeric input with ± buttons. Click +/- (or wheel / arrow keys)
     * to step; range/step/precision configurable. on_change fires with
     * the new value. */
    YETTY_YGUI_WIDGET_SPINNER,
    /* Drag-to-resize divider sitting between two siblings inside a
     * flex container. Detects orientation from the parent's flex
     * direction; resizes by mutating the two siblings' authored sizes
     * (so the children must NOT use flex-basis:0 / flex-grow:1 — see
     * yetty_ygui_engine_splitter docs). */
    YETTY_YGUI_WIDGET_SPLITTER,
    /* Multi-line editable text. Cursor + typing + Backspace + Enter +
     * Home/End + arrow keys, viewport scrolling. Selection / clipboard
     * not yet implemented. */
    YETTY_YGUI_WIDGET_TEXTAREA,
    /* Pill-shaped on/off control with sliding thumb. Same callback
     * shape as checkbox. */
    YETTY_YGUI_WIDGET_TOGGLE,
    /* Small pill label with optional ✕ close button. on_remove fires
     * when the user clicks ✕. */
    YETTY_YGUI_WIDGET_CHIP,
    /* Horizontal sequence of clickable segments separated by ' › '.
     * on_change fires with the clicked segment index. */
    YETTY_YGUI_WIDGET_BREADCRUMBS,
    /* Editable text input with a dropdown of suggested values reached
     * via a small arrow button on the right side. */
    YETTY_YGUI_WIDGET_COMBO,
    /* Horizontal strip of menu buttons; each opens an attached
     * popup_menu anchored beneath the click. */
    YETTY_YGUI_WIDGET_MENUBAR,
    /* Numbered-step indicator: circles + connecting lines + per-step
     * state (complete / current / upcoming). */
    YETTY_YGUI_WIDGET_STEPPER,
    /* Compact month calendar with prev/next month navigation. */
    YETTY_YGUI_WIDGET_DATEPICKER,
    /* File / folder picker — list of directory entries with
     * navigation. Often used inside a modal popup. */
    YETTY_YGUI_WIDGET_FILEPICKER,
    /* Bottom-of-window status strip: short primary text + optional
     * right-aligned secondary text (e.g. "Ready  |  ln 12, col 3"). */
    YETTY_YGUI_WIDGET_STATUSBAR,
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

typedef enum { YETTY_YGUI_FLEX_ROW = 0, YETTY_YGUI_FLEX_COLUMN } ygui_flex_direction_t;

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
    YETTY_YGUI_ALIGN_BASELINE /* cross-axis: line up children by text baseline */
} ygui_align_t;

typedef enum {
    YETTY_YGUI_FLEX_NOWRAP = 0,
    YETTY_YGUI_FLEX_WRAP /* break to new line when children overflow */
} ygui_flex_wrap_t;

typedef enum {
    YETTY_YGUI_POSITION_RELATIVE = 0, /* default — participates in flex flow */
    YETTY_YGUI_POSITION_ABSOLUTE      /* skipped by flex; positioned at authored x/y */
} ygui_position_t;

struct yetty_ygui_layout {
    ygui_layout_mode_t mode;
    ygui_flex_direction_t direction;
    ygui_flex_wrap_t wrap;
    ygui_justify_t justify_content;
    ygui_align_t align_items;
    ygui_align_t align_self;
    ygui_align_t align_content; /* multi-line cross-axis alignment */
    ygui_position_t position;

    float flex_grow;
    float flex_shrink;
    float flex_basis; /* <= 0: use authored size on main axis */
    float
        flex_basis_percent; /* > 0: percent of parent's main content size (overrides flex_basis) */

    float gap;
    float padding_top, padding_right, padding_bottom, padding_left;
    float margin_top, margin_right, margin_bottom, margin_left;

    float min_w, min_h; /* 0: unset (absolute pixels) */
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
typedef void (*ygui_key_callback_t)(struct yetty_ygui_engine *engine, uint32_t key, int mods,
                                    void *userdata);

/* Widget-specific callbacks */
typedef void (*ygui_click_callback_t)(struct yetty_ygui_widget *widget, void *userdata);
typedef void (*ygui_change_callback_t)(struct yetty_ygui_widget *widget, float value,
                                       void *userdata);
typedef void (*ygui_text_callback_t)(struct yetty_ygui_widget *widget, const char *text,
                                     void *userdata);
typedef void (*ygui_check_callback_t)(struct yetty_ygui_widget *widget, int checked,
                                      void *userdata);

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
 *
 * Framework, not just a library: the engine owns the libuv loop, the pty
 * wrapping the process's stdin/stdout, the wire-side OSC handshake, and
 * the render scheduling. Apps only see widgets, callbacks, and the
 * engine handle.
 *
 * Lifecycle:
 *   1. eng = yetty_ygui_engine_create({.name = "myapp"}).value
 *      Allocates the loop, wires the pty, installs handles, emits the
 *      init OSCs (CSI 16t, subscribe_clicks/moves, CARD_PLACE, CANVAS_FIT
 *      placeholder). The pty is live before this returns.
 *   2. Build widget tree against `eng`; register callbacks.
 *   3. yetty_ygui_engine_run(eng) — blocks on the engine's own loop until
 *      shutdown.
 *   4. yetty_ygui_engine_destroy(eng) — unsubscribes, drops the card,
 *      tears down handles, closes the loop.
 *
 * Apps never pass file descriptors, pty pointers, loop pointers, cell
 * sizes, canvas modes, or any pixel geometry — every one of those is
 * either framework-internal or discovered at runtime from the host's
 * OSC reply.
 *===========================================================================*/

/* Construction parameters. Pass by value; the engine copies what it
 * needs. Both fields are optional; sensible defaults are supplied. */
struct yetty_ygui_engine_args {
    /* Human-readable identifier ("ygreeter", "ytop", …). Used for logs
     * and for matching legacy name-keyed OSC events. NULL → "ygui". */
    const char *name;
    /* Theme handle. NULL → built-in brand-palette default. The engine
     * takes ownership of NULL-input themes; caller-supplied themes
     * remain caller-owned and must outlive the engine. */
    struct yetty_ygui_theme *theme;
};

/* Construct the engine. Returns a ready-to-use handle with the loop +
 * pty + handshake already in place. */
struct ygui_engine_ptr_result yetty_ygui_engine_create(struct yetty_ygui_engine_args args);

/* Destroy engine (kills card, frees all resources, closes the loop). */
struct yetty_ycore_void_result yetty_ygui_engine_destroy(struct yetty_ygui_engine *engine);

/* Render a frame (clear buffer → rebuild → serialize → send OSC)
 * Usually not needed - engine auto-renders when dirty. */
struct yetty_ycore_void_result yetty_ygui_engine_render(struct yetty_ygui_engine *engine);

/* Run only the layout pass — no rendering, no OSC.
 * After this call, every visible widget has resolved layout_x/y/w/h available
 * through yetty_ygui_widget_get_layout_box(). Useful for tests, headless
 * inspection, and tools that want to query post-flex geometry without
 * triggering a render. */
struct yetty_ycore_void_result yetty_ygui_engine_layout(struct yetty_ygui_engine *engine);

/* Run the engine's event loop. Blocks until yetty_ygui_engine_stop() is
 * called, the user closes the card, or the host shuts the pty. */
void yetty_ygui_engine_run(struct yetty_ygui_engine *engine);

/* Stop the event loop. Safe to call from any uv-loop callback. */
void yetty_ygui_engine_stop(struct yetty_ygui_engine *engine);

/* Configuration */
void yetty_ygui_engine_set_size(struct yetty_ygui_engine *engine, float width, float height);
struct pixel_size_result yetty_ygui_engine_get_size(const struct yetty_ygui_engine *engine);
void yetty_ygui_engine_set_theme(struct yetty_ygui_engine *engine, struct yetty_ygui_theme *theme);

/* Keyboard callback */
void yetty_ygui_engine_on_key(struct yetty_ygui_engine *engine, ygui_key_callback_t callback,
                              void *userdata);

/* Legacy event callback (all events go through one callback) */
void yetty_ygui_engine_set_event_callback(struct yetty_ygui_engine *engine,
                                          ygui_event_callback_t callback, void *userdata);

/* State */
int yetty_ygui_engine_is_dirty(const struct yetty_ygui_engine *engine);
void yetty_ygui_engine_mark_dirty(struct yetty_ygui_engine *engine);

/* Direct mouse-event injection. Used when the engine isn't being driven
 * by an OSC mouse stream (e.g. yui in-process — the host hands
 * events here so the engine's hit-test + dispatch routes them to the
 * widget tree). Coords are in widget pixel space, same convention the
 * OSC SC_MOUSE handler uses. */
void yetty_ygui_engine_mouse_down(struct yetty_ygui_engine *engine, float x, float y, int button);
void yetty_ygui_engine_mouse_up(struct yetty_ygui_engine *engine, float x, float y, int button);
void yetty_ygui_engine_mouse_move(struct yetty_ygui_engine *engine, float x, float y);

/* True iff the engine currently has a widget under a held mouse button.
 * Used by yui after engine_mouse_down to decide whether a click landed
 * on a widget (consume) or on empty space (fall through to the host).
 * Cleared by engine_mouse_up. */
int yetty_ygui_engine_has_pressed_widget(const struct yetty_ygui_engine *engine);

/* The widget currently under the cursor (hover) and the widget that
 * grabbed the press (drag target), respectively. Both return NULL when
 * nothing is hovered / pressed. Used by hosts that need to cross-check
 * widget kind for things like OS cursor-shape changes. */
struct yetty_ygui_widget *yetty_ygui_engine_hovered_widget(const struct yetty_ygui_engine *engine);
struct yetty_ygui_widget *yetty_ygui_engine_pressed_widget(const struct yetty_ygui_engine *engine);

/* Get the widget's type tag. Returns the underlying YETTY_YGUI_WIDGET_*
 * enum value as int (negative if widget is NULL). Useful for narrow
 * cross-checks like "is this a splitter" without exposing the entire
 * internal widget struct. */
int yetty_ygui_widget_get_type(const struct yetty_ygui_widget *widget);

/* Direct keyboard-event injection. Same use case as the mouse variants —
 * the engine's internal dispatch routes the event to engine->focused
 * (textinput taking text, button taking Enter/Escape, …). `text_input`
 * takes a NUL-terminated UTF-8 chunk; the engine appends it to the
 * focused widget if it accepts text. */
void yetty_ygui_engine_key_down(struct yetty_ygui_engine *engine, uint32_t key, int mods);
void yetty_ygui_engine_key_up(struct yetty_ygui_engine *engine, uint32_t key, int mods);
void yetty_ygui_engine_text_input(struct yetty_ygui_engine *engine, const char *utf8);

/* =========================================================================
 * Notifications — toast popups anchored to the top-right of the canvas.
 *
 * Available to every ygui app, not just yui. Push from anywhere with
 * yetty_ygui_engine_notify (printf-style); cards stack top-down with the
 * newest at the top. Each card auto-dismisses after its TTL, or the user
 * dismisses it via the "✕" button. Surviving cards slide up to fill the
 * gap on every dismissal.
 *
 * Severity drives the accent stripe colour (INFO mint, WARN amber, ERROR
 * crimson) — the rest of the card uses the brand surface palette. ERROR
 * defaults to a longer TTL (10 s) than INFO/WARN (4 s); pass an explicit
 * TTL via yetty_ygui_engine_notify_ttl if you want a different value, or
 * 0 to make it sticky (user must click to dismiss).
 * ========================================================================= */
enum yetty_ygui_severity {
    YETTY_YGUI_SEV_INFO = 0,
    YETTY_YGUI_SEV_WARN,
    YETTY_YGUI_SEV_ERROR,
};

void yetty_ygui_engine_notify(struct yetty_ygui_engine *engine, enum yetty_ygui_severity sev,
                              const char *fmt, ...);
void yetty_ygui_engine_notify_ttl(struct yetty_ygui_engine *engine, enum yetty_ygui_severity sev,
                                  uint32_t ttl_ms, const char *fmt, ...);

/* Resize handling. Canvas always tracks the host's reported pixel size
 * (CANVAS_FIT semantics); there is no other mode. */
void yetty_ygui_engine_on_resize(struct yetty_ygui_engine *engine, ygui_resize_callback_t callback,
                                 void *userdata);

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

struct yetty_ygui_widget *yetty_ygui_engine_button(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_label(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, const char *text);

struct yetty_ygui_widget *yetty_ygui_engine_slider(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   float min_val, float max_val, float value);

struct yetty_ygui_widget *yetty_ygui_engine_checkbox(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char *label, int checked);

struct yetty_ygui_widget *yetty_ygui_engine_textinput(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char *placeholder);

struct yetty_ygui_widget *yetty_ygui_engine_panel(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_hbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_vbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_dropdown(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char **options,
                                                     int option_count);

struct yetty_ygui_widget *yetty_ygui_engine_progress(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, float value);

struct yetty_ygui_widget *yetty_ygui_engine_separator(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h);

struct yetty_ygui_widget *yetty_ygui_engine_colorpicker(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h);

struct yetty_ygui_widget *yetty_ygui_engine_popup(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h,
                                                  const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_collapsing_header(struct yetty_ygui_engine *engine,
                                                              const char *id, float x, float y,
                                                              float w, float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_tooltip(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_selectable(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h, const char *label);

struct yetty_ygui_widget *yetty_ygui_engine_choicebox(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char **options,
                                                      int option_count);

struct yetty_ygui_widget *yetty_ygui_engine_vscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h);

struct yetty_ygui_widget *yetty_ygui_engine_hscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h);

/* List — generic row-aware vertical container. Children are arbitrary
 * widgets; the list tracks a selected child and fires on_select on
 * click. CSS configures layout (gap, padding). */
struct yetty_ygui_widget *yetty_ygui_engine_list(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h);

/* Tree node — a row with a chevron + label header and an auto-allocated
 * children list. add_child(tree_node_children(node), …) populates the
 * sub-rows. set_expanded toggles visibility of the children list (which
 * already participates in flex layout). */
struct yetty_ygui_widget *yetty_ygui_engine_tree_node(struct yetty_ygui_engine *engine,
                                                      const char *id, const char *label);

/* Rich content surface — holds a ydraw-core buffer of pre-built primitives
 * (text spans, SDF shapes, yplot, yimage, ...). The widget reserves a flex
 * layout box; at render time every primitive is translated by the widget's
 * resolved (layout_x, layout_y), so authors compose content in widget-local
 * coordinates (0..w, 0..h) and don't have to know where the widget will end
 * up on the canvas.
 *
 * Two constructors:
 *   - rich(engine, id, x, y, w, h)            — empty surface. Populate it
 *                                                later with set_buffer or
 *                                                set_yaml.
 *   - rich_from_yaml(... , yaml, yaml_len)    — convenience: parses the
 *                                                YAML via ydraw-yaml and
 *                                                hands the buffer to the
 *                                                widget. Equivalent to
 *                                                rich() + set_yaml(). */
struct yetty_ygui_widget *yetty_ygui_engine_rich(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_engine_rich_from_yaml(struct yetty_ygui_engine *engine,
                                                           const char *id, float x, float y,
                                                           float w, float h, const char *yaml,
                                                           size_t yaml_len);

/* Replace the widget's current content with primitives parsed from a YAML
 * string. Mirrors yetty_ydraw_yaml_parse internally; ownership of the
 * resulting buffer stays with the widget. Returns the parser's error result
 * unchanged (use YETTY_IS_ERR / propagate via YETTY_RETURN_IF_ERR). */
struct yetty_ycore_void_result yetty_ygui_widget_rich_set_yaml(struct yetty_ygui_widget *widget,
                                                               const char *yaml, size_t yaml_len);

/* Transfer ownership of an externally-constructed ydraw-core buffer into
 * the widget. The widget destroys the buffer in its own destroy hook (and
 * on the next set_yaml / set_buffer / clear call). Passing NULL clears. */
void yetty_ygui_widget_rich_set_buffer(struct yetty_ygui_widget *widget,
                                       struct yetty_ydraw_draw_list *buffer);

/* Drop the current buffer without replacement. Equivalent to
 * set_buffer(widget, NULL). */
void yetty_ygui_widget_rich_clear(struct yetty_ygui_widget *widget);

/* Window — top-level frame containing every other widget in an app.
 * Draws a title bar with a centred title text and a close 'x' button
 * pinned to the upper-right corner. Clicking the close button stops
 * the engine event loop AND tells engine_destroy to skip the YDRAW
 * clear, so the last rendered frame stays on the ydraw canvas after
 * the app exits.
 *
 * The window auto-allocates an inner body widget (a flex-column vbox)
 * — get a handle to it via yetty_ygui_widget_window_body() and add
 * children there as usual. The body is laid out below the title bar
 * via padding-top, so children don't have to know about the title
 * area. Pass NULL or "" for `title` to render a bare title bar
 * (close button still drawn). */
struct yetty_ygui_widget *yetty_ygui_engine_window(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *title);

/* Returns the inner body container (a vbox). Add app widgets here. */
struct yetty_ygui_widget *yetty_ygui_widget_window_body(struct yetty_ygui_widget *window);

void yetty_ygui_widget_window_set_title(struct yetty_ygui_widget *window, const char *title);
const char *yetty_ygui_widget_window_get_title(const struct yetty_ygui_widget *window);

/* Optional callback fired when the close button is clicked, BEFORE the
 * default behaviour runs. Return is ignored — set the callback to
 * intercept the close (e.g. show a "save changes?" popup, then call
 * engine_close_preserve yourself when ready). The default close path
 * still runs after the callback unless you override it via a custom
 * close_action set on the window (no API today; for v1 the callback is
 * notify-only). */
void yetty_ygui_widget_window_on_close(struct yetty_ygui_widget *window,
                                       ygui_click_callback_t callback, void *userdata);

/* Stop the engine loop and arrange for engine_destroy to leave the
 * last rendered ydraw frame on the canvas (no YDRAW_CLEAR sent).
 * This is what the window's close button calls; user code can call it
 * directly for the same "exit but keep view" semantics. */
void yetty_ygui_engine_close_preserve(struct yetty_ygui_engine *engine);

/* Hand a popup menu to the window. The window's title-bar hamburger
 * button toggles its OPEN flag instead of closing the app. Pass NULL to
 * remove a previously-attached menu (the hamburger reverts to acting
 * as a direct close button). The window does NOT take ownership — the
 * menu is a normal engine-managed widget and is freed alongside the
 * engine. */
void yetty_ygui_widget_window_set_menu(struct yetty_ygui_widget *window,
                                       struct yetty_ygui_widget *menu);

/*=============================================================================
 * Window menubar + statusbar
 *
 * Attach a menubar (any widget; typically a MENUBAR) between the
 * window's title strip and its body, and a statusbar (typically a
 * STATUSBAR) pinned to the bottom of the window. The widget is
 * re-parented into the window's child list and removed from the
 * engine's top-level chain (same lifecycle as window_body children).
 * Passing NULL clears the slot — the previously-set widget is
 * un-parented back to the engine's top-level chain (so the caller
 * remains responsible for destroying it via the engine).
 *===========================================================================*/

void yetty_ygui_widget_window_set_menubar(struct yetty_ygui_widget *window,
                                          struct yetty_ygui_widget *menubar);
void yetty_ygui_widget_window_set_statusbar(struct yetty_ygui_widget *window,
                                            struct yetty_ygui_widget *statusbar);

/* Popup menu — a floating, vertically-stacked list of clickable items.
 * Inherits the visuals of the popup dialog (rounded body + drop
 * shadow + optional modal overlay) and specialises it for menus: each
 * row is just a label + callback (no sub-widgets), the menu auto-grows
 * in height as items are added, and clicking an item fires its
 * callback and then closes the menu.
 *
 * Typical wiring:
 *   m = yetty_ygui_engine_popup_menu(engine, "app_menu", 0, 0, 220);
 *   yetty_ygui_widget_popup_menu_add_item(m, "About",   on_about, app);
 *   yetty_ygui_widget_popup_menu_add_separator(m);
 *   yetty_ygui_widget_popup_menu_add_item(m, "Close",   on_close, app);
 *   yetty_ygui_widget_window_set_menu(window, m);
 *
 * The menu starts closed. Open it via yetty_ygui_widget_popup_menu_open_at
 * (positions then toggles OPEN), or let the window's hamburger toggle it. */
struct yetty_ygui_widget *yetty_ygui_engine_popup_menu(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w);

/* Append a clickable item. cb fires when clicked; the menu auto-closes
 * after the callback returns. */
void yetty_ygui_widget_popup_menu_add_item(struct yetty_ygui_widget *menu, const char *label,
                                           ygui_click_callback_t cb, void *userdata);

/* Append a drill-down item — same as add_item, but the menu STAYS OPEN
 * after the callback returns. Used to navigate into a submenu in place:
 * the callback typically calls popup_menu_clear + popup_menu_set_title +
 * popup_menu_set_back + popup_menu_add_item to re-populate the menu
 * with the child level's content. The caller is conventionally
 * expected to render a "▸" suffix in the label so users see it's a
 * drill-down. */
void yetty_ygui_widget_popup_menu_add_drill_item(struct yetty_ygui_widget *menu, const char *label,
                                                 ygui_click_callback_t cb, void *userdata);

/* Append a non-interactive separator row (a thin divider). */
void yetty_ygui_widget_popup_menu_add_separator(struct yetty_ygui_widget *menu);

void yetty_ygui_widget_popup_menu_open_at(struct yetty_ygui_widget *menu, float x, float y);
void yetty_ygui_widget_popup_menu_close(struct yetty_ygui_widget *menu);
void yetty_ygui_widget_popup_menu_set_modal(struct yetty_ygui_widget *menu, int modal);
int yetty_ygui_widget_popup_menu_is_open(const struct yetty_ygui_widget *menu);

/* Remove every item / separator from the menu. The geometry collapses
 * back to header + padding. Used by drill-down menus that re-populate
 * in place when the user navigates between levels. The menu stays open;
 * the caller is responsible for adding the new level's items + setting
 * the new title / back handler. */
void yetty_ygui_widget_popup_menu_clear(struct yetty_ygui_widget *menu);

/* Set the header label rendered at the top of the menu body (the
 * "breadcrumb" line — e.g. "Menu" or "Menu › New view"). NULL/empty
 * removes the header. The header is shown above the items and pushes
 * the first row down by one row-height. */
void yetty_ygui_widget_popup_menu_set_title(struct yetty_ygui_widget *menu, const char *title);

/* Install the back-handler. When non-NULL, a `<` chevron button is
 * painted at the left side of the header; clicking it fires the
 * callback and does NOT close the menu (the callback typically calls
 * popup_menu_clear + populates the parent level). Pass NULL to remove
 * the back button (root level). */
void yetty_ygui_widget_popup_menu_set_back(struct yetty_ygui_widget *menu,
                                           ygui_click_callback_t on_back, void *userdata);

/* Tabbar — browser-style tab strip across the top of the widget's box, with
 * one content panel per tab below. Only the active panel is rendered/laid
 * out; clicking a header swaps the active tab and (optionally) fires
 * on_change with the new index in `value`.
 *
 * Each call to add_tab creates a new vbox panel sized to fill the area
 * below the header strip. The returned panel is a normal ygui widget — add
 * children to it the usual way (yetty_ygui_widget_add_child) or apply CSS.
 *
 * The tabbar is itself a flex column container, so it composes inside the
 * usual hbox / vbox layouts (set_size_percent, flex: 1 0 0, ...).
 *
 * Use change_callback on the tabbar (yetty_ygui_widget_tabbar_on_change) to
 * react to tab switches — the new index is delivered through the standard
 * `float value` field. */
struct yetty_ygui_widget *yetty_ygui_engine_tabbar(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h);

struct yetty_ygui_widget *yetty_ygui_widget_tabbar_add_tab(struct yetty_ygui_widget *tabbar,
                                                           const char *label);

void yetty_ygui_widget_tabbar_set_active(struct yetty_ygui_widget *tabbar, int index);
int yetty_ygui_widget_tabbar_get_active(const struct yetty_ygui_widget *tabbar);
int yetty_ygui_widget_tabbar_count(const struct yetty_ygui_widget *tabbar);

void yetty_ygui_widget_tabbar_on_change(struct yetty_ygui_widget *tabbar,
                                        ygui_change_callback_t callback, void *userdata);

/* Programmatically remove a tab. Frees its panel and label, compacts the
 * internal arrays, and re-anchors the active index (advancing to the
 * next tab when the removed one was active). Also called when the
 * user clicks the per-tab close 'x' button. */
void yetty_ygui_widget_tabbar_remove_tab(struct yetty_ygui_widget *tabbar, int index);

/* Optional notify-only callback fired BEFORE the default close path
 * runs (which removes the tab). The tab index is delivered through
 * the `value` argument — same shape as the on_change callback. */
void yetty_ygui_widget_tabbar_on_tab_close(struct yetty_ygui_widget *tabbar,
                                           ygui_change_callback_t callback, void *userdata);

/* Optional "+" new-tab button. When `callback` is non-NULL the tabbar
 * renders a small "+" pill at the end of the header strip and routes
 * clicks on it to the callback. The callback is responsible for
 * appending a new tab (via widget_tabbar_add_tab) or asking a host
 * model to do so. Pass NULL to hide the button. */
void yetty_ygui_widget_tabbar_on_new_tab(struct yetty_ygui_widget *tabbar,
                                         ygui_click_callback_t callback, void *userdata);

/* Uniform per-button size used by tab close 'x' and (via the window
 * widget) the hamburger menu. Useful when an app builds custom UI
 * elements that should visually match the tabbar's close buttons. */
float yetty_ygui_tabbar_button_size(void);

/*=============================================================================
 * Widget Callbacks
 *===========================================================================*/

/* Button */
void yetty_ygui_widget_button_on_click(struct yetty_ygui_widget *button,
                                       ygui_click_callback_t callback, void *userdata);

/* Slider */
void yetty_ygui_widget_slider_on_change(struct yetty_ygui_widget *slider,
                                        ygui_change_callback_t callback, void *userdata);

/* Checkbox */
void yetty_ygui_widget_checkbox_on_change(struct yetty_ygui_widget *checkbox,
                                          ygui_check_callback_t callback, void *userdata);

/* TextInput */
void yetty_ygui_widget_textinput_on_change(struct yetty_ygui_widget *input,
                                           ygui_text_callback_t callback, void *userdata);

/*=============================================================================
 * Widget Hierarchy
 *===========================================================================*/

void yetty_ygui_widget_add_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *child);
void yetty_ygui_widget_remove_child(struct yetty_ygui_widget *parent,
                                    struct yetty_ygui_widget *child);
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
struct pixel_coord_result yetty_ygui_widget_get_position(const struct yetty_ygui_widget *widget);

void yetty_ygui_widget_set_size(struct yetty_ygui_widget *widget, float w, float h);
struct pixel_size_result yetty_ygui_widget_get_size(const struct yetty_ygui_widget *widget);

/* Resolved (post-layout) absolute outer box. Valid after engine_layout() or
 * engine_render() has run. */
struct rectangle_result yetty_ygui_widget_get_layout_box(const struct yetty_ygui_widget *widget);

/* Resolved (post-layout) content area — the layout box minus padding. This
 * is the rectangle inside which the widget's children are positioned, and
 * therefore the right "client area" to query when sizing an embedded
 * producer (yzoo, yjungle, ...) or any other content that must fit within
 * a container without spilling into its padding band. */
struct rectangle_result yetty_ygui_widget_get_content_box(const struct yetty_ygui_widget *widget);

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
void yetty_ygui_widget_set_position_mode(struct yetty_ygui_widget *widget,
                                         ygui_position_t position);
void yetty_ygui_widget_set_flex_basis_percent(struct yetty_ygui_widget *widget, float pct);
void yetty_ygui_widget_set_size_percent(struct yetty_ygui_widget *widget, float w_pct, float h_pct);
void yetty_ygui_widget_set_min_size_percent(struct yetty_ygui_widget *widget, float min_w_pct,
                                            float min_h_pct);
void yetty_ygui_widget_set_max_size_percent(struct yetty_ygui_widget *widget, float max_w_pct,
                                            float max_h_pct);

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
void yetty_ygui_widget_slider_set_range(struct yetty_ygui_widget *widget, float min_val,
                                        float max_val);

/* Checkbox */
void yetty_ygui_widget_checkbox_set_checked(struct yetty_ygui_widget *widget, int checked);
int yetty_ygui_widget_checkbox_get_checked(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_checkbox_set_label(struct yetty_ygui_widget *widget, const char *label);

/* TextInput */
void yetty_ygui_widget_textinput_set_text(struct yetty_ygui_widget *widget, const char *text);
const char *yetty_ygui_widget_textinput_get_text(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_textinput_set_placeholder(struct yetty_ygui_widget *widget,
                                                 const char *text);

/* Panel */
void yetty_ygui_widget_panel_set_scroll(struct yetty_ygui_widget *widget, float x, float y);
void yetty_ygui_widget_panel_get_scroll(const struct yetty_ygui_widget *widget, float *x, float *y);
void yetty_ygui_widget_panel_set_content_size(struct yetty_ygui_widget *widget, float w, float h);
void yetty_ygui_widget_panel_set_header_height(struct yetty_ygui_widget *widget, float h);

/* Progress */
void yetty_ygui_widget_progress_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_progress_get_value(const struct yetty_ygui_widget *widget);

/* Dropdown */
void yetty_ygui_widget_dropdown_set_options(struct yetty_ygui_widget *widget, const char **options,
                                            int count);
void yetty_ygui_widget_dropdown_set_selected(struct yetty_ygui_widget *widget, int index);
int yetty_ygui_widget_dropdown_get_selected(const struct yetty_ygui_widget *widget);

/* ColorPicker */
void yetty_ygui_widget_colorpicker_set_color(struct yetty_ygui_widget *widget, float r, float g,
                                             float b, float a);
void yetty_ygui_widget_colorpicker_get_color(const struct yetty_ygui_widget *widget, float *r,
                                             float *g, float *b, float *a);

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
void yetty_ygui_widget_collapsing_header_set_label(struct yetty_ygui_widget *widget,
                                                   const char *label);
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
void yetty_ygui_widget_choicebox_set_options(struct yetty_ygui_widget *widget, const char **options,
                                             int count);
void yetty_ygui_widget_choicebox_set_selected(struct yetty_ygui_widget *widget, int index);
int yetty_ygui_widget_choicebox_get_selected(const struct yetty_ygui_widget *widget);

/* Scrollbars (V/H share the same value 0..1) */
void yetty_ygui_widget_scrollbar_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_scrollbar_get_value(const struct yetty_ygui_widget *widget);

/*=============================================================================
 * Radio group / Radio button
 *
 * A RADIO_GROUP is a single-select container. RADIOs are its children;
 * clicking one selects it and deselects its siblings inside the same
 * group. The group is also a flex column by default — apply CSS to
 * customise (e.g. `flex-direction: row;` for horizontal radio bars).
 *
 *   group = yetty_ygui_engine_radio_group(eng, "fruit", 0, 0, 200, 100);
 *   yetty_ygui_widget_radio_group_add(group, "r_apple",  "Apple");
 *   yetty_ygui_widget_radio_group_add(group, "r_banana", "Banana");
 *   yetty_ygui_widget_radio_group_set_selected_index(group, 0);
 *   yetty_ygui_widget_radio_group_on_change(group, my_cb, NULL);
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_radio_group(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h);

/* Append a radio. Returns the newly-created radio widget so the caller
 * can apply CSS / hook a per-radio callback if needed. */
struct yetty_ygui_widget *yetty_ygui_widget_radio_group_add(struct yetty_ygui_widget *group,
                                                            const char *id, const char *label);

void yetty_ygui_widget_radio_group_set_selected_index(struct yetty_ygui_widget *group, int index);
int yetty_ygui_widget_radio_group_get_selected_index(const struct yetty_ygui_widget *group);

/* Cb gets the new index in `value` (cast from float). -1 means none. */
void yetty_ygui_widget_radio_group_on_change(struct yetty_ygui_widget *group,
                                             ygui_change_callback_t cb, void *userdata);

/*=============================================================================
 * Spinner — numeric input with ± buttons.
 *
 * Wheel and Up/Down keys also step. The widget paints its value with
 * `precision` decimal places (set 0 for integer-style); editing the
 * displayed string by click is not supported yet — for arbitrary entry
 * use a textinput.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_spinner(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, float min_val, float max_val,
                                                    float step, float value);

void yetty_ygui_widget_spinner_set_value(struct yetty_ygui_widget *widget, float value);
float yetty_ygui_widget_spinner_get_value(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_spinner_set_range(struct yetty_ygui_widget *widget, float min_val,
                                         float max_val);
void yetty_ygui_widget_spinner_set_step(struct yetty_ygui_widget *widget, float step);
void yetty_ygui_widget_spinner_set_precision(struct yetty_ygui_widget *widget, int decimals);
void yetty_ygui_widget_spinner_on_change(struct yetty_ygui_widget *widget,
                                         ygui_change_callback_t cb, void *userdata);

/*=============================================================================
 * Splitter — drag-to-resize divider.
 *
 * Place between two siblings inside a flex hbox or vbox. Detects the
 * parent's flex direction automatically (row → resizes widths,
 * column → resizes heights). On drag, mutates `authored_w` / `authored_h`
 * of the adjacent siblings so the layout pass re-flows.
 *
 * IMPORTANT: the siblings being resized must use authored sizes for
 * their main-axis dimension. Do NOT apply `flex: 1 0 0` (which sets
 * flex-basis to 0 and ignores authored size). Use `flex-grow: 0` and
 * an explicit width/height, or let the flex pass infer from the
 * authored size.
 *
 *   left  = panel(...,  300, 0);
 *   split = yetty_ygui_engine_splitter(eng, "sp", 0, 0, 6, 0);
 *   right = panel(...,  500, 0);
 *   add_child(hbox, left); add_child(hbox, split); add_child(hbox, right);
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_splitter(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h);

/* Minimum size enforced on each side during drag. Default 30 px. */
void yetty_ygui_widget_splitter_set_min(struct yetty_ygui_widget *widget, float min_size);

/* External-drive mode. When set, the splitter does NOT mutate its
 * sibling widgets' authored sizes on drag — instead it fires the
 * callback with a signed pixel `delta` along the main axis (positive =
 * sibling-after grows by `delta`, sibling-before shrinks). The host is
 * then responsible for re-laying out and re-positioning the splitter
 * widget itself. Use this when the divided regions aren't sibling
 * widgets in a flex container (e.g. yui's pane tile tree). */
void yetty_ygui_widget_splitter_on_change(struct yetty_ygui_widget *widget,
                                          ygui_change_callback_t cb, void *userdata);

/* Pin the drag axis explicitly. row=1 → vertical bar that resizes left
 * vs right; row=0 → horizontal bar that resizes top vs bottom. Use
 * this together with splitter_on_change for absolute-positioned
 * splitters whose parent isn't a flex container (so the auto-detect
 * from the parent's flex direction doesn't apply). */
void yetty_ygui_widget_splitter_set_axis(struct yetty_ygui_widget *widget, int row);

/* Read back the axis override last set via splitter_set_axis. -1 when
 * not explicitly pinned (auto-detect mode) or when widget isn't a
 * splitter. */
int yetty_ygui_widget_splitter_get_axis(const struct yetty_ygui_widget *widget);

/*=============================================================================
 * Modal dialog — popup with a title, message, and bottom button row.
 *
 * Convenience helper that builds a regular POPUP with the standard
 * layout (title bar, body label, button row) so an app doesn't have
 * to hand-wire the same pattern every time.
 *
 *   const char *btns[] = {"Cancel", "OK"};
 *   yetty_ygui_widget_dialog_args args = {
 *       .id = "save_q", .title = "Save changes?",
 *       .message = "Unsaved edits will be lost.",
 *       .buttons = btns, .button_count = 2,
 *       .on_button = my_cb, .userdata = self,
 *   };
 *   dlg = yetty_ygui_engine_dialog(engine, &args);
 *   yetty_ygui_widget_popup_set_open(dlg, 1);
 *
 * Button click fires on_button(dlg, index, userdata) and closes the
 * dialog. The dialog widget is owned by the engine (regular popup
 * lifetime); the helper does not destroy it. */

typedef void (*yetty_ygui_dialog_button_fn)(struct yetty_ygui_widget *dialog, int button_index,
                                            void *userdata);

struct yetty_ygui_dialog_args {
    const char *id;
    const char *title;
    const char *message;
    const char *const *buttons; /* button_count labels */
    int button_count;
    yetty_ygui_dialog_button_fn on_button;
    void *userdata;
    int modal; /* non-zero → dim the rest of the canvas while open */
};

struct yetty_ygui_widget *yetty_ygui_engine_dialog(struct yetty_ygui_engine *engine,
                                                   const struct yetty_ygui_dialog_args *args);

/*=============================================================================
 * Multi-line text area — editable text with cursor + keyboard nav.
 *
 * Backing store: a single owned `char *` with embedded '\n' line
 * breaks. Cursor is a byte offset. Viewport scrolls by line when the
 * cursor moves off-screen.
 *
 * Supported input (v1):
 *   - text input (via the engine's text_input path)
 *   - Backspace (delete char left of cursor)
 *   - Enter   (insert newline)
 *   - Left/Right (move cursor by char)
 *   - Up/Down    (move cursor by line)
 *   - Home/End   (start / end of current line)
 *
 * Not yet implemented: selection, clipboard, undo, word wrap,
 * line numbers, find/replace.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_textarea(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char *initial_text);

/* Same as engine_textarea but with word-wrap enabled at construction.
 * Equivalent to engine_textarea(...) + widget_textarea_set_wrap(...,1).
 * Use this for read-only / display-oriented text where lines may exceed
 * the widget's width — they wrap at word boundaries instead of being
 * truncated at the right edge. */
struct yetty_ygui_widget *yetty_ygui_engine_textarea_wrapped(struct yetty_ygui_engine *engine,
                                                             const char *id, float x, float y,
                                                             float w, float h,
                                                             const char *initial_text);

void yetty_ygui_widget_textarea_set_text(struct yetty_ygui_widget *widget, const char *text);
const char *yetty_ygui_widget_textarea_get_text(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_textarea_on_change(struct yetty_ygui_widget *widget, ygui_text_callback_t cb,
                                          void *userdata);

/* Toggle word-wrap. When enabled, lines that don't fit the widget's
 * inner width are broken at word boundaries (or hard-broken mid-word
 * for tokens wider than the widget). When disabled (default), lines
 * are truncated at the right edge — text is never painted outside the
 * widget's surface in either mode. */
void yetty_ygui_widget_textarea_set_wrap(struct yetty_ygui_widget *widget, int wrap);

/*=============================================================================
 * Scrollarea — generic vertical scrolling container.
 *
 * Behaves as a flex-column container that renders its children with a
 * vertical scroll offset. Wheel events scroll. Exposes the scrollable
 * interface so a vscrollbar can be bound to it via scrollbar_bind.
 *
 *   sa = yetty_ygui_engine_scrollarea(eng, "sa", 0, 0, 0, 0);
 *   yetty_ygui_widget_apply_css(sa, "flex: 1 0 0; align-self: stretch;");
 *   yetty_ygui_widget_add_child(sa, my_long_vbox);
 *
 * The scrollarea computes its own content height from the layout
 * (preflight intrinsic-sizing pass). scroll_y clamps to
 * max(0, content_h - viewport_h).
 *
 * Hit testing: render_all adjusts every visible descendant's
 * layout_y by -scroll_y so the spatial grid sees on-screen positions
 * (clicks work correctly even when scrolled).
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_scrollarea(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h);

void yetty_ygui_widget_scrollarea_scroll_to(struct yetty_ygui_widget *widget, float y);
void yetty_ygui_widget_scrollarea_scroll_by(struct yetty_ygui_widget *widget, float dy);
float yetty_ygui_widget_scrollarea_get_scroll(const struct yetty_ygui_widget *widget);

/*=============================================================================
 * Toggle switch — boolean on/off with sliding thumb.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_toggle(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *label, int on);

void yetty_ygui_widget_toggle_set_on(struct yetty_ygui_widget *widget, int on);
int yetty_ygui_widget_toggle_get_on(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_toggle_on_change(struct yetty_ygui_widget *widget, ygui_check_callback_t cb,
                                        void *userdata);

/*=============================================================================
 * Chip / tag — small pill label with optional close button.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_chip(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h,
                                                 const char *label, int closable);

void yetty_ygui_widget_chip_on_remove(struct yetty_ygui_widget *widget, ygui_click_callback_t cb,
                                      void *userdata);

/*=============================================================================
 * Breadcrumbs — horizontal segments separated by ' › '.
 *
 * on_change fires with the clicked segment index as the float value.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_breadcrumbs(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h, const char *const *labels, int n);

void yetty_ygui_widget_breadcrumbs_on_change(struct yetty_ygui_widget *widget,
                                             ygui_change_callback_t cb, void *userdata);

/*=============================================================================
 * Indeterminate progress
 *
 * When set, the progress widget renders a sliding bar (no value).
 * Useful for "working…" indicators where no percentage is known.
 *===========================================================================*/

void yetty_ygui_widget_progress_set_indeterminate(struct yetty_ygui_widget *widget,
                                                  int indeterminate);

/*=============================================================================
 * Right-click context menu
 *
 * Attach any popup_menu widget to any other widget. Right-clicking
 * inside the target's bounding box opens the menu at the cursor
 * position. Pass NULL to clear.
 *
 * The menu widget is borrowed — caller must keep it alive (regular
 * engine widget lifetime is fine).
 *===========================================================================*/

void yetty_ygui_widget_set_context_menu(struct yetty_ygui_widget *widget,
                                        struct yetty_ygui_widget *menu);

/*=============================================================================
 * Combo box — editable textinput with a dropdown of suggestions.
 *
 * Click the right-edge ▼ arrow to open the suggestions list. Type
 * directly into the field to enter any value (free-form). Clicking
 * a suggestion replaces the field's text.
 *
 * on_change fires whenever the text changes (typing OR picking a
 * suggestion), with the new text in `data.string_value`.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_combo(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h,
                                                  const char *initial_text,
                                                  const char *const *options, int option_count);

void yetty_ygui_widget_combo_set_text(struct yetty_ygui_widget *widget, const char *text);
const char *yetty_ygui_widget_combo_get_text(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_combo_on_change(struct yetty_ygui_widget *widget, ygui_text_callback_t cb,
                                       void *userdata);

/*=============================================================================
 * Menubar — top-of-window strip of menu buttons.
 *
 * Each button label opens its attached popup_menu when clicked. The
 * menus are borrowed pointers (regular engine-managed popup_menu
 * widgets); the menubar does not own them.
 *
 *   bar = yetty_ygui_engine_menubar(eng, "mb", 0, 0, 800, 28);
 *   file_m = yetty_ygui_engine_popup_menu(eng, "fm", 0, 0, 180);
 *   yetty_ygui_widget_popup_menu_add_item(file_m, "New",  ...);
 *   yetty_ygui_widget_menubar_add(bar, "File", file_m);
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_menubar(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h);

void yetty_ygui_widget_menubar_add(struct yetty_ygui_widget *menubar, const char *label,
                                   struct yetty_ygui_widget *menu);

/*=============================================================================
 * Stepper — numbered-step progress indicator.
 *
 * Renders horizontal circles numbered 1..N with a label below each.
 * `current` selects the active step: steps < current paint as
 * completed (filled accent), step == current paints active (filled +
 * outlined), steps > current paint as upcoming (outlined only).
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_stepper(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, const char *const *labels,
                                                    int n_steps);

void yetty_ygui_widget_stepper_set_current(struct yetty_ygui_widget *widget, int step);
int yetty_ygui_widget_stepper_get_current(const struct yetty_ygui_widget *widget);

/*=============================================================================
 * Sortable / resizable table columns.
 *
 * - Click a column header to sort by it (cycle none → asc → desc → none).
 *   The table sorts its rows in place by the column's string value.
 *   on_select index is preserved by re-locating the previously-selected
 *   row's pointer after sort.
 * - Drag the right edge of a header cell (~6 px wide grip) to resize the
 *   column. The new width is stored in column_widths[i].
 *===========================================================================*/

void yetty_ygui_widget_table_set_sortable(struct yetty_ygui_widget *table, int enabled);
int yetty_ygui_widget_table_get_sort_column(const struct yetty_ygui_widget *table);
int yetty_ygui_widget_table_get_sort_order(const struct yetty_ygui_widget *table);
void yetty_ygui_widget_table_sort_by(struct yetty_ygui_widget *table, int column, int descending);

/*=============================================================================
 * Date picker — compact month calendar.
 *
 * Click a day to select; click ← / → in the header to step months.
 * on_change fires with a packed integer `yyyymmdd` (year*10000 +
 * month*100 + day) so apps can decode without a multi-value callback.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_datepicker(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h);

void yetty_ygui_widget_datepicker_set_date(struct yetty_ygui_widget *widget, int year,
                                           int month_0_based, int day);
void yetty_ygui_widget_datepicker_get_date(const struct yetty_ygui_widget *widget, int *year,
                                           int *month_0_based, int *day);
void yetty_ygui_widget_datepicker_on_change(struct yetty_ygui_widget *widget,
                                            ygui_change_callback_t cb, void *userdata);

/*=============================================================================
 * File picker — directory listing widget.
 *
 * Lists the entries of a directory. Click ".." to go up. Click a
 * directory to navigate into it. Click a file to select it. The
 * selected path is the absolute path of the currently-selected entry
 * inside the current working directory.
 *
 * The widget owns its current path; navigation updates it and re-reads
 * the directory.
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_filepicker(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h, const char *initial_dir);

const char *yetty_ygui_widget_filepicker_get_cwd(const struct yetty_ygui_widget *widget);
/* Returns the currently-selected entry name (e.g. "main.c"); NULL if
 * nothing is selected. Combine with get_cwd to form a full path. */
const char *yetty_ygui_widget_filepicker_get_selected(const struct yetty_ygui_widget *widget);
void yetty_ygui_widget_filepicker_on_change(struct yetty_ygui_widget *widget,
                                            ygui_text_callback_t cb, void *userdata);

/*=============================================================================
 * Statusbar — bottom-of-window strip with primary + optional secondary text.
 *
 * Layout: [ left_text ........................................ right_text ]
 *
 * Typical wiring: an app sets `left_text` to the current activity
 * ("Ready", "Loading…") and `right_text` to a coordinate / mode
 * indicator ("ln 12, col 3", "INSERT").
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_statusbar(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char *left_text);

void yetty_ygui_widget_statusbar_set_left(struct yetty_ygui_widget *widget, const char *text);
void yetty_ygui_widget_statusbar_set_right(struct yetty_ygui_widget *widget, const char *text);

/*=============================================================================
 * Engine-wide bars: titlebar + menubar + statusbar
 *
 * For apps that don't use the WINDOW widget. The engine pins the
 * supplied widget to its full canvas width at the top (titlebar
 * then menubar below it) or bottom (statusbar) on every layout pass.
 * The bar widgets are normal top-level widgets in the engine's chain;
 * clear by passing NULL.
 *
 * Don't combine engine-level bars with window-level bars in the same
 * app — they overlap. Apps using window_set_menubar / _set_statusbar
 * typically also use the window's built-in title bar.
 *===========================================================================*/

void yetty_ygui_engine_set_titlebar(struct yetty_ygui_engine *engine,
                                    struct yetty_ygui_widget *widget);
void yetty_ygui_engine_set_menubar(struct yetty_ygui_engine *engine,
                                   struct yetty_ygui_widget *widget);
void yetty_ygui_engine_set_statusbar(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/* Bind a scrollbar to a scrollable widget (today: ypdf — anything that
 * exposes the internal scrollable interface). The scrollbar becomes a
 * pure view of the target's scroll state:
 *
 *   - render: thumb position from target.scroll / target.max_scroll;
 *             thumb size proportional to target.viewport / target.content
 *             (the bigger the document, the smaller the thumb).
 *   - input:  click / drag / wheel on the scrollbar all call
 *             target.scroll_to(...) — one canonical path.
 *   - sync:   target.scroll_to marks the scrollbar dirty so the thumb
 *             repaints in the same frame, including when the change
 *             originated elsewhere (wheel on the target, keyboard, ...).
 *
 * No callbacks to wire, no reentrancy guards. Pass `target = NULL` to
 * unbind and revert to free-running mode (the scrollbar value is what
 * yetty_ygui_widget_scrollbar_set_value puts there). */
void yetty_ygui_widget_scrollbar_bind(struct yetty_ygui_widget *scrollbar,
                                      struct yetty_ygui_widget *target);

/* List */
void yetty_ygui_widget_list_set_selected(struct yetty_ygui_widget *list,
                                         struct yetty_ygui_widget *child);
struct yetty_ygui_widget *yetty_ygui_widget_list_get_selected(const struct yetty_ygui_widget *list);
void yetty_ygui_widget_list_on_select(struct yetty_ygui_widget *list, ygui_click_callback_t cb,
                                      void *userdata);

/* Table — header row + N data rows × N columns of strings.
 *
 * Construction:
 *   t = yetty_ygui_engine_table(engine, "procs", x, y, w, h);
 *   const char *names[]   = {"PID", "USER",  "%CPU", "COMMAND"};
 *   const float widths[]  = {  60,    100,      60,        0  };  // 0 = stretch
 *   yetty_ygui_widget_table_set_columns(t, names, widths, 4);
 *
 *   const char *cells[] = {"1", "root", "0.0", "/sbin/init"};
 *   yetty_ygui_widget_table_add_row(t, cells, 4);
 *
 * Cells are duplicated on insertion; the table owns them. set_row replaces
 * an existing row's contents; clear_rows wipes everything but keeps the
 * column configuration. on_select fires when a non-header row is clicked. */
typedef void (*yetty_ygui_table_select_fn)(struct yetty_ygui_widget *table, int row,
                                           void *userdata);

struct yetty_ygui_widget *yetty_ygui_engine_table(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h);

void yetty_ygui_widget_table_set_columns(struct yetty_ygui_widget *table, const char *const *names,
                                         const float *widths, int n_columns);

void yetty_ygui_widget_table_clear_rows(struct yetty_ygui_widget *table);

/* Append a row of n_cells strings (must equal the column count). */
void yetty_ygui_widget_table_add_row(struct yetty_ygui_widget *table, const char *const *cells,
                                     int n_cells);

/* Replace contents of an existing row. No-op if row index is out of range. */
void yetty_ygui_widget_table_set_row(struct yetty_ygui_widget *table, int row,
                                     const char *const *cells, int n_cells);

int yetty_ygui_widget_table_row_count(const struct yetty_ygui_widget *table);

void yetty_ygui_widget_table_set_selected(struct yetty_ygui_widget *table, int row);
int yetty_ygui_widget_table_get_selected(const struct yetty_ygui_widget *table);

void yetty_ygui_widget_table_on_select(struct yetty_ygui_widget *table,
                                       yetty_ygui_table_select_fn cb, void *userdata);

/* 0 = use theme->row_height. */
void yetty_ygui_widget_table_set_row_height(struct yetty_ygui_widget *table, float h);

/* Tree node */
void yetty_ygui_widget_tree_node_set_label(struct yetty_ygui_widget *node, const char *label);
const char *yetty_ygui_widget_tree_node_get_label(const struct yetty_ygui_widget *node);
void yetty_ygui_widget_tree_node_set_expanded(struct yetty_ygui_widget *node, int expanded);
int yetty_ygui_widget_tree_node_is_expanded(const struct yetty_ygui_widget *node);
/* Auto-allocated children list (a YETTY_YGUI_WIDGET_LIST). Use as the
 * parent for sub-tree_nodes or any other widgets. */
struct yetty_ygui_widget *yetty_ygui_widget_tree_node_children(struct yetty_ygui_widget *node);
void yetty_ygui_widget_tree_node_on_toggle(struct yetty_ygui_widget *node, ygui_check_callback_t cb,
                                           void *userdata);

/*=============================================================================
 * Widget Lookup
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_find(struct yetty_ygui_engine *engine, const char *id);
struct yetty_ygui_widget *yetty_ygui_engine_widget_at(struct yetty_ygui_engine *engine, float x,
                                                      float y);

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

/* Soft-elevation shadow tuning. low / medium / high are vertical offsets
 * in pixels (set to 0 to disable shadows of that level). alpha is a 0..1
 * multiplier on theme.shadow's alpha channel (set to 0 to disable
 * shadows globally without zeroing each level). enable_gradient toggles
 * the subtle top-edge highlight on buttons. */
void yetty_ygui_theme_set_elevation(struct yetty_ygui_theme *theme, float low, float medium,
                                    float high, float alpha);
void yetty_ygui_theme_set_gradient(struct yetty_ygui_theme *theme, int enable);

/* Overlay the style/ygui/... block from a loaded yetty config on top of
 * the theme. Missing keys are skipped (the theme keeps its current value
 * for those fields). Hex strings are parsed via yetty_ycore_parse_hex_color
 * — see config.yaml's style section for the canonical key list.
 *
 * Client apps that want config-driven theming call this after building
 * their default theme:
 *   theme = yetty_ygui_theme_create_default();
 *   yetty_ygui_theme_apply_config(theme, config);
 *   yetty_ygui_engine_set_theme(engine, theme); */
void yetty_ygui_theme_apply_config(struct yetty_ygui_theme *theme,
                                   const struct yetty_yconfig_config *config);

/*=============================================================================
 * Testing API
 *===========================================================================*/

/* Set custom input/output file descriptors (must be called before attach/run)
 * Useful for PTY-based testing. Default is STDIN_FILENO/STDOUT_FILENO. */
void yetty_ygui_engine_set_input_fd(struct yetty_ygui_engine *engine, int fd);
void yetty_ygui_engine_set_output_fd(struct yetty_ygui_engine *engine, int fd);

/* Route the engine's ydraw frame OSC envelopes to a yetty_platform_pty
 * instead of `output_fd`. Used when ygui lives in the same process as the
 * renderer (yetty's app-level yui) to avoid a stdout round-trip.
 * `pty` is borrowed — the caller owns its lifetime and must outlive the
 * engine. Pass NULL to revert to the file-descriptor sink. */
struct yetty_platform_pty;
void yetty_ygui_engine_set_output_pty(struct yetty_ygui_engine *engine,
                                      struct yetty_platform_pty *pty);

/* Set card dimensions for testing coordinate scaling
 * These are normally set by ygui_engine_create() but can be overridden for tests */
void yetty_ygui_engine_set_card_size(struct yetty_ygui_engine *engine, int card_w, int card_h);

/* Set display pixel size directly for testing.
 * Normally this comes from OSC 777780 after show().
 * For tests, call this to set canvas size before creating widgets. */
void yetty_ygui_engine_set_display_pixel_size(struct yetty_ygui_engine *engine, float width,
                                              float height);

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

enum {
    YETTY_YGUI_VERSION_MAJOR = 0,
    YETTY_YGUI_VERSION_MINOR = 2,
    YETTY_YGUI_VERSION_PATCH = 0,
};

const char *yetty_ygui_version(void);

#ifdef __cplusplus
}
#endif

#endif /* YGUI_H */
