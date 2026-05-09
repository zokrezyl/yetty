/*
 * ymgui/wire.h — binary wire format shared by frontend (C++) and backend (C).
 *
 * All envelopes are carried via yface (see <yetty/yface/yface.h>):
 *
 *     \e]<code>;<flag>;<base64[(LZ4F)payload]>\e\\
 *
 * The OSC <code> identifies the message type — there are no verbs in the
 * body. Codes in the 600000-range flow client→server (card lifecycle,
 * frame, texture, clear); 700000-range flows server→client (mouse,
 * resize, focus).
 *
 * The single character right after the first ';' is the compression flag:
 * '0' for raw b64, '1' for LZ4F+b64. Compression is on for big payloads
 * (frames, textures, future video) and off for short events (mouse,
 * resize, focus, card lifecycle).
 *
 * Cards (v2): a single client process may own multiple "cards" — placed
 * sub-regions of the terminal grid (col,row,w_cells,h_cells), each with
 * its own ImGui frame and font atlas. Every CS/SC payload carries a
 * card_id, so frames/textures/inputs are routed per card. Mouse coords
 * are card-local pixels — the client never needs to know where its
 * cards sit on the pane. See the "Cards" section below for the full
 * placement model.
 *
 * All integers are little-endian. All structs are naturally aligned at 4 B.
 * Vertex layout matches Dear ImGui's ImDrawVert exactly: {pos,uv,col} = 20 B.
 * Index type is 16-bit (ImDrawIdx default); if frontend is built with
 * `#define ImDrawIdx unsigned int` the flag YMGUI_FRAME_FLAG_IDX32 is set.
 */

#ifndef YETTY_YMGUI_WIRE_H
#define YETTY_YMGUI_WIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Cards
 *
 * A "card" is a placed sub-region of the terminal grid that one ImGui
 * (or other) client app draws into. Multiple cards may coexist, owned by
 * the same client process. Cards are first-class citizens of the terminal
 * stream — placed at (col, row, w_cells, h_cells) like an ncurses dialog,
 * anchored to a rolling-row, scrolling with terminal content, and
 * advancing the terminal cursor past their bottom edge so subsequent
 * stdout flows naturally underneath.
 *
 * Each card owns:
 *   - its grid placement (col/row/w/h in cells; row resolved to a
 *     rolling_row at placement time, then drift-corrected as the
 *     terminal scrolls)
 *   - one ImGui frame (vertex/index/cmd mesh)
 *   - one font atlas (R8 today; user textures in v2)
 *   - one focus state
 *
 * Card IDs are client-allocated u32. ID 0 is reserved (= "no card",
 * used by transitional / legacy paths and as a sentinel).
 *
 * Every CS payload after CARD_PLACE carries a card_id so the server
 * routes uploads to the right card. SC payloads carry a card_id so the
 * client routes input to the right per-card ImGuiContext.
 *===========================================================================*/

#define YMGUI_CARD_ID_NONE 0u

/*=============================================================================
 * OSC codes
 *
 * Allocation policy: 6xxxxx = client→server, 7xxxxx = server→client. Within
 * each direction the code itself discriminates the message type, so dispatch
 * is a single switch on osc_code with no body inspection.
 *===========================================================================*/

/* client → server (frontend → ymgui-layer). 600000–600003 belong to
 * ypaint (see <yetty/yterm/pty-reader.h>); ymgui starts at 610000. */
#define YMGUI_OSC_CS_CLEAR 610000       /* ymgui_wire_clear,        comp=0 */
#define YMGUI_OSC_CS_FRAME 610001       /* ymgui_wire_frame,        comp=1 */
#define YMGUI_OSC_CS_TEX 610002         /* ymgui_wire_tex,          comp=1 */
#define YMGUI_OSC_CS_CARD_PLACE 610003  /* ymgui_wire_card_place,   comp=0 */
#define YMGUI_OSC_CS_CARD_REMOVE 610004 /* ymgui_wire_card_remove,  comp=0 */

