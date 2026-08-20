/*
 * tty-render.c — faithful ports of tmux's TTY emitter (pinned d5afb67) for the
 * fixed xterm-256color capability profile. See tty-render.h.
 *
 * xterm-256color capability strings this file encodes directly (the fixed
 * profile the yscene grid advertises):
 *   home=\E[H  cub1=^H  cuf1=\E[C  cuu1=\E[A  cud1=\n
 *   hpa=\E[%i%p1%dG  vpa=\E[%i%p1%dd   (%i => 1-based param)
 *   cub=\E[%p1%dD  cuf=\E[%p1%dC  cuu=\E[%p1%dA  cud=\E[%p1%dB
 *   cup=\E[%i%p1%d;%p2%dH
 */
#include "tty-render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ytrace/ytrace.h>

struct yetty_ymux_tty_caps yetty_ymux_tty_caps_xterm_256color(void)
{
    struct yetty_ymux_tty_caps caps = {0};
    caps.colors_256 = 1;
    caps.colors_rgb = 1; /* the attach negotiates RGB off for 256-only clients */
    caps.ech = 1;
    caps.insert_delete_line = 1;
    caps.insert_line = 1;
    caps.delete_line = 1;
    caps.ich = 1;
    caps.dch = 1;
    caps.decstbm = 1;
    caps.bce = 1;
    /* xterm-256color terminfo has NO Smulx/Setulc/hyperlink/ACS-needed caps:
     * the exotic-pen drops (review #12) are exactly these bits being off. */
    caps.extended_underline = 0;
    caps.underline_colour = 0;
    caps.hyperlink = 0;
    caps.acs = 0;
    return caps;
}

/* ONE named tmux feature applied to the capability profile — the complete
 * tty-features.c mapping (review #19), plus `name@` CANCELLATION (the
 * terminal-overrides semantics adapted to the features channel: a trailing
 * '@' removes what the name would add). Unknown names are ignored exactly
 * as tmux ignores features its tty cannot use. */
static void tty_feature_apply(struct yetty_ymux_tty_caps *caps, const char *token, size_t token_len)
{
    int cancel = 0;
    if (token_len > 0 && token[token_len - 1] == '@') {
        cancel = 1;
        --token_len;
    }
    unsigned value = cancel ? 0u : 1u;
#define YMUX_FEATURE_IS(literal)                                                                   \
    (token_len == sizeof(literal) - 1 && strncasecmp(token, literal, token_len) == 0)
    if (YMUX_FEATURE_IS("256")) {
        caps->colors_256 = value;
    } else if (YMUX_FEATURE_IS("RGB")) {
        caps->colors_rgb = value;
        if (!cancel) {
            caps->colors_256 = 1; /* RGB implies 256 (tmux feature order) */
        }
    } else if (YMUX_FEATURE_IS("usstyle")) {
        caps->extended_underline = value;
        caps->underline_colour = value;
    } else if (YMUX_FEATURE_IS("hyperlinks")) {
        caps->hyperlink = value;
    } else if (YMUX_FEATURE_IS("mouse")) {
        caps->mouse = value;
    } else if (YMUX_FEATURE_IS("title")) {
        caps->title = value;
    } else if (YMUX_FEATURE_IS("clipboard")) {
        caps->clipboard = value;
    } else if (YMUX_FEATURE_IS("focus")) {
        caps->focus = value;
    } else if (YMUX_FEATURE_IS("cstyle")) {
        caps->cursor_style = value;
    } else if (YMUX_FEATURE_IS("ccolour") || YMUX_FEATURE_IS("ccolor")) {
        caps->cursor_colour = value;
    } else if (YMUX_FEATURE_IS("margins")) {
        caps->margins = value;
    } else if (YMUX_FEATURE_IS("overline")) {
        caps->overline = value;
    } else if (YMUX_FEATURE_IS("strikethrough")) {
        caps->strikethrough = value;
    } else if (YMUX_FEATURE_IS("osc7")) {
        caps->osc7 = value;
    } else if (YMUX_FEATURE_IS("extkeys")) {
        caps->extkeys = value;
    } else if (YMUX_FEATURE_IS("rectfill")) {
        caps->rectfill = value;
    } else if (YMUX_FEATURE_IS("sixel")) {
        caps->sixel = value;
    } else if (YMUX_FEATURE_IS("sync")) {
        caps->sync = value;
    } else if (YMUX_FEATURE_IS("margins")) {
        caps->margins = value;
    }
    /* CAPABILITY-name cancellations (terminal-overrides `cap@`, distinct from
     * the feature tokens above): a cancelled terminfo capability flips the
     * strategy boolean so the projector takes tmux's fallback instead of the
     * cancelled operation. caps_resolve is the single boolean authority — it
     * knows the synthetic families (xterm-nocsr's decstbm=0) the raw terminfo
     * model cannot; the model is consulted only for the emitted BYTES. Only
     * the cancel form matters (a bare cap name is already implied by family). */
    else if (cancel && YMUX_FEATURE_IS("ech")) {
        caps->ech = 0;
    } else if (cancel && YMUX_FEATURE_IS("csr")) {
        caps->decstbm = 0;
    } else if (cancel && YMUX_FEATURE_IS("il")) {
        caps->insert_line = 0; /* independent: il@ does not cancel dl */
        caps->insert_delete_line = 0;
    } else if (cancel && YMUX_FEATURE_IS("dl")) {
        caps->delete_line = 0; /* independent: dl@ does not cancel il */
        caps->insert_delete_line = 0;
    } else if (cancel && YMUX_FEATURE_IS("smulx")) {
        caps->extended_underline = 0;
    } else if (cancel && YMUX_FEATURE_IS("setulc")) {
        caps->underline_colour = 0;
    } else if (cancel && YMUX_FEATURE_IS("smxx")) {
        caps->strikethrough = 0;
    } else if (cancel && (YMUX_FEATURE_IS("smol") || YMUX_FEATURE_IS("rmol"))) {
        caps->overline = 0;
    }
#undef YMUX_FEATURE_IS
}

static void tty_features_apply_list(struct yetty_ymux_tty_caps *caps, const char *list)
{
    const char *cursor = list;
    while (cursor && *cursor) {
        /* Split ONLY on comma/space — a ':' is a legitimate character INSIDE a
         * terminfo override value (e.g. `Setulc=\E[58::2::...`), so splitting on
         * it would fragment the value into bogus tokens. This matches the model
         * tokenizer in tty-term.c (apply_features), which also keeps ':'. */
        size_t token_len = strcspn(cursor, ", ");
        if (token_len > 0) {
            tty_feature_apply(caps, cursor, token_len);
        }
        cursor += token_len;
        if (*cursor) {
            ++cursor;
        }
    }
}

/* tmux's terminfo/features STATE MODEL (tty-features.c/tty-term.c).
 * Resolution order is tmux's exactly: the TERM entry's own capabilities
 * form the base, then tty_default_features(TERM) implies features, then
 * the client's explicit features string applies (adds, or cancels via
 * `name@`). Every family row is a PINNED table mirroring the tic-compiled
 * entry + tmux's terminal-features defaults for that family — and an
 * UNKNOWN TERM resolves to a minimal vt100-class profile, never silently
 * to xterm (review #19). */
struct term_family_row {
    const char *prefix;      /* prefix match, tmux-style (options_match) */
    unsigned colors_256 : 1; /* setaf/setab 256 */
    unsigned ech : 1;
    unsigned idl : 1; /* il1/dl1 */
    unsigned decstbm : 1;
    unsigned bce : 1;
    unsigned acs : 1;
    const char *default_features; /* tty_default_features row (NULL = none) */
};

/* The pinned tty_default_features + fallback-profile rows. Function-local
 * static (no file-scope data). */
