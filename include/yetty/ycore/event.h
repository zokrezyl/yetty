#ifndef YETTY_YCORE_EVENT_H
#define YETTY_YCORE_EVENT_H

#include <stdint.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_ycore_event_type {
    YETTY_EVENT_NONE = 0,
    /* Input events */
    YETTY_EVENT_KEY_DOWN,
    YETTY_EVENT_KEY_UP,
    YETTY_EVENT_CHAR,
    YETTY_EVENT_MOUSE_DOWN,
    YETTY_EVENT_MOUSE_UP,
    YETTY_EVENT_MOUSE_MOVE,
    YETTY_EVENT_MOUSE_DRAG,
    YETTY_EVENT_SCROLL,
    /* Focus events */
    YETTY_EVENT_SET_FOCUS,
    /* Resize */
    YETTY_EVENT_RESIZE,
    /* Poll */
    YETTY_EVENT_POLL_READABLE,
    YETTY_EVENT_POLL_WRITABLE,
    /* Timer */
    YETTY_EVENT_TIMER,
    /* Context menu */
    YETTY_EVENT_CONTEXT_MENU_ACTION,
    /* Card-local events */
    YETTY_EVENT_CARD_MOUSE_DOWN,
    YETTY_EVENT_CARD_MOUSE_UP,
    YETTY_EVENT_CARD_MOUSE_MOVE,
    YETTY_EVENT_CARD_SCROLL,
    YETTY_EVENT_CARD_KEY_DOWN,
    YETTY_EVENT_CARD_CHAR,
    /* Tree manipulation */
    YETTY_EVENT_CLOSE,
    YETTY_EVENT_SPLIT_PANE,
    /* Clipboard */
    YETTY_EVENT_COPY,
    YETTY_EVENT_PASTE,
    /* Command mode */
    YETTY_EVENT_COMMAND_KEY,
    /* Cursor shape */
    YETTY_EVENT_SET_CURSOR,
    /* Card repack */
    YETTY_EVENT_CARD_BUFFER_REPACK,
    YETTY_EVENT_CARD_TEXTURE_REPACK,
    /* Frame rate */
    YETTY_EVENT_SET_FRAME_RATE,
    /* Render */
    YETTY_EVENT_RENDER,
    /* Window contents need a full repaint (e.g. X11 Expose after being
     * uncovered). The texture-surface target just re-renders the whole
     * frame, but damage-aware targets (X11-tile) need to mark every tile
     * dirty or nothing gets blitted — the GPU content didn't change. */
    YETTY_EVENT_WINDOW_REFRESH,
    /* Shutdown - window close, propagates destroy */
    YETTY_EVENT_SHUTDOWN,
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
    YETTY_EVENT_ZOOM_VISUAL,
    YETTY_EVENT_ZOOM_VISUAL_APPLY,
    YETTY_EVENT_ZOOM_VISUAL_PAN,
    YETTY_EVENT_ZOOM_CELL_SIZE,
    /* Must be last - used for array sizing */
    YETTY_EVENT_COUNT
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

struct yetty_ycore_event_scroll {
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

struct yetty_ycore_event {
    enum yetty_ycore_event_type type;
    union {
        struct yetty_ycore_event_key key;
        struct yetty_ycore_event_char chr;
        struct yetty_ycore_event_mouse mouse;
        struct yetty_ycore_event_scroll scroll;
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
    };
    void *payload; /* optional heap-allocated data (copy/paste text) */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_EVENT_H */
