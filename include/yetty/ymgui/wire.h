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
 * figure_id, so frames/textures/inputs are routed per card. Mouse coords
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
 * Every CS payload after CARD_PLACE carries a figure_id so the server
 * routes uploads to the right card. SC payloads carry a figure_id so the
 * client routes input to the right per-card ImGuiContext.
 *===========================================================================*/

#define YMGUI_FIGURE_ID_NONE 0u

/*=============================================================================
 * OSC codes
 *
 * Allocation policy: 6xxxxx = client→server, 7xxxxx = server→client. Within
 * each direction the code itself discriminates the message type, so dispatch
 * is a single switch on osc_code with no body inspection.
 *===========================================================================*/

/* Client → server traffic flows through the typed yfigure yclass stubs over
 * yclass-RPC: yetty_yfigure_apply_child_body addressed to a figure id carries
 * a body that is one of the YETTY_YMGUI_FIGURE_SUB_* sub-records defined
 * below. There are no per-message client→server OSC codes — the typed figure
 * stubs carry everything.
 *
 * The CARD_* / CLEAR / FRAME / TEX CS codes below are legacy producer
 * fallback; they will be deleted with that producer. The pane-wide
 * client-input channel lives in <yetty/yterminal/client-input.h>. */
#define YMGUI_OSC_CS_CLEAR 610000       /* ymgui_wire_clear,        comp=0 */
#define YMGUI_OSC_CS_FRAME 610001       /* ymgui_wire_frame,        comp=1 */
#define YMGUI_OSC_CS_TEX 610002         /* ymgui_wire_tex,          comp=1 */
#define YMGUI_OSC_CS_CARD_PLACE 610003  /* ymgui_wire_card_place,   comp=0 */
#define YMGUI_OSC_CS_CARD_REMOVE 610004 /* ymgui_wire_card_remove,  comp=0 */

/* Server → client client-input events (figure-tagged and pane-wide variants)
 * live in <yetty/yterminal/client-input.h>. */

/*=============================================================================
 * Magic numbers + versioning
 *===========================================================================*/
#define YMGUI_WIRE_MAGIC_FRAME 0x4D47494Fu       /* 'OIGM' → "YMGI" */
#define YMGUI_WIRE_MAGIC_TEX 0x4D58544Fu         /* 'OTXM' → "YMTX" */
#define YMGUI_WIRE_MAGIC_CLEAR 0x4D4C4359u       /* "YCLM" */
#define YMGUI_WIRE_MAGIC_CARD_PLACE 0x4D504443u  /* "CDPM" */
#define YMGUI_WIRE_MAGIC_CARD_REMOVE 0x4D524443u /* "CDRM" */

#define YMGUI_WIRE_VERSION 4u

/* Texture IDs. Per-figure namespace: each figure has its own tex_id space.
 * tex_id=1 = that card's font atlas. User textures (v2) allocate 2..N
 * within the card. tex_id=0 = "no texture" (solid-colored triangles only). */
#define YMGUI_TEX_ID_NONE 0u
#define YMGUI_TEX_ID_FONT_ATLAS 1u

/* Texture formats. */
#define YMGUI_TEX_FMT_R8 1u
#define YMGUI_TEX_FMT_RGBA8 2u

/* Frame flags. */
#define YMGUI_FRAME_FLAG_IDX32 (1u << 0)

