/*
 * yterm/client-input.h — client-input wire format.
 *
 * The terminal sends "client-input" events (mouse / resize / focus / key) to
 * a running client process via OSC envelopes. Two delivery modes share the
 * same payload structs:
 *
 *   - figure-tagged: routed to one ymgui card (figure_id != 0) by the
 *     compositor's hit table. Coords are card-local pixels.
 *     OSCs: YETTY_OSC_SC_CLIENT_INPUT_FIGURE_{MOUSE,RESIZE,FOCUS,KEY}.
 *
 *   - pane-wide: figure_id == 0, coords are pane-local pixels. Delivered
 *     to the foreground PTY reader that subscribed via
 *     YETTY_OSC_CS_CLIENT_INPUT_SUB. Used by programs that aren't
 *     ymgui-shaped (browser, file manager, ymesh, yjungle, ymaze, yzoo,
 *     ylexbor, ynetsurf, …) and want raw input events.
 *     OSCs: YETTY_OSC_SC_CLIENT_INPUT_{MOUSE,RESIZE,KEY}.
 *
 * Both modes carry the same on-wire structs (yetty_client_input_mouse /
 * _resize / _focus / _key) — only the figure_id field and coordinate space
 * differ.
 */

#ifndef YETTY_YTERMINAL_CLIENT_INPUT_H
#define YETTY_YTERMINAL_CLIENT_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * OSC codes
 *===========================================================================*/

/* Client → server: subscribe / update / unsubscribe pane-wide (flags=0). */
#define YETTY_OSC_CS_CLIENT_INPUT_SUB 610010 /* yetty_client_input_sub, comp=0 */

/* Client → server: bounce an input event back for DEFAULT handling. A
 * subscriber that previews forwarded events (client-side focus model)
 * re-ships the ones it did NOT consume on this code; the terminal then
 * applies its unsubscribed behavior (wheel → scrollback, …) without
 * re-forwarding the event to the subscriber. Payload:
 * yetty_client_input_mouse. */
#define YETTY_OSC_CS_CLIENT_INPUT_REINJECT 610011 /* yetty_client_input_mouse, comp=0 */

/* Server → client, figure-tagged variants. figure_id != 0; (x, y) are
 * card-local pixels. */
#define YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE 700000  /* yetty_client_input_mouse,  comp=0 */
#define YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE 700001 /* yetty_client_input_resize, comp=0 */
#define YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS 700002  /* yetty_client_input_focus,  comp=0 */
#define YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY 700003    /* yetty_client_input_key,    comp=0 */

/* Server → client, pane-wide variants. figure_id == 0; (x, y) are
 * pane-local pixels. Sent only to subscribers of
 * YETTY_OSC_CS_CLIENT_INPUT_SUB. */
#define YETTY_OSC_SC_CLIENT_INPUT_MOUSE 700010  /* yetty_client_input_mouse,  comp=0 */
#define YETTY_OSC_SC_CLIENT_INPUT_RESIZE 700011 /* yetty_client_input_resize, comp=0 */
#define YETTY_OSC_SC_CLIENT_INPUT_KEY 700012    /* yetty_client_input_key,    comp=0 */

/*=============================================================================
 * Magic numbers
 *===========================================================================*/
#define YETTY_CLIENT_INPUT_SUB_MAGIC 0x53504954u    /* "TIPS" */
#define YETTY_CLIENT_INPUT_MOUSE_MAGIC 0x4D49534Du  /* "MSIM" reversed: "MISM" */
#define YETTY_CLIENT_INPUT_RESIZE_MAGIC 0x4D52534Du /* "MSRM" reversed: "MRSM" */
#define YETTY_CLIENT_INPUT_FOCUS_MAGIC 0x4D434F46u  /* "FOCM" */
#define YETTY_CLIENT_INPUT_KEY_MAGIC 0x4D59454Bu    /* "KEYM" */

/*=============================================================================
 * Pane-wide subscription bitmask (YETTY_OSC_CS_CLIENT_INPUT_SUB).
 *
 * Selects which event categories the inferior wants forwarded as
 * YETTY_OSC_SC_CLIENT_INPUT_*. Sending flags=0 unsubscribes from all.
 *===========================================================================*/
#define YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK (1u << 0)
#define YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE (1u << 1)
#define YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL (1u << 2)
#define YETTY_CLIENT_INPUT_SUB_KEY (1u << 3)