static const struct term_family_row *family_rows(size_t *count)
{
    static const struct term_family_row families[] = {
        {"xterm-nocsr", 1, 1, 1, 0, 1, 1, "clipboard,ccolour,cstyle,focus,title"},
        {"xterm-nobce", 1, 1, 1, 1, 0, 1, "clipboard,ccolour,cstyle,focus,title"},
        {"xterm-256color", 1, 1, 1, 1, 1, 1, "clipboard,ccolour,cstyle,focus,title,mouse"},
        {"xterm", 0, 1, 1, 1, 1, 1, "clipboard,ccolour,cstyle,focus,title,mouse"},
        /* screen: no ECH (the no-ech spaces fallback). */
        {"screen-256color", 1, 0, 1, 1, 1, 1, "title,mouse"},
        {"screen", 0, 0, 1, 1, 1, 1, "title,mouse"},
        /* tmux's own entries: the modern styling set. */
        {"tmux-256color", 1, 1, 1, 1, 1, 1,
         "256,RGB,overline,usstyle,hyperlinks,strikethrough,clipboard,ccolour,cstyle,focus,"
         "title,mouse"},
        {"tmux", 1, 1, 1, 1, 1, 1,
         "256,RGB,overline,usstyle,hyperlinks,strikethrough,clipboard,ccolour,cstyle,focus,"
         "title,mouse"},
        {"rxvt-unicode", 1, 1, 1, 1, 1, 1, "256,ccolour,cstyle,mouse,title"},
        {"rxvt", 0, 1, 1, 1, 1, 1, "title,mouse"},
        {"putty", 1, 1, 1, 1, 1, 1, "256,ccolour,cstyle,mouse,title"},
        {"foot", 1, 1, 1, 1, 1, 1, "256,RGB,ccolour,cstyle,focus,extkeys,mouse,sync,title"},
        {"kitty", 1, 1, 1, 1, 1, 1,
         "256,RGB,ccolour,cstyle,clipboard,extkeys,focus,hyperlinks,mouse,overline,"
         "strikethrough,sync,title,usstyle,osc7"},
        {"alacritty", 1, 1, 1, 1, 1, 1, "256,RGB,ccolour,cstyle,clipboard,focus,mouse,title"},
        {"vte", 1, 1, 1, 1, 1, 1, "256,RGB,clipboard,focus,title,mouse"},
        {"gnome", 1, 1, 1, 1, 1, 1, "256,RGB,clipboard,focus,title,mouse"},
        {"iterm2", 1, 1, 1, 1, 1, 1,
         "256,RGB,clipboard,cstyle,extkeys,margins,mouse,sync,title,usstyle,osc7"},
        {"wezterm", 1, 1, 1, 1, 1, 1,
         "256,RGB,ccolour,cstyle,clipboard,focus,hyperlinks,mouse,overline,strikethrough,"
         "title,usstyle"},
        {"contour", 1, 1, 1, 1, 1, 1,
         "256,RGB,ccolour,clipboard,cstyle,extkeys,focus,mouse,overline,rectfill,"
         "strikethrough,sync,title,usstyle"},
        {"mintty", 1, 1, 1, 1, 1, 1,
         "256,RGB,ccolour,clipboard,cstyle,extkeys,margins,mouse,overline,strikethrough,"
         "title,usstyle"},
        {"st-256color", 1, 1, 1, 1, 1, 1, "256,RGB,ccolour,cstyle,focus,mouse,title"},
        {"st", 0, 1, 1, 1, 1, 1, "ccolour,cstyle,focus,mouse,title"},
        {"linux", 0, 1, 1, 1, 1, 1, NULL},
        /* vt100: no colour, no ECH, no il1/dl1 param forms — the minimal
         * profile (scroll idioms only via csr). */
        {"vt100", 0, 0, 0, 1, 0, 1, NULL},
    };
    *count = sizeof(families) / sizeof(families[0]);
    return families;
}

/* Prefix-match a TERM name against the pinned family rows (tmux-style). */
static const struct term_family_row *family_lookup(const char *name)
{
    size_t family_count = 0;
    const struct term_family_row *families = family_rows(&family_count);
    for (size_t index = 0; index < family_count; ++index) {
        size_t prefix_len = strlen(families[index].prefix);
        if (strncmp(name, families[index].prefix, prefix_len) == 0) {
            return &families[index];
        }
    }
    return NULL;
}

struct yetty_ymux_tty_caps yetty_ymux_tty_caps_resolve(const char *term_name, const char *features)
{
    struct yetty_ymux_tty_caps caps = {0};
    const char *name = term_name ? term_name : "xterm-256color";
    const struct term_family_row *row = family_lookup(name);
    if (!row) {
        /* UNKNOWN terminal: a MINIMAL vt100-class profile — never silently
         * the xterm row (review #19). Scroll idioms via CSR + IL/DL and
         * line drawing; no colour promises, no ECH, no styling. The client
         * can still ADD real features explicitly. */
        ydebug("ymux tty caps: unknown TERM '%s' — minimal profile", name);
        caps.insert_delete_line = 1;
        caps.insert_line = 1;
        caps.delete_line = 1;
        caps.decstbm = 1;
        caps.acs = 1;
        tty_features_apply_list(&caps, features);
        return caps;
    }
    caps.ich = 1; /* the family table has no ich/dch column; the production model
                   * (projector_set_terminal) overrides these from has(ICH/DCH) */
    caps.dch = 1;
    caps.colors_256 = row->colors_256;
    caps.ech = row->ech;
    caps.insert_delete_line = row->idl;
    caps.insert_line = row->idl;
    caps.delete_line = row->idl;
    caps.decstbm = row->decstbm;
    caps.bce = row->bce;
    caps.acs = row->acs;
    /* --- Features: TERM-implied first, then the explicit string (adds and
     * `name@` cancellations, in order). --- */
    tty_features_apply_list(&caps, row->default_features);
    tty_features_apply_list(&caps, features);
    return caps;
}

/* FULL-model resolution (review #21): terminfo entry -> TERM-implied default
 * features -> client features/overrides — the tmux tty-term.c/tty-features.c
 * pipeline, producing the capability-STRING model the emitters expand. */
struct yetty_ycore_void_result yetty_ymux_tty_term_resolve(struct yetty_ymux_tty_term *out,
                                                           const char *term_name,
                                                           const char *features)
{
    const char *name = term_name ? term_name : "xterm-256color";
    struct yetty_ycore_void_result load_res = yetty_ymux_tty_term_load(out, name, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, load_res, "tty_term_resolve: load");
    const struct term_family_row *row = family_lookup(name);
    if (row && row->default_features) {
        struct yetty_ycore_void_result defaults_res =
            yetty_ymux_tty_term_apply_features(out, row->default_features);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, defaults_res, "tty_term_resolve: defaults");
    }
    /* Fold the family row's capability CANCELLATIONS into the MODEL so the model
     * is the single authority — a synthetic family (xterm-nocsr, xterm-noech,
     * screen without ech, …) whose real/fallback terminfo would carry the cap
     * must have it removed here. The projector then derives every string-cap
     * strategy boolean from the model's presence, so strategy and bytes agree.
     * (bce is a terminfo BOOLEAN with no string slot — it stays a caps flag.) */
    if (row) {
        if (!row->decstbm) {
            struct yetty_ycore_void_result cancel_res =
                yetty_ymux_tty_term_apply_features(out, "csr@");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cancel_res, "tty_term_resolve: family csr@");
        }
        if (!row->idl) {
            struct yetty_ycore_void_result cancel_res =
                yetty_ymux_tty_term_apply_features(out, "il@,dl@,il1@,dl1@");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cancel_res, "tty_term_resolve: family il/dl@");
        }
        if (!row->ech) {
            struct yetty_ycore_void_result cancel_res =
                yetty_ymux_tty_term_apply_features(out, "ech@");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cancel_res, "tty_term_resolve: family ech@");
        }
    }
    struct yetty_ycore_void_result features_res = yetty_ymux_tty_term_apply_features(out, features);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, features_res, "tty_term_resolve: features");
    return YETTY_OK_VOID();
}

void yetty_ymux_tty_init(struct yetty_ymux_tty *tty, uint32_t rows, uint32_t cols)
{
    /* Initialization writes the COMPLETE object without reading it (review
     * #14: peeking at the incoming caps was undefined behavior on the
     * uninitialized automatics tests correctly pass in). Callers that carry
     * a negotiated profile across a re-init preserve it EXPLICITLY, like the
     * DECSCUSR cache. */
    tty->caps = yetty_ymux_tty_caps_xterm_256color();
    tty->cx = 0;
    tty->cy = 0;
    tty->sx = cols;
    tty->sy = rows;
    tty->rupper = 0;
    tty->rlower = rows ? rows - 1 : 0;
    tty->rleft = 0;
    tty->rright = cols ? cols - 1 : 0;
    tty->cell_attr = 0;
    tty->cell_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->cell_bg = YMUX_TTY_COLOR_DEFAULT;
    tty->active_underline_colour[0] = 0;
    tty->active_link[0] = 0;
    tty->active_link_id = 0;
    tty->last_attr = 0;
    tty->last_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->last_bg = YMUX_TTY_COLOR_DEFAULT;
    tty->cursor_visible = -1;
    tty->cursor_shape_param = -1;
    tty->term = NULL;
}

