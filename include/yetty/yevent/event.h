#ifndef YETTY_YEVENT_EVENT_H
#define YETTY_YEVENT_EVENT_H

#include <stdint.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yevent_event_type {
    YETTY_YCORE_NONE = 0,
    /* Input events */
    YETTY_YCORE_KEY_DOWN,
    YETTY_YCORE_KEY_UP,
    YETTY_YCORE_CHAR,
    YETTY_YCORE_MOUSE_DOWN,
    YETTY_YCORE_MOUSE_UP,
    YETTY_YCORE_MOUSE_MOVE,
    YETTY_YCORE_MOUSE_DRAG,
    YETTY_YCORE_MOUSE_SCROLL,
    /* Synthesized by the platform event loop right after a MOUSE_DOWN
     * whose button + time + position match the prior MOUSE_DOWN within
     * the double-click window (see os-event-loop/default.c). Payload is
     * the second DOWN's mouse state; consumers should not also process
     * the preceding MOUSE_DOWN as a click target if they handle
     * double-click. GLFW gives raw button events only — this is the one
     * place we do the timer math so widgets don't each reinvent it. */
    YETTY_YCORE_MOUSE_DOUBLE_CLICK,
    /* Focus events */
    YETTY_YCORE_SET_FOCUS,
    /* Resize */
    YETTY_YCORE_RESIZE,
    /* Poll */
    YETTY_YCORE_POLL_READABLE,
    YETTY_YCORE_POLL_WRITABLE,
    /* Timer */
    YETTY_YCORE_TIMER,
    /* Context menu */
    YETTY_YCORE_CONTEXT_MENU_ACTION,
    /* Card-local events */
    YETTY_YCORE_CARD_MOUSE_DOWN,
    YETTY_YCORE_CARD_MOUSE_UP,
    YETTY_YCORE_CARD_MOUSE_MOVE,
    YETTY_YCORE_CARD_SCROLL,
    YETTY_YCORE_CARD_KEY_DOWN,
    YETTY_YCORE_CARD_CHAR,
    /* Tree manipulation. Naming convention: OBJECT_VERB (the noun is the
     * target, the verb is the action — "pane split", not "split pane",
     * which reads like a command). All chrome-driven creation events
     * carry pre-allocated ids minted by yetty_ycore_next_object_id, so
     * the handler can construct the tile with the exact id the chrome
     * already keyed its widget map on — no discovery round-trip. */
    YETTY_YCORE_CLOSE,
    YETTY_YCORE_WORKSPACE_CREATE,
    YETTY_YCORE_PANE_CREATE,
    YETTY_YCORE_PANE_SPLIT,
    YETTY_YCORE_SPLIT_RESIZE,
    /* Clipboard */
    YETTY_YCORE_COPY,
    YETTY_YCORE_PASTE,
    /* Command mode */
    YETTY_YCORE_COMMAND_KEY,
    /* Cursor shape */
    YETTY_YCORE_SET_CURSOR,
    /* Card repack */
    YETTY_YCORE_CARD_BUFFER_REPACK,
    YETTY_YCORE_CARD_TEXTURE_REPACK,
    /* Frame rate */
    YETTY_YCORE_SET_FRAME_RATE,
    /* Render */
    YETTY_YCORE_RENDER,
    /* Window contents need a full repaint (e.g. X11 Expose after being
     * uncovered). The texture-surface target just re-renders the whole
     * frame, but damage-aware targets (X11-tile) need to mark every tile
     * dirty or nothing gets blitted — the GPU content didn't change. */
    YETTY_YCORE_WINDOW_REFRESH,
    /* Shutdown - window close, propagates destroy */
    YETTY_YCORE_SHUTDOWN,
    /* Named zoom events (produced from raw SCROLL + modifier combinations by
     * yetty_event_handler; decoupled so rpc/kb-mapping can inject them too).
     * ZOOM_VISUAL        = input: scale delta + anchor, consumed by yetty.
     * ZOOM_VISUAL_APPLY  = output: computed {scale, off_x, off_y}, forwarded
     *                      through the workspace so each terminal layer can
     *                      push the values into its fragment-shader uniforms.
     *                      MSDF/SDF is re-evaluated per fragment, so glyphs
     *                      stay crisp at any zoom level.
     * ZOOM_VISUAL_PAN    = pan the visually-zoomed view (drag translated).
     * ZOOM_CELL_SIZE     = structural zoom (changes cell pixel size → cols/rows). */
    YETTY_YCORE_ZOOM_VISUAL,
    YETTY_YCORE_ZOOM_VISUAL_APPLY,
    YETTY_YCORE_ZOOM_VISUAL_PAN,
    YETTY_YCORE_ZOOM_CELL_SIZE,
    /* Capture the rendered window contents to a file. Triggered today by an
     * event-loop event (later wired to a keyboard shortcut). The path is
     * inline for simple cases; an empty string means "auto-pick a default". */
    YETTY_YCORE_SCREENSHOT,
    /* Render→main "output pipe" window-control requests. Produced by the
     * tabbar's custom title-bar buttons (the OS frame is gone because of
     * GLFW_DECORATED=FALSE) and drained on the main thread, which is the
     * only place GLFW window calls are safe. */
    YETTY_YCORE_WINDOW_ICONIFY,
    YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE,
    YETTY_YCORE_WINDOW_CLOSE,
    YETTY_YCORE_WINDOW_DRAG_BY,
    /* Adjust window outer size by (dx, dy) screen pixels, anchored at the
     * top-left. Issued by the tabbar/yetty event handler when the user
     * drags one of the edge/corner resize handles (since GLFW_DECORATED
     * is off, the OS doesn't provide built-in resize grips). */
    YETTY_YCORE_WINDOW_RESIZE_BY,
    /* Hand the move gesture off to the compositor (Wayland) or no-op
     * (X11, where per-pixel WINDOW_DRAG_BY via glfwSetWindowPos works).
     * Wayland doesn't let clients position themselves; the protocol-
     * correct way to move a CSD window is xdg_toplevel.move(seat, serial),
     * which lets the compositor take over the drag until release. The
     * tabbar emits this once on the MOUSE_DOWN that starts a titlebar
     * drag; the main-thread handler dispatches to the right backend. */
    YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE,
    /* Same shape as BEGIN_INTERACTIVE_MOVE but for resize: the compositor
     * (Wayland) grabs the pointer and resizes the window from `edge` until
     * release. The tabbar emits this once on the MOUSE_DOWN that lands in
     * an edge/corner band, after the drag-slop threshold is crossed; the
     * main-thread handler dispatches to the right backend (no-op on X11,
     * where per-pixel WINDOW_RESIZE_BY via glfwSetWindowSize works). */
    YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE,
    /* Must be last - used for array sizing */
    YETTY_YCORE_COUNT
};

