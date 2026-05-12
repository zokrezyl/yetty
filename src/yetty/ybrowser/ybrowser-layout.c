/*
 * ylexbor-layout — naive block flow + line wrapping.
 *
 * Block boxes stack vertically. Inline-text children of a block are
 * wrapped into lines based on naive font metrics (per-glyph width =
 * font_size * 0.55 — same shortcut ynetsurf uses; FreeType integration
 * is a follow-up). Each laid-out line replaces its source
 * YL_BOX_INLINE_TEXT box's geometry; if a single text box wraps into N
 * lines we *split* it into N inline-text boxes so the paint pass can
 * emit one TEXT_SPAN per line.
 *
 * Margin collapsing: vertical margins between adjacent block siblings
 * collapse to the larger of the two. Padding / horizontal margins are
 * not yet wired through.
 *
 * What's deliberately omitted:
 *   - Floats, position:absolute/fixed/sticky
 *   - Flex / Grid (those elements layout as plain block — children
 *     stack vertically, which looks broken for flex rows but at least
 *     doesn't crash)
 *   - Tables (layout as block, again broken for real tables)
 *   - CSS width / max-width / min-width (everything fills the parent)
 *   - Box-sizing variations
 */

#include "ybrowser-internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lexbor/tag/const.h>
#include <libcss/libcss.h>

#include <yetty/ytrace/ytrace.h>

/* Forward decls — recursive. */
static float layout_block(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                          float content_w);
static float wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                             float content_w, int text_align);

/* ---------------------------------------------------------------------------
 * Wrap one inline-text box into one-or-more lines. Replaces the original
 * box in-place and inserts additional sibling boxes for the extra
 * lines. Returns the total height (line_count * line_height).
 * -------------------------------------------------------------------------*/

/* Helper — return the seg index covering byte offset `off`. Linear walk
 * from `start_hint` since segs are appended in order. */
static size_t seg_index_at(const struct yetty_ylexbor_inline_seg *segs, size_t segs_count,
                           size_t n, size_t off, size_t start_hint)
{
    if (segs_count == 0) {
        return 0;
    }
    size_t i = start_hint < segs_count ? start_hint : 0;
    while (i + 1 < segs_count && segs[i + 1].start <= off) {
        i++;
    }
    (void)n;
    return i;
}

/* Emit a per-line, per-segment sub-box. Returns the new box index or
 * UINT32_MAX on alloc failure. If `reuse_idx` is non-zero, the function
 * writes into r->boxes.data[reuse_idx] instead of allocating; this lets
 * the first emitted fragment land in the source INLINE_TEXT box that
 * `flush_inline` already created (preserving DOM-child linkage). */
static uint32_t emit_fragment(struct yetty_ylexbor *r, uint32_t reuse_idx, float x, float y,
                              float w, float h, float font_size, const char *text, size_t text_len,
                              const struct yetty_ylexbor_inline_seg *seg)
{
    uint32_t fidx;
    struct yetty_ylexbor_box *target;
    if (reuse_idx != 0) {
        fidx = reuse_idx;
        target = &r->boxes.data[fidx];
        /* Preserve element / next_sibling / first_child relationships
		 * already set on the source box (we're reusing it for the
		 * first fragment of the first line). */
        target->kind = YL_BOX_INLINE_TEXT;
    } else {
        struct yetty_ycore_void_result rr =
            _yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
        if (YETTY_IS_ERR(rr)) {
            return UINT32_MAX;
        }
        fidx = r->boxes.size++;
        target = &r->boxes.data[fidx];
        memset(target, 0, sizeof(*target));
        target->kind = YL_BOX_INLINE_TEXT;
    }
    target->text = text;
    target->text_len = text_len;
    target->x = x;
    target->y = y;
    target->w = w;
    target->h = h;
    target->font_size = font_size;
    target->font_weight = seg->font_weight;
    target->font_italic = seg->font_italic;
    target->fg = seg->fg;
    target->underline = seg->underline;
    return fidx;
}