void yetty_ymux_tty_invalidate(struct yetty_ymux_tty *tty)
{
    /* cx/cy unknown -> the next tty_cursor takes the absolute (cup) branch.
     * Margins likewise unknown (tmux tty_invalidate sets rleft/rright to
     * UINT_MAX) so the next tty_margin emission is forced, never deduped
     * against stale assumed state. */
    tty->cx = UINT32_MAX;
    tty->cy = UINT32_MAX;
    tty->rleft = UINT32_MAX;
    tty->rright = UINT32_MAX;
    /* Pen assumed default (matching the sgr0 the redraw emits on the wire). */
    tty->cell_attr = 0;
    tty->cell_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->cell_bg = YMUX_TTY_COLOR_DEFAULT;
    tty->last_attr = 0;
    tty->last_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->last_bg = YMUX_TTY_COLOR_DEFAULT;
    /* Cursor visibility unknown -> the redraw re-emits civis/cnorm. The
     * SHAPE cache is PRESERVED: tmux never re-emits DECSCUSR after a pane
     * redraw (its cstyle cache survives tty_invalidate), and re-emitting it
     * broke alt-switch byte parity (review #13). Region default. */
    tty->cursor_visible = -1;
    tty->rupper = 0;
    tty->rlower = tty->sy ? tty->sy - 1 : 0;
}

static struct yetty_ycore_void_result tty_puts(struct yetty_ycore_buffer *out, const char *bytes)
{
    return yetty_ycore_buffer_write(out, bytes, strlen(bytes));
}

/* CSI with one 1-based parameter + final byte, e.g. "\e[<n>G" (hpa/cub/…). */
static struct yetty_ycore_void_result tty_csi1(struct yetty_ycore_buffer *out, uint32_t param,
                                               char final)
{
    char buffer[24];
    int len = snprintf(buffer, sizeof(buffer), "\x1b[%u%c", param, final);
    return yetty_ycore_buffer_write(out, buffer, (size_t)len);
}

/* CSI with two parameters + final byte, e.g. "\e[<row>;<col>H" (cup). */
static struct yetty_ycore_void_result tty_csi2(struct yetty_ycore_buffer *out, uint32_t first,
                                               uint32_t second, char final)
{
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "\x1b[%u;%u%c", first, second, final);
    return yetty_ycore_buffer_write(out, buffer, (size_t)len);
}

/* Emit capability `slot` with tparm parameters when the tty carries the full
 * terminfo model; `written` reports whether the capability produced bytes.
 * A tty without a model (or a terminal without the capability) writes
 * nothing and reports 0 — callers fall back to the pinned byte form. */
static struct yetty_ycore_void_result tty_emit_cap(struct yetty_ymux_tty *tty,
                                                   struct yetty_ycore_buffer *out,
                                                   enum yetty_ymux_tty_term_slot slot, long param1,
                                                   long param2, size_t param_count, int *written)
{
    *written = 0;
    if (!tty->term) {
        return YETTY_OK_VOID();
    }
    char expanded[96];
    long params[2] = {param1, param2};
    size_t len =
        yetty_ymux_tty_term_emit(tty->term, slot, params, param_count, expanded, sizeof(expanded));
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    *written = 1;
    return yetty_ycore_buffer_write(out, expanded, len);
}

/* Capability-first byte emitter: expand `slot` when the model is present,
 * else write the pinned literal `legacy`. */
/* Is capability `slot` AVAILABLE for emission? With no model the legacy byte
 * form is always available (the bare-tty test path). With a model present, the
 * cap is available only if the model carries it — a `cap@` cancellation
 * removes it, and the emitter must then take a fallback (or nothing) rather
 * than silently reintroducing the cancelled operation as a literal (cycle-23
 * P0). */
static int tty_cap_present(const struct yetty_ymux_tty *tty, enum yetty_ymux_tty_term_slot slot)
{
    return !tty->term || yetty_ymux_tty_term_has(tty->term, slot);
}

static struct yetty_ycore_void_result tty_put_cap(struct yetty_ymux_tty *tty,
                                                  struct yetty_ycore_buffer *out,
                                                  enum yetty_ymux_tty_term_slot slot,
                                                  const char *legacy)
{
    /* Cancelled (model present, cap absent): emit NOTHING — do not fall back
     * to the legacy literal, which would reintroduce the cancelled cap. */
    if (!tty_cap_present(tty, slot)) {
        return YETTY_OK_VOID();
    }
    int written = 0;
    struct yetty_ycore_void_result res = tty_emit_cap(tty, out, slot, 0, 0, 0, &written);
    if (YETTY_IS_ERR(res) || written) {
        return res;
    }
    return tty_puts(out, legacy);
}

/* Capability-first 1-parameter emitter (hpa/vpa carry %i in the capability,
 * so `param` is 0-based there; counts pass through unchanged). `legacy_param`
 * is the pre-model literal parameter (1-based where the sequence needs it). */
static struct yetty_ycore_void_result tty_csi1_cap(struct yetty_ymux_tty *tty,
                                                   struct yetty_ycore_buffer *out,
                                                   enum yetty_ymux_tty_term_slot slot, long param,
                                                   uint32_t legacy_param, char legacy_final)
{
    if (!tty_cap_present(tty, slot)) {
        return YETTY_OK_VOID(); /* cancelled — no literal reintroduction */
    }
    int written = 0;
    struct yetty_ycore_void_result res = tty_emit_cap(tty, out, slot, param, 0, 1, &written);
    if (YETTY_IS_ERR(res) || written) {
        return res;
    }
    return tty_csi1(out, legacy_param, legacy_final);
}