/* Terminal-wide input subscription — independent of the card hit table.
 * Programs that aren't ymgui-shaped (browser, file manager, anything that
 * draws its own UI directly into the pane) subscribe via this OSC and
 * receive YMGUI_OSC_SC_TERM_* events tagged with card_id=0 and pane-local
 * pixel coords. The card path is unchanged: cards keep getting their own
 * card-tagged events; both fan out simultaneously when both subscriptions
 * are active. */
#define YMGUI_OSC_CS_TERM_INPUT_SUB 610010 /* ymgui_wire_term_input_sub, comp=0 */

/* server → client (yetty terminal → frontend / ygui / yrich) */
#define YMGUI_OSC_SC_MOUSE 700000  /* ymgui_wire_input_mouse,  comp=0 */
#define YMGUI_OSC_SC_RESIZE 700001 /* ymgui_wire_input_resize, comp=0 */
#define YMGUI_OSC_SC_FOCUS 700002  /* ymgui_wire_input_focus,  comp=0 */
#define YMGUI_OSC_SC_KEY 700003    /* ymgui_wire_input_key,    comp=0 */

/* Terminal-wide variants — same wire structs as the card-tagged events
 * (ymgui_wire_input_mouse / _key / _resize), but card_id=0 and x/y are in
 * pane-local pixels (origin = pane top-left). Sent only to the
 * foreground PTY reader that subscribed via YMGUI_OSC_CS_TERM_INPUT_SUB. */
#define YMGUI_OSC_SC_TERM_MOUSE 700010  /* ymgui_wire_input_mouse,  comp=0 */
#define YMGUI_OSC_SC_TERM_RESIZE 700011 /* ymgui_wire_input_resize, comp=0 */
#define YMGUI_OSC_SC_TERM_KEY 700012    /* ymgui_wire_input_key,    comp=0 */

/*=============================================================================
 * Magic numbers + versioning
 *===========================================================================*/
#define YMGUI_WIRE_MAGIC_FRAME 0x4D47494Fu          /* 'OIGM' → "YMGI" */
#define YMGUI_WIRE_MAGIC_TEX 0x4D58544Fu            /* 'OTXM' → "YMTX" */
#define YMGUI_WIRE_MAGIC_CLEAR 0x4D4C4359u          /* "YCLM" */
#define YMGUI_WIRE_MAGIC_CARD_PLACE 0x4D504443u     /* "CDPM" */
#define YMGUI_WIRE_MAGIC_CARD_REMOVE 0x4D524443u    /* "CDRM" */
#define YMGUI_WIRE_MAGIC_INPUT_MOUSE 0x4D49534Du    /* "MSIM" reversed: "MISM" */
#define YMGUI_WIRE_MAGIC_INPUT_RESIZE 0x4D52534Du   /* "MSRM" reversed: "MRSM" */
#define YMGUI_WIRE_MAGIC_INPUT_FOCUS 0x4D434F46u    /* "FOCM" */
#define YMGUI_WIRE_MAGIC_INPUT_KEY 0x4D59454Bu      /* "KEYM" */
#define YMGUI_WIRE_MAGIC_TERM_INPUT_SUB 0x53504954u /* "TIPS" */

#define YMGUI_WIRE_VERSION 2u

/*=============================================================================
 * Terminal-wide input subscription flags (YMGUI_OSC_CS_TERM_INPUT_SUB).
 * Bitmask of which event categories to forward as YMGUI_OSC_SC_TERM_*.
 * Sending flags=0 unsubscribes from all of them.
 *===========================================================================*/
#define YETTY_YMGUI_TERM_SUB_MOUSE_CLICK (1u << 0)
#define YETTY_YMGUI_TERM_SUB_MOUSE_MOVE (1u << 1)
#define YETTY_YMGUI_TERM_SUB_MOUSE_WHEEL (1u << 2)
#define YETTY_YMGUI_TERM_SUB_KEY (1u << 3)

