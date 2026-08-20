/*
 * tty-render.h — the ymux per-attachment TTY renderer (#699).
 *
 * The ymux analog of tmux's `struct tty` + tty.c emitter (pinned baseline
 * d5afb67). Per #699 the classical text path must produce BYTE-IDENTICAL output
 * to tmux for the same screen operations, geometry, and terminal capabilities;
 * these are faithful ports of the corresponding tmux functions, driven by the
 * NEGOTIATED capability profile (struct yetty_ymux_tty_caps — the tmux
 * terminfo analog, defaulting to xterm-256color): a missing capability takes
 * tmux's documented fallback (ECH -> spaces, extended underline / underline
 * colour / hyperlink -> dropped-but-dirty pen). Vertical margins (DECSTBM)
 * are ported; horizontal margins are not (matching the tmux baseline's
 * xterm profile). The emitted bytes are ordinary terminal output
 * carried unchanged as the yRPC terminal-byte payload — never a semantic-cell
 * or custom-redraw protocol.
 *
 * Plain C leaf helpers (no yclass object): the renderer state is owned by the
 * projector/attachment and passed in explicitly.
 */
#ifndef YETTY_YMUX_TTY_RENDER_H
#define YETTY_YMUX_TTY_RENDER_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include "tty-term.h"

/* The subset of tmux's `struct tty` needed by the ported emitters so far: the
 * assumed cursor position + screen geometry + scroll region that tty_cursor
 * reads and updates. Grows as more of the renderer is ported (pen/SGR cache,
 * modes, terminfo flags, backpressure, invalidation). */
/* Cell attribute bits (the ymux analog of tmux's GRID_ATTR_*), in tmux's SGR
 * emission order. Colors are carried separately (ported later, harness-pinned).*/
enum {
    YMUX_TTY_ATTR_BOLD = 1u << 0,      /* bold  \e[1m  */
    YMUX_TTY_ATTR_DIM = 1u << 1,       /* dim   \e[2m  */
    YMUX_TTY_ATTR_ITALICS = 1u << 2,   /* sitm  \e[3m  */
    YMUX_TTY_ATTR_UNDERLINE = 1u << 3, /* smul  \e[4m  */
    YMUX_TTY_ATTR_BLINK = 1u << 4,     /* blink \e[5m  */
    YMUX_TTY_ATTR_REVERSE = 1u << 5,   /* rev   \e[7m  */
    YMUX_TTY_ATTR_HIDDEN = 1u << 6,    /* invis \e[8m  */
    YMUX_TTY_ATTR_STRIKE = 1u << 7,    /* smxx  \e[9m  */
    /* Requested but UNSUPPORTED on the profile (double/curly underline,
     * overline, underline colour, hyperlink on xterm-256color): never
     * emitted, but the pen is non-default so the end-of-flush reset fires —
     * tmux's dropped-attribute behavior. */
    YMUX_TTY_ATTR_EXOTIC = 1u << 8,
    /* Extended underline STYLE rides bits 9..11 of the attr word (values
     * 2..5 emit \e[4:Nm on Smulx-enabled profiles; style 1 is the base
     * UNDERLINE bit). */
    YMUX_TTY_ATTR_STYLE_SHIFT = 9,
    /* Overline (SGR 53) on an overline-capable profile — emitted via Smol
     * (699-F). Bit 12 (above the 3-bit style field). */
    YMUX_TTY_ATTR_OVERLINE = 1u << 12,
};

/* Emitter colour encoding (an int carrying tmux's colour intent):
 *   -1                       = terminal default        -> \e[39m / \e[49m
 *   0..255                   = palette index           -> setaf/setab
 *   RGB_FLAG | 0xRRGGBB      = truecolour               -> \e[38;2;R;G;Bm
 * The profile advertised by the yscene grid decides whether a true-colour cell
 * emits truecolour (this profile — libvterm supports it) or is downgraded to a
 * palette index via yetty_ymux_rgb_to_256 (a 256-only profile). */
enum { YMUX_TTY_COLOR_DEFAULT = -1, YMUX_TTY_COLOR_RGB_FLAG = 0x1000000 };
#define YMUX_TTY_COLOR_RGB(red, green, blue)                                                       \
    ((int)(YMUX_TTY_COLOR_RGB_FLAG | ((uint32_t)(red) << 16) | ((uint32_t)(green) << 8) |          \
           (uint32_t)(blue)))