/* Edge bitmask for WINDOW_BEGIN_INTERACTIVE_RESIZE. Values mirror the
 * xdg-shell wire protocol (xdg_toplevel.resize_edge) so the Wayland
 * helper can pass them through unchanged; the same constants are mapped
 * onto the relevant axes on other backends if/when they grow support. */
enum yetty_ycore_resize_edge {
    YETTY_YCORE_RESIZE_EDGE_NONE = 0,
    YETTY_YCORE_RESIZE_EDGE_TOP = 1,
    YETTY_YCORE_RESIZE_EDGE_BOTTOM = 2,
    YETTY_YCORE_RESIZE_EDGE_LEFT = 4,
    YETTY_YCORE_RESIZE_EDGE_TOP_LEFT = 5,
    YETTY_YCORE_RESIZE_EDGE_BOTTOM_LEFT = 6,
    YETTY_YCORE_RESIZE_EDGE_RIGHT = 8,
    YETTY_YCORE_RESIZE_EDGE_TOP_RIGHT = 9,
    YETTY_YCORE_RESIZE_EDGE_BOTTOM_RIGHT = 10,
};

struct yetty_ycore_event_key {
    int key;
    int mods;
    int scancode;
};

struct yetty_ycore_event_char {
    uint32_t codepoint;
    int mods;
};

struct yetty_ycore_event_mouse {
    float x;
    float y;
    int button;
    int mods;
};

struct yetty_ycore_event_mouse_scroll {
    float x;
    float y;
    float dx;
    float dy;
    int mods;
};