static float wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                             float content_w, int text_align)
{
    /* Snapshot stable pointers/values BEFORE we start allocating new
	 * boxes — emit_fragment grows the box vector, which can realloc
	 * r->boxes.data and invalidate any cached struct pointer. text and
	 * segs live in separate heaps (text_chunks, segs malloc'd by
	 * flush_inline) so the snapshots stay valid across allocations. */
    struct yetty_ylexbor_box *b = &r->boxes.data[idx];
    const char *text = b->text;
    size_t n = b->text_len;
    float font_size = b->font_size;
    float line_height = font_size * 1.25f; /* CSS default normal */
    const struct yetty_ylexbor_inline_seg *segs = b->segs;
    size_t segs_count = b->segs_count;
    /* Fallback: if the box wasn't built with segments (older path /
	 * direct box mutation), synthesise a single segment from the box's
	 * own style fields so the per-fragment emit loop stays uniform. */
    struct yetty_ylexbor_inline_seg fallback = {
        .start = 0,
        .fg = b->fg,
        .font_weight = b->font_weight,
        .font_italic = b->font_italic,
        .underline = false,
    };
    if (segs_count == 0) {
        segs = &fallback;
        segs_count = 1;
    }

    /* Pre-compute per-glyph width once — naive uniform for now. */
    float per_glyph = font_size * 0.55f;
    if (per_glyph < 1.0f) {
        per_glyph = 1.0f;
    }

    float y = origin_y;
    int first_fragment_emitted = 0;
    size_t seg_hint = 0;

    /* `cursor` walks the original string. For each line we find the
	 * largest prefix [cursor..end) whose visual width fits in
	 * content_w, prefer breaking at the last space, then emit one
	 * sub-box per styled segment-portion within that line so painted
	 * runs carry per-segment fg/weight/italic/underline. */
    size_t cursor = 0;
    while (cursor < n) {
        /* Skip leading space on every wrapped line except the first
		 * (we want left-aligned blocks; the first line we keep as-is
		 * to preserve any author-intended spacing). For pre-formatted
		 * content the run starts with a '\n' (because flush_inline
		 * preserved every byte) — never strip those, they're real
		 * line breaks. */
        if (first_fragment_emitted && cursor < n && text[cursor] != '\n') {
            while (cursor < n && text[cursor] == ' ') {
                cursor++;
            }
            if (cursor >= n) {
                break;
            }
        }
        /* A '\n' at the cursor is an explicit line break — skip past
		 * it and let the rest of the loop emit the following line. */
        if (cursor < n && text[cursor] == '\n') {
            cursor++;
        }

        /* How many bytes fit in content_w? Stop at the next '\n'
		 * regardless of remaining horizontal space so source line
		 * breaks survive verbatim. */
        size_t fit = 0;
        float acc = 0.0f;
        size_t last_break = 0;
        size_t hard_break = 0;
        for (size_t k = cursor; k < n;) {
            unsigned char c = (unsigned char)text[k];
            if (c == '\n') {
                hard_break = k;
                fit = k;
                break;
            }
            size_t step;
            if (c < 0x80) {
                step = 1;
            } else if ((c & 0xE0) == 0xC0) {
                step = 2;
            } else if ((c & 0xF0) == 0xE0) {
                step = 3;
            } else if ((c & 0xF8) == 0xF0) {
                step = 4;
            } else {
                step = 1;
            }
            if (acc + per_glyph > content_w && k > cursor) {
                break;
            }
            acc += per_glyph;
            k += step;
            fit = k;
            if (c == ' ') {
                last_break = k;
            }
        }
        size_t end;
        if (hard_break > 0) {
            end = hard_break;
        } else {
            end = (last_break > cursor && fit < n) ? last_break : fit;
        }
        if (end == cursor && hard_break == 0) {
            end = (cursor + 1 <= n) ? cursor + 1 : n;
        }

        /* Empty line (e.g. consecutive newlines in <pre>) — advance y
		 * without emitting. The orphan empty box from before would
		 * have rendered nothing; skipping the emit saves a vector
		 * entry too. */
        if (end <= cursor) {
            y += line_height;
            cursor = end;
            continue;
        }

        /* Total line width — used for text-align translation only. */
        float line_w = yetty_ylexbor_naive_text_width(text + cursor, end - cursor, font_size);
        float line_origin_x = origin_x;
        if (text_align == 1) {
            line_origin_x = origin_x + (content_w - line_w) * 0.5f;
        } else if (text_align == 2) {
            line_origin_x = origin_x + (content_w - line_w);
        }
        if (line_origin_x < origin_x) {
            line_origin_x = origin_x;
        }

        /* Emit one fragment per segment that overlaps [cursor, end). */
        float frag_x = line_origin_x;
        size_t si = seg_index_at(segs, segs_count, n, cursor, seg_hint);
        while (si < segs_count) {
            size_t seg_start = segs[si].start;
            size_t seg_end = (si + 1 < segs_count) ? segs[si + 1].start : n;
            size_t s0 = seg_start > cursor ? seg_start : cursor;
            size_t s1 = seg_end < end ? seg_end : end;
            if (s0 >= s1) {
                if (seg_end >= end) {
                    break;
                }
                si++;
                continue;
            }
            float frag_w =
                yetty_ylexbor_naive_text_width(text + s0, s1 - s0, font_size);
            uint32_t reuse = first_fragment_emitted ? 0 : idx;
            (void)emit_fragment(r, reuse, frag_x, y, frag_w, line_height, font_size,
                                text + s0, s1 - s0, &segs[si]);
            first_fragment_emitted = 1;
            frag_x += frag_w;
            seg_hint = si;
            if (seg_end >= end) {
                break;
            }
            si++;
        }

        y += line_height;
        cursor = end;
    }

    /* Re-fetch — vector may have moved. */
    b = &r->boxes.data[idx];
    if (!first_fragment_emitted) {
        /* Whole text was empty / all whitespace — collapse the box. */
        b->x = origin_x;
        b->y = origin_y;
        b->w = 0;
        b->h = 0;
    }
    /* The segments have been consumed (baked into each fragment's own
	 * style fields); drop them so destroy doesn't double-free and the
	 * paint pass doesn't try to re-interpret the source box's segs. */
    if (b->segs) {
        free(b->segs);
        b->segs = NULL;
        b->segs_count = 0;
    }

    return y - origin_y;
}

