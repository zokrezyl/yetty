/*
 * ylexbor-layout — naive block flow + line wrapping.
 *
 * Block boxes stack vertically. Inline-text children of a block are
 * wrapped into lines based on naive font metrics (per-glyph width =
 * font_size * 0.55 — same shortcut ynetsurf uses; FreeType integration
 * is a follow-up). Each laid-out line replaces its source
 * YL_BOX_INLINE_TEXT box's geometry; if a single text box wraps into N
 * lines we *split* it into N inline-text boxes so the paint pass can
 * emit one TEXT_DRAWABLE_LIST per line.
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

#include "ylexbor-internal.h"

#include <stdlib.h>
#include <string.h>

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

static float wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                             float content_w, int text_align)
{
    struct yetty_ylexbor_box *b = &r->boxes.data[idx];
    const char *text = b->text;
    size_t n = b->text_len;
    float font_size = b->font_size;
    float line_height = font_size * 1.25f; /* CSS default normal */

    /* Pre-compute per-glyph width once — naive uniform for now. */
    float per_glyph = font_size * 0.55f;
    if (per_glyph < 1.0f) {
        per_glyph = 1.0f;
    }

    /* Walk the string, splitting at the last space whose end fits.
	 * Each split produces one line. We keep the very first line in
	 * the original box (idx) and splice subsequent line boxes into
	 * the parent's sibling chain right after `idx`, preserving
	 * whatever followed.  */

    float y = origin_y;
    int first = 1;
    uint32_t orig_next = b->next_sibling;
    uint32_t prev_idx = idx;

    /* `cursor` walks the original string. For each line we find the
	 * largest prefix [cursor..end) whose visual width fits in
	 * content_w, prefer breaking at the last space, and emit. */

    size_t cursor = 0;
    while (cursor < n) {
        /* Skip leading space on every wrapped line except the first
		 * (we want left-aligned blocks; the first line we keep as-is
		 * to preserve any author-intended spacing). */
        if (!first) {
            while (cursor < n && text[cursor] == ' ') {
                cursor++;
            }
            if (cursor >= n) {
                break;
            }
        }

        /* How many bytes fit in content_w? */
        size_t fit = 0;
        float acc = 0.0f;
        size_t last_break = 0;
        for (size_t k = cursor; k < n;) {
            unsigned char c = (unsigned char)text[k];
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
        size_t end = (last_break > cursor && fit < n) ? last_break : fit;
        if (end == cursor) {
            end = (cursor + 1 <= n) ? cursor + 1 : n;
        }

        size_t line_len = end - cursor;

        struct yetty_ylexbor_box *target;
        if (first) {
            target = &r->boxes.data[idx];
            first = 0;
        } else {
            /* Need a new sibling box. Reserve, copy style. */
            struct yetty_ycore_void_result rr =
                _yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
            if (YETTY_IS_ERR(rr)) {
                return y - origin_y;
            }
            uint32_t new_idx = r->boxes.size++;
            /* The base pointer may have moved — re-fetch. */
            b = &r->boxes.data[idx];
            target = &r->boxes.data[new_idx];
            memset(target, 0, sizeof(*target));
            target->kind = YL_BOX_INLINE_TEXT;
            target->font_size = b->font_size;
            target->font_weight = b->font_weight;
            target->font_italic = b->font_italic;
            target->fg = b->fg;
            /* Splice into the parent's sibling chain right after
			 * the previously emitted line (originally `idx`,
			 * then the prior line). */
            r->boxes.data[prev_idx].next_sibling = new_idx;
            target->next_sibling = orig_next;
            prev_idx = new_idx;
        }

        target->text = text + cursor;
        target->text_len = line_len;
        float line_w =
            yetty_ylexbor_naive_text_width(target->text, target->text_len, target->font_size);
        /* Honor text-align by shifting the line within the
		 * content area. The wrap algorithm is greedy left-to-right;
		 * with center/right alignment we just translate. justify
		 * (text_align==3) falls through to left for now — would
		 * need per-word spacing pass. */
        float line_x = origin_x;
        if (text_align == 1) {
            line_x = origin_x + (content_w - line_w) * 0.5f;
        } else if (text_align == 2) {
            line_x = origin_x + (content_w - line_w);
        }
        if (line_x < origin_x) {
            line_x = origin_x;
        }
        target->x = line_x;
        target->y = y;
        target->w = line_w;
        target->h = line_height;

        y += line_height;
        cursor = end;
    }

    if (first) {
        /* Empty text — collapse the box to zero height. */
        b->x = origin_x;
        b->y = origin_y;
        b->w = 0;
        b->h = 0;
    }

    return y - origin_y;
}

