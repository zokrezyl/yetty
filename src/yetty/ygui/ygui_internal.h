/*
 * ygui_internal.h - Internal structures for YGui-C
 */

#ifndef YGUI_INTERNAL_H
#define YGUI_INTERNAL_H

#include <yetty/ygui/ygui.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <yetty/yplot/yplot.h>
#include <yetty/yvideo/yvideo.h>
#include <yetty/yzoo/yzoo.h>
#include <yetty/yjungle/yjungle.h>
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
 * to sit here (ydraw-capi.gen.*) is gone; widgets call yetty_ydraw_draw_list_add_cmd_add_* and
 * yetty_ydraw_draw_list_add_text directly.
 *===========================================================================*/

struct yetty_ygui_render_ctx {
    struct yetty_ydraw_draw_list *buffer;
    const struct yetty_ygui_theme *theme;
    float offset_x, offset_y;
    float clip_x, clip_y, clip_w, clip_h;
    int has_clip;
    /* When 1: emit GROUP for every visible widget regardless of its
     * `dirty` bit, and skip the DELETE prefix (CMD_ZERO at the envelope
     * head supersedes everything). Set by the engine on the first frame
     * after create / CMD_ZERO. When 0: emit only widgets whose dirty
     * bit is set, prefixing each with a DELETE record. */
    int force_full_redraw;
};

/* Wrap one widget's own drawables in a scene-canvas GROUP keyed by
 * self->group_id. Called from render_all_default and from every custom
 * render_all that needs to emit its widget's drawables. Caller passes
 * the widget's `render` function pointer (NULL is allowed — emits an
 * empty group for layout-only containers).
 *
 * Behaviour:
 *   - if widget is not dirty AND ctx->force_full_redraw == 0: no-op.
 *   - else: optionally emit DELETE(group_id), then begin_group + render
 *     + end_group, then clear widget->dirty.
 */
struct yetty_ycore_void_result yetty_ygui_widget_emit_self_in_group(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx,
    struct yetty_ycore_void_result (*render_fn)(struct yetty_ygui_widget *,
                                                struct yetty_ygui_render_ctx *));

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
 * Scrollable interface — a thin vtable any widget can expose so a
 * scrollbar (or any other view) can drive it without knowing its
 * concrete type. ypdf wires it in its constructor.
 *
 * Conventions: all four sizes are in DOCUMENT pixels. `max_scroll` is
 * `max(0, content - viewport)`. `scroll_to` clamps and fires the
 * widget's own observers (e.g. marks `scroll_observer` dirty).
 *===========================================================================*/

struct yetty_ygui_scrollable {
    float (*get_content_h)(const struct yetty_ygui_widget *self);
    float (*get_viewport_h)(const struct yetty_ygui_widget *self);
    float (*get_scroll)(const struct yetty_ygui_widget *self);
    float (*get_max_scroll)(const struct yetty_ygui_widget *self);
    void  (*scroll_to)(struct yetty_ygui_widget *self, float y);
};

/*=============================================================================
 * Widget Structure
 *===========================================================================*/

struct yetty_ygui_widget {
    /* Identity */
    char *id;
    ygui_widget_type_t type;

    /* Sequential u32 assigned at widget alloc, stable for the widget's
     * lifetime. Sent on the wire as the GROUP id so the receiver
     * (scene-canvas) can route incremental DELETE/GROUP updates back to
     * the same entity. 0 is reserved for the scene-canvas root, so the
     * sequence starts at 1. */
    uint32_t group_id;

    /* Set by any setter that changes geometry / styling / text. Cleared
     * after the widget's GROUP is re-emitted. The first frame after
     * widget alloc starts dirty. */
    uint8_t dirty;

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

    /* Optional: widget implements the scrollable interface. NULL = not
     * scrollable. Set in the constructor by widgets that own scrollable
     * content (ypdf today; panel and future ymarkdown/ybrowser later). */
    const struct yetty_ygui_scrollable *scrollable;