/* ---------------------------------------------------------------------------
 * Flex container — single function handling both row and column directions.
 * Resolves each item's main-axis size from flex-basis + flex-grow, lays
 * them out along the main axis honoring justify-content, and stretches
 * (default align-items=stretch) or aligns on the cross axis.
 *
 * What's modelled:
 *   - flex-direction: row / column
 *   - flex-grow: distributes leftover main-axis space proportionally
 *   - flex-basis: starting main-axis size; auto falls back to css_width
 *     (row) / css_height (column), or 0 if neither
 *   - justify-content: flex-start / flex-end / center / space-between /
 *     space-around / space-evenly
 *   - align-items: stretch (default) / flex-start / flex-end / center
 *
 * Not modelled (deferred):
 *   - flex-wrap (we never wrap; items overflow the container)
 *   - flex-shrink (we don't shrink overflowing items)
 *   - row-reverse / column-reverse (treated as row / column for now)
 *   - align-self (per-item cross-axis override)
 *
 * For column direction without an explicit css_height, the container's
 * main-axis budget is undefined — we treat it as the sum of items'
 * basis values (no growth) so a vertical flex without height looks
 * identical to plain block stacking, which is what the surrounding
 * pages expect.
 * -------------------------------------------------------------------------*/

#define YL_FLEX_MAX_CHILDREN 256