struct yetty_ycore_void_result yetty_ymux_tty_cursor(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t cx,
                                                     uint32_t cy)
{
    uint32_t thisx = tty->cx;
    uint32_t thisy = tty->cy;
    /* On a CSR-less terminal tmux can never set its region cache (tty_region
     * early-returns before the rupper/rlower assignment), so the bounds stay
     * at their tty_start value UINT_MAX — which makes the same-column VPA
     * condition below fire for EVERY multi-row up-move. Model that with
     * effective bounds. */
    uint32_t region_upper = tty->caps.decstbm ? tty->rupper : UINT32_MAX;
    uint32_t region_lower = tty->caps.decstbm ? tty->rlower : UINT32_MAX;
    struct yetty_ycore_void_result res = YETTY_OK_VOID();

    /* In the automargin space and want to stay there: do not move. */
    if (cx == thisx && cy == thisy && cx == tty->sx) {
        return res;
    }
    /* Force the cursor into range. */
    if (tty->sx > 0 && cx > tty->sx - 1) {
        cx = tty->sx - 1;
    }
    /* No change. */
    if (cx == thisx && cy == thisy) {
        return res;
    }
    /* Currently at the very end of the line — use absolute movement. */
    if (tty->sx > 0 && thisx > tty->sx - 1) {
        goto absolute;
    }
    /* Move to home position (0, 0). If `home` was cancelled (home@) the
     * optimised form is gone — position absolutely via `cup` so the cursor
     * stays in sync rather than silently not moving. */
    if (cx == 0 && cy == 0) {
        if (!tty_cap_present(tty, YMUX_TTY_TERM_HOME)) {
            goto absolute;
        }
        res = tty_put_cap(tty, out, YMUX_TTY_TERM_HOME, "\x1b[H"); /* home */
        goto done;
    }
    /* Zero on the next line. Under horizontal margins CR moves to the margin
     * left, so the shortcut needs rleft == 0 (tmux: !tty_use_margin || rleft
     * == 0). */
    if (cx == 0 && cy == thisy + 1 && thisy != region_lower &&
        (!tty->caps.margins || tty->rleft == 0)) {
        res = tty_puts(out, "\r\n");
        goto done;
    }
    if (cy == thisy) {
        /* Moving column only, row staying the same. */
        if (cx == 0 && (!tty->caps.margins || tty->rleft == 0)) {
            res = tty_puts(out, "\r"); /* to left edge */
            goto done;
        }
        /* Each optimised column move falls back to absolute `cup` when its
         * cap was cancelled, so the cursor never silently stalls. */
        if (cx == thisx - 1 && tty_cap_present(tty, YMUX_TTY_TERM_CUB1)) {
            res = tty_put_cap(tty, out, YMUX_TTY_TERM_CUB1, "\b"); /* cub1 */
            goto done;
        }
        if (cx == thisx + 1 && tty_cap_present(tty, YMUX_TTY_TERM_CUF1)) {
            res = tty_put_cap(tty, out, YMUX_TTY_TERM_CUF1, "\x1b[C"); /* cuf1 */
            goto done;
        }
        int change = (int)thisx - (int)cx; /* +ve left, -ve right */
        uint32_t magnitude = (uint32_t)(change < 0 ? -change : change);
        if (magnitude > cx && tty_cap_present(tty, YMUX_TTY_TERM_HPA)) {
            res = tty_csi1_cap(tty, out, YMUX_TTY_TERM_HPA, (long)cx, cx + 1, 'G'); /* hpa */
            goto done;
        }
        /* Multi-cell CUB/CUF are DISABLED on a margins-capable profile (tmux
         * guards both branches with !tty_use_margin — the move could cross a
         * margin) — fall through to absolute. The change==2 double-cub1 lives
         * inside tmux's CUB branch, so it is margin-guarded too. */
        if (change > 0 && !tty->caps.margins) {
            if (change == 2) {
                res = tty_puts(out, "\b\b"); /* cub1 x2 */
                goto done;
            }
            if (!tty_cap_present(tty, YMUX_TTY_TERM_CUB)) {
                goto absolute;
            }
            res =
                tty_csi1_cap(tty, out, YMUX_TTY_TERM_CUB, change, (uint32_t)change, 'D'); /* cub */
            goto done;
        }
        if (change < 0 && !tty->caps.margins) {
            if (!tty_cap_present(tty, YMUX_TTY_TERM_CUF)) {
                goto absolute;
            }
            res = tty_csi1_cap(tty, out, YMUX_TTY_TERM_CUF, (long)magnitude, magnitude,
                               'C'); /* cuf */
            goto done;
        }
        goto absolute;
    }
    if (cx == thisx) {
        /* Moving row only, column staying the same. Each optimised vertical
         * move falls back to absolute `cup` when its cap was cancelled. */
        if (thisy != region_upper && cy == thisy - 1 && tty_cap_present(tty, YMUX_TTY_TERM_CUU1)) {
            res = tty_put_cap(tty, out, YMUX_TTY_TERM_CUU1, "\x1b[A"); /* cuu1 */
            goto done;
        }
        if (thisy != region_lower && cy == thisy + 1) {
            res = tty_puts(out, "\n"); /* cud1: one below */
            goto done;
        }
        int change = (int)thisy - (int)cy; /* +ve up, -ve down */
        uint32_t magnitude = (uint32_t)(change < 0 ? -change : change);
        if (magnitude > cy || (change < 0 && (uint32_t)((int)cy - change) > region_lower) ||
            (change > 0 && (uint32_t)((int)cy - change) < region_upper)) {
            if (!tty_cap_present(tty, YMUX_TTY_TERM_VPA)) {
                goto absolute;
            }
            res = tty_csi1_cap(tty, out, YMUX_TTY_TERM_VPA, (long)cy, cy + 1, 'd'); /* vpa */
            goto done;
        }
        if (change > 0) {
            if (!tty_cap_present(tty, YMUX_TTY_TERM_CUU)) {
                goto absolute;
            }
            res =
                tty_csi1_cap(tty, out, YMUX_TTY_TERM_CUU, change, (uint32_t)change, 'A'); /* cuu */
            goto done;
        }
        if (!tty_cap_present(tty, YMUX_TTY_TERM_CUD)) {
            goto absolute;
        }
        res = tty_csi1_cap(tty, out, YMUX_TTY_TERM_CUD, (long)magnitude, magnitude, 'B'); /* cud */
        goto done;
    }

absolute: {
    int written = 0;
    res = tty_emit_cap(tty, out, YMUX_TTY_TERM_CUP, (long)cy, (long)cx, 2, &written);
    if (YETTY_IS_OK(res) && !written) {
        res = tty_csi2(out, cy + 1, cx + 1, 'H'); /* cup (1-based row;col) */
    }
}
done:
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    tty->cx = cx;
    tty->cy = cy;
    return res;
}

/* tmux's colour_256to16 → SGR mapping (extracted byte-for-byte from the pinned
 * oracle): the foreground SGR code a 256-palette index degrades to on a terminal
 * whose `colors` count is below 256. 30-37 = basic, 90-97 = bright; a background
 * is the same code + 10. A 16-colour terminal keeps the bright range, an 8-colour
 * terminal folds everything to 30-37. Returns 0 for an out-of-range index. */
static int tty_colour_256_to_low(int index, int colors)
{
    static const uint8_t map16[256] = {
        30, 31, 32, 33, 34, 35, 36, 37, 90, 91, 92, 93, 94, 95, 96, 97, 30, 34, 34, 34, 94, 94,
        32, 36, 34, 34, 94, 94, 32, 32, 36, 34, 94, 94, 32, 32, 32, 36, 94, 94, 92, 92, 92, 92,
        96, 94, 92, 92, 92, 92, 92, 96, 31, 35, 34, 34, 94, 94, 33, 90, 34, 34, 94, 94, 32, 32,
        36, 34, 94, 94, 32, 32, 32, 36, 94, 94, 92, 92, 92, 92, 96, 94, 92, 92, 92, 92, 92, 96,
        31, 31, 35, 34, 94, 94, 31, 31, 35, 34, 94, 94, 33, 33, 90, 34, 94, 94, 32, 32, 32, 36,
        94, 94, 92, 92, 92, 92, 96, 94, 92, 92, 92, 92, 92, 96, 31, 31, 31, 35, 94, 94, 31, 31,
        31, 35, 94, 94, 31, 31, 31, 35, 94, 94, 33, 33, 33, 37, 94, 94, 92, 92, 92, 92, 96, 94,
        92, 92, 92, 92, 92, 96, 91, 91, 91, 91, 95, 94, 91, 91, 91, 91, 95, 94, 91, 91, 91, 91,
        95, 94, 91, 91, 91, 91, 95, 94, 93, 93, 93, 93, 37, 94, 92, 92, 92, 92, 92, 96, 91, 91,
        91, 91, 91, 95, 91, 91, 91, 91, 91, 95, 91, 91, 91, 91, 91, 95, 91, 91, 91, 91, 91, 95,
        91, 91, 91, 91, 91, 95, 93, 93, 93, 93, 93, 97, 30, 30, 30, 30, 30, 30, 90, 90, 90, 90,
        90, 90, 37, 37, 37, 37, 37, 37, 97, 97, 97, 97, 97, 97,
    };
    static const uint8_t map8[256] = {
        30, 31, 32, 33, 34, 35, 36, 37, 30, 31, 32, 33, 34, 35, 36, 37, 30, 34, 34, 34, 34, 34,
        32, 36, 34, 34, 34, 34, 32, 32, 36, 34, 34, 34, 32, 32, 32, 36, 34, 34, 32, 32, 32, 32,
        36, 34, 32, 32, 32, 32, 32, 36, 31, 35, 34, 34, 34, 34, 33, 30, 34, 34, 34, 34, 32, 32,
        36, 34, 34, 34, 32, 32, 32, 36, 34, 34, 32, 32, 32, 32, 36, 34, 32, 32, 32, 32, 32, 36,
        31, 31, 35, 34, 34, 34, 31, 31, 35, 34, 34, 34, 33, 33, 30, 34, 34, 34, 32, 32, 32, 36,
        34, 34, 32, 32, 32, 32, 36, 34, 32, 32, 32, 32, 32, 36, 31, 31, 31, 35, 34, 34, 31, 31,
        31, 35, 34, 34, 31, 31, 31, 35, 34, 34, 33, 33, 33, 37, 34, 34, 32, 32, 32, 32, 36, 34,
        32, 32, 32, 32, 32, 36, 31, 31, 31, 31, 35, 34, 31, 31, 31, 31, 35, 34, 31, 31, 31, 31,
        35, 34, 31, 31, 31, 31, 35, 34, 33, 33, 33, 33, 37, 34, 32, 32, 32, 32, 32, 36, 31, 31,
        31, 31, 31, 35, 31, 31, 31, 31, 31, 35, 31, 31, 31, 31, 31, 35, 31, 31, 31, 31, 31, 35,
        31, 31, 31, 31, 31, 35, 33, 33, 33, 33, 33, 37, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
        30, 30, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37, 37,
    };
    if (index < 0 || index > 255) {
        return 0;
    }
    return colors >= 16 ? map16[index] : map8[index];
}