    /* Optional: a sibling widget (typically a vscrollbar) that observes
     * this widget's scroll position. Set by yetty_ygui_widget_scrollbar_bind.
     * On every scroll mutation the widget marks `scroll_observer->dirty`
     * so the bound scrollbar's thumb repaints in the same frame. */
    struct yetty_ygui_widget *scroll_observer;

    /* Optional: popup_menu widget that opens on right-click anywhere
     * inside this widget's bounding box. Set by
     * yetty_ygui_widget_set_context_menu. The engine routes right-click
     * dispatch to this menu, bypassing the widget's own on_press. */
    struct yetty_ygui_widget *context_menu;

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
            /* Selected child radio (pointer for stability across
             * insertion/removal). NULL = no selection (index -1). */
            struct yetty_ygui_widget *selected;
        } radio_group;

        struct {
            char *label;
            /* Back-pointer to the owning RADIO_GROUP. Set by
             * yetty_ygui_widget_radio_group_add at construction time. */
            struct yetty_ygui_widget *group;
        } radio;

        struct {
            float value, min_val, max_val, step;
            int precision; /* decimal places when rendering */
        } spinner;

        struct {
            /* Smallest size enforced on each adjacent sibling during
             * drag (main-axis dimension). */
            float min_size;
            /* Drag axis override for external-drive mode. When set
             * (1=row → vertical bar, 0=column → horizontal bar),
             * bypass splitter_axis_row's flex-parent inspection — the
             * widget is absolutely positioned over a non-flex region,
             * so there is no parent direction to infer from.
             * -1 = auto (use parent flex direction, default).
             *
             * When change_callback (on widget base) is non-NULL, the
             * splitter forwards drag deltas (in pixels along the main
             * axis) instead of mutating sibling widgets' authored
             * sizes. The host re-positions the splitter widget itself
             * after applying its layout decision. Used by yui's pane
             * tile tree, where the "siblings" aren't ygui widgets at
             * all. */
            int axis_override;
        } splitter;

        struct {
            char *text;     /* owned, NUL-terminated; may be NULL=="" */
            int   length;   /* bytes (excluding NUL) */
            int   cursor;   /* byte offset, 0..length */
            int   scroll_line; /* first visible line index */
            /* Word-wrap mode. When non-zero, the renderer breaks each
             * logical line into visual sub-lines that fit the widget's
             * inner width at word boundaries (falling back to a hard
             * break mid-word for tokens wider than the widget). When
             * zero, lines that don't fit are truncated at the widget's
             * right edge so no glyph is ever painted outside the
             * widget's surface. */
            int   wrap;
        } textarea;

        struct {
            float scroll_y;
            /* When 0 = auto-derive from children's union bounding box on
             * every render; otherwise explicit override. */
            float content_h;
        } scrollarea;

        struct {
            int on; /* 0 = off, 1 = on */
            char *label; /* optional, drawn to the right of the pill */
        } toggle;

        struct {
            char *label;
            int closable;        /* 1 = render ✕ button */
            ygui_widget_click_fn on_remove;
            void *on_remove_userdata;
        } chip;

        struct {
            char **labels;
            int n;
        } breadcrumbs;

        struct {
            char *text;     /* current text (owned) */
            int cursor_pos; /* unused today, kept for parity with textinput */
            char **options; /* preset values shown in the dropdown */
            int option_count;
            int dropdown_open;
        } combo;

        struct {
            char **labels;                  /* menu button labels */
            struct yetty_ygui_widget **menus; /* per-button popup_menu pointers (borrowed) */
            int n;
            int capacity;
        } menubar;

        struct {
            char **labels; /* one per step */
            int n_steps;
            int current; /* index into labels */
        } stepper;

        struct {
            /* Currently shown month (year/month — 0-indexed month). */
            int shown_year, shown_month;
            /* Selected date — (-1, -1, -1) = none. */
            int sel_year, sel_month, sel_day;
        } datepicker;

        struct {
            char *cwd;          /* current directory (owned) */
            char **entries;     /* listing (owned strings) */
            int entry_count;
            int selected;       /* -1 if none */
            int scroll;         /* first visible row */
        } filepicker;

        struct {
            char *left_text;  /* primary status string (owned) */
            char *right_text; /* optional right-aligned text (owned, NULL = none) */
        } statusbar;

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
            int   indeterminate; /* 1 = render sliding bar; ignore value */
            float anim_phase;    /* 0..1 sliding-bar position, updated on render */
        } progress;

        struct {
            float hue, sat, val, alpha;
        } colorpicker;

        struct {
            char *label;
            int modal;
            uint32_t header_color; /* 0 = use theme bg_header */
            float scene_w, scene_h;
            /* Title-bar drag state — set in on_press when the click
             * lands in the title strip, consumed by on_drag to move
             * the popup. Coordinates are widget-local at press time
             * (lx, ly) plus the popup's widget-coord origin (orig_x/y)
             * so the on_drag delta math is press-relative. */
            int   dragging;
            float drag_press_lx, drag_press_ly;
            float drag_orig_x,    drag_orig_y;
        } popup;

        struct {
            char *label;
            /* Height of the header strip (NOT the full expanded box).
             * Preserved across frames because `authored_h` is rewritten
             * each preflight to `header_h` (closed) or
             * `header_h + Σ children` (open) so the flex layout above
             * stacks siblings correctly. Seeded from the constructor's
             * `h` arg. */
            float header_h;
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
            float value; /* 0..1 — used when no target is bound */
            /* When bound, the scrollbar acts as a pure view of the
             * target's scroll state: it reads target.scroll on render,
             * forwards click/drag/wheel to target.scroll_to / scroll_by.
             * NULL = legacy free-running mode (value drives nothing). */
            struct yetty_ygui_widget *target;
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
            /* Sortable columns. -1 = no sort; 0..n-1 = column index.
             * sort_order: 0=asc, 1=desc. Clicking the header cycles
             * none -> asc -> desc -> none. */
            int sort_column;
            int sort_order;
            /* Column resize drag state. Active resize column = the
             * column being shrunk/grown by the drag, or -1. The user
             * grabs near the right edge of a column header to start. */
            int resizing_column;
            float resize_start_w;
            float resize_start_x;
        } table;

        struct {
            /* Owned ydraw-core buffer. NULL while empty. Authors fill the
             * primitives in widget-local coordinates (0..w, 0..h); the
             * widget's render translates them to absolute canvas coords. */
            struct yetty_ydraw_draw_list *buffer;
        } rich;

        /* Producer-widget shared shape: every per-feature widget
         * (yimage / yplot / yvideo / yzoo / yjungle) owns its source
         * state and a cache of the last-built prim. The render hook
         * checks (layout_w, layout_h) against (last_w, last_h); if
         * they differ or `cached` is NULL, it asks the producer for
         * a fresh draw_list at the current size. Same code path each
         * frame whether the widget is being painted for the first
         * time or after a resize. */
        struct {
            /* One of these two is set; the other is NULL. */
            char    *path;        /* heap-owned mp4/image file path */
            uint8_t *data;        /* heap-owned in-memory image bytes */
            size_t   data_len;

            struct yetty_ydraw_draw_list *cached;
            float    last_w;
            float    last_h;
        } yimage;

        struct {
            char *source;                            /* yexpr-plot text */
            size_t source_len;
            struct yetty_yplot_render_config cfg;    /* x/y range, flags */
            int has_cfg;
            /* Optional data buffers (live audio scopes etc.). The
             * caller's f32 sample arrays are copied so the widget can
             * hand them straight back to yplot_render on every
             * rebuild without burdening the caller. */
            struct yplot_buffer_slot {
                float    *samples;     /* heap-owned copy */
                size_t    count;
                uint32_t  color;
            } *buffers;
            size_t buffer_count;

            struct yetty_ydraw_draw_list *cached;
            float    last_w;
            float    last_h;
        } yplot;

        struct {
            /* The wire NAL bytes (heap-owned). When this is set the
             * render config carries the SPS dims + fps etc. */
            uint8_t *nal_bytes;
            size_t   nal_len;
            struct yetty_yvideo_render_config cfg;
            int has_cfg;

            struct yetty_ydraw_draw_list *cached;
            float    last_w;
            float    last_h;
        } yvideo;

        /* yzoo: snapshot-style producer driven by host ticks. Each
         * widget owns its yzoo instance + a flattened paint buffer
         * for the most recent tick. The render hook calls
         * yetty_yzoo_set_scene_size when layout_w/h changes and
         * repaints from the cached flat buffer. */
        struct {
            struct yetty_yzoo            *producer;
            struct yetty_ydraw_draw_list *raw;   /* yzoo_render target */
            struct yetty_ydraw_draw_list *flat;  /* flattened render output */
            struct yetty_yzoo_config cfg;
            int has_cfg;
            uint32_t seed;
            float t_seconds;                     /* virtual playback clock */
            float last_w;
            float last_h;
        } yzoo;

        /* yjungle: incremental producer driven by host ticks. The
         * widget mirrors the on-wire CMD_ZERO/CMD_DELETE/CMD_GROUP
         * delta stream into a `live[]` segment map; on every tick it
         * rebuilds an accumulator buffer from `live[]` and flattens
         * into `flat` for paint. */
        struct {
            struct yetty_yjungle         *producer;
            struct yetty_ydraw_draw_list *delta;
            struct yetty_ydraw_draw_list *acc;
            struct yetty_ydraw_draw_list *flat;
            struct yetty_yjungle_config cfg;
            int has_cfg;
            uint32_t seed;
            uint64_t t0_ms;                      /* start of monotonic clock */
            struct yjungle_live_seg {
                uint32_t id;
                uint8_t *bytes;
                size_t   size;
            } *live;
            size_t live_count;
            size_t live_cap;
            float last_w;
            float last_h;
        } yjungle;

        struct {
            /* Per-page sub-buffers; each is in page-local coordinates
             * (page origin at 0, 0). Allocated by the ypdf widget
             * constructor and freed by its destroy hook. */
            struct ypdf_page_entry {
                struct yetty_ydraw_draw_list *buf;
                float abs_y; /* document-absolute Y of the page top */
                float h;     /* page height */
            } *pages;
            int n_pages;
            float total_h;  /* sum of page heights + N*page_gap */
            float page_gap; /* spacer between consecutive pages */
            float scroll_y; /* current scroll position [0, max_scroll] */

            /* Concatenated full FONT prims (ttf bytes inline) for every
             * font referenced anywhere in the document. Emitted at the
             * head of every render envelope so any visible-page TEXT_SPAN
             * can resolve its font_id even when the page that originally
             * carried the full FONT prim is currently scrolled off. */
            struct yetty_ydraw_draw_list *font_header;

            /* Fired AFTER the widget mutates scroll_y so a bound
             * scrollbar (or any other observer) can sync. NULL = no
             * observer; the widget still updates internally. */
            void (*on_scroll_change)(struct yetty_ygui_widget *self,
                                     float scroll_y, float max_scroll,
                                     void *userdata);
            void *on_scroll_change_userdata;
        } ypdf;

        struct {
            /* Title text (owned, NUL-terminated, NULL when title is empty). */
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
            /* Optional menubar (any widget; typically a MENUBAR
             * widget) inserted as a flex-column child between the
             * title strip and the body. Set via window_set_menubar.
             * NULL = no menubar. The widget is reparented into the
             * window's child list. */
            struct yetty_ygui_widget *menubar;
            /* Optional statusbar (any widget; typically a STATUSBAR
             * widget) pinned to the bottom of the window. Set via
             * window_set_statusbar. NULL = no statusbar. The widget
             * is reparented into the window's child list. */
            struct yetty_ygui_widget *statusbar;
            /* Title-bar drag state — set in on_press when the click
             * lands in the title strip (and not on the hamburger
             * button), consumed by on_drag to move the window.
             * drag_press_l[xy] are widget-local at press time;
             * drag_orig_[xy] capture the window's authored position
             * at press so on_drag deltas are press-relative. */
            int   dragging;
            float drag_press_lx, drag_press_ly;
            float drag_orig_x,    drag_orig_y;
        } window;

        struct {
            /* Per-row label (owned). NULL slot = separator row (drawn
             * as a thin divider, not clickable). */
            char **item_labels;
            /* Per-row click callback. NULL = item is disabled / a
             * pure separator. */
            ygui_widget_click_fn *item_callbacks;
            void **item_userdata;
            /* Per-row "drill" flag. When non-zero, clicking the item
             * fires its callback BUT does not close the menu — the
             * callback is expected to re-populate items in place
             * (drill-down navigation pattern). Action items leave this
             * at 0 and the menu closes after the callback. */
            int *item_is_drill;
            int n_items;
            int capacity;
            /* Row height in pixels. 0 = derive from theme. */
            float item_h;
            int modal;
            /* Index of the currently-hovered row (for highlight). -1
             * when the cursor isn't over any clickable row. */
            int hover_index;
            /* Optional header rendered at the top of the menu body.
             * `title` is the breadcrumb / path label (e.g. "Menu" or
             * "Menu › New view"); NULL/empty means no header. When
             * `on_back` is set a `<` chevron button is painted at the
             * left of the header and consumes its hit area — the menu
             * stays open and the callback re-populates the items in
             * place (Chrome Android-style drill-down). on_back_userdata
             * is opaque to the widget; it's forwarded as-is. */
            char *title;
            ygui_widget_click_fn on_back;
            void *on_back_userdata;
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
            /* Optional "+" new-tab button. When `on_new_tab` is set the
             * tabbar paints a small "+" pill right after the last tab
             * header and routes clicks on it to the callback. NULL =
             * no button is shown. The callback typically calls back
             * into yetty_ygui_widget_tabbar_add_tab itself (or into a
             * host model that owns the tab list). */
            ygui_widget_click_fn on_new_tab;
            void *on_new_tab_userdata;
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
     * via yetty_ysdf_* and text via yetty_ydraw_draw_list_add_text. */
    struct yetty_ydraw_draw_list *buffer;

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

    /* OSC sink. Every OSC byte ygui emits goes through
     * output_pty->ops->write — the pty abstraction owns the actual
     * delivery mechanism. Two implementations:
     *   - yui (in-process): memory-ring pty pair, write is a memcpy.
     *   - standalone (ygreeter, ytop, …): a libuv-backed pty that
     *     queues bytes via uv_write on a uv_pipe_t wrapping output_fd.
     *     Created and installed by ygui_engine_uv when the engine is
     *     attached to a loop.
     *
     * Either way ygui_osc.c just calls ops->write and gets a non-
     * blocking, complete-or-fail result back. The output_pty MUST be
     * set before any OSC traffic is emitted; OSC functions return an
     * error if it isn't. */
    struct yetty_platform_pty *output_pty;
    int output_pty_owned; /* 1 if engine_destroy should destroy output_pty
                           * (set when ygui_engine_uv created it). */

    /* When set, write_bin ignores YGRID_USE_NEW_OSC and always emits the
     * legacy YDRAW_SCENE_BIN code. yui's internal chrome engine sets this
     * to 1: its SM only registers the legacy codes, so emitting the new
     * compositor code from chrome would leave the OSC undispatched. PTY-
     * facing engines (yetty_ygui_engine_create, ygui_engine_uv) leave it
     * at 0 so the env var still toggles their wire code. */
    int force_legacy_osc;

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

    /* Producer-side scene-canvas wire state.
     *
     * `next_group_id` — sequential u32 assigned to each widget. 0 is
     * reserved for the receiver's implicit root entity, so the counter
     * starts at 1.
     *
     * `pending_deletes` — group_ids of widgets that were destroyed or
     * unparented since the last render. The next render flushes these
     * as DELETE records at the envelope root before re-emitting any
     * dirty groups. */
    uint32_t next_group_id;
    uint32_t *pending_deletes;
    uint32_t pending_delete_count;
    uint32_t pending_delete_cap;

    /* Set to 1 by engine_create / scene_clear / CMD_ZERO emission;
     * forces the next render to be a full-tree redraw (every widget is
     * treated as dirty, no DELETE flush before — CMD_ZERO supersedes
     * everything on the receiver). Cleared after that render. */
    uint8_t needs_full_redraw;

    /* Optional engine-wide bars — top titlebar, top menubar (under
     * titlebar), bottom statusbar. The widgets are normal top-level
     * widgets the app creates; engine_set_titlebar / _set_menubar /
     * _set_statusbar just store the pointers + reposition the
     * widgets to span the canvas at the right edge. NULL = no bar
     * at that slot. Apps using a window widget typically set the
     * bars on the WINDOW instead (window_set_menubar /
     * _set_statusbar). */
    struct yetty_ygui_widget *engine_titlebar;
    struct yetty_ygui_widget *engine_menubar;
    struct yetty_ygui_widget *engine_statusbar;
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
                                struct yetty_ydraw_draw_list *buffer,
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
 * Every OSC function takes `output_pty` as its first parameter and
 * returns _result. Writes go through output_pty->ops->write — either
 * a libuv-backed uv_pipe_t (ygui_engine_uv installs it) or yui's
 * in-process memory-pty. Passing NULL is an error.
 *
 * No silent stdout fallback exists any more; failing to install an
 * output_pty surfaces as an error rather than blocking on a sync
 * write() that could truncate large envelopes. */
struct yetty_ycore_void_result yetty_ygui_osc_create_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, int x, int y, int w,
                                                          int h, const uint8_t *data,
                                                          uint32_t size, int force_legacy);