/* ---------------------------------------------------------------------------
 * Flex row — split content_w evenly across each block-level child,
 * stacking inline text into one truncated line per child slot. This is
 * deliberately the cheapest possible flex implementation: no flex-grow,
 * no flex-basis, no wrap, no main/cross axis distinction beyond row vs
 * column. It exists so that "header bar with logo + nav + user menu"
 * doesn't render as three vertically-stacked rows that overflow the
 * page. Real flex semantics are a follow-up.
 * -------------------------------------------------------------------------*/

static float layout_flex_row(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                             float content_w)
{
    uint32_t slots = r->boxes.data[idx].child_count;
    if (slots == 0) {
        return 0;
    }

    float slot_w = content_w / (float)slots;
    if (slot_w < 1.0f) {
        slot_w = 1.0f;
    }

    float row_h = 0;
    float cursor_x = origin_x;
    for (uint32_t cidx = r->boxes.data[idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        c->x = cursor_x;
        c->y = origin_y;
        c->w = slot_w;
        float h = 0;
        if (c->kind == YL_BOX_BLOCK) {
            h = layout_block(r, cidx, cursor_x, origin_y, slot_w);
        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            h = wrap_inline_box(r, cidx, cursor_x, origin_y, slot_w, /*text_align=*/0);
        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            h = 100;
            c->w = slot_w;
            c->h = h;
        }
        c = &r->boxes.data[cidx]; /* refetch */
        c->h = h;
        if (h > row_h) {
            row_h = h;
        }
        cursor_x += slot_w;
    }
    return row_h;
}

/* ---------------------------------------------------------------------------
 * Lay out a block box and its children. Returns the height consumed.
 * -------------------------------------------------------------------------*/

static float layout_block(struct yetty_ylexbor *r, uint32_t idx, float origin_x, float origin_y,
                          float content_w)
{
    /* Flex row goes through its own splitter. Flex column degrades
	 * to plain block stacking — same vertical flow, just gated by
	 * the explicit display:flex; flex-direction:column author intent. */
    if (r->boxes.data[idx].layout_mode == YL_LAYOUT_FLEX_ROW) {
        return layout_flex_row(r, idx, origin_x, origin_y, content_w);
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

    uint32_t cidx = r->boxes.data[idx].first_child;
    while (cidx != 0) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];

        if (c->kind == YL_BOX_BLOCK) {
            float mt = c->margin_top;
            float collapsed = has_prev ? (mt > prev_margin_bottom ? mt : prev_margin_bottom) : mt;
            cursor_y += collapsed;

            /* Resolve the child's effective width:
			 *   - explicit `width: <px>` pins it,
			 *   - `max-width` clamps from above,
			 *   - `min-width` clamps from below.
			 * The default is the parent's content area minus the
			 * child's left+right margins. */
            float avail = content_width - c->margin_left - c->margin_right;
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
			 * within the parent's content area when both sides
			 * are auto and there's room. */
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

            float child_origin_x = content_origin_x + lead;
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
            /* Inline text wraps into one-or-more lines. The
			 * wrap function may extend the sibling chain by
			 * appending new line-fragment boxes to `idx`. Our
			 * loop walks `next_sibling` so it picks them up
			 * naturally. text_align comes from the parent block. */
            float h = wrap_inline_box(r, cidx, content_origin_x, cursor_y, content_width,
                                      self->text_align);
            cursor_y += h;
            prev_margin_bottom = 0;
            has_prev = 1;

        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            /* The box producer fills c->w / c->h from HTML
			 * width/height attrs or the decoded natural pixel
			 * size. Fall back to a 100x100 placeholder only if
			 * neither was available. Cap width to the content
			 * area so an oversized image doesn't punch through
			 * the right edge. */
            float img_w = c->w > 0 ? c->w : 100;
            float img_h = c->h > 0 ? c->h : 100;
            if (img_w > content_width && content_width > 0) {
                float scale = content_width / img_w;
                img_w = content_width;
                img_h *= scale;
            }
            c->x = content_origin_x;
            c->y = cursor_y;
            c->w = img_w;
            c->h = img_h;
            cursor_y += img_h;
            prev_margin_bottom = 0;
            has_prev = 1;
        }

        cidx = r->boxes.data[cidx].next_sibling;
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