struct yetty_ymgui_wire_term_input_sub {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_TERM_INPUT_SUB */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t flags;   /* bitmask of YETTY_YMGUI_TERM_SUB_* */
    uint32_t _pad0;
};

/* Texture IDs. Per-card namespace: each card has its own tex_id space.
 * tex_id=1 = that card's font atlas. User textures (v2) allocate 2..N
 * within the card. tex_id=0 = "no texture" (solid-colored triangles only). */
#define YMGUI_TEX_ID_NONE 0u
#define YMGUI_TEX_ID_FONT_ATLAS 1u

/* Texture formats. */
#define YMGUI_TEX_FMT_R8 1u
#define YMGUI_TEX_FMT_RGBA8 2u

/* Frame flags. */
#define YMGUI_FRAME_FLAG_IDX32 (1u << 0)

/*---------------------------------------------------------------------------
 * Vertex — identical to ImDrawVert (pos:vec2, uv:vec2, col:u32 RGBA).
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_vertex {
    float pos_x;
    float pos_y;
    float uv_x;
    float uv_y;
    uint32_t col; /* IM_COL32 (AABBGGRR little-endian → R,G,B,A bytes) */
};

/*---------------------------------------------------------------------------
 * Per-draw-call command.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_cmd {
    float clip_min_x;
    float clip_min_y;
    float clip_max_x;
    float clip_max_y;
    uint32_t tex_id;
    uint32_t vtx_offset; /* within the owning cmd-list's vertex array */
    uint32_t idx_offset; /* within the owning cmd-list's index  array */
    uint32_t elem_count; /* number of indices for this call (multiple of 3) */
};

/*---------------------------------------------------------------------------
 * Per-cmd-list header. Followed immediately by:
 *     struct ymgui_wire_vertex vtx[vtx_count];
 *     uint16_t (or uint32_t) idx[idx_count];       // padded to 4 bytes
 *     struct ymgui_wire_cmd    cmds[cmd_count];
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_cmd_list {
    uint32_t vtx_count;
    uint32_t idx_count;
    uint32_t cmd_count;
    uint32_t _pad0;
};

/*---------------------------------------------------------------------------
 * Frame header — payload of YMGUI_OSC_CS_FRAME.
 *
 * Coordinates in the vertex stream are in card-local pixels (origin at the
 * card's top-left). DisplaySize matches the card's pixel size, DisplayPos
 * is (0,0) — the client's ImGui context for the card thinks its display
 * IS the card.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_frame {
    uint32_t magic;      /* YMGUI_WIRE_MAGIC_FRAME */
    uint32_t version;    /* YMGUI_WIRE_VERSION */
    uint32_t flags;      /* YMGUI_FRAME_FLAG_* */
    uint32_t total_size; /* bytes in the whole frame payload */
    uint32_t card_id;    /* card this frame belongs to (must be live) */
    uint32_t cmd_list_count;
    float display_pos_x; /* ImDrawData::DisplayPos (typically 0) */
    float display_pos_y;
    float display_size_x; /* ImDrawData::DisplaySize == card pixel size */
    float display_size_y;
    float fb_scale_x; /* ImDrawData::FramebufferScale */
    float fb_scale_y;
    /* Followed by cmd_list_count × (ymgui_wire_cmd_list + vtx + idx + cmds). */
};

/*---------------------------------------------------------------------------
 * Texture upload payload of YMGUI_OSC_CS_TEX. Pixel data follows the
 * header, length = width*height*bpp (1 = R8, 4 = RGBA8).
 *
 * Textures are owned per-card: tex_id is namespaced to card_id.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_tex {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_TEX */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id; /* card this texture belongs to (must be live) */
    uint32_t tex_id;  /* per-card; 1 = font atlas */
    uint32_t format;  /* YMGUI_TEX_FMT_* */
    uint32_t width;
    uint32_t height;
    uint32_t total_size; /* bytes in the whole tex payload */
    /* Followed by width*height*bpp bytes of pixel data. */
};