/*=============================================================================
 * Figure-tree sub-records
 *
 * When ymgui is carried as a yfigure (kind=YETTY_YFIGURE_KIND_YMGUI=3), each
 * yetty_yfigure_apply_child_body call addressed to the figure id delivers a
 * `body` to the figure's process_bytes that is one self-describing
 * sub-record. The first u32 of `body` is either:
 *
 *   - a YMGUI_WIRE_MAGIC_* word (FRAME / TEX) — the legacy in-figure
 *     dispatcher; or
 *   - a YMGUI_FIGURE_SUB_* enum value — the new tagged form.
 *
 * The dispatcher accepts both for the migration window. New producers
 * should use YMGUI_FIGURE_SUB_*. The magic-prefixed payloads remain
 * available because the original `yetty_ymgui_wire_frame` /
 * `yetty_ymgui_wire_tex` structs already self-describe via their magic
 * field.
 *
 * SUB_FRAME payload after the u32 sub_op:
 *   struct yetty_ymgui_wire_frame frame_hdr;
 *   ... (cmd_list bodies as documented below)
 *
 * SUB_TEX_UPLOAD payload after the u32 sub_op:
 *   struct yetty_ymgui_wire_tex tex_hdr;
 *   uint8_t pixels[];
 *
 * SUB_CLEAR / SUB_TEX_RELEASE / SUB_TERM_INPUT_SUB are reserved for the
 * follow-up PRs; the receiver currently rejects them so encoders don't
 * silently lose state.
 *===========================================================================*/
enum yetty_ymgui_figure_sub_op {
    YETTY_YMGUI_FIGURE_SUB_CLEAR = 1,
    YETTY_YMGUI_FIGURE_SUB_FRAME = 2,
    YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD = 3,
    YETTY_YMGUI_FIGURE_SUB_TEX_RELEASE = 4,
    YETTY_YMGUI_FIGURE_SUB_TERM_INPUT_SUB = 5,
};

/* Per-cmd_list flags (yetty_ymgui_wire_cmd_list.flags).
 *
 * REPEAT (Stage 1): this slot's content is byte-identical to the previous
 *   frame's cmd_list at the same slot index. The wire carries ONLY the
 *   cmd_list header (16 B) — no vertex / index / cmd bytes follow. The
 *   server reuses the cached slot from the previous frame. vtx_count,
 *   idx_count, cmd_count are sent as 0 and must be ignored by the
 *   receiver.
 *
 * CMD_DIFF (Stage 2): the cmd_list's content has changed but most of its
 *   cmds are byte-identical to last frame's cmds (e.g. one button hover
 *   inside an otherwise static window). The wire carries a draw-order
 *   list of per-cmd content hashes plus only the cmds whose hash isn't
 *   in last frame's hash list for this slot. The server reassembles a
 *   flat cmd_list by gathering cached cmds from last frame's slot bytes
 *   and the inline ones from this wire.
 *
 * Either flag — but not both — may be set. Neither set = full cmd_list
 * body (vtx/idx/cmds packed as in pre-Stage1 wire). */
#define YMGUI_CMDLIST_FLAG_REPEAT (1u << 0)
#define YMGUI_CMDLIST_FLAG_CMD_DIFF (1u << 1)

/* CMD_DIFF body (immediately after the cmd_list_hdr when its flags has
 * CMD_DIFF set):
 *
 *     uint32_t hash_count;     // == cmd_list_hdr.cmd_count (draw order)
 *     uint32_t inline_count;   // # of cmds whose full content follows
 *     uint64_t draw_hashes[hash_count];
 *
 *     // inline_count repetitions of:
 *     struct yetty_ymgui_wire_cmd_inline hdr;   // hash + vtx_count
 *     struct yetty_ymgui_wire_cmd       cmd;    // clip, tex, elem_count
 *     yetty_ymgui_wire_vertex vtx[hdr.vtx_count];
 *     idx[cmd.elem_count];                      // idx_bpe each, padded to 4
 *
 * Inline `cmd.vtx_offset` and `cmd.idx_offset` are unused on the wire —
 * the server reassigns them when packing the cmd_list. */
