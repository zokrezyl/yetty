/*
 * projector.c — per-attachment viewport projection: class@ymux:projector
 * (#695 phase 4, CPU contract).
 *
 * Converts (pane, attachment) into VT byte emissions: an idempotent FULL
 * repaint on first use, after a resync request, on viewport
 * geometry/anchor movement, or when the delta would not be smaller; an
 * op-driven incremental delta otherwise.
 * The projector owns a per-attachment SHADOW of the last projected
 * viewport — diffing happens here, per client, so the canonical pane needs
 * no per-attachment dirty plumbing and non-controlling viewports crop/pad
 * freely.
 *
 * Blank filler rows (viewport reaching past the live bottom or above the
 * floor) paint as default-colored blanks.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/rich.h>

#include <yetty/ytrace/ytrace.h>

#include "proto.h"
#include "rich-format.h"
#include "op-stream.h"
#include "tty-render.h"

/* Forward declaration for the same-module rich accessor (its generated header is
 * regenerated in the same codegen run; the redeclaration matches). */
struct yetty_ycore_uint64_result yetty_ymux_rich_creation_hash(struct yetty_yclass_object *obj,
                                                               uint64_t rich_id);

/* Forward declaration for the same-module engine cursor-style accessor (new
 * this change; its generated header is regenerated in the same codegen run). */
struct yetty_ycore_void_result yetty_ymux_engine_cursor_style(struct yetty_yclass_object *obj,
                                                              int *out_shape, int *out_blink);

/* Upper bound on figures tracked for the scroll-reposition fast path. Beyond
 * this the projector falls back to full frames (still correct, just re-sends
 * payloads on scroll) — a pane with >32 simultaneously-visible figures is far
 * outside normal use. */
enum { YMUX_PROJECTOR_RICH_MAX = 32 };
enum { YMUX_PROJECTOR_RESOURCE_MAX = 64 };

/* The projector — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:projector") yetty_ymux_projector {
    /* Borrowed: the session owns lifetimes (projector dies first). */
    struct yetty_yclass_object *pane;
    struct yetty_yclass_object *attachment;

    /* VT projection shadow — the cells last emitted as a VT redraw, kept
     * separate from the cell-format shadow above so project_vt can diff and
     * emit ONLY the rows that changed (a keystroke re-sends one row, not the
     * whole screen). Invalid → the next project_vt is a full redraw. */
    struct yetty_ymux_cell *vt_shadow_cells;
    uint32_t vt_shadow_rows;
    uint32_t vt_shadow_cols;
    int vt_shadow_valid;
    /* The VIEW the shadow was projected from (review #17): the op-delta
     * model describes the LIVE screen — an anchored (scrolled-back) view
     * changes via view_top, not ops. A view change forces a complete
     * redraw of the new view; while anchored the replay is disabled (the
     * shadow cell-diff covers residual changes). */
    uint64_t vt_shadow_view_top;
    int vt_shadow_following;
    /* The engine op sequence this attachment has replayed (#699.1): the delta
     * path renders FROM the recorded screen-write operations; eviction or an
     * INVALIDATE in the window forces the plain cell-diff fallback. */
    uint64_t vt_op_consumed;
    /* Set by an upward scroll in the current replay window: tmux's collect
     * flags the pane scrollbar on every SU, and the following redraw-needed
     * pass paints zero cells but emits the cursor wrap \e[?25l\e[?12l\e[?25h
     * — deterministic under the oracle's schedule, so byte parity emits it
     * too (raw comparison, no canonicalization). */
    int vt_pending_scrollbar_wrap;

    /* The assumed state of the client terminal — the validated tmux-parity
     * renderer (yetty_ymux_tty): cursor cache + pen/SGR cache + cursor
     * visibility, so a move/SGR/visibility is emitted ONLY when it changes and
     * the bytes are byte-identical to tmux (verified vs pinned d5afb67). The
     * default colours + full 256 palette resolve each cell's RGB back to tmux
     * colour intent (index -> setaf, non-palette -> truecolour). */
    struct yetty_ymux_tty vt_tty;
    /* Owned terminfo model behind vt_tty.term (see projector_set_terminal). */
    struct yetty_ymux_tty_term vt_term;
    uint32_t vt_default_fg;
    uint32_t vt_default_bg;
    uint32_t vt_palette[256];

    /* The client's advertised terminfo capability profile (YMUX_TERM_CAP_*),
     * set from the attach handshake. Decides colour emission: with truecolor a
     * non-palette RGB cell passes through as \e[38;2;R;G;Bm; without it the cell
     * is downgraded to the nearest 256 palette index. */
    uint32_t vt_capabilities;

    /* Attach preamble (review #17): emit tmux's attach-time setup before
     * the first full projection (modes, bracketed paste, theme query,
     * pen reset, region, home) — enabled by the ATTACH_PREAMBLE cap;
     * emitted exactly once. */
    int vt_attach_preamble;
    int vt_attach_preamble_sent;

    /* Deferred-wrap epilogue (review #16): after a bottom-right wrap+scroll
     * was deferred, the flush that emits the wrapped run appends tmux's
     * trailing EL (clearing the freshly-scrolled row's tail). */
    int vt_deferred_wrap_el;

    /* Per-attachment terminal-RESPONSE state (review #16/#17): raw bytes
     * the renderer's terminal endpoint produced in answer to the RENDERER's
     * own queries. A STREAMING state machine (no fixed stitch window): the
     * current sequence accumulates in a growable buffer, so arbitrarily
     * fragmented and arbitrarily large DCS/OSC responses parse losslessly.
     * The renderer-side analog of tmux's tty response handling — never
     * keyboard input, never the application PTY. */
    enum {
        YMUX_RESPONSE_GROUND = 0,
        YMUX_RESPONSE_ESC,
        YMUX_RESPONSE_CSI,
        YMUX_RESPONSE_DCS,
        YMUX_RESPONSE_DCS_ESC, /* saw ESC inside DCS — ST candidate */
        YMUX_RESPONSE_OSC,
        YMUX_RESPONSE_OSC_ESC
    } response_state;
    uint8_t *response_seq; /* the current sequence body (growable) */
    uint32_t response_seq_len;
    uint32_t response_seq_cap;
    uint32_t response_da_count;     /* primary DA replies (\e[?...c) */
    uint32_t response_da2_count;    /* secondary DA replies (\e[>...c) */
    uint32_t response_cpr_count;    /* cursor-position reports (\e[r;cR) */
    uint32_t response_decrpm_count; /* mode reports (\e[?...$y) */
    uint32_t response_dcs_count;    /* DCS replies (XTGETTCAP / DECRQSS) */
    uint32_t response_osc_count;    /* OSC replies (colors etc.) */
    uint32_t response_other_count;  /* complete but unrecognized */
    /* Client THEME (review #19): the ?997;N n report's recorded scheme —
     * 0 none seen / no preference, 1 dark, 2 light. Response-DRIVEN state,
     * not telemetry. */
    int response_theme_scheme;
    uint32_t response_theme_count;
    uint32_t response_last_cpr_row; /* 1-based, from the last CPR */
    uint32_t response_last_cpr_col;
    char response_last_da[64];  /* last primary DA payload, verbatim */
    char response_last_da2[64]; /* last secondary DA payload, verbatim */
    /* XTGETTCAP capability table: decoded hex name=value pairs. LOSSLESS
     * (review #19): growable entries with owned exact-length strings —
     * no fixed name/value truncation, no fixed entry cap. */
    struct yetty_ymux_response_cap {
        char *name;  /* owned */
        char *value; /* owned */
    } *response_caps;
    uint32_t response_caps_count;
    uint32_t response_caps_capacity;
    char response_last_osc[64]; /* last OSC reply payload, verbatim */

    /* Cached copies of the engine's exotic value tables (refreshed each
     * projection) — the tty cells reference these strings. */
    char vt_exotic_colours[256][40];
    /* Per-frame hyperlink cache keyed by EXTERNAL id (tmux<N>): the store now
     * uses tmux's per-session ids, so a flat 256-slot table no longer indexes
     * it. Filled lazily as cells are drawn; reset each projection. */
    struct vt_link_cache_entry {
        uint32_t id;
        char uri[1025]; /* MAX_HYPERLINK_URI + NUL */
    } *vt_links;
    size_t vt_links_count;
    size_t vt_links_cap;
    struct yetty_yclass_object *vt_frame_engine; /* transient: this projection's engine */

    /* Rich half: the store revision last emitted (rich_emitted 0 = never;
     * the first call always emits — clients start from nothing). shadow_rich_
     * view_top + the visible-id set let a pure SCROLL (view_top moved, content
     * and visible set unchanged) emit a cheap position-only reposition frame
     * instead of re-transmitting every figure's payload. */
    uint64_t shadow_rich_revision;
    uint64_t shadow_rich_view_top;
    uint64_t shadow_rich_visible_ids[YMUX_PROJECTOR_RICH_MAX];
    uint32_t shadow_rich_visible_count;
    int rich_emitted;

    /* Content-addressed heavy-payload delivery (#695): the creation-payload
     * hashes this attachment's client has already received + cached. A full
     * rich frame REFERENCES an already-delivered payload by hash instead of
     * re-sending it. Reset on invalidate (the reconnected client's cache is
     * empty). Bounded — when full the projector just re-sends (graceful). */
    uint64_t rich_delivered_hashes[YMUX_PROJECTOR_RESOURCE_MAX];
    uint32_t rich_delivered_count;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_projector_class_get(void);
struct yetty_ymux_projector_ptr_result yetty_ymux_projector_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_projector_ptr, struct yetty_ymux_projector *);

/*===========================================================================
 * Paint writers.
 *=========================================================================*/

static struct yetty_ycore_void_result paint_put(struct yetty_ycore_buffer *out, uint32_t word)
{
    return yetty_ycore_buffer_write(out, &word, sizeof(word));
}

static int paint_cells_equal(const struct yetty_ymux_cell *left,
                             const struct yetty_ymux_cell *right)
{
    if (left->codepoint != right->codepoint || left->fg != right->fg || left->bg != right->bg ||
        left->attrs != right->attrs || left->width != right->width ||
        left->mark_count != right->mark_count) {
        return 0;
    }
    for (uint8_t mark = 0; mark < left->mark_count; ++mark) {
        if (left->marks[mark] != right->marks[mark]) {
            return 0;
        }
    }
    return 1;
}

/*===========================================================================
 * Viewport gather: resolve the attachment's viewport into the shadow-shaped
 * cell rectangle (crop/pad to the attachment's view size).
 *=========================================================================*/

static void paint_blank_cell(struct yetty_ymux_cell *cell)
{
    memset(cell, 0, sizeof(*cell));
    cell->width = 1;
}

static struct yetty_ycore_void_result projector_gather(struct yetty_ymux_projector *projector,
                                                       struct yetty_ymux_cell *cells,
                                                       uint8_t *continuations, uint32_t view_rows,
                                                       uint32_t view_cols, uint64_t *out_view_top)
{
    struct yetty_ycore_uint64_result view_top_res =
        yetty_ymux_attachment_view_top(projector->attachment);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_top_res, "ymux projector: view top");
    uint64_t view_top = view_top_res.value;
    if (out_view_top) {
        *out_view_top = view_top;
    }
    for (uint32_t row = 0; row < view_rows; ++row) {
        struct yetty_ymux_cell *row_cells = cells + (size_t)row * view_cols;
        continuations[row] = 0;
        struct yetty_ymux_history_row_result row_res =
            yetty_ymux_pane_resolve_row(projector->pane, view_top + row);
        if (YETTY_IS_ERR(row_res)) {
            /* Past the live bottom (short pane, or viewport taller than the
             * pane): blank filler. */
            yetty_ycore_error_destroy(row_res.error);
            for (uint32_t col = 0; col < view_cols; ++col) {
                paint_blank_cell(&row_cells[col]);
            }
            continue;
        }
        uint32_t copy_cols = row_res.value.cols < view_cols ? row_res.value.cols : view_cols;
        memcpy(row_cells, row_res.value.cells, (size_t)copy_cols * sizeof(struct yetty_ymux_cell));
        for (uint32_t col = copy_cols; col < view_cols; ++col) {
            paint_blank_cell(&row_cells[col]); /* pad narrow canonical rows */
        }
        continuations[row] = row_res.value.continuation ? 1 : 0;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_projector_make(
    struct yetty_yclass_object *pane, struct yetty_yclass_object *attachment)
{
    if (!pane || !attachment) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux projector_make: invalid arguments");
    }
    struct yetty_yclass_ptr_result class_res = yetty_ymux_projector_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux projector_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux projector_make: alloc");
    struct yetty_ymux_projector_ptr_result projector_res =
        yetty_ymux_projector_from(object_res.value);
    if (YETTY_IS_ERR(projector_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux projector_make: from_obj", projector_res);
    }
    projector_res.value->pane = pane;
    projector_res.value->attachment = attachment;
    /* Default to truecolor until the attach handshake says otherwise: a direct
     * project_vt caller (and the common yscene client) renders RGB SGR. The
     * capability TABLE must be valid from birth — the colour path reads it
     * before the first full redraw runs tty_init. */
    projector_res.value->vt_capabilities = YMUX_TERM_CAP_TRUECOLOR;
    projector_res.value->vt_tty.caps = yetty_ymux_tty_caps_xterm_256color();
    /* Resolve the DEFAULT terminfo model at birth so production emission is
     * capability-string driven from the first frame — not only after an
     * attach carrying a TERM. set_terminal replaces it when the client names
     * a terminal. The tty borrows it; every tty_init re-points via
     * projector_reattach_term (below). */
    {
        /* Resolve into a temporary and swap only on success. On failure FREE
         * the partial model and leave term NULL — the explicitly-selected safe
         * fallback (NULL term → the legacy xterm literal emitters, which match
         * the default caps). No partial/stale model is ever pointed at
         * (cycle-24 P1). */
        struct yetty_ymux_tty_term default_model;
        memset(&default_model, 0, sizeof(default_model));
        struct yetty_ycore_void_result term_res =
            yetty_ymux_tty_term_resolve(&default_model, "xterm-256color", "256,RGB");
        if (YETTY_IS_ERR(term_res)) {
            yetty_ycore_error_destroy(term_res.error);
            yetty_ymux_tty_term_free(&default_model);
            projector_res.value->vt_tty.term = NULL;
        } else {
            projector_res.value->vt_term = default_model;
            projector_res.value->vt_tty.term = &projector_res.value->vt_term;
        }
    }
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

/* Re-point the tty at the projector's owned terminfo model after a tty_init
 * (which zeroes tty->term). The model is CONNECTION state, not screen state —
 * it must survive every attach-preamble reset and full redraw, or production
 * silently falls back to hard-coded ANSI (cycle-22 P0). Only re-points when a
 * model is actually resolved (a cup string is the sentinel). */
static void projector_reattach_term(struct yetty_ymux_projector *projector)
{
    if (yetty_ymux_tty_term_has(&projector->vt_term, YMUX_TTY_TERM_CUP)) {
        projector->vt_tty.term = &projector->vt_term;
    }
}

/* Streaming response feeders (review #17). The sequence body accumulates
 * without a fixed window; classification happens at the terminator. */
static int response_seq_append(struct yetty_ymux_projector *projector, uint8_t byte)
{
    enum { RESPONSE_SEQ_MAX = 65536 };
    if (projector->response_seq_len >= RESPONSE_SEQ_MAX) {
        return 0; /* runaway sequence — drop at the classifier */
    }
    if (projector->response_seq_len == projector->response_seq_cap) {
        uint32_t new_cap = projector->response_seq_cap ? projector->response_seq_cap * 2 : 128;
        uint8_t *grown = realloc(projector->response_seq, new_cap);
        if (!grown) {
            return 0;
        }
        projector->response_seq = grown;
        projector->response_seq_cap = new_cap;
    }
    projector->response_seq[projector->response_seq_len++] = byte;
    return 1;
}

static void response_copy_string(char *destination, size_t destination_size, const uint8_t *bytes,
                                 uint32_t byte_count)
{
    uint32_t copy_len =
        byte_count < destination_size - 1 ? byte_count : (uint32_t)destination_size - 1;
    memcpy(destination, bytes, copy_len);
    destination[copy_len] = 0;
}

static int response_hex_nibble(uint8_t hex)
{
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    }
    if (hex >= 'a' && hex <= 'f') {
        return hex - 'a' + 10;
    }
    if (hex >= 'A' && hex <= 'F') {
        return hex - 'A' + 10;
    }
    return -1;
}

/* Decode one hex-encoded XTGETTCAP token span into `out` (NUL-terminated). */
static void response_hex_decode(char *out, size_t out_size, const uint8_t *hex, uint32_t hex_len)
{
    size_t out_len = 0;
    for (uint32_t index = 0; index + 1 < hex_len && out_len + 1 < out_size; index += 2) {
        int high = response_hex_nibble(hex[index]);
        int low = response_hex_nibble(hex[index + 1]);
        if (high < 0 || low < 0) {
            break;
        }
        out[out_len++] = (char)((high << 4) | low);
    }
    out[out_len] = 0;
}

/* Recorded client theme from the ?997;N n report (review #19) — 0 none
 * seen, 1 dark, 2 light. Hand-written, module-internal (tests and
 * theme-aware consumers read the response-driven state). */
int yetty_ymux_projector_theme_scheme(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    if (YETTY_IS_ERR(projector_res)) {
        yetty_ycore_error_destroy(projector_res.error);
        return 0;
    }
    return projector_res.value->response_theme_scheme;
}

/* Classify a COMPLETE CSI response (body excludes ESC[ and the final). */
static void response_classify_csi(struct yetty_ymux_projector *projector, uint8_t final_byte)
{
    const uint8_t *body = projector->response_seq;
    uint32_t body_len = projector->response_seq_len;
    if (final_byte == 'c' && body_len > 0 && body[0] == '?') {
        ++projector->response_da_count;
        response_copy_string(projector->response_last_da, sizeof(projector->response_last_da), body,
                             body_len);
        return;
    }
    if (final_byte == 'c' && body_len > 0 && body[0] == '>') {
        ++projector->response_da2_count;
        response_copy_string(projector->response_last_da2, sizeof(projector->response_last_da2),
                             body, body_len);
        return;
    }
    if (final_byte == 'R') {
        char numbers[32];
        response_copy_string(numbers, sizeof(numbers), body, body_len);
        unsigned int report_row = 0;
        unsigned int report_col = 0;
        if (sscanf(numbers, "%u;%u", &report_row, &report_col) == 2) {
            ++projector->response_cpr_count;
            projector->response_last_cpr_row = report_row;
            projector->response_last_cpr_col = report_col;
            return;
        }
    }
    if (final_byte == 'y' && body_len > 0 && body[body_len - 1] == '$') {
        ++projector->response_decrpm_count;
        /* Response-DRIVEN cap transition (review #19): a DECRPM report for
         * mode 2026 (synchronized output) toggles the SYNC capability on
         * this attachment's profile — set/reset (1/2) means recognized,
         * 0 means unsupported. */
        if (body_len >= 7 && body[0] == '?') {
            char numbers[24];
            response_copy_string(numbers, sizeof(numbers), body + 1, body_len - 2);
            unsigned int mode = 0, status = 0;
            if (sscanf(numbers, "%u;%u", &mode, &status) == 2 && mode == 2026) {
                if (status == 1 || status == 2) {
                    projector->vt_capabilities |= YMUX_TERM_CAP_SYNC;
                } else {
                    projector->vt_capabilities &= ~(uint32_t)YMUX_TERM_CAP_SYNC;
                }
            }
        }
        return;
    }
    if (final_byte == 'n' && body_len >= 5 && memcmp(body, "?997;", 5) == 0) {
        /* THEME report (review #19) — the ?996n query's answer, and mode
         * 2031's change notification. tmux records the client theme; this
         * DRIVES state (not telemetry): the recorded scheme survives on the
         * projector and is what a theme-aware consumer reads back. */
        char numbers[8];
        response_copy_string(numbers, sizeof(numbers), body + 5, body_len - 5);
        unsigned int scheme = 0;
        if (sscanf(numbers, "%u", &scheme) == 1 && scheme <= 2) {
            projector->response_theme_scheme = (int)scheme;
            ++projector->response_theme_count;
        }
        return;
    }
    ++projector->response_other_count;
}

/* Classify a COMPLETE DCS response (body excludes ESC P and the ST). */
static void response_classify_dcs(struct yetty_ymux_projector *projector)
{
    const uint8_t *body = projector->response_seq;
    uint32_t body_len = projector->response_seq_len;
    ++projector->response_dcs_count;
    if (body_len >= 3 && body[0] == '1' && body[1] == '+' && body[2] == 'r') {
        /* XTGETTCAP: 1+r hexname=hexvalue[;hexname=hexvalue...] — stored
         * LOSSLESSLY (review #19): exact-length owned strings, growable
         * entry table. */
        uint32_t offset = 3;
        while (offset < body_len) {
            uint32_t token_end = offset;
            while (token_end < body_len && body[token_end] != ';') {
                ++token_end;
            }
            uint32_t equals = offset;
            while (equals < token_end && body[equals] != '=') {
                ++equals;
            }
            if (equals > offset && equals < token_end) {
                if (projector->response_caps_count == projector->response_caps_capacity) {
                    uint32_t new_capacity = projector->response_caps_capacity
                                                ? projector->response_caps_capacity * 2
                                                : 8;
                    struct yetty_ymux_response_cap *grown =
                        realloc(projector->response_caps, new_capacity * sizeof(*grown));
                    if (!grown) {
                        return; /* keep what we have */
                    }
                    projector->response_caps = grown;
                    projector->response_caps_capacity = new_capacity;
                }
                uint32_t name_hex = equals - offset;
                uint32_t value_hex = token_end - equals - 1;
                char *name = malloc(name_hex / 2 + 1);
                char *value = malloc(value_hex / 2 + 1);
                if (!name || !value) {
                    free(name);
                    free(value);
                    return;
                }
                response_hex_decode(name, name_hex / 2 + 1, body + offset, name_hex);
                response_hex_decode(value, value_hex / 2 + 1, body + equals + 1, value_hex);
                uint32_t slot = projector->response_caps_count++;
                projector->response_caps[slot].name = name;
                projector->response_caps[slot].value = value;
            }
            offset = token_end + 1;
        }
    }
}

/* Consume RAW terminal-response bytes from this attachment's renderer
 * (review #16/#17): a STREAMING state machine — arbitrary fragmentation,
 * arbitrary length (growable body, bounded at 64 KB per sequence). Raw
 * bytes are never re-encoded; this is the response CONSUMER. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_consume_tty_response(
    struct yetty_yclass_object *obj, const uint8_t *bytes, uint32_t byte_count)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res, "ymux projector consume_tty_response");
    struct yetty_ymux_projector *projector = projector_res.value;
    for (uint32_t index = 0; index < byte_count; ++index) {
        uint8_t byte = bytes[index];
        switch (projector->response_state) {
        case YMUX_RESPONSE_GROUND:
            if (byte == 0x1b) {
                projector->response_state = YMUX_RESPONSE_ESC;
            }
            break;
        case YMUX_RESPONSE_ESC:
            projector->response_seq_len = 0;
            if (byte == '[') {
                projector->response_state = YMUX_RESPONSE_CSI;
            } else if (byte == 'P') {
                projector->response_state = YMUX_RESPONSE_DCS;
            } else if (byte == ']') {
                projector->response_state = YMUX_RESPONSE_OSC;
            } else {
                projector->response_state = YMUX_RESPONSE_GROUND;
            }
            break;
        case YMUX_RESPONSE_CSI:
            if (byte >= 0x40 && byte <= 0x7E) {
                response_classify_csi(projector, byte);
                projector->response_state = YMUX_RESPONSE_GROUND;
            } else {
                (void)response_seq_append(projector, byte);
            }
            break;
        case YMUX_RESPONSE_DCS:
            if (byte == 0x1b) {
                projector->response_state = YMUX_RESPONSE_DCS_ESC;
            } else {
                (void)response_seq_append(projector, byte);
            }
            break;
        case YMUX_RESPONSE_DCS_ESC:
            if (byte == '\\') {
                response_classify_dcs(projector);
                projector->response_state = YMUX_RESPONSE_GROUND;
            } else {
                /* Not an ST — the ESC belonged to the body. */
                (void)response_seq_append(projector, 0x1b);
                (void)response_seq_append(projector, byte);
                projector->response_state = YMUX_RESPONSE_DCS;
            }
            break;
        case YMUX_RESPONSE_OSC:
            if (byte == 0x07) {
                ++projector->response_osc_count;
                response_copy_string(projector->response_last_osc,
                                     sizeof(projector->response_last_osc), projector->response_seq,
                                     projector->response_seq_len);
                projector->response_state = YMUX_RESPONSE_GROUND;
            } else if (byte == 0x1b) {
                projector->response_state = YMUX_RESPONSE_OSC_ESC;
            } else {
                (void)response_seq_append(projector, byte);
            }
            break;
        case YMUX_RESPONSE_OSC_ESC:
            if (byte == '\\') {
                ++projector->response_osc_count;
                response_copy_string(projector->response_last_osc,
                                     sizeof(projector->response_last_osc), projector->response_seq,
                                     projector->response_seq_len);
            } else {
                (void)response_seq_append(projector, 0x1b);
                (void)response_seq_append(projector, byte);
                projector->response_state = YMUX_RESPONSE_OSC;
                break;
            }
            projector->response_state = YMUX_RESPONSE_GROUND;
            break;
        }
    }
    return YETTY_OK_VOID();
}