/*---------------------------------------------------------------------------
 * Clear payload of YMGUI_OSC_CS_CLEAR.
 *
 * card_id == YMGUI_CARD_ID_NONE: drop the entire ymgui state for this
 * client (every live card on this terminal). This is what the client
 * issues at shutdown (see ImGui_ImplYetty_Clear). Otherwise: drop only
 * the named card. In both cases, the server may promote the dropped
 * card(s) to the static layer (see ymgui-static-layer) so the last
 * frame remains visible as scrollback.
 *
 * The flags field controls promotion:
 *   FLAG_KEEP_VISIBLE: archive last frame to the static layer (default
 *                      on app shutdown).
 *   (no flag set):     drop without archiving (interactive `--clear`).
 *-------------------------------------------------------------------------*/
#define YMGUI_CLEAR_FLAG_KEEP_VISIBLE (1u << 0)

struct yetty_ymgui_wire_clear {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_CLEAR */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id; /* YMGUI_CARD_ID_NONE = all cards */
    uint32_t flags;   /* YMGUI_CLEAR_FLAG_* */
};

/*---------------------------------------------------------------------------
 * Card placement / move / resize — payload of YMGUI_OSC_CS_CARD_PLACE.
 *
 * First emit for a given card_id creates the card; subsequent emits
 * with the same id move/resize it.
 *
 * Placement model — ncurses-dialog style:
 *   col, row are in grid cells, relative to the visible top-left at
 *   message arrival. The server resolves row to a rolling_row anchor
 *   (= row0_absolute + row), so the card scrolls with terminal content
 *   exactly like ypaint primitives or the previous single-frame ymgui
 *   layer. If row + h_cells doesn't fit below the cursor, the server
 *   scrolls the terminal up before placement.
 *
 *   After placement, the text-layer cursor jumps to (col=0, row+h_cells)
 *   so subsequent stdout flows underneath the card.
 *
 *   On resize/move (existing card_id), the cursor is NOT moved — only
 *   create-time placement advances it. col/row in a move-emit are
 *   re-resolved against the current visible window.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_card_place {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_CARD_PLACE */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id; /* must be != YMGUI_CARD_ID_NONE */
    uint32_t flags;   /* reserved, send 0 */
    int32_t col;      /* grid column (0-based) */
    int32_t row;      /* grid row (0-based, relative to visible top) */
    uint32_t w_cells; /* width  in cells; 0 = "until right edge"   */
    uint32_t h_cells; /* height in cells; 0 = "until bottom (at
                               *                       placement time)"    */
};

/*---------------------------------------------------------------------------
 * Card removal — payload of YMGUI_OSC_CS_CARD_REMOVE.
 *
 * Drops a single card. Same archive-vs-discard policy as the per-card
 * variant of CLEAR (see flags). Use this when a single ImGui window
 * closes inside an app that owns multiple cards.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_card_remove {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_CARD_REMOVE */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id;
    uint32_t flags; /* YMGUI_CLEAR_FLAG_* */
};

/*=============================================================================
 * Input events (server → client)
 *
 * All input events carry a card_id. The server hit-tests the cursor
 * against the live cards' pixel rects (drift-corrected for current
 * scroll) and routes the event to the topmost card under the cursor.
 * Coordinates x, y are in card-local pixels (origin = card's top-left,
 * already drift-corrected) — the client never needs to know where the
 * card sits on the pane.
 *===========================================================================*/