struct yetty_ymgui_wire_cmd_inline {
    uint64_t hash;
    uint32_t vtx_count; /* # vertices owned by this cmd */
    uint32_t flags;     /* reserved, send 0 */
};

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
 * Per-cmd-list header. When flags & YMGUI_CMDLIST_FLAG_REPEAT == 0,
 * followed immediately by:
 *     struct ymgui_wire_vertex vtx[vtx_count];
 *     uint16_t (or uint32_t) idx[idx_count];       // padded to 4 bytes
 *     struct ymgui_wire_cmd    cmds[cmd_count];
 *
 * When the REPEAT flag is set, NOTHING follows the header — the server
 * reuses the slot's content from the previous frame at the same index.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_cmd_list {
    uint32_t vtx_count;
    uint32_t idx_count;
    uint32_t cmd_count;
    uint32_t flags; /* YMGUI_CMDLIST_FLAG_* */
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
    uint32_t figure_id;  /* card this frame belongs to (must be live) */
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
 * Textures are owned per-card: tex_id is namespaced to figure_id.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_tex {
    uint32_t magic;     /* YMGUI_WIRE_MAGIC_TEX */
    uint32_t version;   /* YMGUI_WIRE_VERSION */
    uint32_t figure_id; /* card this texture belongs to (must be live) */
    uint32_t tex_id;    /* per-card; 1 = font atlas */
    uint32_t format;    /* YMGUI_TEX_FMT_* */
    uint32_t width;
    uint32_t height;
    uint32_t total_size; /* bytes in the whole tex payload */
    /* Followed by width*height*bpp bytes of pixel data. */
};

/*---------------------------------------------------------------------------
 * Clear payload of YMGUI_OSC_CS_CLEAR.
 *
 * figure_id == YMGUI_FIGURE_ID_NONE: drop the entire ymgui state for this
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
    uint32_t magic;     /* YMGUI_WIRE_MAGIC_CLEAR */
    uint32_t version;   /* YMGUI_WIRE_VERSION */
    uint32_t figure_id; /* YMGUI_FIGURE_ID_NONE = all cards */
    uint32_t flags;     /* YMGUI_CLEAR_FLAG_* */
};

/*---------------------------------------------------------------------------
 * Card placement / move / resize — payload of YMGUI_OSC_CS_CARD_PLACE.
 *
 * First emit for a given figure_id creates the card; subsequent emits
 * with the same id move/resize it.
 *
 * Placement model — ncurses-dialog style:
 *   col, row are in grid cells, relative to the visible top-left at
 *   message arrival. The server resolves row to a rolling_row anchor
 *   (= row0_absolute + row), so the card scrolls with terminal content
 *   exactly like ydraw primitives or the previous single-frame ymgui
 *   layer. If row + h_cells doesn't fit below the cursor, the server
 *   scrolls the terminal up before placement.
 *
 *   After placement, the text-layer cursor jumps to (col=0, row+h_cells)
 *   so subsequent stdout flows underneath the card.
 *
 *   On resize/move (existing figure_id), the cursor is NOT moved — only
 *   create-time placement advances it. col/row in a move-emit are
 *   re-resolved against the current visible window.
 *-------------------------------------------------------------------------*/
struct yetty_ymgui_wire_card_place {
    uint32_t magic;     /* YMGUI_WIRE_MAGIC_CARD_PLACE */
    uint32_t version;   /* YMGUI_WIRE_VERSION */
    uint32_t figure_id; /* must be != YMGUI_FIGURE_ID_NONE */
    uint32_t flags;     /* reserved, send 0 */
    int32_t col;        /* grid column (0-based) */
    int32_t row;        /* grid row (0-based, relative to visible top) */
    uint32_t w_cells;   /* width  in cells; 0 = "track right edge"  */
    uint32_t h_cells;   /* height in cells; 0 = "track bottom edge"
                               * (both dimensions follow grid resizes
                               * dynamically while the value stays 0)  */
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
    uint32_t figure_id;
    uint32_t flags; /* YMGUI_CLEAR_FLAG_* */
};

/* Server → client client-input event structs (mouse / resize / focus / key)
 * live in <yetty/yterminal/client-input.h>. */

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMGUI_WIRE_H */