struct yetty_ycore_void_result yetty_ygui_osc_update_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, const uint8_t *data,
                                                          uint32_t size, int force_legacy);
struct yetty_ycore_void_result yetty_ygui_osc_kill_card(struct yetty_platform_pty *output_pty,
                                                        const char *name);
struct yetty_ycore_void_result yetty_ygui_osc_subscribe_clicks(struct yetty_platform_pty *output_pty,
                                                               int enable);
struct yetty_ycore_void_result yetty_ygui_osc_subscribe_moves(struct yetty_platform_pty *output_pty,
                                                              int enable);
struct yetty_ycore_void_result yetty_ygui_osc_subscribe_view_changes(
    struct yetty_platform_pty *output_pty, int enable);
struct yetty_ycore_void_result yetty_ygui_osc_query_cell_size(
    struct yetty_platform_pty *output_pty);
struct yetty_ycore_void_result yetty_ygui_osc_card_place(struct yetty_platform_pty *output_pty,
                                                         uint32_t card_id, int col, int row,
                                                         uint32_t w_cells, uint32_t h_cells);
struct yetty_ycore_void_result yetty_ygui_osc_card_remove(struct yetty_platform_pty *output_pty,
                                                          uint32_t card_id);
struct yetty_ycore_void_result yetty_ygui_osc_zoom_card(struct yetty_platform_pty *output_pty,
                                                        const char *name, float level);