/* The decoded XTGETTCAP capability table: entry `index` copied out as
 * "name=value"; returns the entry count. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_projector_response_cap(struct yetty_yclass_object *obj,
                                                                   uint32_t cap_index,
                                                                   char *out_text,
                                                                   uint32_t out_capacity)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, projector_res, "ymux projector response_cap");
    struct yetty_ymux_projector *projector = projector_res.value;
    if (cap_index < projector->response_caps_count && out_text && out_capacity > 0 &&
        projector->response_caps) {
        snprintf(out_text, out_capacity, "%s=%s", projector->response_caps[cap_index].name,
                 projector->response_caps[cap_index].value);
    }
    return YETTY_OK(yetty_ycore_uint32, projector->response_caps_count);
}

/* Response-state observers: (da_count << 40) | (cpr_count << 16) | row<<8 | col
 * would be cramped — expose the counts and last CPR separately. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_projector_response_state(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, projector_res, "ymux projector response_state");
    struct yetty_ymux_projector *projector = projector_res.value;
    uint64_t packed = ((uint64_t)projector->response_da_count << 48) |
                      ((uint64_t)projector->response_cpr_count << 32) |
                      ((uint64_t)(projector->response_last_cpr_row & 0xFFFF) << 16) |
                      (uint64_t)(projector->response_last_cpr_col & 0xFFFF);
    return YETTY_OK(yetty_ycore_uint64, packed);
}

/* Record the attachment's terminfo capability profile (YMUX_TERM_CAP_*); the
 * VT colour path reads it to choose truecolor passthrough vs an RGB->256
 * downgrade. Set once from the attach handshake. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_set_capabilities(
    struct yetty_yclass_object *obj, uint32_t capabilities)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res,
                        "ymux projector_set_capabilities: from_obj");
    projector_res.value->vt_capabilities = capabilities;
    /* Translate the negotiated mask into the emitter's capability profile
     * (review #13): the default-constructed profile is xterm-256color; the
     * mask refines it. RGB rides the TRUECOLOR bit; the rest map 1:1. */
    struct yetty_ymux_tty_caps caps = yetty_ymux_tty_caps_xterm_256color();
    caps.colors_rgb = (capabilities & YMUX_TERM_CAP_TRUECOLOR) ? 1 : 0;
    caps.ech = (capabilities & YMUX_TERM_CAP_ECH) ? 1 : 0;
    caps.insert_delete_line = (capabilities & YMUX_TERM_CAP_ILDL) ? 1 : 0;
    caps.insert_line = caps.insert_delete_line; /* wire mask carries the pair */
    caps.delete_line = caps.insert_delete_line;
    caps.decstbm = (capabilities & YMUX_TERM_CAP_DECSTBM) ? 1 : 0;
    caps.bce = (capabilities & YMUX_TERM_CAP_BCE) ? 1 : 0;
    caps.extended_underline = (capabilities & YMUX_TERM_CAP_EXTENDED_UNDERLINE) ? 1 : 0;
    caps.underline_colour = (capabilities & YMUX_TERM_CAP_UNDERLINE_COLOUR) ? 1 : 0;
    caps.hyperlink = (capabilities & YMUX_TERM_CAP_HYPERLINK) ? 1 : 0;
    caps.sync = (capabilities & YMUX_TERM_CAP_SYNC) ? 1 : 0;
    caps.margins = (capabilities & YMUX_TERM_CAP_MARGINS) ? 1 : 0;
    projector_res.value->vt_attach_preamble =
        (capabilities & YMUX_TERM_CAP_ATTACH_PREAMBLE) ? 1 : 0;
    projector_res.value->vt_tty.caps = caps;
    return YETTY_OK_VOID();
}

/* tmux's terminfo/features STATE MODEL entry (review #17 item 8): resolve
 * the client's TERM name + features string through the tty-features
 * pipeline (family base -> TERM-implied defaults -> explicit additions)
 * instead of a pre-chewed capability bitmask. The bitmask path above stays
 * as the legacy/override input; this is what an attach carrying TERM
 * strings uses. The ATTACH_PREAMBLE toggle stays with the mask path (a
 * client-mode choice, not a terminfo property). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_set_terminal(struct yetty_yclass_object *obj,
                                                                 const char *term_name,
                                                                 const char *features)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res, "ymux projector_set_terminal: from_obj");
    struct yetty_ymux_tty_caps caps = yetty_ymux_tty_caps_resolve(term_name, features);
    struct yetty_ymux_projector *projector = projector_res.value;

    /* Resolve the capability MODEL TRANSACTIONALLY: build into a temporary and
     * swap into the live slot only on success, so a failed resolution never
     * leaves vt_tty.term pointing at a half-built model (cycle-24 P1). */
    struct yetty_ymux_tty_term new_model;
    memset(&new_model, 0, sizeof(new_model));
    struct yetty_ycore_void_result term_model_res =
        yetty_ymux_tty_term_resolve(&new_model, term_name, features);
    if (YETTY_IS_ERR(term_model_res)) {
        yetty_ymux_tty_term_free(&new_model);
        return YETTY_ERR(yetty_ycore_void,
                         "ymux projector_set_terminal: capability model resolution failed",
                         term_model_res);
    }
    yetty_ymux_tty_term_free(&projector->vt_term);
    projector->vt_term = new_model; /* takes ownership of the string table */
    projector->vt_tty.term = &projector->vt_term;

    /* SINGLE AUTHORITY (cycle-24 P0): every string-capability STRATEGY boolean
     * is derived from the resolved MODEL's capability PRESENCE — the family
     * cancellations are already folded into the model by term_resolve, so a
     * synthetic family, a real terminfo entry that omits a cap, a `cap@`
     * cancellation, and a `cap=` addition ALL flow through one place, and the
     * strategy can never disagree with the emitted bytes. IL and DL resolve
     * INDEPENDENTLY. Non-string flags (bce, colours, hyperlink, mouse, …) have
     * no terminfo string slot and keep their caps_resolve value. */
    struct yetty_ymux_tty_term *model = &projector->vt_term;
    caps.decstbm = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_CSR);
    caps.ech = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_ECH);
    caps.insert_line = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_IL);
    caps.delete_line = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_DL);
    caps.insert_delete_line = caps.insert_line && caps.delete_line;
    caps.ich = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_ICH);
    caps.dch = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_DCH);
    caps.colors_rgb = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SETRGBF) &&
                      yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SETRGBB);
    caps.extended_underline = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SMULX);
    caps.underline_colour = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SETULC);
    caps.strikethrough = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SMXX);
    caps.overline = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SMOL);
    caps.sync = yetty_ymux_tty_term_has(model, YMUX_TTY_TERM_SYNC);
    /* BCE is a terminfo BOOLEAN, not a string slot: when the model parsed the
     * boolean section, it is the authority (so bce@ actually cancels back-color
     * erase and the projector falls back to painting spaces); otherwise keep the
     * family caps_resolve guess. */
    if (model->bools_loaded) {
        caps.bce = yetty_ymux_tty_term_bool(model, YMUX_TTY_TERM_BOOL_BCE);
        /* tmux TERM_NOAM: a terminal without automatic right margins (am
         * absent) cannot safely take the bottom-right glyph — the emitter
         * truncates a run at the last row so that cell stays unwritten. xenl is
         * derived for model completeness only; tmux never branches rendering on
         * it (the bottom-right behaviour is derived from am alone). */
        caps.noam = !yetty_ymux_tty_term_bool(model, YMUX_TTY_TERM_BOOL_AM);
        caps.xenl = yetty_ymux_tty_term_bool(model, YMUX_TTY_TERM_BOOL_XENL) != 0;
    }
    projector->vt_tty.caps = caps;

    /* Mirror the FINAL resolved verdicts into the mask so mask readers
     * (feature gates, tests) observe the SAME negotiated state the renderer
     * strategy uses — both are the model-derived `caps` (cycle-23/24 P1). */
    uint32_t mask = projector->vt_capabilities &
                    (uint32_t)(YMUX_TERM_CAP_VT_TEXT | YMUX_TERM_CAP_ATTACH_PREAMBLE);
    if (caps.colors_rgb) {
        mask |= YMUX_TERM_CAP_TRUECOLOR;
    }
    if (caps.ech) {
        mask |= YMUX_TERM_CAP_ECH;
    }
    if (caps.insert_delete_line) {
        mask |= YMUX_TERM_CAP_ILDL;
    }
    if (caps.decstbm) {
        mask |= YMUX_TERM_CAP_DECSTBM;
    }
    if (caps.bce) {
        mask |= YMUX_TERM_CAP_BCE;
    }
    if (caps.extended_underline) {
        mask |= YMUX_TERM_CAP_EXTENDED_UNDERLINE;
    }
    if (caps.underline_colour) {
        mask |= YMUX_TERM_CAP_UNDERLINE_COLOUR;
    }
    if (caps.hyperlink) {
        mask |= YMUX_TERM_CAP_HYPERLINK;
    }
    projector->vt_capabilities = mask;
    return YETTY_OK_VOID();
}

/* The negotiated capability mask (post terminal-strings resolution when the
 * attach named its terminal). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_projector_capabilities(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, projector_res, "ymux projector_capabilities");
    return YETTY_OK(yetty_ycore_uint32, projector_res.value->vt_capabilities);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res, "ymux projector_dispose: from_obj");
    free(projector_res.value->response_seq);
    projector_res.value->response_seq = NULL;
    for (uint32_t cap = 0; cap < projector_res.value->response_caps_count; ++cap) {
        free(projector_res.value->response_caps[cap].name);
        free(projector_res.value->response_caps[cap].value);
    }
    free(projector_res.value->response_caps);
    projector_res.value->response_caps = NULL;
    projector_res.value->response_caps_count = 0;
    free(projector_res.value->vt_shadow_cells);
    free(projector_res.value->vt_links);
    yetty_ymux_tty_term_free(&projector_res.value->vt_term);
    return yetty_yclass_object_free(obj);
}

/* Force the next projection to be a FULL (client resync request, transport
 * loss, geometry change). The rich half resends too. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_invalidate(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res, "ymux projector_invalidate: from_obj");
    projector_res.value->vt_shadow_valid = 0;
    projector_res.value->rich_emitted = 0;
    /* The reconnected client starts with an empty by-hash cache — forget what
     * we delivered so the next full frame re-sends payloads, not dangling refs. */
    projector_res.value->rich_delivered_count = 0;
    return YETTY_OK_VOID();
}

/* Resolve one rich anchor to a viewport (row, col). Returns 1 when
 * visible. PRIMARY anchors match by stable logical-line identity across
 * the resolved viewport rows; ALT anchors are absolute alt-screen rows. */
static int rich_anchor_resolve(struct yetty_ymux_projector *projector, int anchor_kind,
                               uint64_t anchor_a, uint32_t anchor_b, uint64_t view_top,
                               uint32_t view_rows, uint32_t span_rows, int32_t *out_row,
                               uint32_t *out_col)
{
    if (anchor_kind == YETTY_YMUX_RICH_ANCHOR_ALT) {
        if (anchor_b >= view_rows) {
            return 0;
        }
        *out_row = (int32_t)anchor_b;
        *out_col = 0;
        return 1;
    }
    /* The figure's top row visible in the viewport. */
    for (uint32_t row = 0; row < view_rows; ++row) {
        struct yetty_ymux_history_row_result resolve_res =
            yetty_ymux_pane_resolve_row(projector->pane, view_top + row);
        if (YETTY_IS_ERR(resolve_res)) {
            yetty_ycore_error_destroy(resolve_res.error);
            continue;
        }
        if (resolve_res.value.logical_line_id != anchor_a) {
            continue;
        }
        uint32_t row_start = resolve_res.value.logical_cell_start;
        if (anchor_b < row_start || anchor_b - row_start >= resolve_res.value.cols) {
            continue;
        }
        *out_row = (int32_t)row;
        *out_col = anchor_b - row_start;
        return 1;
    }
    /* The figure's top scrolled ABOVE the viewport but a taller-than-screen
     * figure may still be partly visible below. Search up to span_rows into
     * history above view_top; a hit `back` rows up gives a NEGATIVE top offset,
     * and the figure overlaps the viewport when back < span_rows. */
    uint32_t search = span_rows > 1u ? span_rows - 1u : 0u;
    for (uint32_t back = 1; back <= search && (uint64_t)back <= view_top; ++back) {
        struct yetty_ymux_history_row_result resolve_res =
            yetty_ymux_pane_resolve_row(projector->pane, view_top - back);
        if (YETTY_IS_ERR(resolve_res)) {
            yetty_ycore_error_destroy(resolve_res.error);
            continue;
        }
        if (resolve_res.value.logical_line_id != anchor_a) {
            continue;
        }
        uint32_t row_start = resolve_res.value.logical_cell_start;
        if (anchor_b < row_start || anchor_b - row_start >= resolve_res.value.cols) {
            continue;
        }
        *out_row = -(int32_t)back;
        *out_col = anchor_b - row_start;
        return 1;
    }
    return 0;
}