/* ymgui_wire_input_mouse.kind */
enum yetty_ymgui_wire_input_mouse_kind {
    YETTY_YMGUI_INPUT_MOUSE_POS = 0,    /* x,y; buttons_held mask for drag tracking */
    YETTY_YMGUI_INPUT_MOUSE_BUTTON = 1, /* button transition: button + pressed + x,y */
    YETTY_YMGUI_INPUT_MOUSE_WHEEL = 2,  /* wheel: wheel_dy at x,y */
};

/* Single struct covers move / button / wheel — kind discriminates.
 * Fields not relevant to a kind are zero on the wire. */
struct yetty_ymgui_wire_input_mouse {
    uint32_t magic;        /* YMGUI_WIRE_MAGIC_INPUT_MOUSE */
    uint32_t version;      /* YMGUI_WIRE_VERSION */
    uint32_t card_id;      /* card under cursor (focused for click events) */
    uint32_t kind;         /* enum ymgui_wire_input_mouse_kind */
    int32_t button;        /* BUTTON: 0=left,1=right,2=middle,...; else -1 */
    int32_t pressed;       /* BUTTON: 1=down 0=up; else 0 */
    uint32_t buttons_held; /* POS during drag: bitmask (1<<button) */
    float x;               /* card-local pixel */
    float y;               /* card-local pixel */
    float wheel_dy;        /* WHEEL */
    uint32_t _pad0;
};

/* Sent when a card's pixel size changes — either because the card was
 * (re)placed via CARD_PLACE (server confirms the resolved pixel size,
 * since the client supplied cells), because cell size changed (zoom),
 * or because the terminal was resized and a w_cells=0 card grew/shrank
 * with the right edge.
 *
 * The client uses (width, height) as DisplaySize for that card's
 * ImGuiContext. */
struct yetty_ymgui_wire_input_resize {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_INPUT_RESIZE */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id;
    uint32_t _pad0;
    float width;  /* pixels */
    float height; /* pixels */
};

/* Focus transition for a card. Click-focus model: the focused card
 * changes only on YMGUI_INPUT_MOUSE_BUTTON press. Plain hover does not
 * change focus (avoids twitchy emissions). The client uses the gained
 * event to set its per-card ImGuiContext as current; lost to drain
 * "key up" / "mouse up" on the previously-focused card. */
struct yetty_ymgui_wire_input_focus {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_INPUT_FOCUS */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id;
    int32_t gained; /* 1 = card gained focus, 0 = lost */
};

/*---------------------------------------------------------------------------
 * Keyboard event — payload of YMGUI_OSC_SC_KEY.
 *
 * Sent only when the terminal has a keyboard subscription active for
 * this client (DEC ?1502h, parallel to the mouse ?1500/?1501 model)
 * AND a card has focus. card_id is the focused card. The client uses
 * `key` (a GLFW-compatible keycode the same way text-layer's on_key
 * does) for KEY_DOWN/UP and `codepoint` (UTF-32) for CHAR.
 *
 * Mods bitmask (parallel to GLFW): SHIFT=1, CTRL=2, ALT=4, SUPER=8.
 *-------------------------------------------------------------------------*/
enum yetty_ymgui_wire_input_key_kind {
    YETTY_YMGUI_INPUT_KEY_DOWN = 0,
    YETTY_YMGUI_INPUT_KEY_UP = 1,
    YETTY_YMGUI_INPUT_KEY_CHAR = 2, /* unicode text input (uses codepoint, not key) */
};

struct yetty_ymgui_wire_input_key {
    uint32_t magic;   /* YMGUI_WIRE_MAGIC_INPUT_KEY */
    uint32_t version; /* YMGUI_WIRE_VERSION */
    uint32_t card_id;
    uint32_t kind;      /* enum ymgui_wire_input_key_kind */
    int32_t key;        /* GLFW keycode, DOWN/UP only; -1 for CHAR */
    int32_t mods;       /* bitmask */
    uint32_t codepoint; /* CHAR only; 0 otherwise */
    uint32_t _pad0;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMGUI_WIRE_H */