struct yetty_ycore_void_result yetty_ygui_osc_scroll_card(struct yetty_platform_pty *output_pty,
                                                          const char *name, float x, float y,
                                                          int absolute);
struct yetty_ycore_void_result yetty_ygui_osc_scroll_card_delta(struct yetty_platform_pty *output_pty,
                                                                const char *name, float dx,
                                                                float dy);

/* Error */
void yetty_ygui_set_error(const char *msg);

/* Recursively queue scene-canvas DELETE records for every widget in
 * the given subtree that owns a live entity on the receiver
 * (was_rendered = 1 from the previous frame). Used by set_visible(0)
 * and by widgets that gate their own emission outside of the standard
 * VISIBLE flag (e.g. popup_menu's OPEN check). After this call every
 * touched widget has was_rendered = 0 and dirty = 1, so it re-emits a
 * fresh GROUP the next time it lands in the render walk. */
void yetty_ygui_internal_queue_delete_subtree_rendered(struct yetty_ygui_widget *w);

/* Move a top-level widget to the END of the engine's first_widget
 * chain so it renders LAST (= on top of every previously-painted
 * widget). Used by popup / popup_menu when opening so the menu / dialog
 * is never occluded by widgets that happen to be later in the chain. */
void yetty_ygui_internal_bring_to_front(struct yetty_ygui_widget *w);