static float layout_flex(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                         float content_w, bool column_dir)
{
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];
    float pad_left = self->padding_left;
    float pad_right = self->padding_right;
    float pad_top = self->padding_top;
    float pad_bottom = self->padding_bottom;
    int justify = self->justify_content;
    int align = self->align_items;
    float css_h = self->css_height;
    float content_origin_x = origin_x + pad_left;
    float content_origin_y = origin_y + pad_top;
    float content_width = content_w - pad_left - pad_right;
    if (content_width < 0) {
        content_width = 0;
    }

    /* Collect direct in-flow children (skip floats — they're laid out
	 * separately and don't participate in the flex line). */
    uint32_t children[YL_FLEX_MAX_CHILDREN];
    uint32_t n_children = 0;
    for (uint32_t cidx = self->first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        if (n_children >= YL_FLEX_MAX_CHILDREN) {
            break;
        }
        if (r->boxes.data[cidx].float_side != 0) {
            continue;
        }
        children[n_children++] = cidx;
    }
    if (n_children == 0) {
        return pad_top + pad_bottom;
    }

    /* Main-axis budget. */
    float main_budget;
    if (column_dir) {
        main_budget = css_h > 0 ? css_h : 0;
    } else {
        main_budget = content_width;
    }

    /* Resolve each child's hypothetical main-axis size. */
    float main_size[YL_FLEX_MAX_CHILDREN];
    float total_basis = 0.0f;
    float total_grow = 0.0f;
    int autobasis_count = 0;
    for (uint32_t i = 0; i < n_children; i++) {
        struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
        float basis = c->flex_basis_px;
        if (basis < 0.0f) {
            /* auto — fall back to explicit width/height (the CSS
			 * "main size" property). When neither is set, we'd
			 * normally use the item's intrinsic content size; we
			 * don't compute that today, so leave it as 0 and let
			 * the auto-basis fallback below distribute space. */
            basis = column_dir ? c->css_height : c->css_width;
            if (basis < 0.0f) {
                basis = 0.0f;
            }
            if (basis == 0.0f) {
                autobasis_count++;
            }
        }
        main_size[i] = basis;
        total_basis += basis;
        if (c->flex_grow > 0.0f) {
            total_grow += c->flex_grow;
        }
    }

    /* Auto-basis fallback. If every item has basis=auto AND none of
	 * them grow, splitting evenly is the safest reading of the spec
	 * for our case: it gives sensible widths to header bars / nav
	 * rows whose authors leaned on flex semantics without setting
	 * explicit basis or grow. Without this, items would collapse to
	 * 0 width and content would wrap to one glyph per line. */
    if (autobasis_count == (int)n_children && total_grow == 0.0f && main_budget > 0.0f) {
        float per = main_budget / (float)n_children;
        for (uint32_t i = 0; i < n_children; i++) {
            main_size[i] = per;
        }
        total_basis = main_budget;
    }

    /* Distribute leftover main-axis space. */
    float leftover = main_budget - total_basis;
    if (leftover > 0.0f && total_grow > 0.0f) {
        for (uint32_t i = 0; i < n_children; i++) {
            struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
            if (c->flex_grow > 0.0f) {
                main_size[i] += leftover * (c->flex_grow / total_grow);
            }
        }
        leftover = 0.0f;
    }

    /* If anything's still left over, distribute via justify-content. */
    float leading = 0.0f;
    float gap = 0.0f;
    if (leftover > 0.0f) {
        switch (justify) {
        case CSS_JUSTIFY_CONTENT_FLEX_END:
            leading = leftover;
            break;
        case CSS_JUSTIFY_CONTENT_CENTER:
            leading = leftover * 0.5f;
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_BETWEEN:
            gap = n_children > 1 ? leftover / (float)(n_children - 1) : 0.0f;
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_AROUND:
            gap = leftover / (float)n_children;
            leading = gap * 0.5f;
            break;
        case CSS_JUSTIFY_CONTENT_SPACE_EVENLY:
            gap = leftover / (float)(n_children + 1);
            leading = gap;
            break;
        default:
            break;
        }
    }

    /* Place children. Cross-axis position depends on align-items;
	 * stretch (default) gives every item the full cross extent of
	 * the container. */
    float cross_budget = column_dir ? content_width : (css_h > 0 ? css_h : 0);
    float natural_h[YL_FLEX_MAX_CHILDREN];
    float natural_w[YL_FLEX_MAX_CHILDREN];
    /* First pass — lay each child at its resolved main-size, learn
	 * natural cross-size. */
    float cursor = (column_dir ? content_origin_y : content_origin_x) + leading;
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        if (column_dir) {
            c->x = content_origin_x;
            c->y = cursor;
            c->w = content_width;
            c->h = main_size[i];
        } else {
            c->x = cursor;
            c->y = content_origin_y;
            c->w = main_size[i];
            c->h = 0;
        }
        float h = 0.0f;
        if (c->kind == YL_BOX_BLOCK) {
            h = layout_block(r, cidx, c->x, c->y, c->w);
        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            h = wrap_inline_box(r, cidx, c->x, c->y, c->w, /*text_align=*/0);
        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            h = c->h > 0 ? c->h : 100;
        }
        c = &r->boxes.data[cidx];
        if (column_dir) {
            /* For column, the main-axis size is what we set; the
			 * recursive layout returns the natural content height
			 * which we ignore (item fills the slot via main_size). */
            c->h = main_size[i];
        } else {
            c->h = h;
        }
        natural_h[i] = c->h;
        natural_w[i] = c->w;
        cursor += (column_dir ? main_size[i] : main_size[i]) + gap;
    }

    /* Cross-axis: tallest item dictates row height (or container's
	 * css_height when set). For align-items=stretch (default) we
	 * normalise every item to that cross extent. */
    float max_cross = 0.0f;
    for (uint32_t i = 0; i < n_children; i++) {
        float cross = column_dir ? natural_w[i] : natural_h[i];
        if (cross > max_cross) {
            max_cross = cross;
        }
    }
    if (cross_budget < max_cross) {
        cross_budget = max_cross;
    }
    int do_stretch = (align == CSS_ALIGN_ITEMS_STRETCH || align == 0);
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        float cross = column_dir ? natural_w[i] : natural_h[i];
        float cross_origin = column_dir ? content_origin_x : content_origin_y;
        float cross_used = cross_budget;
        if (!do_stretch) {
            cross_used = cross;
        }
        float cross_pos = cross_origin;
        if (!do_stretch) {
            if (align == CSS_ALIGN_ITEMS_FLEX_END) {
                cross_pos = cross_origin + (cross_budget - cross_used);
            } else if (align == CSS_ALIGN_ITEMS_CENTER) {
                cross_pos = cross_origin + (cross_budget - cross_used) * 0.5f;
            }
        }
        if (column_dir) {
            c->x = cross_pos;
            c->w = cross_used;
        } else {
            c->y = cross_pos;
            c->h = cross_used;
        }
    }

    float total_main =
        (column_dir ? cursor - content_origin_y : 0.0f);
    if (!column_dir) {
        /* Row direction — cross_budget is the row's vertical size. */
        return cross_budget + pad_top + pad_bottom;
    }
    return total_main + pad_bottom;
}