/* The negotiated terminal CAPABILITY PROFILE (review #13) — the tmux-terminfo
 * analog. Every emitter branch that has a with/without form consults it: a
 * missing capability takes tmux's documented fallback (ECH -> spaces, CSR/
 * IL/DL -> region redraw, extended underline/underline colour/hyperlink ->
 * dropped-but-dirty pen). Defaults = xterm-256color. */
struct yetty_ymux_tty_caps {
    unsigned colors_256 : 1;
    unsigned colors_rgb : 1;
    unsigned ech : 1;                /* \e[nX */
    unsigned insert_delete_line : 1; /* \e[nL / \e[nM (both present; wire mask) */
    unsigned insert_line : 1;        /* \e[nL — il, resolved independently */
    unsigned delete_line : 1;        /* \e[nM — dl, resolved independently */
    unsigned ich : 1;                /* \e[n@ — parm_ich, resolved from the model */
    unsigned dch : 1;                /* \e[nP — parm_dch, resolved from the model */
    unsigned decstbm : 1;            /* \e[t;br scroll regions */
    unsigned bce : 1;                /* background colour erase */
    unsigned extended_underline : 1; /* Smulx (double/curly) */
    unsigned underline_colour : 1;   /* Setulc */
    unsigned hyperlink : 1;          /* OSC 8 passthrough */
    unsigned acs : 1;                /* alternate charset drawing */
    /* Named tmux FEATURES (tty-features.c model, review #19): resolved from
     * TERM-implied defaults + the client's explicit feature string, with
     * `name@` cancellation. Not every bit branches the emitter yet — they
     * are the capability STATE the renderer consults as its emission
     * coverage grows. */
    unsigned mouse : 1;         /* SGR mouse reporting */
    unsigned title : 1;         /* OSC 0/2 title setting */
    unsigned clipboard : 1;     /* OSC 52 set-clipboard */
    unsigned focus : 1;         /* focus in/out reporting */
    unsigned cursor_style : 1;  /* DECSCUSR (cstyle) */
    unsigned cursor_colour : 1; /* OSC 12 (ccolour) */
    unsigned margins : 1;       /* DECLRMM/DECSLRM horizontal margins */
    unsigned overline : 1;      /* SGR 53 */
    unsigned strikethrough : 1; /* SGR 9 */
    unsigned osc7 : 1;          /* OSC 7 working-directory report */
    unsigned extkeys : 1;       /* extended keys (modifyOtherKeys / CSI u) */
    unsigned rectfill : 1;      /* DECFRA rectangle fill */
    unsigned sixel : 1;         /* sixel graphics */
    unsigned sync : 1;          /* DEC 2026 synchronized output */
    /* Derived from the `am` boolean (tmux TERM_NOAM): a terminal WITHOUT
     * automatic margins cannot safely take a glyph in the bottom-right cell —
     * tmux truncates a run so the last column of the last row stays unwritten
     * (tty_putn). This is the sole bottom-right input.
     *
     * `xenl` (magic-margin / eat-newline-glitch) is carried for model
     * completeness only: tmux has NO xenl capability in its code table and
     * never branches rendering on it — the NOAM workaround exists precisely
     * BECAUSE non-xenl terminals ignore a newline past the last column, so the
     * behaviour is folded into NOAM. ymux must not add an xenl emitter branch
     * or it would diverge from tmux. */
    unsigned noam : 1;
    unsigned xenl : 1;
};

struct yetty_ymux_tty_caps yetty_ymux_tty_caps_xterm_256color(void);
/* tmux's terminfo/features resolution (tty-features.c model): TERM-family
 * base capabilities + TERM-implied default features + the client's explicit
 * features string. Feature tokens ADD; a `name@` token CANCELS (override
 * semantics). Unknown TERM names resolve to a MINIMAL vt100-class profile,
 * never silently to xterm. */
struct yetty_ymux_tty_caps yetty_ymux_tty_caps_resolve(const char *term_name, const char *features);

/* FULL-model resolution (review #21): load the compiled terminfo entry for
 * `term_name` (database or deterministic fallback), then apply the
 * TERM-implied default features and the client's explicit features/overrides
 * string (feature tokens, cap=value overrides, cap@ cancellations) — the
 * tmux tty-term.c + tty-features.c pipeline. Free with
 * yetty_ymux_tty_term_free. */