struct yetty_ycore_event_set_focus {
    yetty_ycore_object_id object_id;
};

struct yetty_ycore_event_resize {
    float width;
    float height;
    /* Display density (framebuffer px / logical px) of the output that
     * requested this resize. 0 = producer has no opinion — the app keeps
     * its current content scale (all local platform paths). A remote yvnc
     * viewer stamps its own display's scale here so the server re-lays
     * out text/chrome at the viewer's density, not its own. */
    float content_scale;
};

struct yetty_ycore_event_poll {
    int fd;
};

struct yetty_ycore_event_timer {
    int timer_id;
};

struct yetty_ycore_event_context_menu_action {
    yetty_ycore_object_id object_id;
    int row;
    int col;
    char action[32];
};

struct yetty_ycore_event_card_mouse {
    yetty_ycore_object_id target_id;
    float x;
    float y;
    int button;
};

struct yetty_ycore_event_card_scroll {
    yetty_ycore_object_id target_id;
    float x;
    float y;
    float dx;
    float dy;
    int mods;
};

struct yetty_ycore_event_card_key {
    yetty_ycore_object_id target_id;
    int key;
    int mods;
    int scancode;
};

struct yetty_ycore_event_card_char {
    yetty_ycore_object_id target_id;
    uint32_t codepoint;
    int mods;
};

struct yetty_ycore_event_close {
    yetty_ycore_object_id object_id;
};

/* Chrome asks the workspace layer to materialise a workspace with the
 * pre-allocated id. The first pane inside the workspace is created
 * separately via PANE_CREATE so chrome owns both ids from the very
 * first moment. */
struct yetty_ycore_event_workspace_create {
    yetty_ycore_object_id workspace_id;
};

/* Chrome asks the workspace layer to place a fresh empty pane (just an
 * empty tile — view selection happens elsewhere) at the workspace root,
 * using the pre-allocated id. Today only used for the very first pane;
 * subsequent panes are born from PANE_SPLIT. */
struct yetty_ycore_event_pane_create {
    yetty_ycore_object_id workspace_id;
    yetty_ycore_object_id pane_id;
};

/* Chrome asks the workspace layer to split target_pane_id into a new
 * split node (new_split_id) holding the existing pane plus a fresh new
 * pane (new_pane_id). All three ids are pre-allocated by chrome so its
 * splitter-widget map stays keyed on the same numbers the tile tree
 * uses. orientation: 0=horizontal divider (panes stacked top/bottom),
 * 1=vertical divider (panes side-by-side). */
struct yetty_ycore_event_pane_split {
    yetty_ycore_object_id workspace_id;
    yetty_ycore_object_id target_pane_id;
    yetty_ycore_object_id new_pane_id;
    yetty_ycore_object_id new_split_id;
    uint8_t orientation;
};

/* Chrome reports a splitter-drag end (or live drag, depending on
 * widget policy) on a specific split. The workspace updates the
 * split's ratio and re-lays out — chrome's splitter widget itself
 * does NOT mutate the tile tree, the round trip via the event loop
 * keeps yui and workspace in sync. */
struct yetty_ycore_event_split_resize {
    yetty_ycore_object_id workspace_id;
    yetty_ycore_object_id split_id;
    float ratio;
};

struct yetty_ycore_event_command_key {
    int key;
    uint32_t codepoint;
    int mods;
};

/* Shape codes for set_cursor.shape. Kept as a plain int on the event
 * so the union stays POD-copyable through the input pipe. */
enum yetty_ycore_cursor_shape {
    YETTY_YCORE_CURSOR_DEFAULT = 0,
    YETTY_YCORE_CURSOR_HRESIZE, /* ↔  left-right resize (vertical bar) */
    YETTY_YCORE_CURSOR_VRESIZE, /* ↕  up-down resize (horizontal bar) */
    YETTY_YCORE_CURSOR_IBEAM,   /* text I-beam */
    YETTY_YCORE_CURSOR_HAND,    /* pointer / hand */
};

struct yetty_ycore_event_set_cursor {
    int shape; /* enum yetty_ycore_cursor_shape */
};