struct yetty_client_input_sub {
    uint32_t magic;   /* YETTY_CLIENT_INPUT_SUB_MAGIC */
    uint32_t version; /* YMGUI_WIRE_VERSION (shared with ymgui wire) */
    uint32_t flags;   /* bitmask of YETTY_CLIENT_INPUT_SUB_* */
    uint32_t _pad0;
};

/*=============================================================================
 * Input event payloads (server → client)
 *
 * Coordinate space depends on the OSC code that delivered the payload:
 *   - YETTY_OSC_SC_CLIENT_INPUT_FIGURE_* → card-local pixels, figure_id != 0
 *   - YETTY_OSC_SC_CLIENT_INPUT_*        → pane-local pixels, figure_id == 0
 *===========================================================================*/

/* yetty_client_input_mouse.kind */
enum yetty_client_input_mouse_kind {
    YETTY_YMGUI_INPUT_MOUSE_POS = 0,    /* x,y; buttons_held mask for drag tracking */
    YETTY_YMGUI_INPUT_MOUSE_BUTTON = 1, /* button transition: button + pressed + x,y */
    YETTY_YMGUI_INPUT_MOUSE_WHEEL = 2,  /* wheel: wheel_dy at x,y */
};

/* Single struct covers move / button / wheel — kind discriminates.
 * Fields not relevant to a kind are zero on the wire. */
struct yetty_client_input_mouse {
    uint32_t magic;        /* YETTY_CLIENT_INPUT_MOUSE_MAGIC */
    uint32_t version;      /* YMGUI_WIRE_VERSION */
    uint32_t figure_id;    /* card under cursor (focused for click events); 0 = pane-wide */
    uint32_t kind;         /* enum yetty_client_input_mouse_kind */
    int32_t button;        /* BUTTON: 0=left,1=right,2=middle,...; else -1 */
    int32_t pressed;       /* BUTTON: 1=down 0=up; else 0 */
    uint32_t buttons_held; /* POS during drag: bitmask (1<<button) */
    float x;
    float y;
    float wheel_dy; /* WHEEL */
    uint32_t _pad0;
};

/* Sent when a target's pixel size changes — for figure-tagged events when a
 * card was (re)placed via CARD_PLACE, when cell size changed (zoom), or when
 * the terminal was resized and a w_cells=0 card grew/shrank with the right
 * edge. For pane-wide events, sent on pane resize.
 *
 * The client uses (width, height) as DisplaySize for the target's
 * rendering context. */
struct yetty_client_input_resize {
    uint32_t magic;   /* YETTY_CLIENT_INPUT_RESIZE_MAGIC */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t figure_id;
    uint32_t _pad0;
    float width;
    float height;
};

/* Focus transition for a figure-tagged subscriber (figure_id != 0).
 * Click-focus model: focused card changes only on MOUSE_BUTTON press. Plain
 * hover does not change focus. The client uses gained to set the per-card
 * context current; lost to drain "key up" / "mouse up" on the previously
 * focused card. */
struct yetty_client_input_focus {
    uint32_t magic;   /* YETTY_CLIENT_INPUT_FOCUS_MAGIC */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t figure_id;
    int32_t gained; /* 1 = gained focus, 0 = lost */
};

/* yetty_client_input_key.kind */
enum yetty_client_input_key_kind {
    YETTY_YMGUI_INPUT_KEY_DOWN = 0,
    YETTY_YMGUI_INPUT_KEY_UP = 1,
    YETTY_YMGUI_INPUT_KEY_CHAR = 2, /* unicode text input (uses codepoint, not key) */
};

/* Sent only when a keyboard subscription is active (DEC ?1502h for the
 * pane-wide variant, parallel to the mouse ?1500/?1501 model) AND, for
 * figure-tagged events, a card has focus. `key` is a GLFW-compatible
 * keycode (same as text-layer's on_key) for DOWN/UP; `codepoint` (UTF-32)
 * is set for CHAR.
 *
 * Mods bitmask (parallel to GLFW): SHIFT=1, CTRL=2, ALT=4, SUPER=8. */
struct yetty_client_input_key {
    uint32_t magic;   /* YETTY_CLIENT_INPUT_KEY_MAGIC */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t figure_id;
    uint32_t kind;      /* enum yetty_client_input_key_kind */
    int32_t key;        /* GLFW keycode, DOWN/UP only; -1 for CHAR */
    int32_t mods;       /* bitmask */
    uint32_t codepoint; /* CHAR only; 0 otherwise */
    uint32_t _pad0;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERMINAL_CLIENT_INPUT_H */