struct yetty_ycore_void_result yetty_ymux_tty_term_resolve(struct yetty_ymux_tty_term *out,
                                                           const char *term_name,
                                                           const char *features);

struct yetty_ymux_tty {
    struct yetty_ymux_tty_caps caps; /* the negotiated profile (derived flags) */
    /* The FULL capability model (review #21): the loaded terminfo entry with
     * features/overrides applied. When set, parameterized emission expands
     * these capability strings (tparm) instead of hard-coded ANSI; NULL
     * keeps the pinned xterm-256color byte fallback (tests that construct
     * a bare tty). BORROWED — owned by the projector/attachment. */
    const struct yetty_ymux_tty_term *term;
    uint32_t cx, cy;         /* assumed cursor position (tmux tty->cx/cy) */
    uint32_t sx, sy;         /* screen columns, rows (tmux tty->sx/sy) */
    uint32_t rupper, rlower; /* scroll region rows (default 0 .. sy-1) */
    /* Horizontal margins (tmux tty->rleft/rright): the DECSLRM columns a
     * margins-capable client is currently narrowed to (default 0 .. sx-1 =
     * full width; UINT32_MAX after invalidate so the next set is forced).
     * Only consulted when caps.margins — a non-margin profile never emits
     * DECSLRM and its cursor shortcuts stay unrestricted. */
    uint32_t rleft, rright;
    /* The terminal pen actually emitted (tmux tty->cell) + the last requested
     * cell for the early-out (tmux tty->last_cell). */
    uint16_t cell_attr;
    /* Exotic emission state (review #17, enabled profiles): the ACTIVE
     * underline-colour SGR token (empty = none; cleared by sgr0) and the
     * active hyperlink URI (OSC-8 persists ACROSS sgr0 — closed only
     * explicitly). Link ids intern per tty in tmux's "tmuxN" order. */
    char active_underline_colour[40];
    /* Active hyperlink URI (up to MAX_HYPERLINK_URI) + its EXTERNAL id. Change
     * detection keys on the id, not the URI: two anonymous links to the SAME URI
     * carry distinct ids, so the run must re-open with the new id. */
    char active_link[1025];
    uint32_t active_link_id;
    int cell_fg, cell_bg;
    uint16_t last_attr;
    int last_fg, last_bg;
    int cursor_visible;     /* -1 unknown, 0 hidden, 1 shown (tmux MODE_CURSOR) */
    int cursor_shape_param; /* -1 unknown, else the last DECSCUSR parameter emitted */
};

/* Emit the SGR bytes to move the terminal pen to (attr, fg, bg) and update the
 * cache — a faithful port of tmux tty_attributes + tty_colours + tty_reset for
 * the fixed xterm-256color profile (AX = default-colour reset supported):
 *   - no output when the cell equals the last requested cell;
 *   - reset (sgr0 = \e(B\e[m) when any active attribute bit is being CLEARED;
 *   - colours (tty_colours): \e[39m/\e[49m to go default, else setaf/setab
 *     (n<8 -> 3n/4n, 8..15 -> 9(n-8)/10(n-8), 16..255 -> 38;5;n / 48;5;n);
 *   - then only the newly-SET attribute bits, in tmux's order.
 * fg/bg are YMUX_TTY_COLOR_DEFAULT or a 0..255 palette index. Byte-identical to
 * tmux for the same transition. (hyperlink/charset/extended-underline/RGB and the
 * underscore colour follow, harness-pinned.) */
struct yetty_ycore_void_result yetty_ymux_tty_region(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t top,
                                                     uint32_t bottom);
struct yetty_ycore_void_result yetty_ymux_tty_pen_reset(struct yetty_ymux_tty *tty,
                                                        struct yetty_ycore_buffer *out);
struct yetty_ycore_void_result yetty_ymux_tty_attributes(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out,
                                                         uint16_t attr, int fg, int bg);

/* Exotic-channel variant (review #17): underline_colour = the verbatim SGR
 * token ("58:5:196", NULL = none), link = the URI (NULL = none — a change
 * to NULL emits the OSC-8 close). Plain-profile callers use the base
 * function (equivalent to NULL, NULL). */