/* Emit a non-default colour: truecolour (\e[38;2;R;G;Bm) when the RGB flag is
 * set, else the xterm-256color setaf/setab expansion for a palette index 0..255
 * (basic 3n/4n, bright 9(n-8)/10(n-8), 256 -> 38;5;n / 48;5;n). `base` is 3 for
 * fg / 4 for bg. A terminal with fewer than 256 colours degrades the index to
 * its palette (tmux colour_256to16) BEFORE emitting. */
static struct yetty_ycore_void_result tty_colour_emit(struct yetty_ymux_tty *tty,
                                                      struct yetty_ycore_buffer *out, int colour,
                                                      int base)
{
    char buffer[32];
    int len;
    if (colour & YMUX_TTY_COLOR_RGB_FLAG) {
        int red = (colour >> 16) & 0xFF, green = (colour >> 8) & 0xFF, blue = colour & 0xFF;
        if (tty && tty->term) {
            long rgb[3] = {red, green, blue};
            char expanded[48];
            size_t expanded_len = yetty_ymux_tty_term_emit(
                tty->term, base == 3 ? YMUX_TTY_TERM_SETRGBF : YMUX_TTY_TERM_SETRGBB, rgb, 3,
                expanded, sizeof(expanded));
            if (expanded_len > 0) {
                return yetty_ycore_buffer_write(out, expanded, expanded_len);
            }
            /* Model present but setrgbf/setrgbb CANCELLED: tmux DOWNGRADES to the
             * nearest palette index rather than emitting a raw truecolour CSI the
             * terminal declared it cannot honour (cycle-25 P0). Fall through to
             * the palette path with the downgraded index. */
            colour = yetty_ymux_rgb_to_256((uint8_t)red, (uint8_t)green, (uint8_t)blue);
        } else {
            /* No model (bare-tty test path): the pinned direct truecolour form. */
            len = snprintf(buffer, sizeof(buffer), "\x1b[%d;2;%d;%d;%dm", base == 3 ? 38 : 48, red,
                           green, blue);
            return yetty_ycore_buffer_write(out, buffer, (size_t)len);
        }
    }
    /* Palette colours: the terminfo setaf/setab strings encode tmux's exact
     * 3n / 9n / 38;5;n selection as tparm conditionals — expand them when
     * the model is present. */
    if (tty && tty->term) {
        /* Fewer than 256 colours: degrade the index to the terminal's palette
         * (tmux colour_256to16) and emit the basic/bright SGR directly — the
         * setaf/setab tparm for a low-colour terminal cannot render a high
         * index. Background is the mapped fg code + 10. */
        if (tty->term->colors < 256) {
            int code = tty_colour_256_to_low(colour, tty->term->colors);
            if (code > 0) {
                len = snprintf(buffer, sizeof(buffer), "\x1b[%dm", code + (base == 4 ? 10 : 0));
                return yetty_ycore_buffer_write(out, buffer, (size_t)len);
            }
        }
        long palette[1] = {colour};
        char expanded[48];
        size_t expanded_len = yetty_ymux_tty_term_emit(
            tty->term, base == 3 ? YMUX_TTY_TERM_SETAF : YMUX_TTY_TERM_SETAB, palette, 1, expanded,
            sizeof(expanded));
        if (expanded_len > 0) {
            return yetty_ycore_buffer_write(out, expanded, expanded_len);
        }
        /* Model present but setaf/setab CANCELLED (setaf@/setab@): tmux DROPS
         * the colour — the terminal declared it cannot set it — rather than
         * emitting a raw palette SGR (cycle-26, byte-verified). */
        return YETTY_OK_VOID();
    }
    if (colour < 8) {
        len = snprintf(buffer, sizeof(buffer), "\x1b[%dm", base * 10 + colour);
    } else if (colour < 16) {
        len = snprintf(buffer, sizeof(buffer), "\x1b[%dm", (base + 6) * 10 + (colour - 8));
    } else {
        len = snprintf(buffer, sizeof(buffer), "\x1b[%d;5;%dm", base == 3 ? 38 : 48, colour);
    }
    return yetty_ycore_buffer_write(out, buffer, (size_t)len);
}

/* Faithful port of tmux tty_colours for the fixed profile (AX supported): go to
 * default via \e[39m/\e[49m, otherwise setaf/setab. Updates the pen (cell_fg/bg).*/
static struct yetty_ycore_void_result tty_colours(struct yetty_ymux_tty *tty,
                                                  struct yetty_ycore_buffer *out, int fg, int bg)
{
    struct yetty_ycore_void_result res = YETTY_OK_VOID();
    if (fg == tty->cell_fg && bg == tty->cell_bg) {
        return res;
    }
    if (fg == YMUX_TTY_COLOR_DEFAULT || bg == YMUX_TTY_COLOR_DEFAULT) {
        if (fg == YMUX_TTY_COLOR_DEFAULT && tty->cell_fg != YMUX_TTY_COLOR_DEFAULT) {
            res = tty_puts(out, "\x1b[39m");
            if (YETTY_IS_ERR(res)) {
                return res;
            }
            tty->cell_fg = YMUX_TTY_COLOR_DEFAULT;
        }
        if (bg == YMUX_TTY_COLOR_DEFAULT && tty->cell_bg != YMUX_TTY_COLOR_DEFAULT) {
            res = tty_puts(out, "\x1b[49m");
            if (YETTY_IS_ERR(res)) {
                return res;
            }
            tty->cell_bg = YMUX_TTY_COLOR_DEFAULT;
        }
    }
    if (fg != YMUX_TTY_COLOR_DEFAULT && fg != tty->cell_fg) {
        res = tty_colour_emit(tty, out, fg, 3);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        tty->cell_fg = fg;
    }
    if (bg != YMUX_TTY_COLOR_DEFAULT && bg != tty->cell_bg) {
        res = tty_colour_emit(tty, out, bg, 4);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        tty->cell_bg = bg;
    }
    return res;
}

/* Emit a MANDATORY capability (sgr0 reset, absolute cup): the model's
 * expansion when it carries the cap (honouring a `cap=` override), else the
 * literal — the operation cannot be skipped, so unlike tty_put_cap a cancelled
 * cap still falls back to the literal rather than emitting nothing. */
static struct yetty_ycore_void_result tty_put_cap_mandatory(struct yetty_ymux_tty *tty,
                                                            struct yetty_ycore_buffer *out,
                                                            enum yetty_ymux_tty_term_slot slot,
                                                            const char *literal)
{
    int written = 0;
    struct yetty_ycore_void_result res = tty_emit_cap(tty, out, slot, 0, 0, 0, &written);
    if (YETTY_IS_ERR(res) || written) {
        return res;
    }
    return tty_puts(out, literal);
}

static struct yetty_ycore_void_result tty_reset(struct yetty_ymux_tty *tty,
                                                struct yetty_ycore_buffer *out)
{
    struct yetty_ycore_void_result res =
        tty_put_cap_mandatory(tty, out, YMUX_TTY_TERM_SGR0, "\x1b(B\x1b[m"); /* sgr0 */
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    tty->cell_attr = 0;
    tty->cell_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->cell_bg = YMUX_TTY_COLOR_DEFAULT;
    tty->last_attr = 0;
    tty->last_fg = YMUX_TTY_COLOR_DEFAULT;
    tty->last_bg = YMUX_TTY_COLOR_DEFAULT;
    tty->active_underline_colour[0] = 0; /* sgr0 clears 58; OSC-8 persists */
    return res;
}

/* DECSTBM scroll region (tmux tty_region): emits `\e[<top>;<bottom>r`
 * (1-based, bottom inclusive) only when the cached region differs. Setting a
 * region homes the terminal cursor — the position becomes unknown, so the
 * cached cursor invalidates and the next move takes the absolute branch
 * (tmux marks its cursor UINT_MAX the same way). */