static float layout_flex_row(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                             float content_w)
{
    return layout_flex(r, idx, origin_x, origin_y, content_w, /*column_dir=*/false);
}

static float layout_flex_column(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                float origin_y, float content_w)
{
    return layout_flex(r, idx, origin_x, origin_y, content_w, /*column_dir=*/true);
}

/* ---------------------------------------------------------------------------
 * Table — gather every descendant <tr> under this table block (transparent
 * through tbody/thead/tfoot), then lay each row's cell children out
 * side-by-side with equal column widths.
 *
 * Cell width = content_w / max_cells_per_row (with min 1px). Cells with
 * shorter content waste horizontal space but the columns line up
 * vertically — exactly what infoboxes / attribute tables need to read
 * coherently. Real CSS table layout would compute column widths from
 * cell content, honor colspan/rowspan, and run two passes; that's a
 * follow-up. The cheap algorithm here is enough to take a Wikipedia
 * infobox from "every cell stacks in one column" to "labels and values
 * sit side by side".
 *
 * `<tr>` rows are identified by element local-name (LBX_TAG_TR) rather
 * than by box->layout_mode, so the tbody/thead/tfoot wrapper blocks
 * pass through transparently (their own children get visited). Cells
 * are direct children of a row that's a BLOCK — we don't insist on
 * tag==TD/TH so that authored td/th-replacement elements with
 * `display: table-cell` still work if a stylesheet sets that.
 * -------------------------------------------------------------------------*/

/* Recursively visit blocks under `idx`, calling `visit(visit_ctx, row_idx)`
 * for every box whose element is a <tr>. Skips diving into any box that
 * is itself a row — nested tables not supported yet (they'd be flattened
 * which is wrong but also rare in real content). */
typedef void (*table_row_visitor)(void *ctx, uint32_t row_idx);