struct yetty_ycore_void_result yetty_ymux_tty_attributes_exotic(struct yetty_ymux_tty *tty,
                                                                struct yetty_ycore_buffer *out,
                                                                uint16_t attr, int fg, int bg,
                                                                const char *underline_colour,
                                                                const char *link, uint32_t link_id);

/* Show/hide the cursor, emitting only on a change (tmux tty_update_mode /
 * MODE_CURSOR): show -> cnorm (\e[?12l\e[?25h), hide -> civis (\e[?25l). */
struct yetty_ycore_void_result yetty_ymux_tty_cursor_visible(struct yetty_ymux_tty *tty,
                                                             struct yetty_ycore_buffer *out,
                                                             int visible);

/* Emit the cursor STYLE via DECSCUSR (\e[<n> q), the xterm Ss capability tmux
 * uses. `shape` is a VTERM_PROP_CURSORSHAPE_* value (1 block, 2 underline, 3
 * bar); the parameter is n = (shape-1)*2 + (blink ? 1 : 2), i.e. 1/2 block,
 * 3/4 underline, 5/6 bar (odd = blinking). Emits only when the parameter
 * changes from what was last sent. */
struct yetty_ycore_void_result yetty_ymux_tty_cursor_shape(struct yetty_ymux_tty *tty,
                                                           struct yetty_ycore_buffer *out,
                                                           int shape, int blink);

/* Write `len` raw text bytes of total display `width` and advance the assumed
 * cursor — a faithful port of tmux tty_putn for the automargin profile: the
 * bytes are emitted verbatim, the cursor advances by width, and filling the last
 * column PARKS cx at sx (tmux's DEFERRED autowrap) so the next tty_cursor forces
 * an absolute move. `bytes` are the caller's already-encoded UTF-8/ACS text. */
struct yetty_ycore_void_result yetty_ymux_tty_putn(struct yetty_ymux_tty *tty,
                                                   struct yetty_ycore_buffer *out,
                                                   const void *bytes, size_t len, uint32_t width);

/* Clear from the cursor to the end of the line — el (\e[K). With BCE this clears
 * using the pen's current background, so the caller sets the desired blank style
 * (tty_attributes) first, exactly as tmux does. Cursor unchanged. */
struct yetty_ycore_void_result yetty_ymux_tty_clear_line(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out);

/* Erase `count` characters from the cursor — ech (\e[<count>X), BCE background.
 * Cursor unchanged. count 0 emits nothing. */
struct yetty_ycore_void_result yetty_ymux_tty_clear_chars(struct yetty_ymux_tty *tty,
                                                          struct yetty_ycore_buffer *out,
                                                          uint32_t count);

/* One projected screen cell handed to the row emitter (the shape the projector
 * derives from the ymux engine's grid). `text` is the already-encoded UTF-8 (or
 * ACS) glyph bytes; a blank cell is a single space with default style. */
struct yetty_ymux_tty_cell {
    const char *text;
    uint32_t len;                 /* byte length of text */
    uint16_t attr;                /* YMUX_TTY_ATTR_* (style in bits 9..11) */
    const char *underline_colour; /* verbatim SGR token or NULL */
    const char *link;             /* URI or NULL */
    uint32_t link_id;             /* hyperlink EXTERNAL id (tmux<N>); 0 = none */
    int fg, bg;                   /* YMUX_TTY_COLOR_DEFAULT or 0..255 */
    uint32_t width;               /* display columns (1 or 2) */
};

/* Nearest 256-palette index (0..255) for an RGB colour — the RGB->256 downgrade
 * the fixed no-truecolour profile applies to a true-colour cell. Faithful port of
 * tmux colour_find_rgb / colour_to_6cube (colour.c): 6x6x6 cube + 24-step
 * grayscale, whichever is closer. */
int yetty_ymux_rgb_to_256(uint8_t red, uint8_t green, uint8_t blue);

/* Build the full 256-entry palette (packed 0xAABBGGRR) the reverse-map needs:
 * 0..15 from `base16` (the engine's OSC-4-settable base colours), 16..231 the
 * fixed xterm 6x6x6 colour cube, 232..255 the fixed 24-step grayscale ramp. */
void yetty_ymux_build_palette256(const uint32_t base16[16], uint32_t palette[256]);

