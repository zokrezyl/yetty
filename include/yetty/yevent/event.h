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
    /* Tree manipulation */
    YETTY_YCORE_CLOSE,
    YETTY_YCORE_SPLIT_PANE,
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
    /* Must be last - used for array sizing */
    YETTY_YCORE_COUNT
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

struct yetty_ycore_event_split_pane {
    yetty_ycore_object_id object_id;
    uint8_t orientation;
};

struct yetty_ycore_event_command_key {
    int key;
    uint32_t codepoint;
    int mods;
};

struct yetty_ycore_event_set_cursor {
    int shape;
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
        struct yetty_ycore_event_split_pane split_pane;
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
    };
    void *payload; /* optional heap-allocated data (copy/paste text) */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEVENT_EVENT_H */