static void collect_table_rows(struct yetty_ylexbor *r, uint32_t idx,
                               table_row_visitor visit, void *ctx)
{
    for (uint32_t cidx = r->boxes.data[idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        if (c->kind != YL_BOX_BLOCK) {
            continue;
        }
        if (c->element && c->element->node.local_name == LXB_TAG_TR) {
            visit(ctx, cidx);
            continue;
        }
        /* tbody / thead / tfoot / colgroup / arbitrary wrapper — recurse
		 * to find descendant rows. */
        collect_table_rows(r, cidx, visit, ctx);
    }
}

struct table_row_list {
    uint32_t *rows;
    size_t count, cap;
};

static void row_collector(void *ctx, uint32_t row_idx)
{
    struct table_row_list *lst = ctx;
    if (lst->count == lst->cap) {
        size_t nc = lst->cap ? lst->cap * 2 : 16;
        uint32_t *p = realloc(lst->rows, nc * sizeof(*p));
        if (!p) {
            return;
        }
        lst->rows = p;
        lst->cap = nc;
    }
    lst->rows[lst->count++] = row_idx;
}

static uint32_t row_cell_count(struct yetty_ylexbor *r, uint32_t row_idx)
{
    uint32_t n = 0;
    for (uint32_t cidx = r->boxes.data[row_idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        if (r->boxes.data[cidx].kind == YL_BOX_BLOCK) {
            n++;
        }
    }
    return n;
}

static float layout_table(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                          float content_w)
{
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];
    float pad_left = self->padding_left;
    float pad_right = self->padding_right;
    float pad_top = self->padding_top;
    float pad_bottom = self->padding_bottom;
    float content_origin_x = origin_x + pad_left;
    float content_origin_y = origin_y + pad_top;
    float content_width = content_w - pad_left - pad_right;
    if (content_width < 0) {
        content_width = 0;
    }

    struct table_row_list rows = {0};
    collect_table_rows(r, idx, row_collector, &rows);
    if (rows.count == 0) {
        free(rows.rows);
        /* Empty table — still consume vertical padding. */
        return pad_top + pad_bottom;
    }

    /* Number of columns = max cell count across rows. */
    uint32_t cols = 0;
    for (size_t i = 0; i < rows.count; i++) {
        uint32_t n = row_cell_count(r, rows.rows[i]);
        if (n > cols) {
            cols = n;
        }
    }
    if (cols == 0) {
        free(rows.rows);
        return pad_top + pad_bottom;
    }

    float col_w = content_width / (float)cols;
    if (col_w < 1.0f) {
        col_w = 1.0f;
    }

    float cursor_y = content_origin_y;
    for (size_t i = 0; i < rows.count; i++) {
        uint32_t row_idx = rows.rows[i];
        struct yetty_ylexbor_box *row = &r->boxes.data[row_idx];
        row->x = content_origin_x;
        row->y = cursor_y;
        row->w = content_width;

        float row_h = 0;
        float cell_x = content_origin_x;
        for (uint32_t cidx = row->first_child; cidx != 0;
             cidx = r->boxes.data[cidx].next_sibling) {
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            if (c->kind != YL_BOX_BLOCK) {
                continue;
            }
            c->x = cell_x;
            c->y = cursor_y;
            c->w = col_w;
            float h = layout_block(r, cidx, cell_x, cursor_y, col_w);
            /* Refetch — vector may have moved. */
            c = &r->boxes.data[cidx];
            row = &r->boxes.data[row_idx];
            c->h = h;
            if (h > row_h) {
                row_h = h;
            }
            cell_x += col_w;
        }

        /* Equalise cell heights to the row's tallest cell so adjacent
		 * cells line up at the bottom edge — same convention CSS uses
		 * for vertical-align: top. */
        for (uint32_t cidx = row->first_child; cidx != 0;
             cidx = r->boxes.data[cidx].next_sibling) {
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            if (c->kind != YL_BOX_BLOCK) {
                continue;
            }
            c->h = row_h;
        }

        row->h = row_h;
        cursor_y += row_h;
    }

    free(rows.rows);
    return (cursor_y - origin_y) + pad_bottom;
}

/* ---------------------------------------------------------------------------
 * Lay out a block box and its children. Returns the height consumed.
 * -------------------------------------------------------------------------*/

/* Active-float state for the current block-flow context. We track at
 * most one stack on each side (left, right). New floats stack vertically
 * underneath the previous one of the same side — CSS would chain them
 * horizontally up to the container width, but the single-stack model is
 * enough for the "sidebar + main column" and "image + wrap-around text"
 * patterns that real pages use floats for. Anything fancier degrades
 * to floats laying out under one another, which still reads. */
struct yl_float_ctx {
    float left_bottom;   /* absolute y where the active left float ends */
    float left_width;    /* horizontal pixels consumed by the left float
	                      * at any y < left_bottom */
    float right_bottom;
    float right_width;
};

static void float_advance_y(struct yl_float_ctx *fl, float y)
{
    if (y >= fl->left_bottom) {
        fl->left_width = 0;
    }
    if (y >= fl->right_bottom) {
        fl->right_width = 0;
    }
}

static float layout_block(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                          float content_w)
{
    /* Flex row / column have their own algorithm; table dispatches to
	 * its grid placer. Floats are handled inline below in the default
	 * block-flow path. */
    if (r->boxes.data[idx].layout_mode == YL_LAYOUT_FLEX_ROW) {
        return layout_flex_row(r, idx, origin_x, origin_y, content_w);
    }
    if (r->boxes.data[idx].layout_mode == YL_LAYOUT_FLEX_COLUMN) {
        return layout_flex_column(r, idx, origin_x, origin_y, content_w);
    }
    if (r->boxes.data[idx].layout_mode == YL_LAYOUT_TABLE) {
        return layout_table(r, idx, origin_x, origin_y, content_w);
    }

    /* Inset the children's content rectangle by this block's padding
	 * and horizontal margin. The padding/margin values were resolved
	 * at box-production time from the cascade; layout just wires them
	 * into the origin and content width. */
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];
    float pad_left = self->padding_left;
    float pad_right = self->padding_right;
    float pad_top = self->padding_top;
    float pad_bottom = self->padding_bottom;
    float content_origin_x = origin_x + pad_left;
    float content_origin_y = origin_y + pad_top;
    float content_width = content_w - pad_left - pad_right;
    if (content_width < 0) {
        content_width = 0;
    }

    float cursor_y = content_origin_y;
    float prev_margin_bottom = 0; /* for adjacent-sibling collapsing */
    int has_prev = 0;
    struct yl_float_ctx fl = {0};

    uint32_t cidx = r->boxes.data[idx].first_child;
    while (cidx != 0) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];

        if (c->kind == YL_BOX_BLOCK) {
            float mt = c->margin_top;
            float collapsed = has_prev ? (mt > prev_margin_bottom ? mt : prev_margin_bottom) : mt;
            cursor_y += collapsed;

            /* CSS `clear` — push the cursor below any active float on
			 * the requested side(s) before placing this block. */
            if (c->clear_side == 1 || c->clear_side == 3) {
                if (cursor_y < fl.left_bottom) {
                    cursor_y = fl.left_bottom;
                }
            }
            if (c->clear_side == 2 || c->clear_side == 3) {
                if (cursor_y < fl.right_bottom) {
                    cursor_y = fl.right_bottom;
                }
            }
            float_advance_y(&fl, cursor_y);

            /* Available content rectangle is narrowed by any float
			 * still in effect at this y. */
            float avail_x = content_origin_x + fl.left_width;
            float avail_w = content_width - fl.left_width - fl.right_width;
            if (avail_w < 0) {
                avail_w = 0;
            }

            /* Floated block — pull out of normal flow. Place it at
			 * the left/right edge of the in-flow content rectangle
			 * (or below the prior float on the same side) and add
			 * its extent to the float context. cursor_y is NOT
			 * advanced; subsequent in-flow content keeps flowing
			 * around the float. */
            if (c->float_side != 0) {
                float fw = c->css_width > 0 ? c->css_width : avail_w;
                if (fw <= 0) {
                    fw = content_width > 0 ? content_width : 1;
                }
                if (fw > content_width) {
                    fw = content_width;
                }
                /* If a same-side float is still active, the new
				 * float sits BELOW it (single-stack model). */
                float fy = cursor_y;
                float fx;
                if (c->float_side == 1) {
                    if (fl.left_width > 0) {
                        fy = fl.left_bottom;
                    }
                    fx = content_origin_x;
                } else {
                    if (fl.right_width > 0) {
                        fy = fl.right_bottom;
                    }
                    fx = content_origin_x + content_width - fw;
                }
                c->x = fx;
                c->y = fy;
                c->w = fw;
                float fh = layout_block(r, cidx, fx, fy, fw);
                c = &r->boxes.data[cidx];
                if (c->css_height > 0) {
                    fh = c->css_height;
                }
                c->h = fh;
                if (c->float_side == 1) {
                    fl.left_width = fw;
                    fl.left_bottom = fy + fh;
                } else {
                    fl.right_width = fw;
                    fl.right_bottom = fy + fh;
                }
                /* Floats don't contribute margin-collapsing or
				 * advance cursor_y. */
                cidx = r->boxes.data[cidx].next_sibling;
                continue;
            }

            /* Resolve the child's effective width:
			 *   - explicit `width: <px>` pins it,
			 *   - `max-width` clamps from above,
			 *   - `min-width` clamps from below.
			 * The default is the available rectangle minus the
			 * child's left+right margins. */
            float avail = avail_w - c->margin_left - c->margin_right;
            if (avail < 0) {
                avail = 0;
            }
            float child_w = c->css_width > 0 ? c->css_width : avail;
            if (c->css_max_width > 0 && child_w > c->css_max_width) {
                child_w = c->css_max_width;
            }
            if (c->css_min_width > 0 && child_w < c->css_min_width) {
                child_w = c->css_min_width;
            }
            if (child_w > avail) {
                child_w = avail;
            }

            /* Horizontal margin auto handling — center the box
			 * within the available rect when both sides are auto. */
            float lead = c->margin_left;
            if (c->margin_left_auto && c->margin_right_auto) {
                lead = (avail - child_w) * 0.5f;
                if (lead < 0) {
                    lead = 0;
                }
            } else if (c->margin_left_auto) {
                lead = avail - child_w - c->margin_right;
                if (lead < 0) {
                    lead = 0;
                }
            }

            float child_origin_x = avail_x + lead;
            c->x = child_origin_x;
            c->y = cursor_y;
            c->w = child_w;
            float child_h = layout_block(r, cidx, child_origin_x, cursor_y, child_w);
            /* Re-fetch — vector may have relocated. */
            c = &r->boxes.data[cidx];
            /* `height: <px>` from CSS pins; otherwise content
			 * height wins. */
            if (c->css_height > 0) {
                child_h = c->css_height;
            }
            c->h = child_h;
            cursor_y += child_h;
            prev_margin_bottom = c->margin_bottom;
            has_prev = 1;

        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            /* Inline content flows around active floats — pin the
			 * line's available width to the float-narrowed rect at
			 * cursor_y. */
            float_advance_y(&fl, cursor_y);
            float avail_x = content_origin_x + fl.left_width;
            float avail_w = content_width - fl.left_width - fl.right_width;
            if (avail_w < 0) {
                avail_w = 0;
            }
            float h = wrap_inline_box(r, cidx, avail_x, cursor_y, avail_w, self->text_align);
            cursor_y += h;
            prev_margin_bottom = 0;
            has_prev = 1;

        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            float_advance_y(&fl, cursor_y);
            float avail_x = content_origin_x + fl.left_width;
            float avail_w = content_width - fl.left_width - fl.right_width;
            if (avail_w < 0) {
                avail_w = 0;
            }
            /* The box producer fills c->w / c->h from HTML
			 * width/height attrs or the decoded natural pixel
			 * size. Fall back to a 100x100 placeholder only if
			 * neither was available. Cap width to the available
			 * area so an oversized image doesn't punch through
			 * the right edge. */
            float img_w = c->w > 0 ? c->w : 100;
            float img_h = c->h > 0 ? c->h : 100;
            if (img_w > avail_w && avail_w > 0) {
                float scale = avail_w / img_w;
                img_w = avail_w;
                img_h *= scale;
            }
            c->x = avail_x;
            c->y = cursor_y;
            c->w = img_w;
            c->h = img_h;
            cursor_y += img_h;
            prev_margin_bottom = 0;
            has_prev = 1;
        }

        cidx = r->boxes.data[cidx].next_sibling;
    }

    /* If trailing floats extend past in-flow content, our height
	 * must cover them too — otherwise the next sibling would render
	 * on top of the float. */
    if (fl.left_bottom > cursor_y) {
        cursor_y = fl.left_bottom;
    }
    if (fl.right_bottom > cursor_y) {
        cursor_y = fl.right_bottom;
    }

    /* Total consumed height includes our own padding-top + content +
	 * padding-bottom. The caller stored origin_y as our top, so
	 * (cursor_y - origin_y) already counts pad_top + content; just
	 * add pad_bottom. */
    return (cursor_y - origin_y) + pad_bottom;
}

/* ===========================================================================
 * Public entry point — lay out the root and propagate content height.
 * ===========================================================================*/

struct yetty_ycore_void_result yetty_ylexbor_layout(struct yetty_ylexbor *r)
{
    if (r == NULL || r->boxes.size == 0) {
        return YETTY_OK_VOID();
    }

    /* The root box (idx 0) fills the viewport horizontally; vertical
	 * size comes from layout. */
    struct yetty_ylexbor_box *root = &r->boxes.data[0];
    root->x = 0;
    root->y = 0;
    root->w = (float)r->viewport_w;

    float h = layout_block(r, 0, 0, 0, (float)r->viewport_w);
    r->boxes.data[0].h = h;
    r->content_height = (int)h;

    return YETTY_OK_VOID();
}