/* Recover an emitter colour (default / palette index / truecolour) from an ymux
 * engine cell's RESOLVED RGB (packed 0xAABBGGRR), given the terminal default
 * colour and the `palette_count`-entry palette (same packing). Since the cell
 * dropped tmux's index/RGB intent, we approximate it: RGB == default -> DEFAULT;
 * RGB matching a palette entry -> that INDEX (so an indexed colour re-emits as
 * setaf, matching tmux); otherwise -> truecolour (YMUX_TTY_COLOR_RGB). The rare
 * true-RGB-equals-a-palette-entry case renders identically. A 256-only profile
 * would route the else branch through yetty_ymux_rgb_to_256 instead. */
int yetty_ymux_rgb_to_tty_color(uint32_t rgb, uint32_t default_rgb, const uint32_t *palette,
                                uint32_t palette_count);

/* Draw row `py` (`cells[0..count)`) by COMPOSING the primitives above: cursor to
 * the line start, then per cell { tty_attributes(style) + tty_putn(glyph) }, and
 * a trailing run of DEFAULT-blank cells is cleared with EL instead of emitting
 * spaces (BCE). This is the tmux tty_draw_line role for the common case; the
 * full get_empty / homogeneous-run / insert-delete parity is harness-pinned
 * (#17). Cursor/pen state advance to reflect the drawn line. */
struct yetty_ycore_void_result yetty_ymux_tty_draw_line(struct yetty_ymux_tty *tty,
                                                        struct yetty_ycore_buffer *out,
                                                        const struct yetty_ymux_tty_cell *cells,
                                                        uint32_t count, uint32_t py);

/* Initialize for a `rows`x`cols` screen: cursor at (0,0), full-screen scroll
 * region. */
void yetty_ymux_tty_init(struct yetty_ymux_tty *tty, uint32_t rows, uint32_t cols);

/* Discard all assumed terminal state (tmux tty_invalidate) — used on a new
 * attach, a resync, or slow-client recovery (#21): the receiving terminal's
 * state is unknown, so force the NEXT cursor move to be absolute (cx/cy unknown)
 * and re-establish the pen from default. The caller then emits a complete redraw
 * (which begins with an sgr0 reset on the wire). No bytes are emitted here. */
void yetty_ymux_tty_invalidate(struct yetty_ymux_tty *tty);

/* Emit the SHORTEST cursor move to (cx,cy) into `out` and update tty->cx/cy — a
 * faithful port of tmux tty_cursor (tty.c) including the horizontal-margin
 * interactions: on a margins-capable profile the CR shortcuts require rleft == 0
 * and the multi-cell CUB/CUF moves are disabled, exactly as tmux guards them.
 * Byte-identical to tmux for the same state + target. */
struct yetty_ycore_void_result yetty_ymux_tty_cursor(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t cx,
                                                     uint32_t cy);

/* Set the horizontal margins to columns [rleft..rright] — a faithful port of
 * tmux tty_margin: no-op on a non-margins profile; no-op when unchanged;
 * otherwise re-emits the vertical region (CSR, unconditionally — tmux does),
 * then DECSLRM (`\e[s` for full width, `\e[<l+1>;<r+1>s` otherwise), and
 * invalidates the cursor. yetty_ymux_tty_margin_off restores full width. */
struct yetty_ycore_void_result yetty_ymux_tty_margin(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t rleft,
                                                     uint32_t rright);
struct yetty_ycore_void_result yetty_ymux_tty_margin_off(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out);

/* Scroll the rectangle [top..bottom] x [rleft..rright] up by `lines` on a
 * margins-capable client — the partial-width path of tmux tty_cmd_scrollup:
 * region to the rect rows, DECSLRM to the rect columns, then either cursor to
 * the margin bottom-right + `\n` (single line) or `\e[<n>S` (indn). The caller
 * restores region/margins afterwards exactly as tmux's reset does. */
struct yetty_ycore_void_result yetty_ymux_tty_margin_scrollup(struct yetty_ymux_tty *tty,
                                                              struct yetty_ycore_buffer *out,
                                                              uint32_t top, uint32_t bottom,
                                                              uint32_t rleft, uint32_t rright,
                                                              uint32_t lines);

#endif /* YETTY_YMUX_TTY_RENDER_H */
