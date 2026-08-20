/* vtermgrid-store.h — the CLUSTER-PRESERVING terminal cell store (#699.5).
 *
 * The read-only libvterm fork's screen layer drops combining marks (its
 * 16-byte screen cell holds a single glyph slot), so the vtermgrid will cut
 * over from vterm_obtain_screen to STATE callbacks backed by this store —
 * the same pattern the ymux engine uses on the daemon side, but simpler: the
 * projector feeds a flattened view, so there is no alt-screen, no history,
 * and no identity minting here.
 *
 * The store is a PURE MODEL: cells + pen + cursor + the canonical mutation
 * operations (put, erase, scroll, move, resize). It has no libvterm and no
 * GPU dependency, so every cutover-critical semantic — mark retention, wide
 * head/continuation shape, pen application, reverse resolution at read time
 * (cell attr XOR screen-wide DECSCNM) — is pinned by direct unit tests
 * BEFORE the live-path swap. Module-private (the tty-render.h pattern). */

#ifndef YETTY_YSCENE_VTERMGRID_STORE_H
#define YETTY_YSCENE_VTERMGRID_STORE_H

#include <yetty/ycore/result.h>

#include <stdint.h>

enum { YETTY_YSCENE_VTERMGRID_STORE_MAX_MARKS = 5 };

/* DECSCA protection (review #11): stamped into cell attrs at put time from
 * the pen; selective erase (DECSEL/DECSED) skips protected cells. Bit chosen
 * above the SGR attribute bits the grid packs. */
enum { YETTY_YSCENE_VTERMGRID_STORE_ATTR_PROTECTED = 1u << 9 };

/* One stored cell. Colors are packed 0xAABBGGRR, pen-applied at write time
 * but NOT reverse-resolved — reads resolve reverse so the render contract
 * matches the fork's screen layer (which pre-swaps). width: 1 normal,
 * 2 wide head, 0 wide continuation. */
struct yetty_yscene_vtermgrid_store_cell {
    uint32_t codepoint; /* 0 = blank */
    uint32_t marks[YETTY_YSCENE_VTERMGRID_STORE_MAX_MARKS];
    uint8_t mark_count;
    uint8_t width;
    uint16_t attrs; /* YETTY_YSCENE_VTERMGRID_ATTR_* bits */
    uint32_t fg;
    uint32_t bg;
};

struct yetty_yscene_vtermgrid_store {
    struct yetty_yscene_vtermgrid_store_cell *cells; /* rows × cols, row-major */
    uint32_t rows;
    uint32_t cols;

    /* Pen mirror (applied to cells at put/erase time). */
    uint32_t pen_fg;
    uint32_t pen_bg;
    uint16_t pen_attrs;
    int pen_protected; /* DECSCA: subsequent puts mark cells protected */
    uint32_t default_fg;
    uint32_t default_bg;

    /* Screen-wide DECSCNM reverse (\e[?5h): XORs with the per-cell reverse
     * attribute at read time. */
    int global_reverse;

    uint32_t cursor_row;
    uint32_t cursor_col;
    int cursor_visible;
};

/* Lifecycle. init allocates rows×cols blanks with the given defaults; free
 * releases the cell array (the struct itself is caller-owned). */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_init(
    struct yetty_yscene_vtermgrid_store *store, uint32_t rows, uint32_t cols, uint32_t default_fg,
    uint32_t default_bg);
void yetty_yscene_vtermgrid_store_free(struct yetty_yscene_vtermgrid_store *store);

/* Canonical mutations (the future state-callback bodies). */
struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_put(
    struct yetty_yscene_vtermgrid_store *store, uint32_t row, uint32_t col,
    const uint32_t *codepoints, uint32_t codepoint_count, int wide);
void yetty_yscene_vtermgrid_store_erase(struct yetty_yscene_vtermgrid_store *store,
                                        uint32_t start_row, uint32_t end_row, uint32_t start_col,
                                        uint32_t end_col);
/* Selective (DECSEL/DECSED) erase: protected cells survive. selective == 0
 * behaves exactly like the plain erase. */
void yetty_yscene_vtermgrid_store_erase_selective(struct yetty_yscene_vtermgrid_store *store,
                                                  uint32_t start_row, uint32_t end_row,
                                                  uint32_t start_col, uint32_t end_col,
                                                  int selective);
/* Full-width vertical scroll: positive = content moves up (SU). Vacated rows
 * blank with the current pen (BCE). */
void yetty_yscene_vtermgrid_store_scroll(struct yetty_yscene_vtermgrid_store *store, int amount);
/* Horizontal single-row shift at start_col (ICH positive = cells move right,
 * DCH negative = cells move left); vacated cells blank with the pen (BCE). */
void yetty_yscene_vtermgrid_store_hshift(struct yetty_yscene_vtermgrid_store *store, uint32_t row,
                                         uint32_t start_col, int amount);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_store_resize(
    struct yetty_yscene_vtermgrid_store *store, uint32_t rows, uint32_t cols);

/* Read one cell with the RENDER contract applied: reverse resolved (attr XOR
 * DECSCNM swaps fg/bg — matching the fork's screen layer, which pre-swaps),
 * marks retained. Out-of-range reads yield a default blank. */
void yetty_yscene_vtermgrid_store_read(const struct yetty_yscene_vtermgrid_store *store,
                                       uint32_t row, uint32_t col,
                                       struct yetty_yscene_vtermgrid_store_cell *out);

#endif /* YETTY_YSCENE_VTERMGRID_STORE_H */