/* Walk every prim in `src`, translate by (dx, dy), append to
 * `ctx->buffer`. Shared between rich_render and every per-producer
 * widget (yimage / yplot / yvideo / yzoo / yjungle). */
struct yetty_ydraw_draw_list;
struct yetty_ycore_void_result yetty_ygui_internal_emit_buffer_translated(
    struct yetty_ygui_render_ctx *ctx,
    const struct yetty_ydraw_draw_list *src,
    float dx, float dy);

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

/* Construct the libuv runtime and the output pty inside an
 * already-allocated engine, then fire the init OSC handshake
 * (query_cell_size, subscribe_clicks/moves, CARD_PLACE, CANVAS_FIT
 * placeholder). Lives in ygui_engine_uv.c because it owns the libuv
 * dependency; called from yetty_ygui_engine_create after the engine
 * struct has been allocated and its non-libuv state set up.
 *
 * If `borrowed_pty` is non-NULL the engine uses it as output_pty
 * (yui-style in-process memory pty); otherwise the engine wraps its
 * own STDOUT_FILENO via a libuv pipe. If `borrowed_loop` is non-NULL
 * the engine attaches to it; otherwise the engine allocates a loop and
 * owns it.
 *
 * On error, the engine is left consistent (no half-wired handles) and
 * the caller can call yetty_ygui_engine_destroy normally. */