struct yetty_ycore_void_result yetty_ymux_tty_region(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t top,
                                                     uint32_t bottom)
{
    if (tty->rupper == top && tty->rlower == bottom) {
        return YETTY_OK_VOID();
    }
    /* No DECSTBM on this profile: tmux returns BEFORE the cache update, so
     * the region stays unset (UINT_MAX bounds) forever. */
    if (!tty->caps.decstbm) {
        return YETTY_OK_VOID();
    }
    /* tmux's PuTTY wrap-flag workaround (tty_region): with the cursor parked
     * past the last column or unknown, do an explicit move first — home when
     * the row is unknown too. This is where the \e[1;1H before a region
     * reset after IL/DL (cursor invalidated) comes from. */
    if (tty->cx >= tty->sx) {
        struct yetty_ycore_void_result move_res;
        if (tty->cy == UINT32_MAX) {
            move_res = yetty_ymux_tty_cursor(tty, out, 0, 0);
        } else {
            move_res = yetty_ymux_tty_cursor(tty, out, 0, tty->cy);
        }
        if (YETTY_IS_ERR(move_res)) {
            return move_res;
        }
    }
    struct yetty_ycore_void_result res;
    {
        int written = 0;
        res = tty_emit_cap(tty, out, YMUX_TTY_TERM_CSR, (long)top, (long)bottom, 2, &written);
        if (YETTY_IS_OK(res) && !written) {
            char buf[24];
            snprintf(buf, sizeof(buf), "\x1b[%u;%ur", top + 1, bottom + 1);
            res = tty_puts(out, buf);
        }
    }
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    tty->rupper = top;
    tty->rlower = bottom;
    tty->cx = UINT32_MAX;
    tty->cy = UINT32_MAX;
    return res;
}

struct yetty_ycore_void_result yetty_ymux_tty_margin(struct yetty_ymux_tty *tty,
                                                     struct yetty_ycore_buffer *out, uint32_t rleft,
                                                     uint32_t rright)
{
    /* tty_use_margin: a profile without DECSLRM never emits margins. */
    if (!tty->caps.margins) {
        return YETTY_OK_VOID();
    }
    if (tty->rleft == rleft && tty->rright == rright) {
        return YETTY_OK_VOID();
    }
    /* tmux re-emits the vertical region UNCONDITIONALLY before a margin
     * change (tty_margin: tty_putcode_ii CSR without a dedupe check). */
    struct yetty_ycore_void_result res;
    {
        int written = 0;
        res = tty_emit_cap(tty, out, YMUX_TTY_TERM_CSR, (long)tty->rupper, (long)tty->rlower, 2,
                           &written);
        if (YETTY_IS_OK(res) && !written) {
            char buf[24];
            snprintf(buf, sizeof(buf), "\x1b[%u;%ur", tty->rupper + 1, tty->rlower + 1);
            res = tty_puts(out, buf);
        }
    }
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    tty->rleft = rleft;
    tty->rright = rright;
    if (rleft == 0 && rright == tty->sx - 1) {
        res = tty_puts(out, "\x1b[s"); /* Clmg: clear to full width */
    } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "\x1b[%u;%us", rleft + 1, rright + 1); /* Cmg (%i 1-based) */
        res = tty_puts(out, buf);
    }
    tty->cx = UINT32_MAX;
    tty->cy = UINT32_MAX;
    return res;
}

struct yetty_ycore_void_result yetty_ymux_tty_margin_off(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out)
{
    return yetty_ymux_tty_margin(tty, out, 0, tty->sx ? tty->sx - 1 : 0);
}

struct yetty_ycore_void_result yetty_ymux_tty_margin_scrollup(struct yetty_ymux_tty *tty,
                                                              struct yetty_ycore_buffer *out,
                                                              uint32_t top, uint32_t bottom,
                                                              uint32_t rleft, uint32_t rright,
                                                              uint32_t lines)
{
    /* tmux tty_cmd_scrollup, the not-full-width + margins-capable path:
     * region to the rect rows, DECSLRM to the rect columns, then scroll. */
    struct yetty_ycore_void_result res = yetty_ymux_tty_region(tty, out, top, bottom);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    res = yetty_ymux_tty_margin(tty, out, rleft, rright);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    int have_indn = tty->term && yetty_ymux_tty_term_has(tty->term, YMUX_TTY_TERM_INDN);
    if (lines == 1 || !have_indn) {
        /* Under margins the cursor goes to the margin BOTTOM-RIGHT (tmux:
         * tty_cursor(tty, tty->rright, tty->rlower)), then one \n per line. */
        res = yetty_ymux_tty_cursor(tty, out, tty->rright, tty->rlower);
        for (uint32_t line = 0; YETTY_IS_OK(res) && line < lines; ++line) {
            res = tty_puts(out, "\n");
        }
        return res;
    }
    /* indn: cursor to column 0 of the current row (home when unknown), \e[nS. */
    if (tty->cy == UINT32_MAX) {
        res = yetty_ymux_tty_cursor(tty, out, 0, 0);
    } else {
        res = yetty_ymux_tty_cursor(tty, out, 0, tty->cy);
    }
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    {
        int written = 0;
        res = tty_emit_cap(tty, out, YMUX_TTY_TERM_INDN, (long)lines, 0, 1, &written);
        if (YETTY_IS_OK(res) && !written) {
            char buf[16];
            snprintf(buf, sizeof(buf), "\x1b[%uS", lines);
            res = tty_puts(out, buf);
        }
    }
    return res;
}

/* End-of-flush pen reset (tmux server_client_reset_state → tty_reset): SGR0
 * whenever the pen differs from default, so the client terminal is left in a
 * known state after every projection. */
struct yetty_ycore_void_result yetty_ymux_tty_pen_reset(struct yetty_ymux_tty *tty,
                                                        struct yetty_ycore_buffer *out)
{
    /* End-of-flush closes an open hyperlink BEFORE the sgr0 (tmux order:
     * \e]8;;\e\\ then \e(B\e[m). OSC-8 survives sgr0, so this is the
     * only implicit close point. */
    if (tty->active_link[0]) {
        struct yetty_ycore_void_result close_res = tty_puts(out, "\x1b]8;;\x1b\\");
        if (YETTY_IS_ERR(close_res)) {
            return close_res;
        }
        tty->active_link[0] = 0;
        tty->active_link_id = 0;
    }
    if (tty->cell_attr == 0 && tty->cell_fg == YMUX_TTY_COLOR_DEFAULT &&
        tty->cell_bg == YMUX_TTY_COLOR_DEFAULT) {
        tty->last_attr = 0;
        tty->last_fg = YMUX_TTY_COLOR_DEFAULT;
        tty->last_bg = YMUX_TTY_COLOR_DEFAULT;
        return YETTY_OK_VOID();
    }
    return tty_reset(tty, out);
}

struct yetty_ycore_void_result yetty_ymux_tty_attributes(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out,
                                                         uint16_t attr, int fg, int bg)
{
    return yetty_ymux_tty_attributes_exotic(tty, out, attr, fg, bg, NULL, NULL, 0);
}

/* Normalise an underline-colour SGR-58 token to tmux's emitted form for
 * byte-parity: RGB as `58:2:R:G:B` (the empty colorspace-id sub-parameter that
 * an app may send — `58:2::R:G:B` — is dropped), palette as `58:5:N`, matching
 * how tmux re-emits the stored colour regardless of the spelling received. The
 * first numeric sub-parameter is the SGR code (58), the second the type (2 or
 * 5), and the remaining numerics are the components. Falls back to the verbatim
 * token for any shape it does not recognise. */
static size_t normalize_underline_colour(const char *token, char *out, size_t cap)
{
    int values[8];
    int value_count = 0;
    int type = -1;
    int numeric_index = 0;
    const char *cursor = token;
    while (*cursor) {
        char field[16];
        size_t field_len = 0;
        while (*cursor && *cursor != ':' && field_len + 1 < sizeof(field)) {
            field[field_len++] = *cursor++;
        }
        field[field_len] = '\0';
        if (field_len > 0) {
            int value = atoi(field);
            if (numeric_index == 1) {
                type = value; /* 2 = RGB, 5 = palette */
            } else if (numeric_index >= 2 &&
                       value_count < (int)(sizeof(values) / sizeof(values[0]))) {
                values[value_count++] = value;
            }
            ++numeric_index; /* index 0 is the 58; empty fields do not advance it */
        }
        if (*cursor == ':') {
            ++cursor;
        }
    }
    if (type == 2 && value_count >= 3) {
        return (size_t)snprintf(out, cap, "58:2:%d:%d:%d", values[value_count - 3],
                                values[value_count - 2], values[value_count - 1]);
    }
    if (type == 5 && value_count >= 1) {
        return (size_t)snprintf(out, cap, "58:5:%d", values[value_count - 1]);
    }
    return (size_t)snprintf(out, cap, "%s", token);
}