struct yetty_ycore_event_card_repack {
    yetty_ycore_object_id target_id;
};

struct yetty_ycore_event_set_frame_rate {
    uint32_t fps;
};

struct yetty_ycore_event_zoom_visual {
    float delta;    /* change in zoom scale; reset=1 overrides */
    int reset;      /* non-zero -> set scale back to 1.0, clear offsets */
    float anchor_x; /* pan anchor in pixels (screen-space); 0 if unused */
    float anchor_y;
};

struct yetty_ycore_event_zoom_visual_apply {
    float scale;
    float offset_x;
    float offset_y;
};

struct yetty_ycore_event_zoom_visual_pan {
    float dx; /* screen-space pixel delta (positive = dragged right) */
    float dy; /* screen-space pixel delta (positive = dragged down)  */
};

struct yetty_ycore_event_zoom_cell_size {
    float delta; /* multiplicative delta applied to cell_size; e.g. 0.04 */
    int reset;   /* non-zero -> restore baseline cell size */
};

struct yetty_ycore_event_screenshot {
    /* Output path. Empty string => use a default location. The fixed-size
     * inline buffer keeps the event POD-copyable through the input pipe. */
    char path[256];
};

/* Window-drag delta, in screen pixels. The tabbar emits one per
 * MOUSE_MOVE while the user holds a drag on the strip; the main thread
 * applies it absolutely (current_window_pos += delta). */
struct yetty_ycore_event_window_drag {
    int dx;
    int dy;
};

/* Window-resize delta, in screen pixels. dx grows the right edge, dy
 * grows the bottom edge; negatives shrink. The top-left stays put. */
struct yetty_ycore_event_window_resize {
    int dx;
    int dy;
    /* yetty_ycore_resize_edge bitmask — which edge(s)/corner the gesture grabbed.
     * The X11 per-pixel handler uses it to move the origin for left/top edges
     * (right/bottom keep the top-left fixed). 0 = legacy right/bottom growth. */
    int edge;
};

/* Carries the edge mask for WINDOW_BEGIN_INTERACTIVE_RESIZE. Holds an
 * `enum yetty_ycore_resize_edge` value. */
struct yetty_ycore_event_window_begin_resize {
    int edge;
};

struct yetty_yui_event {
    enum yetty_yevent_event_type type;
    union {
        struct yetty_ycore_event_key key;
        struct yetty_ycore_event_char chr;
        struct yetty_ycore_event_mouse mouse;
        struct yetty_ycore_event_mouse_scroll mouse_scroll;
        struct yetty_ycore_event_set_focus set_focus;
        struct yetty_ycore_event_resize resize;
        struct yetty_ycore_event_poll poll;
        struct yetty_ycore_event_timer timer;
        struct yetty_ycore_event_context_menu_action ctx_menu;
        struct yetty_ycore_event_card_mouse card_mouse;
        struct yetty_ycore_event_card_scroll card_scroll;
        struct yetty_ycore_event_card_key card_key;
        struct yetty_ycore_event_card_char card_char;
        struct yetty_ycore_event_close close;
        struct yetty_ycore_event_workspace_create workspace_create;
        struct yetty_ycore_event_pane_create pane_create;
        struct yetty_ycore_event_pane_split pane_split;
        struct yetty_ycore_event_split_resize split_resize;
        struct yetty_ycore_event_command_key cmd_key;
        struct yetty_ycore_event_set_cursor set_cursor;
        struct yetty_ycore_event_card_repack card_repack;
        struct yetty_ycore_event_set_frame_rate set_frame_rate;
        struct yetty_ycore_event_zoom_visual zoom_visual;
        struct yetty_ycore_event_zoom_visual_apply zoom_visual_apply;
        struct yetty_ycore_event_zoom_visual_pan zoom_visual_pan;
        struct yetty_ycore_event_zoom_cell_size zoom_cell_size;
        struct yetty_ycore_event_screenshot screenshot;
        struct yetty_ycore_event_window_drag window_drag;
        struct yetty_ycore_event_window_resize window_resize;
        struct yetty_ycore_event_window_begin_resize window_begin_resize;
    };
    void *payload; /* optional heap-allocated data (copy/paste text) */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEVENT_EVENT_H */