struct yetty_ycore_void_result yetty_ygui_engine_internal_bootstrap_runtime(
    struct yetty_ygui_engine *engine, struct yetty_platform_pty *borrowed_pty,
    uv_loop_t *borrowed_loop);

/* Allocate-only — used by both the libuv-coupled public entry
 * (yetty_ygui_engine_create, in ygui_engine_uv.c, which follows up
 * with bootstrap_runtime) and the yui in-process path. Lives in
 * ygui_engine.c so ygui_core stays libuv-free. */
struct ygui_engine_ptr_result yetty_ygui_engine_internal_alloc(
    const char *name, struct yetty_ygui_theme *theme);

/* yui-specific allocator. Behaviour is currently identical to
 * internal_alloc; kept as a named alias so the yui call-site is
 * self-documenting and so we can diverge later without touching yui. */
struct ygui_engine_ptr_result yetty_ygui_engine_internal_alloc_for_yui(
    const char *name, struct yetty_ygui_theme *theme);

/* Emit the init OSC handshake (cell size query, subscribe_clicks/moves,
 * CARD_PLACE, CANVAS_FIT placeholder). Called from
 * engine_internal_bootstrap_runtime after output_pty is wired up.
 * Errors are chained — handshake failure should abort engine
 * construction. */
struct yetty_ycore_void_result yetty_ygui_engine_internal_emit_handshake(
    struct yetty_ygui_engine *engine);

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