struct yetty_ycore_void_result yetty_ymux_tty_attributes_exotic(struct yetty_ymux_tty *tty,
                                                                struct yetty_ycore_buffer *out,
                                                                uint16_t attr, int fg, int bg,
                                                                const char *underline_colour,
                                                                const char *link, uint32_t link_id)
{
    struct yetty_ycore_void_result res = YETTY_OK_VOID();
    int colour_changed =
        (underline_colour ? strcmp(tty->active_underline_colour, underline_colour) != 0
                          : tty->active_underline_colour[0] != 0);
    /* Change detection keys on the EXTERNAL id — two anonymous same-URI links
     * differ by id and must re-open. link_id 0 = no link. */
    uint32_t want_link_id = link ? link_id : 0;
    int link_changed = (want_link_id != tty->active_link_id);
    /* Same as the last requested cell — no output (tmux last_cell early-out). */
    if (attr == tty->last_attr && fg == tty->last_fg && bg == tty->last_bg && !colour_changed &&
        !link_changed) {
        return res;
    }
    /* If any active attribute bit is being cleared, reset everything (sgr0). */
    if (tty->cell_attr & (uint16_t)~attr) {
        res = tty_reset(tty, out);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    /* Colours (may add to, never remove, the attributes). */
    res = tty_colours(tty, out, fg, bg);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    /* Only the newly-set attribute bits, in tmux's order. */
    uint16_t changed = (uint16_t)(attr & ~tty->cell_attr);
    tty->cell_attr = attr;
    static const struct {
        uint16_t bit;
        enum yetty_ymux_tty_term_slot slot;
        const char *sgr;
    } order[] = {
        {YMUX_TTY_ATTR_BOLD, YMUX_TTY_TERM_BOLD, "\x1b[1m"},
        {YMUX_TTY_ATTR_DIM, YMUX_TTY_TERM_DIM, "\x1b[2m"},
        {YMUX_TTY_ATTR_ITALICS, YMUX_TTY_TERM_SITM, "\x1b[3m"},
        {YMUX_TTY_ATTR_UNDERLINE, YMUX_TTY_TERM_SMUL, "\x1b[4m"},
        {YMUX_TTY_ATTR_BLINK, YMUX_TTY_TERM_BLINK, "\x1b[5m"},
        {YMUX_TTY_ATTR_REVERSE, YMUX_TTY_TERM_REV, "\x1b[7m"},
        {YMUX_TTY_ATTR_HIDDEN, YMUX_TTY_TERM_INVIS, "\x1b[8m"},
        {YMUX_TTY_ATTR_STRIKE, YMUX_TTY_TERM_SMXX, "\x1b[9m"},
        {YMUX_TTY_ATTR_OVERLINE, YMUX_TTY_TERM_SMOL, "\x1b[53m"},
    };
    for (size_t index = 0; index < sizeof(order) / sizeof(order[0]); ++index) {
        if (changed & order[index].bit) {
            res = tty_put_cap(tty, out, order[index].slot, order[index].sgr);
            if (YETTY_IS_ERR(res)) {
                return res;
            }
        }
    }
    /* Underline COLOUR (Setulc profiles) is emitted BEFORE the extended style,
     * in tmux's normalised form (\e[58:2:R:G:B m / \e[58:5:N m) — tmux orders the
     * colour ahead of the `4:N` style and drops the empty colorspace-id field an
     * app may have sent (cycle-25 byte-parity). */
    if (underline_colour && strcmp(tty->active_underline_colour, underline_colour) != 0) {
        char normalized[48];
        normalize_underline_colour(underline_colour, normalized, sizeof(normalized));
        char colour_sgr[64];
        snprintf(colour_sgr, sizeof(colour_sgr), "\x1b[%sm", normalized);
        res = tty_puts(out, colour_sgr);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        snprintf(tty->active_underline_colour, sizeof(tty->active_underline_colour), "%s",
                 underline_colour);
    }
    /* Extended underline STYLE (Smulx profiles): \e[4:Nm for styles 2..5 —
     * emitted when the style bits newly appear (a reset cleared them). */
    uint16_t style = (uint16_t)((attr >> YMUX_TTY_ATTR_STYLE_SHIFT) & 0x7u);
    uint16_t previous_style = (uint16_t)((changed >> YMUX_TTY_ATTR_STYLE_SHIFT) & 0x7u);
    if (style >= 2 && previous_style != 0) {
        int written = 0;
        res = tty_emit_cap(tty, out, YMUX_TTY_TERM_SMULX, (long)style, 0, 1, &written);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        if (!written) {
            char style_sgr[16];
            snprintf(style_sgr, sizeof(style_sgr), "\x1b[4:%um", style);
            res = tty_puts(out, style_sgr);
            if (YETTY_IS_ERR(res)) {
                return res;
            }
        }
    }
    /* Hyperlink (OSC-8 profiles): persists across sgr0; closed explicitly. The
     * id is the engine's EXTERNAL hyperlink id (tmux<N>), printed in UPPERCASE
     * hex exactly as tmux emits it — the emitter no longer re-interns URIs, so
     * two anonymous links to the same URI keep their distinct ids. */
    if (link_changed) {
        if (link && link_id != 0) {
            char open_osc[1100];
            snprintf(open_osc, sizeof(open_osc), "\x1b]8;id=tmux%X;%s\x1b\\", link_id, link);
            res = tty_puts(out, open_osc);
            if (YETTY_IS_ERR(res)) {
                return res;
            }
            snprintf(tty->active_link, sizeof(tty->active_link), "%s", link);
            tty->active_link_id = link_id;
        } else {
            res = tty_puts(out, "\x1b]8;;\x1b\\");
            if (YETTY_IS_ERR(res)) {
                return res;
            }
            tty->active_link[0] = 0;
            tty->active_link_id = 0;
        }
    }
    tty->last_attr = attr;
    tty->last_fg = fg;
    tty->last_bg = bg;
    return res;
}

struct yetty_ycore_void_result yetty_ymux_tty_cursor_visible(struct yetty_ymux_tty *tty,
                                                             struct yetty_ycore_buffer *out,
                                                             int visible)
{
    visible = visible ? 1 : 0;
    if (tty->cursor_visible == visible) {
        return YETTY_OK_VOID();
    }
    tty->cursor_visible = visible;
    return tty_puts(out, visible ? "\x1b[?12l\x1b[?25h" : "\x1b[?25l");
}

struct yetty_ycore_void_result yetty_ymux_tty_cursor_shape(struct yetty_ymux_tty *tty,
                                                           struct yetty_ycore_buffer *out,
                                                           int shape, int blink)
{
    /* VTERM_PROP_CURSORSHAPE_*: 1 block, 2 underline, 3 bar. DECSCUSR groups
     * them in pairs (blink = odd, steady = even): 1/2 block, 3/4 underline,
     * 5/6 bar. An unknown/zero shape defaults to a steady block. */
    if (shape < 1 || shape > 3) {
        shape = 1;
    }
    int param = (shape - 1) * 2 + (blink ? 1 : 2);
    if (tty->cursor_shape_param == param) {
        return YETTY_OK_VOID();
    }
    tty->cursor_shape_param = param;
    /* tmux syncs the cursor visibility/blink modes BEFORE Ss (its
     * tty_update_mode runs first in reset_state): emit the cnorm pair when
     * the cursor is currently shown. Net visibility unchanged. */
    if (tty->cursor_visible == 1) {
        struct yetty_ycore_void_result sync_res =
            yetty_ycore_buffer_write(out, "\x1b[?12l\x1b[?25h", 12);
        if (YETTY_IS_ERR(sync_res)) {
            return sync_res;
        }
    }
    char decscusr[16];
    int len = snprintf(decscusr, sizeof(decscusr), "\x1b[%d q", param);
    if (len < 0 || (size_t)len >= sizeof(decscusr)) {
        return YETTY_ERR(yetty_ycore_void, "ymux tty_cursor_shape: format");
    }
    return yetty_ycore_buffer_write(out, decscusr, (size_t)len);
}

struct yetty_ycore_void_result yetty_ymux_tty_putn(struct yetty_ymux_tty *tty,
                                                   struct yetty_ycore_buffer *out,
                                                   const void *bytes, size_t len, uint32_t width)
{
    struct yetty_ycore_void_result res = yetty_ycore_buffer_write(out, bytes, len);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    if (tty->cx + width > tty->sx) {
        tty->cx = (tty->cx + width) - tty->sx;
        if (tty->cx <= tty->sx) {
            tty->cy++;
        } else {
            tty->cx = UINT32_MAX;
            tty->cy = UINT32_MAX;
        }
    } else {
        tty->cx += width; /* fills last column -> cx == sx (deferred autowrap park) */
    }
    return res;
}

struct yetty_ycore_void_result yetty_ymux_tty_clear_line(struct yetty_ymux_tty *tty,
                                                         struct yetty_ycore_buffer *out)
{
    return tty_put_cap(tty, out, YMUX_TTY_TERM_EL, "\x1b[K"); /* el */
}

struct yetty_ycore_void_result yetty_ymux_tty_clear_chars(struct yetty_ymux_tty *tty,
                                                          struct yetty_ycore_buffer *out,
                                                          uint32_t count)
{
    (void)tty;
    if (count == 0) {
        return YETTY_OK_VOID();
    }
    return tty_csi1_cap(tty, out, YMUX_TTY_TERM_ECH, (long)count, count, 'X'); /* ech */
}

/* tmux colour_to_6cube: map a channel value to its 6-cube index (0..5). */
static int rgb_to_6cube(int value)
{
    if (value < 48) {
        return 0;
    }
    if (value < 114) {
        return 1;
    }
    return (value - 35) / 40;
}

static int rgb_dist_sq(int r0, int g0, int b0, int r1, int g1, int b1)
{
    return (r0 - r1) * (r0 - r1) + (g0 - g1) * (g0 - g1) + (b0 - b1) * (b0 - b1);
}

int yetty_ymux_rgb_to_256(uint8_t red, uint8_t green, uint8_t blue)
{
    static const int q2c[6] = {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};
    int r = red, g = green, b = blue;
    int qr = rgb_to_6cube(r), cr = q2c[qr];
    int qg = rgb_to_6cube(g), cg = q2c[qg];
    int qb = rgb_to_6cube(b), cb = q2c[qb];
    /* Exact 6x6x6 cube hit. */
    if (cr == r && cg == g && cb == b) {
        return 16 + (36 * qr) + (6 * qg) + qb;
    }
    /* Closest grey. */
    int grey_avg = (r + g + b) / 3;
    int grey_idx = grey_avg > 238 ? 23 : (grey_avg - 3) / 10;
    int grey = 8 + (10 * grey_idx);
    /* Grey or cube — whichever is nearer. */
    int cube_dist = rgb_dist_sq(cr, cg, cb, r, g, b);
    if (rgb_dist_sq(grey, grey, grey, r, g, b) < cube_dist) {
        return 232 + grey_idx;
    }
    return 16 + (36 * qr) + (6 * qg) + qb;
}

void yetty_ymux_build_palette256(const uint32_t base16[16], uint32_t palette[256])
{
    /* Mirror the ymux engine's libvterm palette EXACTLY (its pen.c ramp6/ramp24)
     * so the reverse-map recovers the index the engine resolved a 256-colour to
     * — an indexed colour then re-emits as setaf/setab (matching tmux), not
     * truecolour. NOTE: libvterm's ramps DIFFER from the standard xterm 6x6x6 +
     * 24-grey ramps; the reverse-map must match the producing engine, not xterm
     * (a mismatch mis-emits e.g. index 208 as \e[38;2;255;102;0m). */
    static const int ramp6[6] = {0x00, 0x33, 0x66, 0x99, 0xcc, 0xff};
    static const int ramp24[24] = {0x00, 0x0b, 0x16, 0x21, 0x2c, 0x37, 0x42, 0x4d,
                                   0x58, 0x63, 0x6e, 0x79, 0x85, 0x90, 0x9b, 0xa6,
                                   0xb1, 0xbc, 0xc7, 0xd2, 0xdd, 0xe8, 0xf3, 0xff};
    for (int index = 0; index < 16; ++index) {
        palette[index] = base16[index];
    }
    for (int index = 16; index < 232; ++index) {
        int cube = index - 16;
        int r = ramp6[(cube / 36) % 6], g = ramp6[(cube / 6) % 6], b = ramp6[cube % 6];
        palette[index] =
            0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; /* 0xAABBGGRR */
    }
    for (int index = 232; index < 256; ++index) {
        int value = ramp24[index - 232];
        palette[index] =
            0xFF000000u | ((uint32_t)value << 16) | ((uint32_t)value << 8) | (uint32_t)value;
    }
}

int yetty_ymux_rgb_to_tty_color(uint32_t rgb, uint32_t default_rgb, const uint32_t *palette,
                                uint32_t palette_count)
{
    if ((rgb & 0x00FFFFFFu) == (default_rgb & 0x00FFFFFFu)) {
        return YMUX_TTY_COLOR_DEFAULT;
    }
    if (palette) {
        for (uint32_t index = 0; index < palette_count && index <= 255u; ++index) {
            if ((palette[index] & 0x00FFFFFFu) == (rgb & 0x00FFFFFFu)) {
                return (int)index; /* indexed colour -> setaf(index), as tmux emits */
            }
        }
    }
    /* Non-palette colour: truecolour on this profile. 0xAABBGGRR -> r=low byte. */
    return YMUX_TTY_COLOR_RGB(rgb & 0xFFu, (rgb >> 8) & 0xFFu, (rgb >> 16) & 0xFFu);
}

/* A cell equal to tmux's grid_default_cell: a single space, no attributes,
 * default colours — the run the emitter clears with EL rather than writing. */
static int cell_is_default_blank(const struct yetty_ymux_tty_cell *cell)
{
    return cell->width == 1 && cell->len == 1 && cell->text && cell->text[0] == ' ' &&
           cell->attr == 0 && cell->fg == YMUX_TTY_COLOR_DEFAULT &&
           cell->bg == YMUX_TTY_COLOR_DEFAULT;
}

struct yetty_ycore_void_result yetty_ymux_tty_draw_line(struct yetty_ymux_tty *tty,
                                                        struct yetty_ycore_buffer *out,
                                                        const struct yetty_ymux_tty_cell *cells,
                                                        uint32_t count, uint32_t py)
{
    /* Trailing default-blank cells are cleared with EL, not written — but tmux
     * folds a blank into the EL run ONLY when its bg matches the PRECEDING cell's
     * bg (tty_draw_line_get_empty: gc->bg == last->bg). A default-blank has default
     * bg, so the FIRST blank after a non-default-bg cell must be WRITTEN as a space
     * (committing the bg change) before the same-bg remainder is EL-cleared. */
    uint32_t drawn = count;
    while (drawn > 0 && cell_is_default_blank(&cells[drawn - 1])) {
        int prev_bg = (drawn >= 2) ? cells[drawn - 2].bg : YMUX_TTY_COLOR_DEFAULT;
        if (prev_bg != YMUX_TTY_COLOR_DEFAULT) {
            break;
        }
        --drawn;
    }
    /* tmux draws a line as attribute-runs, emitting each run as tty_attributes
     * THEN tty_cursor THEN tty_putn — so the FIRST run's SGR precedes the cursor
     * move (\e[31m\e[H…, not \e[H\e[31m…). Emit the first drawn cell's attributes
     * before positioning to match byte-for-byte; a no-op when the pen already
     * equals it (e.g. a default-pen line), so unstyled redraws are unaffected. */
    struct yetty_ycore_void_result res = YETTY_OK_VOID();
    if (drawn > 0) {
        res = yetty_ymux_tty_attributes_exotic(tty, out, cells[0].attr, cells[0].fg, cells[0].bg,
                                               cells[0].underline_colour, cells[0].link,
                                               cells[0].link_id);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    res = yetty_ymux_tty_cursor(tty, out, 0, py);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    for (uint32_t index = 0; index < drawn; ++index) {
        const struct yetty_ymux_tty_cell *cell = &cells[index];
        res = yetty_ymux_tty_attributes_exotic(tty, out, cell->attr, cell->fg, cell->bg,
                                               cell->underline_colour, cell->link, cell->link_id);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        res = yetty_ymux_tty_putn(tty, out, cell->text, cell->len, cell->width);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    /* Clear the trailing blank run to end-of-line (BCE, default style). */
    if (drawn < count) {
        res =
            yetty_ymux_tty_attributes(tty, out, 0, YMUX_TTY_COLOR_DEFAULT, YMUX_TTY_COLOR_DEFAULT);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
        res = yetty_ymux_tty_clear_line(tty, out);
    }
    return res;
}