/* Project the rich half of a content transaction: the store's
 * viewport-visible objects as ONE rich body (rich-format.h — the complete
 * visible set; payload = creation record + journal replay). Appends and
 * returns 1 when the store revision moved since the last emission (or on
 * first use / after invalidate); returns 0 with nothing appended
 * otherwise. */
/* Has this attachment's client already received + cached the payload `hash`? */
static int projector_resource_delivered(const struct yetty_ymux_projector *projector, uint64_t hash)
{
    for (uint32_t index = 0; index < projector->rich_delivered_count; ++index) {
        if (projector->rich_delivered_hashes[index] == hash) {
            return 1;
        }
    }
    return 0;
}

/* Record that we sent `hash`'s full payload (so a later frame can reference it).
 * Bounded: once full, further payloads are always re-sent (graceful degrade). */
static void projector_resource_mark(struct yetty_ymux_projector *projector, uint64_t hash)
{
    if (projector->rich_delivered_count < YMUX_PROJECTOR_RESOURCE_MAX) {
        projector->rich_delivered_hashes[projector->rich_delivered_count++] = hash;
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_projector_project_rich(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer *out)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, projector_res, "ymux project_rich: from_obj");
    struct yetty_ymux_projector *projector = projector_res.value;
    if (!out) {
        return YETTY_ERR(yetty_ycore_int, "ymux project_rich: NULL out");
    }
    struct yetty_yclass_object_ptr_result store_res = yetty_ymux_pane_rich_store(projector->pane);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, store_res, "ymux project_rich: store");
    struct yetty_yclass_object *store = store_res.value;

    struct yetty_ycore_uint64_result revision_res = yetty_ymux_rich_revision(store);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, revision_res, "ymux project_rich: revision");

    uint32_t view_rows = 0, view_cols = 0;
    struct yetty_ycore_void_result size_res =
        yetty_ymux_attachment_view_size(projector->attachment, &view_rows, &view_cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, size_res, "ymux project_rich: view size");
    struct yetty_ycore_uint64_result view_top_res =
        yetty_ymux_attachment_view_top(projector->attachment);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, view_top_res, "ymux project_rich: view top");
    uint64_t view_top = view_top_res.value;

    struct yetty_ycore_uint32_result count_res = yetty_ymux_rich_count(store);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, count_res, "ymux project_rich: count");

    /* Pass 1 — gather the visible set (id + resolved viewport row/col) in
     * store order; the set plus view_top decide full-frame vs scroll
     * reposition. */
    uint64_t visible_ids[YMUX_PROJECTOR_RICH_MAX];
    int32_t visible_rows[YMUX_PROJECTOR_RICH_MAX];
    uint32_t visible_cols[YMUX_PROJECTOR_RICH_MAX];
    uint32_t visible = 0;
    int overflow = 0;
    for (uint32_t index = 0; index < count_res.value; ++index) {
        uint64_t rich_id = yetty_ymux_rich_id_at(store, index).value;
        struct yetty_ycore_int_result tombstone_res = yetty_ymux_rich_is_tombstoned(store, rich_id);
        if (YETTY_IS_ERR(tombstone_res) || tombstone_res.value) {
            if (YETTY_IS_ERR(tombstone_res)) {
                yetty_ycore_error_destroy(tombstone_res.error);
            }
            continue;
        }
        int anchor_kind = 0;
        uint64_t anchor_a = 0;
        uint32_t anchor_b = 0;
        uint32_t span_rows = 1;
        struct yetty_ycore_void_result anchor_res =
            yetty_ymux_rich_anchor(store, rich_id, &anchor_kind, &anchor_a, &anchor_b, &span_rows);
        if (YETTY_IS_ERR(anchor_res)) {
            yetty_ycore_error_destroy(anchor_res.error);
            continue;
        }
        int32_t viewport_row = 0;
        uint32_t viewport_col = 0;
        if (!rich_anchor_resolve(projector, anchor_kind, anchor_a, anchor_b, view_top, view_rows,
                                 span_rows, &viewport_row, &viewport_col)) {
            continue;
        }
        if (visible >= YMUX_PROJECTOR_RICH_MAX) {
            overflow = 1;
            break;
        }
        visible_ids[visible] = rich_id;
        visible_rows[visible] = viewport_row;
        visible_cols[visible] = viewport_col;
        ++visible;
    }

    /* Full when the content or the visible SET changed (or first emit /
     * overflow); a moved view_top with an unchanged set is a cheap reposition;
     * otherwise there is nothing to send. */
    int membership_same =
        projector->rich_emitted && !overflow && visible == projector->shadow_rich_visible_count;
    for (uint32_t index = 0; membership_same && index < visible; ++index) {
        if (visible_ids[index] != projector->shadow_rich_visible_ids[index]) {
            membership_same = 0;
        }
    }
    int full = !projector->rich_emitted || overflow ||
               revision_res.value != projector->shadow_rich_revision || !membership_same;
    int moved = view_top != projector->shadow_rich_view_top;
    if (!full && (!moved || visible == 0)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    ydebug("ymux rich %s: visible=%u view_top=%llu row0=%d", full ? "full" : "reposition", visible,
           (unsigned long long)view_top, visible > 0 ? visible_rows[0] : -999);

    /* Header. */
    struct yetty_ycore_void_result write_res = paint_put(out, YMUX_RICH_MAGIC);
    if (YETTY_IS_OK(write_res)) {
        write_res = paint_put(out, YMUX_RICH_VERSION);
    }
    if (YETTY_IS_OK(write_res)) {
        write_res = paint_put(out, visible);
    }
    for (uint32_t index = 0; index < visible && YETTY_IS_OK(write_res); ++index) {
        uint64_t rich_id = visible_ids[index];
        int32_t viewport_row = visible_rows[index];
        uint32_t viewport_col = visible_cols[index];
        uint32_t journal_count = yetty_ymux_rich_journal_count(store, rich_id).value;

        if (!full) {
            /* Reposition record: header only, no payload — the consumer moves
             * the existing figure node without rebuilding the world. */
            write_res = paint_put(out, (uint32_t)rich_id);
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, (uint32_t)(rich_id >> 32));
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, journal_count);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, (uint32_t)viewport_row);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, viewport_col);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, YMUX_RICH_FLAG_REPOSITION);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, 0); /* payload_words */
            }
            continue;
        }

        uint32_t creation_count = 0;
        struct yetty_ycore_const_uint32_ptr_result creation_res =
            yetty_ymux_rich_creation(store, rich_id, &creation_count);
        if (YETTY_IS_ERR(creation_res)) {
            yetty_ycore_error_destroy(creation_res.error);
            continue;
        }
        uint32_t journal_words = 0;
        for (uint32_t entry = 0; entry < journal_count; ++entry) {
            uint32_t entry_count = 0;
            struct yetty_ycore_const_uint32_ptr_result entry_res =
                yetty_ymux_rich_journal_entry(store, rich_id, entry, &entry_count);
            if (YETTY_IS_OK(entry_res)) {
                journal_words += entry_count;
            } else {
                yetty_ycore_error_destroy(entry_res.error);
            }
        }

        /* Content addressing (only when the client caches by hash): EVERY record
         * carries the payload's [hash_lo][hash_hi] so the client can cache it;
         * if the client already has this hash, the creation bytes are OMITTED
         * (YMUX_RICH_FLAG_RESOURCE_REF) and it resolves them from its cache. */
        int cap_ref = (projector->vt_capabilities & YMUX_TERM_CAP_RESOURCE_REF) != 0;
        uint64_t creation_hash = 0;
        int use_ref = 0;
        if (cap_ref) {
            struct yetty_ycore_uint64_result hash_res =
                yetty_ymux_rich_creation_hash(store, rich_id);
            if (YETTY_IS_OK(hash_res)) {
                creation_hash = hash_res.value;
            } else {
                yetty_ycore_error_destroy(hash_res.error);
            }
            use_ref = creation_hash != 0 && projector_resource_delivered(projector, creation_hash);
        }
        uint32_t flags =
            (cap_ref ? YMUX_RICH_FLAG_HASHED : 0u) | (use_ref ? YMUX_RICH_FLAG_RESOURCE_REF : 0u);
        uint32_t payload_words = journal_words;
        if (cap_ref) {
            payload_words += 3u; /* [hash_lo][hash_hi][creation_count] prefix */
        }
        if (!use_ref) {
            payload_words += creation_count; /* inline creation bytes */
        }

        write_res = paint_put(out, (uint32_t)rich_id);
        if (YETTY_IS_OK(write_res)) {
            write_res = paint_put(out, (uint32_t)(rich_id >> 32));
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = paint_put(out, journal_count); /* rich_state_revision */
        }
        if (YETTY_IS_OK(write_res)) {
            write_res =
                paint_put(out, (uint32_t)viewport_row); /* signed: <0 = top above viewport */
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = paint_put(out, viewport_col);
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = paint_put(out, flags);
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = paint_put(out, payload_words);
        }
        if (cap_ref) {
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, (uint32_t)creation_hash);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, (uint32_t)(creation_hash >> 32));
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = paint_put(out, creation_count); /* cached-payload length */
            }
        }
        if (!use_ref) {
            if (YETTY_IS_OK(write_res)) {
                write_res = yetty_ycore_buffer_write(out, creation_res.value,
                                                     (size_t)creation_count * sizeof(uint32_t));
            }
            /* Remember it so a later frame references it instead of re-sending. */
            if (YETTY_IS_OK(write_res) && cap_ref && creation_hash != 0) {
                projector_resource_mark(projector, creation_hash);
            }
        }
        for (uint32_t entry = 0; entry < journal_count && YETTY_IS_OK(write_res); ++entry) {
            uint32_t entry_count = 0;
            struct yetty_ycore_const_uint32_ptr_result entry_res =
                yetty_ymux_rich_journal_entry(store, rich_id, entry, &entry_count);
            if (YETTY_IS_OK(entry_res)) {
                write_res = yetty_ycore_buffer_write(out, entry_res.value,
                                                     (size_t)entry_count * sizeof(uint32_t));
            } else {
                yetty_ycore_error_destroy(entry_res.error);
            }
        }
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_int, write_res, "ymux project_rich: write");

    projector->shadow_rich_revision = revision_res.value;
    projector->shadow_rich_view_top = view_top;
    projector->shadow_rich_visible_count = visible;
    for (uint32_t index = 0; index < visible; ++index) {
        projector->shadow_rich_visible_ids[index] = visible_ids[index];
    }
    projector->rich_emitted = 1;
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Project the current viewport. Appends ONE update (FULL or DELTA in the
 * paint format) to `out` and returns 1, or returns 0 with nothing appended
 * when the viewport is unchanged. The generation is minted on the
 * attachment only when an update is actually produced. */

/*===========================================================================
 * VT projection: serialize the attachment viewport as ordinary terminal
 * bytes (tmux-style redraw) for the yscene client terminal grid (#699). A
 * full, idempotent, absolutely-positioned redraw each call — autowrap off,
 * clear, per-row CUP + SGR runs + UTF-8 text, then the final cursor. Not yet
 * incremental or byte-parity with tmux (that is the later hardening); this is
 * a correct projected redraw the client libvterm consumes.
 *=========================================================================*/

static struct yetty_ycore_void_result vt_puts(struct yetty_ycore_buffer *out, const char *text)
{
    return yetty_ycore_buffer_write(out, text, strlen(text));
}

/* Emit an erase capability (`slot`) through the resolved terminfo model when
 * the model carries it — so a `cap=` OVERRIDE (a custom EL/ED byte sequence)
 * takes effect and output stays byte-identical for the standard xterm-family
 * profiles. When the model CANCELLED the cap (`cap@`), tmux does NOT emit the
 * raw sequence — for a clr_eol cancellation from column 0 it clears the whole
 * line with EL1 (`\e[<sx-1>C\e[1K`) and restores the cursor, exactly the bytes
 * the fixed oracle produces (cycle-26). No model → the raw literal (bare-tty
 * test path). Other cancellations keep the raw fallback (still-correct visible
 * output; those forms are not oracle-modelled yet). */
static struct yetty_ycore_void_result vt_erase_cap(struct yetty_ymux_projector *projector,
                                                   struct yetty_ycore_buffer *out,
                                                   enum yetty_ymux_tty_term_slot slot,
                                                   const char *raw)
{
    const struct yetty_ymux_tty_term *term = projector->vt_tty.term;
    if (term && yetty_ymux_tty_term_has(term, slot)) {
        char expanded[32];
        size_t len = yetty_ymux_tty_term_emit(term, slot, NULL, 0, expanded, sizeof(expanded));
        if (len > 0) {
            return yetty_ycore_buffer_write(out, expanded, len);
        }
    }
    if (term && slot == YMUX_TTY_TERM_EL) {
        /* clr_eol cancelled (el@): tmux's fallback is EL1 from the line end. */
        uint32_t cx = projector->vt_tty.cx;
        uint32_t cy = projector->vt_tty.cy;
        uint32_t sx = projector->vt_tty.sx;
        if (cx == 0 && sx > 0) {
            char buf[48];
            int written;
            if (cy == 0) {
                written = snprintf(buf, sizeof(buf), "\x1b[%uC\x1b[1K\x1b[H", sx - 1);
            } else {
                written = snprintf(buf, sizeof(buf), "\x1b[%uC\x1b[1K\x1b[%u;1H", sx - 1, cy + 1);
            }
            if (written > 0 && (size_t)written < sizeof(buf)) {
                return yetty_ycore_buffer_write(out, buf, (size_t)written);
            }
        }
    }
    return vt_puts(out, raw);
}

/* Emit a single-parameter edit capability (IL/DL/ICH/DCH) through the resolved
 * model so a `cap=` override applies; byte-identical to the raw `\e[<count><f>`
 * literal for the standard xterm-family profiles the parity gates exercise
 * (tmux's il/dl/ich/dch expand to exactly that). No model, or a cancelled cap,
 * falls back to the raw literal — same rationale as vt_erase_cap. */
static struct yetty_ycore_void_result vt_param_cap(struct yetty_ymux_projector *projector,
                                                   struct yetty_ycore_buffer *out,
                                                   enum yetty_ymux_tty_term_slot slot,
                                                   uint32_t count, char final)
{
    const struct yetty_ymux_tty_term *term = projector->vt_tty.term;
    if (term && yetty_ymux_tty_term_has(term, slot)) {
        char expanded[32];
        long params[1] = {(long)count};
        size_t len = yetty_ymux_tty_term_emit(term, slot, params, 1, expanded, sizeof(expanded));
        if (len > 0) {
            return yetty_ycore_buffer_write(out, expanded, len);
        }
    }
    if (term && slot == YMUX_TTY_TERM_DCH && count > 0) {
        /* parm_dch cancelled (dch@): tmux repeats dch1 (\e[P) count times — a
         * single delete-char is still available (cycle-26, byte-verified). */
        for (uint32_t index = 0; index < count; ++index) {
            struct yetty_ycore_void_result step = vt_puts(out, "\x1b[P");
            YETTY_RETURN_IF_ERR(yetty_ycore_void, step, "vt_param_cap: dch1 repeat");
        }
        return YETTY_OK_VOID();
    }
    char raw[16];
    int raw_len = snprintf(raw, sizeof(raw), "\x1b[%u%c", count, final);
    if (raw_len < 0 || (size_t)raw_len >= sizeof(raw)) {
        return YETTY_ERR(yetty_ycore_void, "vt_param_cap: edit-capability format overflow");
    }
    return yetty_ycore_buffer_write(out, raw, (size_t)raw_len);
}

/* UTF-8 encode one codepoint (0 → space) into `out`. */
static struct yetty_ycore_void_result vt_put_codepoint(struct yetty_ycore_buffer *out, uint32_t cp)
{
    uint8_t bytes[4];
    size_t count;
    if (cp == 0) {
        cp = 0x20;
    }
    if (cp < 0x80) {
        bytes[0] = (uint8_t)cp;
        count = 1;
    } else if (cp < 0x800) {
        bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
        bytes[1] = (uint8_t)(0x80 | (cp & 0x3F));
        count = 2;
    } else if (cp < 0x10000) {
        bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (cp & 0x3F));
        count = 3;
    } else {
        bytes[0] = (uint8_t)(0xF0 | (cp >> 18));
        bytes[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (uint8_t)(0x80 | (cp & 0x3F));
        count = 4;
    }
    return yetty_ycore_buffer_write(out, bytes, count);
}

/* Emit an SGR that resets then sets the run's attributes + truecolor fg/bg
 * (packed 0xFFBBGGRR). */
/* Engine cell attrs (YETTY_YMUX_ATTR_*) -> emitter attrs (YMUX_TTY_ATTR_*). */
/* Fill a tty cell's exotic channel from the engine's interned tables when
 * the negotiated caps enable each feature (review #17). Style 1 stays the
 * base UNDERLINE bit; 2..5 ride the attr word's style bits and suppress the
 * dropped-EXOTIC marking that plain profiles use. */
/* Resolve a hyperlink EXTERNAL id → its URI for this frame, caching the engine
 * lookup. Returns NULL if the id expired off the store ring. */
