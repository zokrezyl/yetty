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

#include "ylexbor-internal.h"

#include <stdlib.h>
#include <string.h>


/* Forward decl — recursive. */
static float layout_block(struct yetty_ylexbor *r, uint32_t idx,
			  float origin_x, float origin_y,
			  float content_w);

/* ---------------------------------------------------------------------------
 * Wrap one inline-text box into one-or-more lines. Replaces the original
 * box in-place and inserts additional sibling boxes for the extra
 * lines. Returns the total height (line_count * line_height).
 * -------------------------------------------------------------------------*/

static float wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx,
			     float origin_x, float origin_y,
			     float content_w)
{
	struct yetty_ylexbor_box *b = &r->boxes.data[idx];
	const char *text = b->text;
	size_t      n    = b->text_len;
	float font_size  = b->font_size;
	float line_height = font_size * 1.25f;  /* CSS default normal */

	/* Pre-compute per-glyph width once — naive uniform for now. */
	float per_glyph = font_size * 0.55f;
	if (per_glyph < 1.0f) per_glyph = 1.0f;

	/* Walk the string, splitting at the last space whose end fits.
	 * Each split produces one line. We keep the very first line in
	 * the original box (idx) and append subsequent lines as new
	 * boxes in the parent. */

	float y = origin_y;
	int   first = 1;
	uint32_t parent_box_count = b->kind == YL_BOX_INLINE_TEXT ? 0 : 0;
	(void)parent_box_count;

	/* `cursor` walks the original string. For each line we find the
	 * largest prefix [cursor..end) whose visual width fits in
	 * content_w, prefer breaking at the last space, and emit. */

	size_t cursor = 0;
	while (cursor < n) {
		/* Skip leading space on every wrapped line except the first
		 * (we want left-aligned blocks; the first line we keep as-is
		 * to preserve any author-intended spacing). */
		if (!first) {
			while (cursor < n && text[cursor] == ' ') cursor++;
			if (cursor >= n) break;
		}

		/* How many bytes fit in content_w? */
		size_t fit = 0;
		float  acc = 0.0f;
		size_t last_break = 0;
		for (size_t k = cursor; k < n; ) {
			unsigned char c = (unsigned char)text[k];
			size_t step;
			if      (c < 0x80) step = 1;
			else if ((c & 0xE0) == 0xC0) step = 2;
			else if ((c & 0xF0) == 0xE0) step = 3;
			else if ((c & 0xF8) == 0xF0) step = 4;
			else                          step = 1;
			if (acc + per_glyph > content_w && k > cursor) break;
			acc += per_glyph;
			k += step;
			fit = k;
			if (c == ' ') last_break = k;
		}
		size_t end = (last_break > cursor && fit < n) ? last_break : fit;
		if (end == cursor) end = (cursor + 1 <= n) ? cursor + 1 : n;

		size_t line_len = end - cursor;

		struct yetty_ylexbor_box *target;
		if (first) {
			target = &r->boxes.data[idx];
			first = 0;
		} else {
			/* Need a new sibling box. Reserve, copy style. */
			struct yetty_ycore_void_result rr =
				_yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
			if (YETTY_IS_ERR(rr)) return y - origin_y;
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
		}

		target->text = text + cursor;
		target->text_len = line_len;
		target->x = origin_x;
		target->y = y;
		target->w = yetty_ylexbor_naive_text_width(target->text,
							    target->text_len,
							    target->font_size);
		target->h = line_height;

		y += line_height;
		cursor = end;
	}

	if (first) {
		/* Empty text — collapse the box to zero height. */
		b->x = origin_x; b->y = origin_y; b->w = 0; b->h = 0;
	}

	return y - origin_y;
}

/* ---------------------------------------------------------------------------
 * Lay out a block box and its children. Returns the height consumed.
 * -------------------------------------------------------------------------*/

static float layout_block(struct yetty_ylexbor *r, uint32_t idx,
			  float origin_x, float origin_y,
			  float content_w)
{
	/* Snapshot the box header — vector may relocate during recursion
	 * if children's text boxes need to grow the array. We re-fetch
	 * after each child loop. */
	uint32_t first_child = r->boxes.data[idx].first_child;
	uint32_t child_count = r->boxes.data[idx].child_count;

	float cursor_y = origin_y;
	float prev_margin_bottom = 0;  /* for adjacent-sibling collapsing */
	int   has_prev = 0;

	for (uint32_t i = 0; i < child_count; i++) {
		uint32_t cidx = first_child + i;
		struct yetty_ylexbor_box *c = &r->boxes.data[cidx];

		if (c->kind == YL_BOX_BLOCK) {
			float mt = c->margin_top;
			float collapsed = has_prev
				? (mt > prev_margin_bottom ? mt : prev_margin_bottom)
				: mt;
			cursor_y += collapsed;

			c->x = origin_x;
			c->y = cursor_y;
			c->w = content_w;
			float child_h = layout_block(r, cidx, origin_x,
						     cursor_y, content_w);
			/* Re-fetch — vector may have relocated. */
			c = &r->boxes.data[cidx];
			c->h = child_h;
			cursor_y += child_h;
			prev_margin_bottom = c->margin_bottom;
			has_prev = 1;

		} else if (c->kind == YL_BOX_INLINE_TEXT) {
			/* Inline text wraps into one-or-more lines. The wrap
			 * function may grow the vector (extra-line siblings),
			 * which moves first_child if our parent was relocated.
			 * Refetch first_child / child_count from `idx` after
			 * the wrap. */
			float h = wrap_inline_box(r, cidx, origin_x,
						   cursor_y, content_w);
			cursor_y += h;
			/* If wrap added new sibling boxes, account for them
			 * in this block's child_count so subsequent paints
			 * see them. */
			if (r->boxes.data[idx].child_count != child_count) {
				child_count = r->boxes.data[idx].child_count;
			}
			prev_margin_bottom = 0;
			has_prev = 1;

		} else if (c->kind == YL_BOX_INLINE_IMAGE) {
			/* Placeholder — fixed 100x100 grey box. Real image
			 * decoding is a TODO. */
			c->x = origin_x;
			c->y = cursor_y;
			c->w = 100;
			c->h = 100;
			cursor_y += 100;
			prev_margin_bottom = 0;
			has_prev = 1;
		}
	}

	return cursor_y - origin_y;
}

/* ===========================================================================
 * Public entry point — lay out the root and propagate content height.
 * ===========================================================================*/

struct yetty_ycore_void_result yetty_ylexbor_layout(struct yetty_ylexbor *r)
{
	if (r == NULL || r->boxes.size == 0)
		return YETTY_OK_VOID();

	/* The root box (idx 0) fills the viewport horizontally; vertical
	 * size comes from layout. */
	struct yetty_ylexbor_box *root = &r->boxes.data[0];
	root->x = 0; root->y = 0; root->w = (float)r->viewport_w;

	float h = layout_block(r, 0, 0, 0, (float)r->viewport_w);
	r->boxes.data[0].h = h;
	r->content_height = (int)h;

	return YETTY_OK_VOID();
}