static const char *vt_link_uri(struct yetty_ymux_projector *projector, uint32_t link_id)
{
    for (size_t entry = 0; entry < projector->vt_links_count; ++entry) {
        if (projector->vt_links[entry].id == link_id) {
            return projector->vt_links[entry].uri[0] ? projector->vt_links[entry].uri : NULL;
        }
    }
    if (!projector->vt_frame_engine) {
        return NULL;
    }
    if (projector->vt_links_count == projector->vt_links_cap) {
        size_t grown_cap = projector->vt_links_cap ? projector->vt_links_cap * 2 : 32;
        struct vt_link_cache_entry *grown =
            realloc(projector->vt_links, grown_cap * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        projector->vt_links = grown;
        projector->vt_links_cap = grown_cap;
    }
    struct vt_link_cache_entry *slot = &projector->vt_links[projector->vt_links_count++];
    slot->id = link_id;
    slot->uri[0] = 0;
    struct yetty_ycore_uint32_result link_res = yetty_ymux_engine_exotic_link(
        projector->vt_frame_engine, link_id, slot->uri, (uint32_t)sizeof(slot->uri));
    if (YETTY_IS_ERR(link_res)) {
        yetty_ycore_error_destroy(link_res.error);
        slot->uri[0] = 0;
    }
    return slot->uri[0] ? slot->uri : NULL;
}

static void vt_fill_exotic(struct yetty_ymux_projector *projector,
                           const struct yetty_ymux_cell *cell, struct yetty_ymux_tty_cell *tty_cell)
{
    tty_cell->underline_colour = NULL;
    tty_cell->link = NULL;
    tty_cell->link_id = 0;
    if (projector->vt_tty.caps.extended_underline && cell->underline_style >= 2) {
        tty_cell->attr = (uint16_t)(tty_cell->attr & ~YMUX_TTY_ATTR_EXOTIC);
        tty_cell->attr |= (uint16_t)(cell->underline_style << YMUX_TTY_ATTR_STYLE_SHIFT);
    }
    /* 8+24 exotic ref: high 8 bits = underline-colour slot (0..255), low 24 bits
     * = hyperlink EXTERNAL id (0 = none). */
    uint8_t colour_ref = (uint8_t)(cell->exotic_ref >> 24);
    if (projector->vt_tty.caps.underline_colour && colour_ref != 0) {
        tty_cell->underline_colour = projector->vt_exotic_colours[colour_ref];
    }
    uint32_t link_id = cell->exotic_ref & 0xFFFFFFu;
    if (projector->vt_tty.caps.hyperlink && link_id != 0) {
        tty_cell->link = vt_link_uri(projector, link_id);
        tty_cell->link_id = tty_cell->link ? link_id : 0;
    }
}

static uint16_t vt_map_attrs(uint16_t attrs)
{
    uint16_t mapped = 0;
    if (attrs & YETTY_YMUX_ATTR_BOLD) {
        mapped |= YMUX_TTY_ATTR_BOLD;
    }
    if (attrs & YETTY_YMUX_ATTR_DIM) {
        mapped |= YMUX_TTY_ATTR_DIM; /* filter-carried: the fork drops SGR 2 */
    }
    if (attrs & YETTY_YMUX_ATTR_UNDERLINE) {
        mapped |= YMUX_TTY_ATTR_UNDERLINE;
    }
    /* Double/curly underline has no xterm-256color capability: DROPPED on
     * the wire (tmux emits nothing without Smulx) but the pen goes dirty. */
    if (attrs & YETTY_YMUX_ATTR_UNDERLINE2) {
        mapped |= YMUX_TTY_ATTR_EXOTIC;
    }
    if (attrs & YETTY_YMUX_ATTR_EXOTIC) {
        mapped |= YMUX_TTY_ATTR_EXOTIC;
    }
    if (attrs & YETTY_YMUX_ATTR_ITALIC) {
        mapped |= YMUX_TTY_ATTR_ITALICS;
    }
    if (attrs & YETTY_YMUX_ATTR_BLINK) {
        mapped |= YMUX_TTY_ATTR_BLINK;
    }
    if (attrs & YETTY_YMUX_ATTR_REVERSE) {
        mapped |= YMUX_TTY_ATTR_REVERSE;
    }
    if (attrs & YETTY_YMUX_ATTR_STRIKE) {
        mapped |= YMUX_TTY_ATTR_STRIKE;
    }
    if (attrs & YETTY_YMUX_ATTR_CONCEAL) {
        mapped |= YMUX_TTY_ATTR_HIDDEN;
    }
    if (attrs & YETTY_YMUX_ATTR_OVERLINE) {
        mapped |= YMUX_TTY_ATTR_OVERLINE;
    }
    return mapped;
}

/* Shortest cursor move — delegates to the validated tmux-parity emitter. */
static struct yetty_ycore_void_result vt_cursor(struct yetty_ymux_projector *projector,
                                                struct yetty_ycore_buffer *out, uint32_t col,
                                                uint32_t row)
{
    return yetty_ymux_tty_cursor(&projector->vt_tty, out, col, row);
}

/* A non-palette RGB colour on a client that can't render truecolor is downgraded
 * to the nearest 256-palette index (tmux colour_find_rgb); default/index pass
 * through unchanged. */
static int vt_downgrade_color(int color, int colors_rgb)
{
    /* DEFAULT is -1 — every bit set, INCLUDING the RGB flag: it must pass
     * through untouched or a 256-color client paints default cells white. */
    if (color == YMUX_TTY_COLOR_DEFAULT || colors_rgb || !(color & YMUX_TTY_COLOR_RGB_FLAG)) {
        return color;
    }
    return yetty_ymux_rgb_to_256((uint8_t)((color >> 16) & 0xFF), (uint8_t)((color >> 8) & 0xFF),
                                 (uint8_t)(color & 0xFF));
}

/* Recover an emitter colour (default / index / truecolour) from an engine cell's
 * resolved RGB, then apply the client's colour-depth profile — the negotiated
 * capability table is the single decision source. */
static int vt_cell_color(struct yetty_ymux_projector *projector, uint32_t rgb, uint32_t default_rgb)
{
    int tty_color = yetty_ymux_rgb_to_tty_color(rgb, default_rgb, projector->vt_palette, 256);
    return vt_downgrade_color(tty_color, projector->vt_tty.caps.colors_rgb);
}

/* Intent-aware variant (review #17): a cell stamped with the RGB-form
 * intent emits the RGB form even when its value matches a palette entry
 * (the fork collapses intent to RGB; the filter preserved it). Engine
 * cells pack R low; the tty colour packs R high. */
static int vt_cell_color_intent(struct yetty_ymux_projector *projector, uint32_t rgb,
                                uint32_t default_rgb, int rgb_intent)
{
    if (rgb_intent && rgb != default_rgb) {
        uint32_t red = rgb & 0xFF;
        uint32_t green = (rgb >> 8) & 0xFF;
        uint32_t blue = (rgb >> 16) & 0xFF;
        return vt_downgrade_color(
            (int)(YMUX_TTY_COLOR_RGB_FLAG | (red << 16) | (green << 8) | blue),
            projector->vt_tty.caps.colors_rgb);
    }
    return vt_cell_color(projector, rgb, default_rgb);
}

/* SGR minimization — map the cell's resolved RGB back to tmux colour intent
 * (default / palette index / truecolour), apply the client's colour-depth
 * profile, and delegate to the validated emitter. */
/* tmux tty_fake_bce: a terminal without the BCE flag clears to the DEFAULT
 * background — clearing under a non-default background must paint explicit
 * spaces (tty_repeat_space) instead of EL/EL1/ECH/ED. */
static int vt_fake_bce(struct yetty_ymux_projector *projector, uint32_t bg)
{
    if (projector->vt_tty.caps.bce) {
        return 0;
    }
    return vt_cell_color(projector, bg, projector->vt_default_bg) != YMUX_TTY_COLOR_DEFAULT;
}

/* tmux tty_repeat_space: plain spaces; the terminal cursor advances. */
static struct yetty_ycore_void_result vt_repeat_space(struct yetty_ymux_projector *projector,
                                                      struct yetty_ycore_buffer *out,
                                                      uint32_t count)
{
    struct yetty_ycore_void_result write_res = YETTY_OK_VOID();
    for (uint32_t space = 0; YETTY_IS_OK(write_res) && space < count; ++space) {
        write_res = yetty_ycore_buffer_write(out, " ", 1);
    }
    if (YETTY_IS_OK(write_res)) {
        projector->vt_tty.cx += count;
    }
    return write_res;
}

static struct yetty_ycore_void_result vt_attributes(struct yetty_ymux_projector *projector,
                                                    struct yetty_ycore_buffer *out, uint32_t fg,
                                                    uint32_t bg, uint16_t attrs)
{
    /* Colour-INTENT carriage (review #17): when the application used the
     * RGB form (38;2/48;2), the filter stamped the intent — emit the RGB
     * form even when the value happens to match a palette entry (the fork
     * collapses everything to RGB, so the backmap would otherwise re-index
     * it). The depth downgrade for 256-only clients still applies. */
    int tty_fg = vt_cell_color_intent(projector, fg, projector->vt_default_fg,
                                      (attrs & YETTY_YMUX_ATTR_FG_RGB_INTENT) != 0);
    int tty_bg = vt_cell_color_intent(projector, bg, projector->vt_default_bg,
                                      (attrs & YETTY_YMUX_ATTR_BG_RGB_INTENT) != 0);
    return yetty_ymux_tty_attributes(&projector->vt_tty, out, vt_map_attrs(attrs), tty_fg, tty_bg);
}

/* Attributes for an ERASE fill. tmux clears with a default cell whose only
 * meaningful colour is the erase BACKGROUND (back-colour erase); the pane pen's
 * foreground is never part of a clear (tty_default_attributes / tty_check_bg).
 * Forcing the foreground to default here stops a coloured pen from leaking a
 * spurious `\e[3Nm` ahead of the clear — and the sgr0 reset that a later cell
 * would otherwise emit to cancel it — which is what diverged ymux from tmux on
 * foreground-only EL/ED. */
static struct yetty_ycore_void_result vt_erase_attributes(struct yetty_ymux_projector *projector,
                                                          struct yetty_ycore_buffer *out,
                                                          uint32_t bg)
{
    return vt_attributes(projector, out, projector->vt_default_fg, bg, 0);
}

/* Cell-aware variant: additionally resolves the exotic channel (extended
 * underline style / underline colour / hyperlink) from the cell's carried
 * values on enabled profiles — the incremental emit path's counterpart of
 * vt_fill_exotic. */
static struct yetty_ycore_void_result vt_attributes_cell(struct yetty_ymux_projector *projector,
                                                         struct yetty_ycore_buffer *out,
                                                         const struct yetty_ymux_cell *cell)
{
    uint16_t attr = vt_map_attrs(cell->attrs);
    const char *underline_colour = NULL;
    const char *link = NULL;
    if (projector->vt_tty.caps.extended_underline && cell->underline_style >= 2) {
        attr = (uint16_t)(attr & ~YMUX_TTY_ATTR_EXOTIC);
        attr |= (uint16_t)(cell->underline_style << YMUX_TTY_ATTR_STYLE_SHIFT);
    }
    uint8_t colour_ref = (uint8_t)(cell->exotic_ref >> 24);
    if (projector->vt_tty.caps.underline_colour && colour_ref != 0) {
        underline_colour = projector->vt_exotic_colours[colour_ref];
    }
    uint32_t link_id = cell->exotic_ref & 0xFFFFFFu;
    if (projector->vt_tty.caps.hyperlink && link_id != 0) {
        link = vt_link_uri(projector, link_id);
    }
    if (!link) {
        link_id = 0;
    }
    int tty_fg = vt_cell_color_intent(projector, cell->fg, projector->vt_default_fg,
                                      (cell->attrs & YETTY_YMUX_ATTR_FG_RGB_INTENT) != 0);
    int tty_bg = vt_cell_color_intent(projector, cell->bg, projector->vt_default_bg,
                                      (cell->attrs & YETTY_YMUX_ATTR_BG_RGB_INTENT) != 0);
    return yetty_ymux_tty_attributes_exotic(&projector->vt_tty, out, attr, tty_fg, tty_bg,
                                            underline_colour, link, link_id);
}

/* Encode a cell's base codepoint + combining marks as UTF-8 into `buf`. Returns
 * the byte length. A zero codepoint becomes a space (a real blank cell). */
static uint32_t vt_encode_cell_text(const struct yetty_ymux_cell *cell, char *buf, size_t cap)
{
    uint32_t codepoints[9];
    uint32_t count = 0;
    codepoints[count++] = cell->codepoint ? cell->codepoint : 0x20u;
    for (uint8_t mark = 0; mark < cell->mark_count && count < 9; ++mark) {
        codepoints[count++] = cell->marks[mark];
    }
    uint32_t len = 0;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t codepoint = codepoints[index];
        uint8_t encoded[4];
        size_t encoded_len;
        if (codepoint < 0x80) {
            encoded[0] = (uint8_t)codepoint;
            encoded_len = 1;
        } else if (codepoint < 0x800) {
            encoded[0] = (uint8_t)(0xC0 | (codepoint >> 6));
            encoded[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
            encoded_len = 2;
        } else if (codepoint < 0x10000) {
            encoded[0] = (uint8_t)(0xE0 | (codepoint >> 12));
            encoded[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
            encoded[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
            encoded_len = 3;
        } else {
            encoded[0] = (uint8_t)(0xF0 | (codepoint >> 18));
            encoded[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
            encoded[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
            encoded[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
            encoded_len = 4;
        }
        if ((size_t)len + encoded_len > cap) {
            break;
        }
        memcpy(buf + len, encoded, encoded_len);
        len += (uint32_t)encoded_len;
    }
    return len;
}

/* Operation-driven FULL redraw — tmux's tty_draw_line model, byte-for-byte: hide
 * the cursor, then draw EVERY row via yetty_ymux_tty_draw_line (cursor-to-line-
 * start, per-cell attributes+text writing INTERIOR blanks, and a trailing
 * default-blank run cleared with EL). Rows advance by \r\n exactly as tmux, with
 * NO \e[2J and NO per-row CUP. `cells` is view_rows*view_cols of engine cells;
 * `text_arena` is scratch of at least view_cols*4*9 bytes; `tty_cells` holds one
 * row. */
static struct yetty_ycore_void_result vt_full_redraw(struct yetty_ymux_projector *projector,
                                                     struct yetty_ycore_buffer *out,
                                                     const struct yetty_ymux_cell *cells,
                                                     uint32_t view_rows, uint32_t view_cols,
                                                     struct yetty_ymux_tty_cell *tty_cells,
                                                     char *text_arena, size_t arena_cap)
{
    /* Hide the cursor for the duration of the redraw (tmux civis bracket). */
    struct yetty_ycore_void_result res = yetty_ymux_tty_cursor_visible(&projector->vt_tty, out, 0);
    if (YETTY_IS_ERR(res)) {
        return res;
    }
    for (uint32_t row = 0; row < view_rows; ++row) {
        const struct yetty_ymux_cell *cur_row = cells + (size_t)row * view_cols;
        size_t arena_used = 0;
        for (uint32_t col = 0; col < view_cols; ++col) {
            const struct yetty_ymux_cell *cell = &cur_row[col];
            struct yetty_ymux_tty_cell *tty_cell = &tty_cells[col];
            if (cell->width == 0) {
                /* Trailing half of a wide glyph — the head cell drew both columns;
                 * emit nothing for this column. A zero-length, zero-width cell is a
                 * no-op for the draw. */
                tty_cell->text = "";
                tty_cell->len = 0;
                tty_cell->attr = 0;
                tty_cell->fg = YMUX_TTY_COLOR_DEFAULT;
                tty_cell->bg = YMUX_TTY_COLOR_DEFAULT;
                tty_cell->width = 0;
                tty_cell->underline_colour = NULL;
                tty_cell->link = NULL;
                continue;
            }
            char *slot = text_arena + arena_used;
            uint32_t text_len = vt_encode_cell_text(cell, slot, arena_cap - arena_used);
            arena_used += text_len;
            tty_cell->text = slot;
            tty_cell->len = text_len;
            tty_cell->attr = vt_map_attrs(cell->attrs);
            tty_cell->fg = vt_cell_color_intent(projector, cell->fg, projector->vt_default_fg,
                                                (cell->attrs & YETTY_YMUX_ATTR_FG_RGB_INTENT) != 0);
            tty_cell->bg = vt_cell_color_intent(projector, cell->bg, projector->vt_default_bg,
                                                (cell->attrs & YETTY_YMUX_ATTR_BG_RGB_INTENT) != 0);
            tty_cell->width = cell->width;
            vt_fill_exotic(projector, cell, tty_cell);
        }
        res = yetty_ymux_tty_draw_line(&projector->vt_tty, out, tty_cells, view_cols, row);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    return YETTY_OK_VOID();
}

/* tmux tty_redraw_region: a terminal WITHOUT change_scroll_region cannot
 * scroll — every scroll-shaped operation becomes a REDRAW of the affected
 * rows (tty_draw_line per row), drawn from the per-op-maintained shadow =
 * the operation-time screen. Scratch is heap-allocated per call: this path
 * only runs on csr-less profiles. */
static struct yetty_ycore_void_result vt_redraw_region_rows(
    struct yetty_ymux_projector *projector, struct yetty_ycore_buffer *out,
    const struct yetty_ymux_cell *final_cells, uint32_t top_row, uint32_t bottom_row,
    uint32_t view_cols)
{
    struct yetty_ymux_tty_cell *tty_cells =
        malloc((size_t)view_cols * sizeof(struct yetty_ymux_tty_cell));
    size_t arena_cap = (size_t)view_cols * 4 * 9;
    char *text_arena = malloc(arena_cap);
    if (!tty_cells || !text_arena) {
        free(tty_cells);
        free(text_arena);
        return YETTY_ERR(yetty_ycore_void, "ymux projector: redraw-region scratch");
    }
    struct yetty_ycore_void_result res = yetty_ymux_tty_cursor_visible(&projector->vt_tty, out, 0);
    for (uint32_t row = top_row; YETTY_IS_OK(res) && row < bottom_row; ++row) {
        const struct yetty_ymux_cell *cur_row = final_cells + (size_t)row * view_cols;
        size_t arena_used = 0;
        for (uint32_t col = 0; col < view_cols; ++col) {
            const struct yetty_ymux_cell *cell = &cur_row[col];
            struct yetty_ymux_tty_cell *tty_cell = &tty_cells[col];
            if (cell->width == 0) {
                tty_cell->text = "";
                tty_cell->len = 0;
                tty_cell->attr = 0;
                tty_cell->fg = YMUX_TTY_COLOR_DEFAULT;
                tty_cell->bg = YMUX_TTY_COLOR_DEFAULT;
                tty_cell->width = 0;
                tty_cell->underline_colour = NULL;
                tty_cell->link = NULL;
                continue;
            }
            char *slot = text_arena + arena_used;
            uint32_t text_len = vt_encode_cell_text(cell, slot, arena_cap - arena_used);
            arena_used += text_len;
            tty_cell->text = slot;
            tty_cell->len = text_len;
            tty_cell->attr = vt_map_attrs(cell->attrs);
            tty_cell->fg = vt_cell_color_intent(projector, cell->fg, projector->vt_default_fg,
                                                (cell->attrs & YETTY_YMUX_ATTR_FG_RGB_INTENT) != 0);
            tty_cell->bg = vt_cell_color_intent(projector, cell->bg, projector->vt_default_bg,
                                                (cell->attrs & YETTY_YMUX_ATTR_BG_RGB_INTENT) != 0);
            tty_cell->width = cell->width;
            vt_fill_exotic(projector, cell, tty_cell);
        }
        res = yetty_ymux_tty_draw_line(&projector->vt_tty, out, tty_cells, view_cols, row);
    }
    free(tty_cells);
    free(text_arena);
    return res;
}

/* A cell that matches the cleared (2J) default screen — skipped on a FULL. */
static int vt_cell_is_default_blank(const struct yetty_ymux_cell *cell, uint32_t default_fg,
                                    uint32_t default_bg)
{
    return (cell->codepoint == 0 || cell->codepoint == 0x20) && cell->fg == default_fg &&
           cell->bg == default_bg && cell->attrs == 0 && cell->width <= 1;
}

/* Delta plan DERIVED FROM THE RECORDED OPS (#699.1): the renderer's source is
 * the canonical screen-write operation stream, not settled-grid inference.
 *
 * Returns the net full-width vertical scroll and the per-row horizontal
 * shifts (the fork decomposes ICH/DCH into single-row SCROLLRECTs with
 * rightward != 0). Sets *out_fallback when the window is unusable — evicted
 * (consumer fell behind the ring), an INVALIDATE/MOVERECT intervened, a
 * partial-rect vertical scroll (margins not yet ported), or conflicting
 * per-row shifts — in which case the caller takes the plain cell-diff
 * fallback (correct bytes, not minimal). */
static void vt_op_plan(struct yetty_yclass_object *engine, uint64_t consumed, uint64_t head,
                       uint32_t view_rows, uint32_t view_cols, int *out_fallback, int *out_scroll,
                       uint32_t *out_scroll_top, uint32_t *out_scroll_bottom, int32_t *row_shift,
                       uint32_t *row_shift_pos, uint32_t *row_el_col, uint32_t *row_el_fg,
                       uint32_t *row_el_bg, int32_t *out_ed_row, uint32_t *out_ed_fg,
                       uint32_t *out_ed_bg)
{
    *out_fallback = 0;
    *out_scroll = 0;
    *out_scroll_top = 0;
    *out_scroll_bottom = view_rows;
    *out_ed_row = -1;
    *out_ed_fg = 0;
    *out_ed_bg = 0;
    for (uint32_t row = 0; row < view_rows; ++row) {
        row_shift[row] = 0;
        row_shift_pos[row] = 0;
        row_el_col[row] = UINT32_MAX;
        row_el_fg[row] = 0;
        row_el_bg[row] = 0;
    }
    for (uint64_t sequence = consumed; sequence < head; ++sequence) {
        const struct yetty_ymux_engine_op *op = yetty_ymux_engine_op_at(engine, sequence);
        if (!op) {
            *out_fallback = 1; /* evicted — we fell behind the ring */
            return;
        }
        switch (op->type) {
        case YMUX_ENGINE_OP_PUTGLYPH:
        case YMUX_ENGINE_OP_MOVECURSOR:
            break; /* their results ride the cell diff */
        case YMUX_ENGINE_OP_ERASE: {
            /* A non-selective single-row erase running to end-of-line is
             * tmux's EL shape (tty_cmd_clearendofline): plan `\e[K` at the
             * start column with the pen's BCE colors. Anything else (ED,
             * partial-width, selective) still rides the cell diff. */
            int32_t erase_row = op->rect[0];
            if (op->a == 0 && op->rect[1] == erase_row + 1 && erase_row >= 0 &&
                (uint32_t)erase_row < view_rows && op->rect[3] == (int32_t)view_cols &&
                op->rect[2] >= 0 && op->rect[2] < (int32_t)view_cols) {
                uint32_t start_col = (uint32_t)op->rect[2];
                if (start_col < row_el_col[erase_row]) {
                    row_el_col[erase_row] = start_col;
                }
                row_el_fg[erase_row] = (uint32_t)op->c;
                row_el_bg[erase_row] = (uint32_t)op->b;
            } else if (op->a == 0 && op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols &&
                       erase_row >= 0 && (uint32_t)erase_row < view_rows &&
                       op->rect[1] == (int32_t)view_rows && op->rect[1] > erase_row + 1) {
                /* Multi-row full-width erase to the BOTTOM: the fork's second
                 * half of ED (\e[J) — the partial first row rides the EL plan
                 * independently. Plan tmux's clearendofscreen: cursor to the
                 * block's first row, `\e[J`. */
                *out_ed_row = erase_row;
                *out_ed_fg = (uint32_t)op->c;
                *out_ed_bg = (uint32_t)op->b;
            }
            break;
        }
        case YMUX_ENGINE_OP_INVALIDATE:
        case YMUX_ENGINE_OP_MOVERECT:
            *out_fallback = 1;
            return;
        case YMUX_ENGINE_OP_SCROLLRECT: {
            int downward = op->a;
            int rightward = op->b;
            if (rightward == 0) {
                /* Vertical: any FULL-WIDTH rect coalesces — the full viewport
                 * as plain SU/SD, a partial row range via DECSTBM margins
                 * (tmux tty_region + scrollup). Two different regions in one
                 * window, or a region SD, fall back. */
                int32_t rect_top = op->rect[0];
                int32_t rect_bottom = op->rect[1];
                if (op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols && rect_top >= 0 &&
                    rect_bottom > rect_top && (uint32_t)rect_bottom <= view_rows) {
                    if (*out_scroll != 0 && (*out_scroll_top != (uint32_t)rect_top ||
                                             *out_scroll_bottom != (uint32_t)rect_bottom)) {
                        *out_fallback = 1; /* two different regions in one window */
                        return;
                    }
                    *out_scroll_top = (uint32_t)rect_top;
                    *out_scroll_bottom = (uint32_t)rect_bottom;
                    *out_scroll += downward; /* downward>0 = content up = SU */
                } else {
                    *out_fallback = 1;
                    return;
                }
            } else {
                /* Horizontal single-row shift (the fork's ICH/DCH source):
                 * DCH = rightward>0 (content left), ICH = rightward<0. */
                int32_t row = op->rect[0];
                if (op->rect[1] != row + 1 || row < 0 || (uint32_t)row >= view_rows ||
                    op->rect[3] != (int32_t)view_cols || row_shift[row] != 0) {
                    *out_fallback = 1;
                    return;
                }
                row_shift[row] = -rightward; /* + = insert ('@'), - = delete ('P') */
                row_shift_pos[row] = (uint32_t)(op->rect[2] > 0 ? op->rect[2] : 0);
            }
            break;
        }
        default:
            *out_fallback = 1;
            return;
        }
    }
    uint32_t region_rows = *out_scroll_bottom - *out_scroll_top;
    if (*out_scroll > (int)region_rows || -*out_scroll > (int)region_rows) {
        *out_fallback = 1; /* over-rotated — a full redraw is cheaper anyway */
    }
}

/* Shift the shadow like the client's SU/SD did and blank the exposed rows to
 * default blanks, so the cell diff that follows redraws only genuinely new
 * content (typically just the one line the scroll brought in). */
static void vt_shadow_scroll_region(struct yetty_ymux_cell *shadow, uint32_t cols, uint32_t top,
                                    uint32_t bottom, int scroll, uint32_t default_fg,
                                    uint32_t default_bg);

/* Emit one cell unconditionally: cursor, pen, base codepoint (+ combine
 * staging when the cell carries marks — tmux screen_write_combine's wire
 * shape: base first unless the shadow already shows a prefix, then
 * cursor-back + the full cluster per added mark). Shared by the replay
 * flush and the settled-diff fallback. */
static struct yetty_ycore_void_result vt_replay_emit_cell(struct yetty_ymux_projector *projector,
                                                          struct yetty_ycore_buffer *out,
                                                          const struct yetty_ymux_cell *cell,
                                                          const struct yetty_ymux_cell *shadow_cell,
                                                          uint32_t col, uint32_t row)
{
    struct yetty_ycore_void_result write_res = YETTY_OK_VOID();
    uint32_t glyph_width = cell->width ? cell->width : 1;
    /* No-autowrap terminals (tmux TERM_NOAM, tty_putn): a glyph that would land
     * in the bottom-right cell is NOT written — the last column of the last row
     * stays blank because there is no safe way to advance past it. The cursor is
     * left where it is (tmux does not move it for the truncated tail). */
    if (projector->vt_tty.caps.noam && projector->vt_tty.sy > 0 &&
        row == projector->vt_tty.sy - 1 && col + glyph_width >= projector->vt_tty.sx) {
        return write_res;
    }
    if (cell->mark_count > 0) {
        int shadow_is_prefix = shadow_cell && shadow_cell->codepoint == cell->codepoint &&
                               shadow_cell->fg == cell->fg && shadow_cell->bg == cell->bg &&
                               shadow_cell->attrs == cell->attrs &&
                               shadow_cell->mark_count < cell->mark_count;
        for (uint8_t mark = 0; shadow_is_prefix && mark < shadow_cell->mark_count; ++mark) {
            if (shadow_cell->marks[mark] != cell->marks[mark]) {
                shadow_is_prefix = 0;
            }
        }
        uint8_t start_stage = 0;
        if (shadow_is_prefix) {
            start_stage = shadow_cell->mark_count;
        } else {
            write_res = vt_cursor(projector, out, col, row);
            if (YETTY_IS_OK(write_res)) {
                write_res = vt_attributes_cell(projector, out, cell);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = vt_put_codepoint(out, cell->codepoint);
            }
            projector->vt_tty.cx = col + glyph_width;
            projector->vt_tty.cy = row;
        }
        for (uint8_t stage = start_stage; YETTY_IS_OK(write_res) && stage < cell->mark_count;
             ++stage) {
            write_res = vt_cursor(projector, out, col, row);
            if (YETTY_IS_OK(write_res)) {
                write_res = vt_attributes_cell(projector, out, cell);
            }
            if (YETTY_IS_OK(write_res)) {
                write_res = vt_put_codepoint(out, cell->codepoint);
            }
            for (uint8_t mark = 0; YETTY_IS_OK(write_res) && mark <= stage; ++mark) {
                write_res = vt_put_codepoint(out, cell->marks[mark]);
            }
            projector->vt_tty.cx = col + glyph_width;
            projector->vt_tty.cy = row;
        }
        return write_res;
    }
    write_res = vt_cursor(projector, out, col, row);
    if (YETTY_IS_OK(write_res)) {
        write_res = vt_attributes_cell(projector, out, cell);
    }
    if (YETTY_IS_OK(write_res)) {
        write_res = vt_put_codepoint(out, cell->codepoint);
    }
    projector->vt_tty.cx = col + glyph_width;
    projector->vt_tty.cy = row;
    return write_res;
}

/* Flush the replay's pending text: rows top-down, columns left-to-right —
 * the byte order tmux's collect flush produces for a settled window. */
static struct yetty_ycore_void_result vt_replay_flush_pending(
    struct yetty_ymux_projector *projector, struct yetty_ycore_buffer *out,
    const struct yetty_ymux_cell *pending_cells, uint8_t *pending_used, uint32_t view_rows,
    uint32_t view_cols)
{
    struct yetty_ycore_void_result write_res = YETTY_OK_VOID();
    for (uint32_t row = 0; YETTY_IS_OK(write_res) && row < view_rows; ++row) {
        for (uint32_t col = 0; YETTY_IS_OK(write_res) && col < view_cols; ++col) {
            size_t slot = (size_t)row * view_cols + col;
            if (!pending_used[slot]) {
                continue;
            }
            pending_used[slot] = 0;
            const struct yetty_ymux_cell *shadow_cell =
                projector->vt_shadow_cells ? projector->vt_shadow_cells + slot : NULL;
            /* Tab-shaped forward gap (tmux HT writes a literal space run into
             * its collect when the skipped cells are blank): emit SPACES, not
             * a cursor move, for a small same-row forward gap whose SHADOW
             * cells are default blanks — byte parity with tmux's tab cell.
             * (21 = tmux's tab-cell capacity, sizeof gc.data.data.) */
            if (projector->vt_tty.cy == row && projector->vt_tty.cx < col &&
                col - projector->vt_tty.cx <= 21 && projector->vt_shadow_cells) {
                uint32_t gap_start = projector->vt_tty.cx;
                int all_blank = 1;
                for (uint32_t gap_col = gap_start; gap_col < col; ++gap_col) {
                    const struct yetty_ymux_cell *gap_cell =
                        projector->vt_shadow_cells + (size_t)row * view_cols + gap_col;
                    if ((gap_cell->codepoint != 0 && gap_cell->codepoint != 0x20u) ||
                        gap_cell->attrs != 0) {
                        all_blank = 0;
                        break;
                    }
                }
                if (all_blank) {
                    for (uint32_t gap_col = gap_start; YETTY_IS_OK(write_res) && gap_col < col;
                         ++gap_col) {
                        write_res = vt_puts(out, " ");
                    }
                    projector->vt_tty.cx = col;
                }
            }
            /* Autowrap continuation: the previous cell filled the last column
             * (cursor parked at sx); a pending cell at (row+1, 0) prints
             * WITHOUT any cursor move — the terminal's own wrap places it. */
            if (projector->vt_tty.cx == projector->vt_tty.sx && projector->vt_tty.cy + 1 == row &&
                col == 0) {
                projector->vt_tty.cx = 0;
                projector->vt_tty.cy = row;
            }
            write_res =
                vt_replay_emit_cell(projector, out, &pending_cells[slot], shadow_cell, col, row);
        }
    }
    /* Deferred bottom-right wrap (review #16): tmux draws the wrapped run
     * then clears the rest of the freshly-scrolled row with EL. */
    if (YETTY_IS_OK(write_res) && projector->vt_deferred_wrap_el) {
        projector->vt_deferred_wrap_el = 0;
        write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
    }
    return write_res;
}

/* The scroll idiom (tmux tty_cmd_scrollup/scrolldown): DECSTBM for partial
 * regions (cursor-invalidating set, reset in the tail), SU as LF-at-bottom
 * (n==1, client-scrollback preserving) or INDN, SD as RI at the region top,
 * explicit \e[K per vacated row on the way up, shadow region scroll. */
static struct yetty_ycore_void_result vt_replay_scroll(struct yetty_ymux_projector *projector,
                                                       struct yetty_ycore_buffer *out,
                                                       uint32_t scroll_top, uint32_t scroll_bottom,
                                                       int downward, uint32_t view_rows,
                                                       uint32_t cursor_col, uint32_t default_fg,
                                                       uint32_t default_bg)
{
    struct yetty_ycore_void_result write_res =
        vt_attributes(projector, out, default_fg, default_bg, 0);
    uint32_t magnitude = downward > 0 ? (uint32_t)downward : (uint32_t)(-downward);
    uint32_t region_bottom_row = scroll_bottom ? scroll_bottom - 1 : 0;
    (void)view_rows;
    if (YETTY_IS_OK(write_res)) {
        write_res = yetty_ymux_tty_region(&projector->vt_tty, out, scroll_top, region_bottom_row);
    }
    if (downward > 0 && magnitude == 1) {
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_cursor(projector, out, 0, region_bottom_row);
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_puts(out, "\n");
        }
        projector->vt_tty.cx = 0;
        projector->vt_tty.cy = region_bottom_row;
    } else if (downward > 0) {
        if (YETTY_IS_OK(write_res)) {
            uint32_t keep_row =
                projector->vt_tty.cy < projector->vt_tty.sy ? projector->vt_tty.cy : 0;
            write_res = vt_cursor(projector, out, 0, keep_row);
        }
        if (YETTY_IS_OK(write_res)) {
            char buf[16];
            snprintf(buf, sizeof(buf), "\x1b[%uS", magnitude);
            write_res = vt_puts(out, buf);
        }
    } else {
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_cursor(projector, out, cursor_col, scroll_top);
        }
        for (uint32_t line = 0; YETTY_IS_OK(write_res) && line < magnitude; ++line) {
            write_res = vt_puts(out, "\x1bM");
        }
    }
    if (downward > 0 && !projector->vt_deferred_wrap_el) {
        uint32_t clear_from =
            magnitude < scroll_bottom - scroll_top ? scroll_bottom - magnitude : scroll_top;
        for (uint32_t vac_row = clear_from; YETTY_IS_OK(write_res) && vac_row < scroll_bottom;
             ++vac_row) {
            write_res = vt_cursor(projector, out, 0, vac_row);
            if (YETTY_IS_OK(write_res)) {
                write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
            }
        }
    }
    if (YETTY_IS_OK(write_res) && projector->vt_shadow_cells) {
        vt_shadow_scroll_region(projector->vt_shadow_cells, projector->vt_tty.sx, scroll_top,
                                scroll_bottom, downward, default_fg, default_bg);
    }
    if (YETTY_IS_OK(write_res) && downward > 0) {
        projector->vt_pending_scrollbar_wrap = 1;
    }
    return write_res;
}

static void vt_shadow_scroll_region(struct yetty_ymux_cell *shadow, uint32_t cols, uint32_t top,
                                    uint32_t bottom, int scroll, uint32_t default_fg,
                                    uint32_t default_bg)
{
    struct yetty_ymux_cell blank;
    memset(&blank, 0, sizeof(blank));
    blank.fg = default_fg;
    blank.bg = default_bg;
    blank.width = 1;
    uint32_t region_rows = bottom - top;
    uint32_t magnitude = scroll > 0 ? (uint32_t)scroll : (uint32_t)(-scroll);
    /* A coalesced LF burst (rich figure row reservation, cat of a long file)
     * can exceed the region height. Scrolling a region by >= its height
     * leaves only blank rows — clamp so the retained-row count can't
     * underflow into a wild memmove. */
    if (magnitude > region_rows) {
        magnitude = region_rows;
    }
    struct yetty_ymux_cell *region = shadow + (size_t)top * cols;
    if (scroll > 0) {
        memmove(region, region + (size_t)magnitude * cols,
                (size_t)(region_rows - magnitude) * cols * sizeof(struct yetty_ymux_cell));
        for (uint32_t idx = (region_rows - magnitude) * cols; idx < region_rows * cols; ++idx) {
            region[idx] = blank;
        }
    } else {
        memmove(region + (size_t)magnitude * cols, region,
                (size_t)(region_rows - magnitude) * cols * sizeof(struct yetty_ymux_cell));
        for (uint32_t idx = 0; idx < magnitude * cols; ++idx) {
            region[idx] = blank;
        }
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_projector_project_vt(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer *out)
{
    struct yetty_ymux_projector_ptr_result projector_res = yetty_ymux_projector_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, projector_res, "ymux project_vt: from_obj");
    struct yetty_ymux_projector *projector = projector_res.value;
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: NULL out");
    }

    uint32_t view_rows = 0, view_cols = 0;
    struct yetty_ycore_void_result size_res =
        yetty_ymux_attachment_view_size(projector->attachment, &view_rows, &view_cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, size_res, "ymux project_vt: view size");
    if (view_rows == 0 || view_cols == 0) {
        return YETTY_OK_VOID();
    }

    struct yetty_ymux_cell *cells =
        calloc((size_t)view_rows * view_cols, sizeof(struct yetty_ymux_cell));
    uint8_t *continuations = calloc(view_rows, 1);
    if (!cells || !continuations) {
        free(cells);
        free(continuations);
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: gather alloc");
    }
    uint64_t view_top = 0;
    struct yetty_ycore_void_result gather_res =
        projector_gather(projector, cells, continuations, view_rows, view_cols, &view_top);
    if (YETTY_IS_ERR(gather_res)) {
        free(cells);
        free(continuations);
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: gather", gather_res);
    }

    int following = yetty_ymux_attachment_is_following(projector->attachment).value;
    struct yetty_yclass_object_ptr_result engine_res = yetty_ymux_pane_engine(projector->pane);
    if (YETTY_IS_ERR(engine_res)) {
        free(cells);
        free(continuations);
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: engine", engine_res);
    }
    struct yetty_yclass_object *engine = engine_res.value;
    uint32_t cursor_row = 0, cursor_col = 0;
    int cursor_visible = 0;
    struct yetty_ycore_void_result cursor_res =
        yetty_ymux_engine_cursor(engine, &cursor_row, &cursor_col, &cursor_visible);
    if (YETTY_IS_ERR(cursor_res)) {
        free(cells);
        free(continuations);
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: cursor", cursor_res);
    }
    uint64_t op_head_now = 0;
    {
        struct yetty_ycore_uint64_result op_head_res = yetty_ymux_engine_op_head(engine);
        if (YETTY_IS_OK(op_head_res)) {
            op_head_now = op_head_res.value;
        } else {
            yetty_ycore_error_destroy(op_head_res.error);
        }
    }
    uint32_t default_fg = 0, default_bg = 0;
    struct yetty_ycore_void_result dcolors_res =
        yetty_ymux_engine_default_colors(engine, &default_fg, &default_bg);
    if (YETTY_IS_ERR(dcolors_res)) {
        yetty_ycore_error_destroy(dcolors_res.error);
    }

    /* Recover tmux colour intent from resolved cell RGB: the defaults map to
     * \e[39m/\e[49m, and OSC-4-settable base16 entries map back to setaf/setab
     * indices, so an indexed colour re-emits exactly as tmux would. */
    projector->vt_default_fg = default_fg;
    projector->vt_default_bg = default_bg;
    uint32_t base16[16];
    for (int index = 0; index < 16; ++index) {
        struct yetty_ycore_uint32_result palette_res =
            yetty_ymux_engine_palette_color(engine, index);
        base16[index] = YETTY_IS_OK(palette_res) ? palette_res.value : 0xFF000000u;
        if (YETTY_IS_ERR(palette_res)) {
            yetty_ycore_error_destroy(palette_res.error);
        }
    }
    yetty_ymux_build_palette256(base16, projector->vt_palette);
    /* Underline colours still use a bounded 0..255 slot table; refresh it here.
     * Hyperlinks are resolved lazily by external id (vt_link_uri) — reset that
     * per-frame cache and remember this projection's engine for the lookups. */
    projector->vt_links_count = 0;
    projector->vt_frame_engine = engine;
    if (projector->vt_tty.caps.underline_colour) {
        for (uint32_t ref = 1; ref < 256; ++ref) {
            projector->vt_exotic_colours[ref][0] = 0;
            struct yetty_ycore_uint32_result colour_res =
                yetty_ymux_engine_exotic_colour(engine, ref, projector->vt_exotic_colours[ref],
                                                sizeof(projector->vt_exotic_colours[ref]));
            if (YETTY_IS_ERR(colour_res)) {
                yetty_ycore_error_destroy(colour_res.error);
            }
        }
    }

    /* (Re)size the VT shadow to the current geometry; a size change forces a
     * FULL (a fresh client is established from a known reset). */
    int full = !projector->vt_shadow_valid;
    /* A moved ANCHOR (scroll while not following) or a follow toggle
     * redraws COMPLETE: the recorded ops describe the live screen, never
     * the anchored view. A FOLLOWING view's view_top advances naturally
     * with content — the op replay owns that case. */
    if (projector->vt_shadow_valid && (following != projector->vt_shadow_following ||
                                       (!following && view_top != projector->vt_shadow_view_top))) {
        full = 1;
    }
    /* PER-PROJECTION state: the deferred-wrap epilogue must never leak
     * across deltas — armed-but-unflushed in one projection would silently
     * skip every later single-row erase (bg runs vanish). */
    projector->vt_deferred_wrap_el = 0;
    /* Set when a csr-less scroll became a REGION REDRAW (cursor hidden for
     * its duration): the tail then closes the bracket in tmux's redraw
     * order — visibility restore FIRST, cursor move after. */
    int redraw_bracket = 0;
    /* After a csr-less region redraw mid-replay: later ops whose rows lie
     * entirely inside [skip_top, skip_bottom) are already reflected in the
     * redraw (FINAL content) and skip. bottom 0 = inactive. */
    int32_t redraw_skip_top = 0;
    int32_t redraw_skip_bottom = 0;
    /* An INVALIDATE op in the pending window (alternate-screen switch,
     * resize) forces a FULL redraw: tmux redraws the whole pane on those
     * transitions, and a settled-cell diff cannot reproduce that emission
     * (review #13 alt-enter/exit parity). */
    for (uint64_t scan_seq = projector->vt_op_consumed; !full && scan_seq < op_head_now;
         ++scan_seq) {
        const struct yetty_ymux_engine_op *scan_op = yetty_ymux_engine_op_at(engine, scan_seq);
        if (!scan_op) {
            break; /* evicted — the replay handles fallback */
        }
        if (scan_op->type == YMUX_ENGINE_OP_INVALIDATE) {
            full = 1;
        }
    }
    if (projector->vt_shadow_rows != view_rows || projector->vt_shadow_cols != view_cols ||
        !projector->vt_shadow_cells) {
        struct yetty_ymux_cell *grown =
            realloc(projector->vt_shadow_cells,
                    (size_t)view_rows * view_cols * sizeof(struct yetty_ymux_cell));
        if (!grown) {
            free(cells);
            free(continuations);
            return YETTY_ERR(yetty_ycore_void, "ymux project_vt: shadow alloc");
        }
        projector->vt_shadow_cells = grown;
        projector->vt_shadow_rows = view_rows;
        projector->vt_shadow_cols = view_cols;
        full = 1;
    }

    struct yetty_ycore_void_result write_res = YETTY_OK_VOID();
    size_t delta_base_size = out->size;
    struct yetty_ymux_tty delta_base_tty = projector->vt_tty;
    int preamble_ran = 0; /* the attach preamble handles its own sync wrapping */
    (void)delta_base_size;
    (void)delta_base_tty;

    if (projector->vt_attach_preamble && !projector->vt_attach_preamble_sent &&
        YETTY_IS_OK(write_res)) {
        /* tmux's ATTACH PREAMBLE (review #17), byte-measured from the pinned
         * oracle (outer-terminal wrapping — alt screen, title stack, DECCKM —
         * excluded by the documented comparator strip list): home+clear,
         * cursor sync, mouse resets, bracketed paste, theme notify + query,
         * pen reset, sync, mouse resets, home, DECSTBM, then the hidden-
         * cursor blank-screen redraw, cursor show, home, and the mouse mode
         * block. The projection then continues as a DELTA against the blank
         * screen this preamble established. */
        write_res = vt_puts(out, "\x1b[H\x1b[2J\x1b[?12l\x1b[?25h"
                                 "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?1005l"
                                 "\x1b[?2004h\x1b[?2031h\x1b[?996n");
        /* A margin-capable client gets DECLRMM enabled here (tmux emits ?69h
         * right after ?996n) so the daemon can later use left/right margins. */
        if (YETTY_IS_OK(write_res) && projector->vt_tty.caps.margins) {
            write_res = vt_puts(out, "\x1b[?69h");
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_puts(out, "\x1b(B\x1b[m\x1b[?12l\x1b[?25h"
                                     "\x1b[?1006l\x1b[?1000l\x1b[?1002l\x1b[?1003l");
        }
        if (YETTY_IS_OK(write_res)) {
            /* tmux wraps a FULL redraw in synchronized-output markers when the
             * client advertises the Sync capability: BEGIN (?2026h) right after
             * the scroll-region set and before the cursor is hidden. A
             * margin-capable client also gets a second region set + DECSLRM reset
             * (\e[1;Nr\e[s) establishing full-screen left/right margins. */
            char margin_part[24] = "";
            if (projector->vt_tty.caps.margins) {
                snprintf(margin_part, sizeof(margin_part), "\x1b[1;%ur\x1b[s", view_rows);
            }
            char region_home[64];
            snprintf(region_home, sizeof(region_home), "\x1b[1;1H\x1b[1;%ur%s%s\x1b[?25l\x1b[1;1H",
                     view_rows, margin_part, projector->vt_tty.caps.sync ? "\x1b[?2026h" : "");
            write_res = vt_puts(out, region_home);
        }
        for (uint32_t clear_row = 0; YETTY_IS_OK(write_res) && clear_row < view_rows; ++clear_row) {
            write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
            if (YETTY_IS_OK(write_res) && clear_row + 1 < view_rows) {
                write_res = vt_puts(out, "\r\n");
            }
        }
        /* END synchronized output (?2026l) after the last redrawn row and
         * before the cursor-show epilogue — mirrors tmux's tty_sync_end. */
        if (YETTY_IS_OK(write_res) && projector->vt_tty.caps.sync) {
            write_res = vt_puts(out, "\x1b[?2026l");
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_puts(out, "\x1b[?12l\x1b[?25h\x1b[H"
                                     "\x1b[?1006l\x1b[?1000l\x1b[?1002l\x1b[?1003l"
                                     "\x1b[?1006h\x1b[?1000h\x1b[?1002h");
        }
        projector->vt_attach_preamble_sent = 1;
        preamble_ran = 1;
        /* The receiver's screen is now BLANK and the emitter is at a known
         * home/pen state — continue as a delta against a blank shadow. The
         * default cursor shape counts as already-established (tmux emits no
         * Ss at attach), and every op recorded BEFORE the attach is covered
         * by the blank redraw. */
        yetty_ymux_tty_init(&projector->vt_tty, view_rows, view_cols);
        projector_reattach_term(projector);
        projector->vt_tty.cursor_visible = 1;
        projector->vt_tty.cursor_shape_param = 1;
        if (projector->vt_shadow_cells && projector->vt_shadow_rows == view_rows &&
            projector->vt_shadow_cols == view_cols) {
            memset(projector->vt_shadow_cells, 0,
                   (size_t)view_rows * view_cols * sizeof(struct yetty_ymux_cell));
            for (size_t blank = 0; blank < (size_t)view_rows * view_cols; ++blank) {
                projector->vt_shadow_cells[blank].fg = default_fg;
                projector->vt_shadow_cells[blank].bg = default_bg;
                projector->vt_shadow_cells[blank].width = 1;
            }
            projector->vt_shadow_valid = 1;
            full = 0;
        }
        /* Everything recorded so far (the pane-init clear included) is
         * covered by the preamble's blank redraw — consume it. */
        {
            struct yetty_ycore_uint64_result preamble_head_res = yetty_ymux_engine_op_head(engine);
            if (YETTY_IS_OK(preamble_head_res)) {
                projector->vt_op_consumed = preamble_head_res.value;
            } else {
                yetty_ycore_error_destroy(preamble_head_res.error);
            }
        }
        /* REATTACH content redraw (#699 cycle 19): the preamble established a
         * BLANK receiver, but the pane's live screen may already carry
         * content (attach to an existing session). tmux follows its preamble
         * with a redraw of the current rows; without this every reattach
         * showed blank text until the next output touched a row. Draw only
         * the NON-blank rows — an empty pane adds zero bytes, keeping the
         * 347-byte preamble oracle intact — then mirror the screen into the
         * shadow so later deltas diff against what the receiver now shows. */
        if (projector->vt_shadow_valid && YETTY_IS_OK(write_res)) {
            for (uint32_t content_row = 0; content_row < view_rows && YETTY_IS_OK(write_res);
                 ++content_row) {
                const struct yetty_ymux_cell *row_cells = cells + (size_t)content_row * view_cols;
                int row_blank = 1;
                for (uint32_t col = 0; col < view_cols; ++col) {
                    if (!vt_cell_is_default_blank(&row_cells[col], default_fg, default_bg)) {
                        row_blank = 0;
                        break;
                    }
                }
                if (row_blank) {
                    continue;
                }
                write_res = vt_redraw_region_rows(projector, out, cells, content_row,
                                                  content_row + 1, view_cols);
            }
            memcpy(projector->vt_shadow_cells, cells,
                   (size_t)view_rows * view_cols * sizeof(struct yetty_ymux_cell));
        }
    }
    if (full) {
        /* Operation-driven full redraw, byte-identical to tmux's tty_draw_line:
         * NO \e[2J / \e[?7l — every row is drawn (interior blanks written, the
         * trailing blank run cleared with EL), rows advancing by \r\n. Reset the
         * emitter to a known screen/pen, then seed a NON-origin assumed cursor so
         * the first tty_cursor(0,0) emits an absolute home (\e[H) exactly as tmux
         * does at redraw start. */
        int preserved_shape_param = projector->vt_tty.cursor_shape_param;
        struct yetty_ymux_tty_caps preserved_caps = projector->vt_tty.caps;
        yetty_ymux_tty_init(&projector->vt_tty, view_rows, view_cols);
        /* The DECSCUSR cache, the negotiated capability profile, AND the
         * terminfo model SURVIVE a redraw (tmux never re-emits Ss for a pane
         * redraw; all three are connection state, not screen state). */
        projector->vt_tty.cursor_shape_param = preserved_shape_param;
        projector->vt_tty.caps = preserved_caps;
        projector_reattach_term(projector);
        projector->vt_tty.cy = 1;
        struct yetty_ymux_tty_cell *tty_cells =
            calloc(view_cols, sizeof(struct yetty_ymux_tty_cell));
        size_t arena_cap = (size_t)view_cols * 9 * 4 + 4;
        char *text_arena = malloc(arena_cap);
        if (!tty_cells || !text_arena) {
            free(tty_cells);
            free(text_arena);
            free(cells);
            free(continuations);
            return YETTY_ERR(yetty_ycore_void, "ymux project_vt: full redraw alloc");
        }
        /* A mid-session full redraw (resync / ring eviction / resize) is wrapped
         * in synchronized-output markers for a sync-capable client, the same way
         * tmux's tty_sync_start/tty_sync_end bracket every screen_redraw. The
         * attach-preamble redraw already carries its own markers, so this covers
         * the non-attach full frames. */
        if (projector->vt_tty.caps.sync) {
            write_res = vt_puts(out, "\x1b[?2026h");
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_full_redraw(projector, out, cells, view_rows, view_cols, tty_cells,
                                       text_arena, arena_cap);
        }
        if (YETTY_IS_OK(write_res) && projector->vt_tty.caps.sync) {
            write_res = vt_puts(out, "\x1b[?2026l");
        }
        free(tty_cells);
        free(text_arena);
    } else {
        /* ORDERED OPERATION REPLAY (#699.1, review #11): the recorded op
         * stream is the renderer's source of truth, replayed with tmux's
         * collect discipline — PUTGLYPH ops accumulate per-row pending text
         * (later writes replace earlier pending cells); every non-text op
         * FLUSHES the pending text first, then emits its own idiom (EL/ED,
         * ICH/DCH, scroll/DECSTBM). Pending cells emit UNCONDITIONALLY:
         * tmux's collect does not diff against the screen, so cancelling op
         * pairs (ICH then DCH) and put-then-erase histories reach the wire
         * exactly as tmux emits them. The settled-cell diff survives ONLY as
         * the fallback for windows the replay cannot honor (ring eviction,
         * INVALIDATE, MOVERECT, shapes without a ported idiom). */
        struct yetty_ymux_cell *pending_cells =
            calloc((size_t)view_rows * view_cols, sizeof(struct yetty_ymux_cell));
        uint8_t *pending_used = calloc((size_t)view_rows * view_cols, 1);
        int window_fallback = (pending_cells && pending_used) ? 0 : 1;
        if (!following) {
            /* ANCHORED view: live-screen ops do not apply — the plain
             * shadow cell-diff is the correct (usually empty) delta. */
            window_fallback = 1;
        }

        for (uint64_t sequence = projector->vt_op_consumed;
             !window_fallback && YETTY_IS_OK(write_res) && sequence < op_head_now; ++sequence) {
            const struct yetty_ymux_engine_op *op = yetty_ymux_engine_op_at(engine, sequence);
            if (!op) {
                window_fallback = 1; /* evicted — we fell behind the ring */
                break;
            }
            switch (op->type) {
            case YMUX_ENGINE_OP_PUTGLYPH: {
                int32_t put_row = op->a;
                int32_t put_col = op->b;
                if (put_row < 0 || (uint32_t)put_row >= view_rows || put_col < 0 ||
                    (uint32_t)put_col >= view_cols) {
                    break; /* outside the viewport — nothing for this client */
                }
                if (redraw_skip_bottom > redraw_skip_top && put_row >= redraw_skip_top &&
                    put_row < redraw_skip_bottom) {
                    break; /* already reflected by the csr-less region redraw */
                }
                size_t slot = (size_t)put_row * view_cols + (size_t)put_col;
                pending_cells[slot] = op->cell;
                pending_used[slot] = 1;
                break;
            }
            case YMUX_ENGINE_OP_MOVECURSOR:
                break; /* lazy — the tail repositions (tmux reset_state) */
            case YMUX_ENGINE_OP_ERASE: {
                if (redraw_skip_bottom > redraw_skip_top && op->rect[0] >= redraw_skip_top &&
                    op->rect[1] <= redraw_skip_bottom) {
                    break; /* already reflected by the csr-less region redraw */
                }
                if (projector->vt_deferred_wrap_el && op->rect[1] == op->rect[0] + 1) {
                    /* The scrolled-in blank row of a DEFERRED wrap: tmux's EL
                     * comes AFTER the wrapped run — the deferred epilogue
                     * covers this erase. */
                    break;
                }
                int32_t erase_row = op->rect[0];
                /* clr_eos (ed) cancelled: tmux never emits a raw \e[J it does not
                 * have — it clears the block by scrolling a temporary region. We
                 * do not yet emit that exact scroll idiom, so route a cancelled-ED
                 * block to the window redraw (correct pixels, no cancelled cap on
                 * the wire) rather than shipping \e[J. The fake-BCE branches below
                 * clear via per-row spaces and need no ED, so they are unaffected. */
                int ed_present = !projector->vt_tty.term ||
                                 yetty_ymux_tty_term_has(projector->vt_tty.term, YMUX_TTY_TERM_ED);
                const struct yetty_ymux_engine_op *partner =
                    sequence + 1 < op_head_now ? yetty_ymux_engine_op_at(engine, sequence + 1)
                                               : NULL;
                /* The fork splits ED into row-tail + block; tmux's wire order
                 * is block FIRST (\e[J), row-tail EL second. */
                int is_ed_pair = partner && partner->type == YMUX_ENGINE_OP_ERASE && op->a == 0 &&
                                 partner->a == 0 && op->rect[1] == erase_row + 1 &&
                                 op->rect[3] == (int32_t)view_cols &&
                                 partner->rect[0] == erase_row + 1 &&
                                 partner->rect[1] == (int32_t)view_rows && partner->rect[2] == 0 &&
                                 partner->rect[3] == (int32_t)view_cols;
                if (is_ed_pair) {
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (vt_fake_bce(projector, (uint32_t)partner->b)) {
                        /* tty_clear_area without BCE: per-row spaces. */
                        write_res = vt_erase_attributes(projector, out, (uint32_t)partner->b);
                        for (uint32_t clear_row = (uint32_t)(erase_row + 1);
                             YETTY_IS_OK(write_res) && clear_row < view_rows; ++clear_row) {
                            write_res = vt_cursor(projector, out, 0, clear_row);
                            if (YETTY_IS_OK(write_res)) {
                                write_res = vt_repeat_space(projector, out, view_cols);
                            }
                        }
                    } else if (!ed_present) {
                        /* ed@ (clr_eos cancelled): tmux clears the block
                         * [erase_row+1 .. view_rows-1] by scrolling a temporary
                         * region up by its own height — the blank fill comes from
                         * the scroll, never a raw \e[J. Pen first (so BCE fills the
                         * right background), then region + SU. tty_region
                         * invalidates the cursor, so the row-tail EL and final
                         * reposition below come out absolute, byte-identical to
                         * tmux (\e[<t>;<b>r\e[<n>S…\e[1;<sy>r). */
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_attributes(projector, out, (uint32_t)partner->b);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = yetty_ymux_tty_region(
                                &projector->vt_tty, out, (uint32_t)(erase_row + 1), view_rows - 1);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            char scroll_buf[16];
                            snprintf(scroll_buf, sizeof(scroll_buf), "\x1b[%uS",
                                     view_rows - (uint32_t)(erase_row + 1));
                            write_res = vt_puts(out, scroll_buf);
                        }
                    } else {
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_cursor(projector, out, 0, (uint32_t)(erase_row + 1));
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_attributes(projector, out, (uint32_t)partner->b);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_ED, "\x1b[J");
                        }
                    }
                    if (YETTY_IS_OK(write_res) && op->rect[2] >= 0 &&
                        op->rect[2] < (int32_t)view_cols && erase_row >= 0 &&
                        (uint32_t)erase_row < view_rows) {
                        write_res =
                            vt_cursor(projector, out, (uint32_t)op->rect[2], (uint32_t)erase_row);
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            if (vt_fake_bce(projector, (uint32_t)op->b)) {
                                write_res = vt_repeat_space(projector, out,
                                                            view_cols - (uint32_t)op->rect[2]);
                            } else {
                                write_res =
                                    vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
                            }
                        }
                    }
                    ++sequence; /* the block partner is consumed */
                    break;
                }
                if (op->a == 0 && op->rect[1] == erase_row + 1 && erase_row >= 0 &&
                    (uint32_t)erase_row < view_rows && op->rect[3] == (int32_t)view_cols &&
                    op->rect[2] >= 0 && op->rect[2] < (int32_t)view_cols) {
                    /* EL shape (tmux tty_cmd_clearendofline). Without BCE a
                     * non-default background paints spaces (tty_fake_bce). */
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res)) {
                        write_res =
                            vt_cursor(projector, out, (uint32_t)op->rect[2], (uint32_t)erase_row);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        if (vt_fake_bce(projector, (uint32_t)op->b)) {
                            write_res =
                                vt_repeat_space(projector, out, view_cols - (uint32_t)op->rect[2]);
                        } else {
                            write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
                        }
                    }
                    break;
                }
                if (op->a == 0 && op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols &&
                    erase_row >= 0 && (uint32_t)erase_row < view_rows &&
                    op->rect[1] == (int32_t)view_rows) {
                    /* Standalone bottom block (\e[J from column 0). */
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res) && vt_fake_bce(projector, (uint32_t)op->b)) {
                        write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                        for (uint32_t clear_row = (uint32_t)erase_row;
                             YETTY_IS_OK(write_res) && clear_row < view_rows; ++clear_row) {
                            write_res = vt_cursor(projector, out, 0, clear_row);
                            if (YETTY_IS_OK(write_res)) {
                                write_res = vt_repeat_space(projector, out, view_cols);
                            }
                        }
                        break;
                    }
                    if (!ed_present) {
                        /* ed@ : same scroll-clear as the ED-pair block. tmux
                         * splits \e[J into the erase_row tail (EL) plus a scroll
                         * of the rows below it; pen + region + SU first, then the
                         * EL at erase_row (absolute, cursor invalidated by
                         * tty_region), then the flush epilogue restores the region
                         * and repositions. */
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                        }
                        if (YETTY_IS_OK(write_res) && (uint32_t)erase_row + 1 < view_rows) {
                            write_res = yetty_ymux_tty_region(
                                &projector->vt_tty, out, (uint32_t)(erase_row + 1), view_rows - 1);
                            if (YETTY_IS_OK(write_res)) {
                                char scroll_buf[16];
                                snprintf(scroll_buf, sizeof(scroll_buf), "\x1b[%uS",
                                         view_rows - (uint32_t)(erase_row + 1));
                                write_res = vt_puts(out, scroll_buf);
                            }
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_cursor(projector, out, 0, (uint32_t)erase_row);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_EL, "\x1b[K");
                        }
                        break;
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_cursor(projector, out, 0, (uint32_t)erase_row);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_erase_cap(projector, out, YMUX_TTY_TERM_ED, "\x1b[J");
                    }
                    break;
                }
                if (op->a == 0 && op->rect[1] == erase_row + 1 && erase_row >= 0 &&
                    (uint32_t)erase_row < view_rows && op->rect[2] >= 0 &&
                    op->rect[3] > op->rect[2] && (uint32_t)op->rect[3] <= view_cols) {
                    /* tmux tty_clear_line's decision tree for a partial-row
                     * clear: to-EOL was handled above; at line START use EL1
                     * with the cursor ON the last cleared cell; otherwise ECH
                     * at the clear origin. */
                    uint32_t clear_from = (uint32_t)op->rect[2];
                    uint32_t clear_to = (uint32_t)op->rect[3];
                    int fake_bce = vt_fake_bce(projector, (uint32_t)op->b);
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res) && fake_bce) {
                        /* tty_fake_bce: cursor to the clear origin, spaces. */
                        write_res = vt_cursor(projector, out, clear_from, (uint32_t)erase_row);
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                        }
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_repeat_space(projector, out, clear_to - clear_from);
                        }
                        break;
                    }
                    if (YETTY_IS_OK(write_res)) {
                        if (clear_from == 0) {
                            write_res =
                                vt_cursor(projector, out, clear_to - 1, (uint32_t)erase_row);
                            if (YETTY_IS_OK(write_res)) {
                                write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                            }
                            if (YETTY_IS_OK(write_res)) {
                                write_res = vt_puts(out, "\x1b[1K");
                            }
                        } else {
                            write_res = vt_cursor(projector, out, clear_from, (uint32_t)erase_row);
                            if (YETTY_IS_OK(write_res)) {
                                write_res = vt_erase_attributes(projector, out, (uint32_t)op->b);
                            }
                            if (projector->vt_tty.caps.ech) {
                                if (YETTY_IS_OK(write_res)) {
                                    /* Through the model so an `ech=` override
                                     * is honored (cycle-22); the default
                                     * xterm-256color ech expands to the same
                                     * \e[<n>X bytes the parity oracle pins. */
                                    uint32_t saved_cx = projector->vt_tty.cx;
                                    uint32_t saved_cy = projector->vt_tty.cy;
                                    write_res = yetty_ymux_tty_clear_chars(&projector->vt_tty, out,
                                                                           clear_to - clear_from);
                                    projector->vt_tty.cx = saved_cx;
                                    projector->vt_tty.cy = saved_cy;
                                }
                            } else {
                                /* No ECH capability: tmux writes spaces
                                 * (tty_clear_line's escape-less tail). */
                                for (uint32_t space_col = clear_from;
                                     YETTY_IS_OK(write_res) && space_col < clear_to; ++space_col) {
                                    write_res = vt_puts(out, " ");
                                }
                                projector->vt_tty.cx = clear_to;
                                projector->vt_tty.cy = (uint32_t)erase_row;
                            }
                        }
                    }
                    break;
                }
                window_fallback = 1; /* multi-row partial / selective — no idiom */
                break;
            }
            case YMUX_ENGINE_OP_SCROLLRECT: {
                if (redraw_skip_bottom > redraw_skip_top && op->rect[0] >= redraw_skip_top &&
                    op->rect[1] <= redraw_skip_bottom) {
                    break; /* already reflected by the csr-less region redraw */
                }
                int downward = op->a;
                int rightward = op->b;
                if (rightward == 0 && op->c == -1 &&
                    (op->cell.codepoint == 1 || op->cell.codepoint == 2) &&
                    (downward > 0 ? projector->vt_tty.caps.delete_line
                                  : projector->vt_tty.caps.insert_line) &&
                    op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols &&
                    op->rect[1] == (int32_t)view_rows && downward != 0) {
                    /* No DECSTBM + a rect from a mid-screen row to the bottom:
                     * this is IL (content down) / DL (content up) at the rect
                     * top — tmux emits \e[nL / \e[nM with the cursor on that
                     * row and INVALIDATES its cursor after (some terminals
                     * leave it undefined). No vacated-row clears, no scrollbar
                     * pass. Coalesce runs like the scroll path. */
                    int line_amount = downward;
                    while (sequence + 1 < op_head_now) {
                        const struct yetty_ymux_engine_op *next_op =
                            yetty_ymux_engine_op_at(engine, sequence + 1);
                        if (!next_op || next_op->type != YMUX_ENGINE_OP_SCROLLRECT ||
                            next_op->b != 0 || next_op->c != -1 ||
                            next_op->cell.codepoint != op->cell.codepoint ||
                            next_op->rect[0] != op->rect[0] || next_op->rect[1] != op->rect[1] ||
                            next_op->rect[2] != 0 || next_op->rect[3] != (int32_t)view_cols ||
                            (next_op->a > 0) != (downward > 0)) {
                            break;
                        }
                        line_amount += next_op->a;
                        ++sequence;
                    }
                    uint32_t line_count =
                        line_amount > 0 ? (uint32_t)line_amount : (uint32_t)(-line_amount);
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res)) {
                        /* Operation-time column, not 0 (review #14). */
                        write_res = vt_cursor(projector, out, op->cell.fg, (uint32_t)op->rect[0]);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_attributes(projector, out, default_fg, default_bg, 0);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        enum yetty_ymux_tty_term_slot edit_slot =
                            line_amount > 0 ? YMUX_TTY_TERM_DL : YMUX_TTY_TERM_IL;
                        write_res = vt_param_cap(projector, out, edit_slot, line_count,
                                                 line_amount > 0 ? 'M' : 'L');
                    }
                    projector->vt_tty.cx = UINT32_MAX;
                    projector->vt_tty.cy = UINT32_MAX;
                    if (YETTY_IS_OK(write_res) && projector->vt_shadow_cells) {
                        vt_shadow_scroll_region(projector->vt_shadow_cells, projector->vt_tty.sx,
                                                (uint32_t)op->rect[0], view_rows, line_amount,
                                                default_fg, default_bg);
                    }
                    break;
                }
                if (rightward == 0 && op->c != -1 && downward != 0 && op->rect[2] == 0 &&
                    op->rect[3] == (int32_t)view_cols &&
                    (op->cell.codepoint == 1 || op->cell.codepoint == 2) &&
                    (downward > 0 ? projector->vt_tty.caps.delete_line
                                  : projector->vt_tty.caps.insert_line) &&
                    projector->vt_tty.caps.decstbm && op->rect[0] >= (op->c >> 16) &&
                    op->rect[1] == (op->c & 0xFFFF)) {
                    /* IL/DL INSIDE active margins (review #13): the rect runs
                     * from the insert row to the MARGIN bottom, but tmux sets
                     * the FULL margin region, moves to the app column on the
                     * insert row, and emits \e[nL/\e[nM (cursor invalidated
                     * after — the region reset in the tail then homes first,
                     * tmux's PuTTY workaround). */
                    int32_t margin_top = op->c >> 16;
                    int32_t margin_bottom = op->c & 0xFFFF;
                    int line_amount = downward;
                    while (sequence + 1 < op_head_now) {
                        const struct yetty_ymux_engine_op *next_op =
                            yetty_ymux_engine_op_at(engine, sequence + 1);
                        if (!next_op || next_op->type != YMUX_ENGINE_OP_SCROLLRECT ||
                            next_op->b != 0 || next_op->c != op->c ||
                            next_op->cell.codepoint != op->cell.codepoint ||
                            next_op->rect[0] != op->rect[0] || next_op->rect[1] != op->rect[1] ||
                            next_op->rect[2] != 0 || next_op->rect[3] != (int32_t)view_cols ||
                            (next_op->a > 0) != (downward > 0)) {
                            break;
                        }
                        line_amount += next_op->a;
                        ++sequence;
                    }
                    uint32_t line_count =
                        line_amount > 0 ? (uint32_t)line_amount : (uint32_t)(-line_amount);
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res)) {
                        write_res =
                            yetty_ymux_tty_region(&projector->vt_tty, out, (uint32_t)margin_top,
                                                  (uint32_t)(margin_bottom - 1));
                    }
                    if (YETTY_IS_OK(write_res)) {
                        /* The OPERATION-TIME cursor column (review #14): a
                         * later CUP in the same feed must not move where the
                         * insert/delete renders. */
                        write_res = vt_cursor(projector, out, op->cell.fg, (uint32_t)op->rect[0]);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_attributes(projector, out, default_fg, default_bg, 0);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        enum yetty_ymux_tty_term_slot edit_slot =
                            line_amount > 0 ? YMUX_TTY_TERM_DL : YMUX_TTY_TERM_IL;
                        write_res = vt_param_cap(projector, out, edit_slot, line_count,
                                                 line_amount > 0 ? 'M' : 'L');
                    }
                    projector->vt_tty.cx = UINT32_MAX;
                    projector->vt_tty.cy = UINT32_MAX;
                    if (YETTY_IS_OK(write_res) && projector->vt_shadow_cells) {
                        vt_shadow_scroll_region(projector->vt_shadow_cells, projector->vt_tty.sx,
                                                (uint32_t)op->rect[0], (uint32_t)margin_bottom,
                                                line_amount, default_fg, default_bg);
                    }
                    break;
                }
                if (rightward == 0 && op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols &&
                    op->rect[0] >= 0 && op->rect[1] > op->rect[0] &&
                    (uint32_t)op->rect[1] <= view_rows && downward != 0 &&
                    !projector->vt_tty.caps.decstbm) {
                    /* No change_scroll_region: tmux cannot scroll ANY region
                     * (full screen included) — it redraws the affected rows
                     * (tty_redraw_region). tmux's tty drains at quiescence,
                     * so the redraw shows the FINAL screen; every remaining
                     * op whose effect lies inside the redrawn rows is
                     * already reflected and is CONSUMED. An op outside the
                     * rows keeps the settled fallback (safety). */
                    /* IL/DL inside DECSTBM margins redraw the WHOLE margin
                     * region (tmux tty_redraw_region uses orupper..orlower),
                     * not just the scrolled rows. */
                    int32_t redraw_top = op->rect[0];
                    int32_t redraw_bottom = op->rect[1];
                    if ((op->cell.codepoint == 1 || op->cell.codepoint == 2) && op->c != -1) {
                        redraw_top = op->c >> 16;
                        redraw_bottom = op->c & 0xFFFF; /* packed exclusive */
                    }
                    /* Any UNSUPPORTED later op keeps the settled fallback. */
                    int fallback_needed = 0;
                    for (uint64_t look = sequence + 1; look < op_head_now; ++look) {
                        const struct yetty_ymux_engine_op *later =
                            yetty_ymux_engine_op_at(engine, look);
                        if (!later || (later->type != YMUX_ENGINE_OP_PUTGLYPH &&
                                       later->type != YMUX_ENGINE_OP_MOVECURSOR &&
                                       later->type != YMUX_ENGINE_OP_ERASE &&
                                       later->type != YMUX_ENGINE_OP_SCROLLRECT)) {
                            fallback_needed = 1;
                            break;
                        }
                    }
                    if (fallback_needed) {
                        window_fallback = 1;
                        break;
                    }
                    if ((uint32_t)(redraw_bottom - redraw_top) >= view_rows / 2) {
                        /* LARGE region (tmux tty_large_region): defer to ONE
                         * whole-pane redraw of the FINAL screen — tmux flags
                         * the pane and suppresses every later tty write, so
                         * the redraw lands at quiescence with final content
                         * and all remaining ops are absorbed by it. */
                        write_res = vt_replay_flush_pending(projector, out, pending_cells,
                                                            pending_used, view_rows, view_cols);
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_redraw_region_rows(projector, out, cells, 0, view_rows,
                                                              view_cols);
                        }
                        redraw_skip_top = 0;
                        redraw_skip_bottom = (int32_t)view_rows;
                        /* Whole-pane redraw closes like tmux's client
                         * redraw: sync first, cursor after. */
                        redraw_bracket = 1;
                        break;
                    }
                    /* SMALL region: tmux redraws IMMEDIATELY per command
                     * with the OPERATION-TIME screen (review #17 il-dl):
                     * coalesce the decomposed run of this one command,
                     * bring the shadow up to date (pending glyphs first,
                     * then the structural shift), redraw the region rows
                     * FROM the shadow, and keep replaying — later ops in
                     * the same delta emit normally, each csr-less scroll
                     * command producing its own redraw segment. */
                    int line_amount = downward;
                    while (sequence + 1 < op_head_now) {
                        const struct yetty_ymux_engine_op *next_op =
                            yetty_ymux_engine_op_at(engine, sequence + 1);
                        if (!next_op || next_op->type != YMUX_ENGINE_OP_SCROLLRECT ||
                            next_op->b != 0 || next_op->c != op->c ||
                            next_op->cell.codepoint != op->cell.codepoint ||
                            next_op->rect[0] != op->rect[0] || next_op->rect[1] != op->rect[1] ||
                            next_op->rect[2] != 0 || next_op->rect[3] != (int32_t)view_cols ||
                            (next_op->a > 0) != (downward > 0)) {
                            break;
                        }
                        line_amount += next_op->a;
                        ++sequence;
                    }
                    if (projector->vt_shadow_cells) {
                        for (size_t merge_slot = 0; merge_slot < (size_t)view_rows * view_cols;
                             ++merge_slot) {
                            if (pending_used[merge_slot]) {
                                projector->vt_shadow_cells[merge_slot] = pending_cells[merge_slot];
                            }
                        }
                    }
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res) && projector->vt_shadow_cells) {
                        vt_shadow_scroll_region(projector->vt_shadow_cells, projector->vt_tty.sx,
                                                (uint32_t)op->rect[0], (uint32_t)op->rect[1],
                                                line_amount, default_fg, default_bg);
                        write_res = vt_redraw_region_rows(
                            projector, out, projector->vt_shadow_cells, (uint32_t)redraw_top,
                            (uint32_t)redraw_bottom, view_cols);
                    }
                    break;
                }
                if (rightward == 0 && op->rect[2] == 0 && op->rect[3] == (int32_t)view_cols &&
                    op->rect[0] >= 0 && op->rect[1] > op->rect[0] &&
                    (uint32_t)op->rect[1] <= view_rows && downward != 0 &&
                    projector->vt_tty.caps.decstbm) {
                    /* Coalesce a run of identical-region same-direction scrolls
                     * (a burst of LFs): tmux's collect accumulates ctx->n and
                     * takes the INDN path for n>1. */
                    int scroll_amount = downward;
                    while (sequence + 1 < op_head_now) {
                        const struct yetty_ymux_engine_op *next_op =
                            yetty_ymux_engine_op_at(engine, sequence + 1);
                        if (!next_op || next_op->type != YMUX_ENGINE_OP_SCROLLRECT ||
                            next_op->b != 0 || next_op->rect[0] != op->rect[0] ||
                            next_op->rect[1] != op->rect[1] || next_op->rect[2] != 0 ||
                            next_op->rect[3] != (int32_t)view_cols ||
                            (next_op->a > 0) != (downward > 0)) {
                            break;
                        }
                        scroll_amount += next_op->a;
                        ++sequence;
                    }
                    /* DEFERRED bottom-right wrap (review #16): a pending
                     * glyph in the region's bottom-right cell means this
                     * scroll was caused by an autowrap off the corner. tmux
                     * scrolls FIRST, then writes the whole wrapped run at
                     * the pre-wrap origin (the client's own autowrap
                     * replays the wrap) and clears the fresh row's tail.
                     * Defer the pending flush: shift its rows up by the
                     * scroll and let the continuation glyphs join the run. */
                    int deferred_wrap = 0;
                    if (scroll_amount == 1 && op->rect[1] > op->rect[0] + 1) {
                        size_t corner_slot =
                            (size_t)(op->rect[1] - 1) * view_cols + (view_cols - 1);
                        if (pending_used[corner_slot]) {
                            deferred_wrap = 1;
                        }
                    }
                    if (!deferred_wrap) {
                        write_res = vt_replay_flush_pending(projector, out, pending_cells,
                                                            pending_used, view_rows, view_cols);
                    } else {
                        projector->vt_deferred_wrap_el = 1; /* before the scroll: suppresses
                                                             * its vacated-row EL */
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_replay_scroll(projector, out, (uint32_t)op->rect[0],
                                                     (uint32_t)op->rect[1], scroll_amount,
                                                     view_rows, cursor_col, default_fg, default_bg);
                    }
                    if (deferred_wrap && YETTY_IS_OK(write_res)) {
                        for (int32_t shift_row = op->rect[0] + 1; shift_row < op->rect[1];
                             ++shift_row) {
                            memcpy(&pending_cells[(size_t)(shift_row - 1) * view_cols],
                                   &pending_cells[(size_t)shift_row * view_cols],
                                   (size_t)view_cols * sizeof(struct yetty_ymux_cell));
                            memcpy(&pending_used[(size_t)(shift_row - 1) * view_cols],
                                   &pending_used[(size_t)shift_row * view_cols], view_cols);
                        }
                        memset(&pending_used[(size_t)(op->rect[1] - 1) * view_cols], 0, view_cols);
                    }
                    break;
                }
                if (rightward != 0 && op->rect[1] == op->rect[0] + 1 && op->rect[0] >= 0 &&
                    (uint32_t)op->rect[0] < view_rows && op->rect[3] == (int32_t)view_cols &&
                    op->rect[2] >= 0 && (rightward > 0 ? 1 : projector->vt_tty.caps.ich)) {
                    /* DCH always takes this path: parm_dch present → \e[nP,
                     * cancelled → dch1 repeat (vt_param_cap). ICH is gated on
                     * caps.ich — a cancelled insert-char falls to the window
                     * redraw (tmux redraws rather than repeat-inserting). */
                    /* Single-row tail shift: ICH (rightward<0) / DCH (>0), taken
                     * only when the model HAS that capability — otherwise this
                     * condition fails and the op falls through to the window
                     * redraw fallback rather than emitting a raw CSI for a
                     * cancelled cap (cycle-25 P0). The inserted content follows
                     * as its own PUTGLYPH ops. */
                    uint32_t magnitude =
                        rightward > 0 ? (uint32_t)rightward : (uint32_t)(-rightward);
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res)) {
                        write_res =
                            vt_cursor(projector, out, (uint32_t)op->rect[2], (uint32_t)op->rect[0]);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_attributes(projector, out, default_fg, default_bg, 0);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        enum yetty_ymux_tty_term_slot edit_slot =
                            rightward > 0 ? YMUX_TTY_TERM_DCH : YMUX_TTY_TERM_ICH;
                        write_res = vt_param_cap(projector, out, edit_slot, magnitude,
                                                 rightward > 0 ? 'P' : '@');
                    }
                    break;
                }
                if (rightward == 0 && op->a > 0 && projector->vt_tty.caps.margins &&
                    op->rect[0] >= 0 && op->rect[1] > op->rect[0] &&
                    (uint32_t)op->rect[1] <= view_rows && op->rect[2] >= 0 &&
                    op->rect[3] > op->rect[2] && (uint32_t)op->rect[3] <= view_cols &&
                    (op->rect[2] != 0 || op->rect[3] != (int32_t)view_cols)) {
                    /* PARTIAL-WIDTH vertical scroll on a margins-capable client:
                     * tmux tty_cmd_scrollup's DECSLRM path — pen to the erase
                     * background, region to the rect rows, DECSLRM to the rect
                     * columns, then \n / indn inside the margins. The flush
                     * epilogue restores margins + region (tmux
                     * server_client_reset_state order). Without caps.margins
                     * this falls through to the window redraw, exactly as tmux
                     * redraws for a non-DECSLRM client. */
                    write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                        view_rows, view_cols);
                    if (YETTY_IS_OK(write_res)) {
                        write_res = vt_erase_attributes(projector, out, default_bg);
                    }
                    if (YETTY_IS_OK(write_res)) {
                        write_res = yetty_ymux_tty_margin_scrollup(
                            &projector->vt_tty, out, (uint32_t)op->rect[0],
                            (uint32_t)(op->rect[1] - 1), (uint32_t)op->rect[2],
                            (uint32_t)(op->rect[3] - 1), (uint32_t)op->a);
                    }
                    break;
                }
                if (rightward < 0 && !projector->vt_tty.caps.ich && pending_used &&
                    op->rect[1] == op->rect[0] + 1 && op->rect[0] >= 0 &&
                    (uint32_t)op->rect[0] < view_rows && op->rect[3] == (int32_t)view_cols &&
                    op->rect[2] >= 0) {
                    /* ICH cancelled (ich@): tmux cannot shift with \e[<n>@, so
                     * tty_cmd_insertcharacter redraws the whole row inside a
                     * cursor-hide bracket; the flush epilogue then repositions to
                     * the insert column and re-shows the cursor. The delta path's
                     * move-then-show order (below) matches tmux byte-for-byte.
                     * Only a PURE shift takes this path — a shift mixed with new
                     * text pending on the same row would double-emit, so it falls
                     * through to the window redraw. */
                    uint32_t ins_row = (uint32_t)op->rect[0];
                    int row_has_pending = 0;
                    for (uint32_t col = 0; col < view_cols; ++col) {
                        if (pending_used[(size_t)ins_row * view_cols + col]) {
                            row_has_pending = 1;
                            break;
                        }
                    }
                    if (!row_has_pending) {
                        write_res = vt_replay_flush_pending(projector, out, pending_cells,
                                                            pending_used, view_rows, view_cols);
                        if (YETTY_IS_OK(write_res)) {
                            write_res = vt_redraw_region_rows(projector, out, cells, ins_row,
                                                              ins_row + 1, view_cols);
                        }
                        break;
                    }
                }
                window_fallback = 1;
                break;
            }
            case YMUX_ENGINE_OP_INVALIDATE:
            case YMUX_ENGINE_OP_MOVERECT:
            default:
                window_fallback = 1;
                break;
            }
        }
        if (!window_fallback && YETTY_IS_OK(write_res)) {
            write_res = vt_replay_flush_pending(projector, out, pending_cells, pending_used,
                                                view_rows, view_cols);
        }
        if (window_fallback && YETTY_IS_OK(write_res)) {
            /* Fallback: plain settled-cell diff against the shadow — correct
             * bytes, not tmux-ordered (the window's history was unusable). */
            out->size = delta_base_size;
            projector->vt_tty = delta_base_tty;
            for (uint32_t row = 0; YETTY_IS_OK(write_res) && row < view_rows; ++row) {
                const struct yetty_ymux_cell *cur_row = cells + (size_t)row * view_cols;
                const struct yetty_ymux_cell *shadow_row =
                    projector->vt_shadow_cells + (size_t)row * view_cols;
                for (uint32_t col = 0; YETTY_IS_OK(write_res) && col < view_cols; ++col) {
                    const struct yetty_ymux_cell *cell = &cur_row[col];
                    if (cell->width == 0) {
                        continue;
                    }
                    if (paint_cells_equal(cell, &shadow_row[col])) {
                        continue;
                    }
                    write_res =
                        vt_replay_emit_cell(projector, out, cell, &shadow_row[col], col, row);
                }
            }
        }
        free(pending_cells);
        free(pending_used);
    }

    /* Final cursor position + visibility. A FULL redraw drew every row inside a
     * civis bracket; close it with cnorm FIRST (byte-parity with tmux's redraw
     * segment, which carries no reposition), THEN move to the app cursor OUTSIDE
     * that segment so the client's cursor is correct — tmux likewise repositions
     * after the redraw. A DELTA moves first, then adjusts visibility. */
    int want_shown = (cursor_visible && following) ? 1 : 0;
    /* Wrap-pending phantom column: the pane cursor is unrepresentable there,
     * so tmux HIDES the client cursor instead of positioning it. Mirror that;
     * the visibility cache re-shows once the cursor leaves the phantom. */
    int cursor_phantom = !full && yetty_ymux_engine_cursor_phantom(engine);
    if (cursor_phantom) {
        want_shown = 0;
    }
    if (!full && projector->vt_pending_scrollbar_wrap && YETTY_IS_OK(write_res) && want_shown) {
        /* tmux's post-SU scrollbar redraw pass: paints nothing, emits the
         * cursor wrap. Net visibility unchanged — the cache stays as-is. */
        write_res = vt_puts(out, "\x1b[?25l\x1b[?12l\x1b[?25h");
    }
    projector->vt_pending_scrollbar_wrap = 0;
    /* Region off first (tmux server_client_reset_state → tty_region_off):
     * a delta that set DECSTBM margins puts them back to full-screen before
     * the final cursor move — which then comes out absolute, since resetting
     * the region invalidates the cursor. No-op when already full. Then
     * MARGINS off (tmux's exact reset order: tty_region_off, tty_margin_off)
     * — a delta that narrowed DECSLRM restores full width; no-op on a
     * non-margins profile or when already full. */
    if (YETTY_IS_OK(write_res) && view_rows > 0) {
        write_res = yetty_ymux_tty_region(&projector->vt_tty, out, 0, view_rows - 1);
    }
    if (YETTY_IS_OK(write_res)) {
        write_res = yetty_ymux_tty_margin_off(&projector->vt_tty, out);
    }
    if (full || redraw_bracket) {
        if (YETTY_IS_OK(write_res)) {
            write_res = yetty_ymux_tty_cursor_visible(&projector->vt_tty, out, want_shown);
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = vt_cursor(projector, out, cursor_col, cursor_row);
        }
    } else {
        if (YETTY_IS_OK(write_res) && !cursor_phantom) {
            write_res = vt_cursor(projector, out, cursor_col, cursor_row);
        }
        if (YETTY_IS_OK(write_res)) {
            write_res = yetty_ymux_tty_cursor_visible(&projector->vt_tty, out, want_shown);
        }
    }
    /* End-of-flush pen reset (tmux runs tty_reset after every flush window):
     * a projection that ends with a non-default pen leaves the client at
     * default, byte-identically (\e(B\e[m). No-op when already default. */
    if (YETTY_IS_OK(write_res)) {
        write_res = yetty_ymux_tty_pen_reset(&projector->vt_tty, out);
    }
    /* Cursor STYLE (DECSCUSR) — the client grid renders block/underline/bar per
     * this; emitted only when the shape/blink changes (the tty tracks the last
     * parameter). Ask the engine for the app's current style. */
    if (YETTY_IS_OK(write_res)) {
        int cursor_shape = 0, cursor_blink = 0;
        struct yetty_ycore_void_result style_res =
            yetty_ymux_engine_cursor_style(engine, &cursor_shape, &cursor_blink);
        if (YETTY_IS_OK(style_res)) {
            write_res =
                yetty_ymux_tty_cursor_shape(&projector->vt_tty, out, cursor_shape, cursor_blink);
        } else {
            yetty_ycore_error_destroy(style_res.error);
        }
    }

    if (YETTY_IS_ERR(write_res)) {
        /* Partial redraw: the daemon discards this frame, so resync from a
         * FULL next time rather than trust a half-updated client. */
        projector->vt_shadow_valid = 0;
        free(cells);
        free(continuations);
        return YETTY_ERR(yetty_ycore_void, "ymux project_vt: write", write_res);
    }

    /* Synchronized output around an INCREMENTAL flush (tmux tty_cmd_syncstart):
     * tmux brackets a live screen-write flush in ?2026h/?2026l when it carries a
     * structural op — IL/DL, SU/SD/RI, ED, EL (is_sync=1 in screen-write.c) —
     * but not a plain glyph/ICH/DCH/ECH flush. Detect one of those finals in the
     * delta and, for a sync-capable client, wrap the whole flush: ?2026h at the
     * flush start, ?2026l at the end. The attach preamble and full redraws carry
     * their own markers, so this only runs on the pure incremental path. */
    if (!full && !preamble_ran && projector->vt_tty.caps.sync && out->size > delta_base_size) {
        /* Walk the delta, skipping escape sequences, to classify the flush:
         *   has_always = IL/DL (L/M) or SU/SD (S/T) or RI (ESC M) — tmux always
         *                sync-wraps these (is_sync=1, never collected);
         *   has_erase  = ED/EL (J/K) — sync-wrapped ONLY when the flush emits no
         *                glyph (a glyph collect-flushes the erase, is_sync=0);
         *   has_glyph  = any printable byte written outside an escape sequence. */
        int has_always = 0, has_erase = 0, has_glyph = 0;
        size_t scan = delta_base_size;
        while (scan < out->size) {
            uint8_t byte = out->data[scan];
            if (byte == 0x1b) {
                if (scan + 1 >= out->size) {
                    break;
                }
                uint8_t intro = out->data[scan + 1];
                if (intro == '[') {
                    size_t look = scan + 2;
                    while (look < out->size &&
                           !(out->data[look] >= 0x40 && out->data[look] <= 0x7e)) {
                        ++look;
                    }
                    if (look >= out->size) {
                        break;
                    }
                    uint8_t final = out->data[look];
                    if (final == 'L' || final == 'M' || final == 'S' || final == 'T') {
                        has_always = 1;
                    } else if (final == 'J' || final == 'K') {
                        has_erase = 1;
                    }
                    scan = look + 1;
                } else if (intro == ']') {
                    /* OSC — skip to BEL or ST (ESC \) so the payload is not glyphs */
                    size_t look = scan + 2;
                    while (look < out->size && out->data[look] != 0x07 &&
                           !(out->data[look] == 0x1b && look + 1 < out->size &&
                             out->data[look + 1] == '\\')) {
                        ++look;
                    }
                    scan = (look < out->size && out->data[look] == 0x07) ? look + 1 : look + 2;
                } else if (intro == 'M') {
                    has_always = 1; /* RI */
                    scan += 2;
                } else {
                    scan += 2; /* ESC ( X, ESC = , ESC ) X, ... — not a glyph */
                }
                continue;
            }
            if (byte >= 0x20 && byte != 0x7f) {
                has_glyph = 1;
            }
            ++scan;
        }
        int structural = has_always || (has_erase && !has_glyph);
        if (structural) {
            static const char sync_begin[] = "\x1b[?2026h";
            static const char sync_end[] = "\x1b[?2026l";
            size_t delta_len = out->size - delta_base_size;
            /* grow + shift the delta forward to make room for the BEGIN marker */
            struct yetty_ycore_void_result grow = yetty_ycore_buffer_write(out, sync_begin, 8);
            if (YETTY_IS_OK(grow)) {
                memmove(out->data + delta_base_size + 8, out->data + delta_base_size, delta_len);
                memcpy(out->data + delta_base_size, sync_begin, 8);
                write_res = yetty_ycore_buffer_write(out, sync_end, 8);
            } else {
                write_res = grow;
            }
            if (YETTY_IS_ERR(write_res)) {
                projector->vt_shadow_valid = 0;
                free(cells);
                free(continuations);
                return YETTY_ERR(yetty_ycore_void, "ymux project_vt: sync wrap", write_res);
            }
        }
    }

    projector->vt_op_consumed = op_head_now;
    /* The gathered cells become the new VT shadow (diff base for the next
     * incremental redraw). */
    memcpy(projector->vt_shadow_cells, cells,
           (size_t)view_rows * view_cols * sizeof(struct yetty_ymux_cell));
    projector->vt_shadow_valid = 1;
    projector->vt_shadow_view_top = view_top;
    projector->vt_shadow_following = following;
    free(cells);
    free(continuations);
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ymux/projector.c"
