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

#include "ybrowser-internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lexbor/tag/const.h>
#include <libcss/libcss.h>

#include <yetty/ytrace/ytrace.h>

/* Forward decls — recursive. */
static struct float_result layout_block(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                        float origin_y, float content_w);
static struct float_result wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                           float origin_y, float content_w, int text_align);

/* Walk the box subtree rooted at `idx` and return the largest known
 * width of any INLINE_IMAGE descendant. Stops at YL_CELL_MEASURE_BUDGET
 * boxes so a deep subtree doesn't blow up the cost. Returns 0 when no
 * descendant image has a known width yet (image still loading, no
 * width attr). The float branch above uses a one-level scan over
 * first_child; this recursive variant is needed for `<figure><a><img></a></figure>`,
 * the Wikipedia pattern where the image is wrapped in a link inside
 * the figure block. */
static float find_descendant_img_width(struct yetty_ylexbor *r, uint32_t idx, int *budget)
{
    if (*budget <= 0) {
        return 0;
    }
    (*budget)--;
    float best = 0;
    for (uint32_t cidx = r->boxes.data[idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        if (c->kind == YL_BOX_INLINE_IMAGE && c->w > 0.0f && c->w > best) {
            best = c->w;
        } else if (c->kind == YL_BOX_BLOCK) {
            float sub = find_descendant_img_width(r, cidx, budget);
            if (sub > best) {
                best = sub;
            }
        }
    }
    return best;
}

/* ---------------------------------------------------------------------------
 * Wrap one inline-text box into one-or-more lines. Replaces the original
 * box in-place and inserts additional sibling boxes for the extra
 * lines. Returns the total height (line_count * line_height).
 * -------------------------------------------------------------------------*/

/* Helper — return the seg index covering byte offset `off`. Linear walk
 * from `start_hint` since segs are appended in order. */
static size_t seg_index_at(const struct yetty_ylexbor_inline_seg *segs, size_t segs_count, size_t n,
                           size_t off, size_t start_hint)
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
static struct uint32_result emit_fragment(struct yetty_ylexbor *r, uint32_t reuse_idx, float x,
                                          float y, float w, float h, float font_size,
                                          const char *text, size_t text_len,
                                          const struct yetty_ylexbor_inline_seg *seg,
                                          float word_spacing)
{
    uint32_t fidx;
    struct yetty_ylexbor_box *target;
    if (reuse_idx != 0) {
        fidx = reuse_idx;
        target = &r->boxes.data[fidx];
        /* Preserve next_sibling / first_child relationships set by
		 * flush_inline (we're reusing it for the first fragment of
		 * the first line). element is overwritten below from the
		 * seg's element so click hit-test routes the source box
		 * through the deepest inline ancestor too. */
        target->kind = YL_BOX_INLINE_TEXT;
    } else {
        struct yetty_ycore_void_result rr =
            _yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
        YETTY_RETURN_IF_ERR(uint32, rr, "emit_fragment: reserve");
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
    target->line_through = seg->line_through;
    target->overline = seg->overline;
    target->element = seg->element;
    target->word_spacing = word_spacing;
    return YETTY_OK(uint32, fidx);
}

static struct float_result wrap_inline_box(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                           float origin_y, float content_w, int text_align)
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

    /* Minimum content-area width for the wrap loop. If the container is
	 * narrower than ~3 glyphs, we'd produce a stack of one-glyph-per-line
	 * fragments — the "letters scattered down the page" symptom on
	 * Wikipedia-rendered pages at small viewports. Real browsers
	 * OVERFLOW the container in that case (or text becomes unreadable);
	 * either way it's better than emitting 300 fragments of one letter
	 * each. We clamp content_w so the wrap loop sees a sensible budget,
	 * and let the overflowing line render past the container edge. The
	 * float-narrowed path in layout_block has its own min handling but
	 * deeply-nested narrow contexts (table cells inside table cells,
	 * flex items inside flex items at narrow viewports, etc.) can still
	 * end up here with ridiculous content_w. */
    float wrap_w = content_w;
    float min_wrap_w = per_glyph * 8.0f; /* roughly one short word */
    if (wrap_w < min_wrap_w) {
        wrap_w = min_wrap_w;
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
		 * breaks survive verbatim. Break opportunities recognised:
		 *
		 *   ' ' (U+0020)         — visible space, ASCII default.
		 *   U+00AD soft hyphen   — invisible suggestion to break.
		 *   U+200B zero-width    — invisible explicit break point.
		 *   U+200C / U+200D      — zero-width joiner / non-joiner;
		 *                          allow break after for CJK/Indic.
		 *
		 * The zero-width forms don't contribute to `acc`, so packing
		 * them into a run never narrows the visible character budget. */
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
            /* Classify the codepoint at `k`. zero_width=1 codepoints
			 * don't contribute glyph width; break_after=1 codepoints
			 * mark the position after the codepoint as a wrap point. */
            int zero_width = 0;
            int break_after = 0;
            if (c == ' ') {
                break_after = 1;
            } else if (step == 2 && c == 0xC2 && k + 1 < n && (unsigned char)text[k + 1] == 0xAD) {
                /* U+00AD SOFT HYPHEN */
                zero_width = 1;
                break_after = 1;
            } else if (step == 3 && c == 0xE2 && k + 2 < n && (unsigned char)text[k + 1] == 0x80) {
                unsigned char c2 = (unsigned char)text[k + 2];
                if (c2 == 0x8B || c2 == 0x8C || c2 == 0x8D) {
                    /* U+200B / U+200C / U+200D — zero-width
					 * (joiner) — wrap opportunity, no width. */
                    zero_width = 1;
                    break_after = 1;
                }
            }
            if (!zero_width && acc + per_glyph > wrap_w && k > cursor) {
                break;
            }
            if (!zero_width) {
                acc += per_glyph;
            }
            k += step;
            fit = k;
            if (break_after) {
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

        /* Total line width — used for text-align translation only.
		 * Use wrap_w (with min-floor applied) so squeezed containers
		 * don't produce negative offsets.
		 *
		 * Centering / right-alignment is suppressed when wrap_w is
		 * wide (>400px). Wikipedia's <figcaption>s have
		 * `text-align: center` but their parent figure block is laid
		 * out at full body width (we don't compute shrink-to-fit
		 * widths). Centering each wrapped line independently inside
		 * 1090px produces a stair-step where short trailing lines
		 * land far right of the long first line — visible as
		 * "scattered letters". Suppressing centering for wide
		 * containers gives left-aligned captions that read as one
		 * block. Narrow centered text (table cells, narrow flex
		 * items, real captions on sized figures) still centers
		 * correctly. */
        float line_w = yetty_ylexbor_naive_text_width(text + cursor, end - cursor, font_size);
        float line_origin_x = origin_x;
        int effective_align = text_align;
        if ((effective_align == 1 || effective_align == 2) && wrap_w > 400.0f) {
            effective_align = 0;
        }
        if (effective_align == 1) {
            line_origin_x = origin_x + (wrap_w - line_w) * 0.5f;
        } else if (effective_align == 2) {
            line_origin_x = origin_x + (wrap_w - line_w);
        }
        if (line_origin_x < origin_x) {
            line_origin_x = origin_x;
        }

        /* text-align: justify — distribute leftover slack as extra
		 * spacing after every ASCII space in the line. We route the
		 * slack through the TEXT_DRAWABLE_LIST v2 word_spacing field rather
		 * than padding the spaces with explicit \t-or-similar bytes,
		 * so the wire prim count stays the same and the canvas does
		 * the spacing math. Skip the last line of the paragraph
		 * (cursor will reach `n` after this iteration) and lines
		 * terminated by an explicit '\n' — real browsers don't pad
		 * those, and padding them produces oddly wide trailing gaps
		 * on the last line of every block. */
        float justify_word_spacing = 0.0f;
        if (text_align == 3 && hard_break == 0 && end < n) {
            int space_count = 0;
            for (size_t k = cursor; k < end; k++) {
                if (text[k] == ' ') {
                    space_count++;
                }
            }
            /* Use wrap_w (with min-floor applied) for the slack
			 * calculation — using raw content_w when the container
			 * is squeezed below the min produces NEGATIVE slack and
			 * would shrink lines beyond zero. */
            if (space_count > 0 && wrap_w > line_w) {
                justify_word_spacing = (wrap_w - line_w) / (float)space_count;
            }
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
            /* Whitespace-only fragments produce no visible glyphs but
			 * after P1.2's element-aware seg matching they show up
			 * whenever a text node containing just spaces sits between
			 * two inline elements (e.g. `<a>x</a> <b>y</b>` — the
			 * `" "` between `</a>` and `<b>` opens its own seg with a
			 * different element pointer). Painting them wastes a
			 * TEXT_DRAWABLE_LIST prim per inter-word gap and, more importantly,
			 * forces the canvas to look up a glyph for U+0020 in a
			 * vacuum — fonts that fall back to a placeholder square /
			 * dot for missing metric data render visible artefacts
			 * around the page, the "garbage letters" symptom. Detect
			 * here, skip the emit, but still advance frag_x by the
			 * naive width so the next fragment lands where the canvas
			 * would otherwise have placed it. */
            int ws_only = 1;
            for (size_t k = s0; k < s1; k++) {
                unsigned char c = (unsigned char)text[k];
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    ws_only = 0;
                    break;
                }
            }
            float frag_w = yetty_ylexbor_naive_text_width(text + s0, s1 - s0, font_size);
            /* When justify is on, this fragment's effective render
			 * width grows by word_spacing per space — bump frag_w so
			 * the next fragment in the same line starts where the
			 * canvas will leave the cursor. */
            float frag_extra = 0.0f;
            if (justify_word_spacing > 0.0f) {
                for (size_t k = s0; k < s1; k++) {
                    if (text[k] == ' ') {
                        frag_extra += justify_word_spacing;
                    }
                }
            }
            if (ws_only) {
                /* Don't emit. If this would have been the FIRST
				 * fragment (reuse=idx), collapse the source box so
				 * its leftover full-string content from flush_inline
				 * doesn't paint. The next fragment will allocate its
				 * own box. */
                if (!first_fragment_emitted) {
                    struct yetty_ylexbor_box *bb = &r->boxes.data[idx];
                    bb->x = frag_x;
                    bb->y = y;
                    bb->w = 0;
                    bb->h = 0;
                    bb->text_len = 0;
                    first_fragment_emitted = 1;
                }
                frag_x += frag_w + frag_extra;
                seg_hint = si;
                if (seg_end >= end) {
                    break;
                }
                si++;
                continue;
            }
            uint32_t reuse = first_fragment_emitted ? 0 : idx;
            struct uint32_result emit_res =
                emit_fragment(r, reuse, frag_x, y, frag_w + frag_extra, line_height, font_size,
                              text + s0, s1 - s0, &segs[si], justify_word_spacing);
            YETTY_RETURN_IF_ERR(float, emit_res, "wrap_inline_box: emit_fragment");
            first_fragment_emitted = 1;
            frag_x += frag_w + frag_extra;
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

    return YETTY_OK(float, y - origin_y);
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

static struct float_result layout_flex(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                       float origin_y, float content_w, bool column_dir)
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
    for (uint32_t cidx = self->first_child; cidx != 0; cidx = r->boxes.data[cidx].next_sibling) {
        if (n_children >= YL_FLEX_MAX_CHILDREN) {
            break;
        }
        if (r->boxes.data[cidx].float_side != 0) {
            continue;
        }
        children[n_children++] = cidx;
    }
    if (n_children == 0) {
        return YETTY_OK(float, pad_top + pad_bottom);
    }

    /* Main-axis budget. */
    float main_budget;
    if (column_dir) {
        main_budget = css_h > 0 ? css_h : 0;
    } else {
        main_budget = content_width;
    }

    /* Resolve each child's hypothetical main-axis size. Encoding
	 * (shared with css_width / css_height):
	 *   > 0 = absolute px
	 *   < 0 = percent of main_budget (value is -N/100)
	 *   == 0 = auto / not set → fall back to css_width or
	 *          css_height (same encoding), else 0 and let the
	 *          autobasis branch below distribute space. */
    float main_size[YL_FLEX_MAX_CHILDREN];
    float total_basis = 0.0f;
    float total_grow = 0.0f;
    int autobasis_count = 0;
    for (uint32_t i = 0; i < n_children; i++) {
        struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
        float basis;
        float fbp = c->flex_basis_px;
        if (fbp > 0.0f) {
            basis = fbp;
        } else if (fbp < 0.0f) {
            basis = main_budget * (-fbp);
        } else {
            float css_main = column_dir ? c->css_height : c->css_width;
            if (css_main > 0.0f) {
                basis = css_main;
            } else if (css_main < 0.0f) {
                basis = main_budget * (-css_main);
            } else {
                basis = 0.0f;
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
            struct float_result block_res = layout_block(r, cidx, c->x, c->y, c->w);
            YETTY_RETURN_IF_ERR(float, block_res, "layout_flex: layout_block");
            h = block_res.value;
        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            struct float_result wrap_res = wrap_inline_box(r, cidx, c->x, c->y, c->w,
                                                           /*text_align=*/0);
            YETTY_RETURN_IF_ERR(float, wrap_res, "layout_flex: wrap_inline_box");
            h = wrap_res.value;
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

    float total_main = (column_dir ? cursor - content_origin_y : 0.0f);
    if (!column_dir) {
        /* Row direction — cross_budget is the row's vertical size. */
        return YETTY_OK(float, cross_budget + pad_top + pad_bottom);
    }
    return YETTY_OK(float, total_main + pad_bottom);
}

static struct float_result layout_flex_row(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                           float origin_y, float content_w)
{
    return layout_flex(r, idx, origin_x, origin_y, content_w, /*column_dir=*/false);
}

static struct float_result layout_flex_column(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
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

static void collect_table_rows(struct yetty_ylexbor *r, uint32_t idx, table_row_visitor visit,
                               void *ctx)
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

/* Measure a cell's "max content width" — the visual width of all its
 * inline-text descendants laid out on a single line, plus the natural
 * width of any inline-image child. Used by the table layout to decide
 * how much horizontal space each column wants before the constraint
 * stage kicks in.
 *
 * The cell hasn't been laid out yet at this point, so we measure off
 * the un-wrapped text in the box's INLINE_TEXT children. We also bound
 * the recursion at 32 nodes so a pathological cell with thousands of
 * styled spans doesn't blow up the per-cell measurement cost.
 *
 * NB: this is "max-content", not "min-content". A long sentence in a
 * cell returns the sum of its glyph widths — the column distribution
 * then clamps it to the available space, so wide cells don't blow up
 * the whole row. */
#define YL_CELL_MEASURE_BUDGET 256

static float measure_cell_content_width(struct yetty_ylexbor *r, uint32_t cell_idx, int *budget)
{
    if (*budget <= 0) {
        return 0;
    }
    (*budget)--;
    float sum = 0;
    float max_line = 0;
    for (uint32_t cidx = r->boxes.data[cell_idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        if (c->kind == YL_BOX_INLINE_TEXT) {
            sum += yetty_ylexbor_naive_text_width(c->text, c->text_len, c->font_size);
        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            if (c->w > 0 && c->w > sum) {
                sum = c->w;
            }
        } else if (c->kind == YL_BOX_BLOCK) {
            /* Block children inside a cell (nested layout); use the
			 * larger of: sum of inline run so far, recursive measure
			 * of the nested block's content. A row of two block
			 * siblings stacks them vertically — each contributes its
			 * own max-line, but they don't add horizontally. */
            if (sum > max_line) {
                max_line = sum;
            }
            sum = 0;
            float nested = measure_cell_content_width(r, cidx, budget);
            if (nested > max_line) {
                max_line = nested;
            }
        }
    }
    if (sum > max_line) {
        max_line = sum;
    }
    return max_line;
}

static struct float_result layout_table(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                        float origin_y, float content_w)
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
        return YETTY_OK(float, pad_top + pad_bottom);
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
        return YETTY_OK(float, pad_top + pad_bottom);
    }

    /* Content-aware column widths.
	 *
	 *   Pass A: per-column, accumulate the maximum unwrapped content
	 *           width across all rows. This is the "max-content" size.
	 *
	 *   Pass B: distribute content_width:
	 *           - if sum(max_content) <= content_width: each column
	 *             keeps its max_content, leftover stays unused (good
	 *             for narrow infobox label-value pairs that shouldn't
	 *             stretch to fill the whole table).
	 *           - else: scale each column down proportionally, but
	 *             clamp at min_w_per_col = max(font*4, content/cols*0.3)
	 *             so a single very-long column doesn't squeeze the
	 *             others to zero.
	 *
	 * Cells beyond the natural max_content of their column get the
	 * column's allocated width — they'll wrap internally. Cells with
	 * less content than their column get the column width and waste
	 * some space, which is what real CSS tables do too. */
    float *col_max = calloc(cols, sizeof(float));
    if (!col_max) {
        /* Fall back to even split on OOM. */
        float col_w = content_width / (float)cols;
        if (col_w < 1.0f) {
            col_w = 1.0f;
        }
        for (size_t i = 0; i < rows.count; i++) {
            uint32_t row_idx = rows.rows[i];
            struct yetty_ylexbor_box *row = &r->boxes.data[row_idx];
            float cursor_y_dummy = content_origin_y;
            (void)row;
            (void)cursor_y_dummy;
        }
        free(rows.rows);
        free(col_max);
        return YETTY_OK(float, pad_top + pad_bottom);
    }
    for (size_t i = 0; i < rows.count; i++) {
        uint32_t row_idx = rows.rows[i];
        uint32_t col = 0;
        for (uint32_t cidx = r->boxes.data[row_idx].first_child; cidx != 0;
             cidx = r->boxes.data[cidx].next_sibling) {
            if (r->boxes.data[cidx].kind != YL_BOX_BLOCK) {
                continue;
            }
            if (col >= cols) {
                break;
            }
            int budget = YL_CELL_MEASURE_BUDGET;
            float w = measure_cell_content_width(r, cidx, &budget);
            /* +small padding budget so cells aren't packed to the
			 * exact glyph extent. */
            w += 8.0f;
            if (w > col_max[col]) {
                col_max[col] = w;
            }
            col++;
        }
    }
    float total_max = 0;
    for (uint32_t i = 0; i < cols; i++) {
        total_max += col_max[i];
    }
    /* col_w[i] is the final per-column width. */
    float *col_w = calloc(cols, sizeof(float));
    if (!col_w) {
        free(col_max);
        free(rows.rows);
        return YETTY_OK(float, pad_top + pad_bottom);
    }
    if (total_max <= content_width || total_max == 0.0f) {
        /* Plenty of slack — give each column exactly its
		 * max-content. Tables narrower than the container don't
		 * stretch unless an author asks. */
        for (uint32_t i = 0; i < cols; i++) {
            col_w[i] = col_max[i] > 0 ? col_max[i] : content_width / (float)cols;
        }
    } else {
        /* Squeeze: proportional scale-down with a per-column floor so
		 * very-long columns don't crush their neighbours. */
        float min_per_col = content_width / (float)cols * 0.30f;
        float floor_used = 0;
        int floored = 0;
        for (uint32_t i = 0; i < cols; i++) {
            if (col_max[i] < min_per_col) {
                col_w[i] = col_max[i];
                floor_used += col_max[i];
            } else {
                col_w[i] = -1; /* mark for second pass */
                floored++;
            }
        }
        float remaining = content_width - floor_used;
        float scale_basis = 0;
        for (uint32_t i = 0; i < cols; i++) {
            if (col_w[i] < 0) {
                scale_basis += col_max[i];
            }
        }
        if (scale_basis > 0 && remaining > 0) {
            float scale = remaining / scale_basis;
            for (uint32_t i = 0; i < cols; i++) {
                if (col_w[i] < 0) {
                    col_w[i] = col_max[i] * scale;
                    if (col_w[i] < min_per_col) {
                        col_w[i] = min_per_col;
                    }
                }
            }
        } else {
            for (uint32_t i = 0; i < cols; i++) {
                if (col_w[i] < 0) {
                    col_w[i] = content_width / (float)cols;
                }
            }
        }
        (void)floored;
    }
    free(col_max);

    float cursor_y = content_origin_y;
    for (size_t i = 0; i < rows.count; i++) {
        uint32_t row_idx = rows.rows[i];
        struct yetty_ylexbor_box *row = &r->boxes.data[row_idx];
        row->x = content_origin_x;
        row->y = cursor_y;
        row->w = content_width;

        float row_h = 0;
        float cell_x = content_origin_x;
        uint32_t col = 0;
        for (uint32_t cidx = row->first_child; cidx != 0; cidx = r->boxes.data[cidx].next_sibling) {
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            if (c->kind != YL_BOX_BLOCK) {
                continue;
            }
            float this_col_w = (col < cols) ? col_w[col] : (content_width / (float)cols);
            if (this_col_w < 1.0f) {
                this_col_w = 1.0f;
            }
            c->x = cell_x;
            c->y = cursor_y;
            c->w = this_col_w;
            struct float_result cell_res = layout_block(r, cidx, cell_x, cursor_y, this_col_w);
            if (YETTY_IS_ERR(cell_res)) {
                free(col_w);
                free(rows.rows);
                return YETTY_ERR(float, "layout_table: cell layout_block", cell_res);
            }
            float h = cell_res.value;
            /* Refetch — vector may have moved. */
            c = &r->boxes.data[cidx];
            row = &r->boxes.data[row_idx];
            c->h = h;
            if (h > row_h) {
                row_h = h;
            }
            cell_x += this_col_w;
            col++;
        }

        /* Equalise cell heights to the row's tallest cell so adjacent
		 * cells line up at the bottom edge — same convention CSS uses
		 * for vertical-align: top. */
        for (uint32_t cidx = row->first_child; cidx != 0; cidx = r->boxes.data[cidx].next_sibling) {
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            if (c->kind != YL_BOX_BLOCK) {
                continue;
            }
            c->h = row_h;
        }

        row->h = row_h;
        cursor_y += row_h;
    }

    free(col_w);
    free(rows.rows);
    return YETTY_OK(float, (cursor_y - origin_y) + pad_bottom);
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
    float left_bottom; /* absolute y where the active left float ends */
    float left_width;  /* horizontal pixels consumed by the left float
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

static struct float_result layout_block(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                        float origin_y, float content_w)
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
                float fw;
                if (c->css_width > 0.0f) {
                    fw = c->css_width;
                } else if (c->css_width < 0.0f) {
                    fw = content_width * (-c->css_width);
                } else {
                    /* Auto-width float — CSS would shrink-to-fit
					 * from the content's intrinsic size. We don't
					 * have an intrinsic-size pass, so:
					 *
					 *   1. If the float contains an <img> with a
					 *      known natural width, use that — covers
					 *      Wikipedia's <figure class="mw-halign-*">
					 *      pattern (figure wraps an img with
					 *      explicit width/height attrs).
					 *   2. Otherwise fall back to min(33% of the
					 *      content area, 300 px).
					 *
					 * NEVER default to avail_w: doing so swallowed
					 * the full row when no width was set, leaving
					 * in-flow content with 0 px to wrap into and
					 * producing one-glyph-per-line garbage at the
					 * right edge. Test
					 * `test_float_no_width_doesnt_swallow_row`. */
                    float img_w = 0.0f;
                    for (uint32_t scan = c->first_child; scan != 0;
                         scan = r->boxes.data[scan].next_sibling) {
                        struct yetty_ylexbor_box *cs = &r->boxes.data[scan];
                        if (cs->kind == YL_BOX_INLINE_IMAGE && cs->w > 0.0f && cs->w > img_w) {
                            img_w = cs->w;
                        }
                    }
                    if (img_w > 0.0f) {
                        fw = img_w;
                    } else {
                        fw = content_width / 3.0f;
                        if (fw > 300.0f) {
                            fw = 300.0f;
                        }
                    }
                }
                if (fw <= 0) {
                    fw = content_width > 0 ? content_width / 3.0f : 1;
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
                struct float_result float_res = layout_block(r, cidx, fx, fy, fw);
                YETTY_RETURN_IF_ERR(float, float_res, "layout_block: float child");
                float fh = float_res.value;
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
			 *   - explicit `width: <px>` pins it (positive value),
			 *   - explicit `width: N%` (stored as -N/100 by the
			 *     libcss bridge) resolves against the parent's
			 *     content area (avail_w),
			 *   - `max-width` clamps from above,
			 *   - `min-width` clamps from below.
			 * The default is the available rectangle minus the
			 * child's left+right margins. */
            float avail = avail_w - c->margin_left - c->margin_right;
            if (avail < 0) {
                avail = 0;
            }
            float child_w;
            if (c->css_width > 0.0f) {
                child_w = c->css_width;
            } else if (c->css_width < 0.0f) {
                child_w = avail_w * (-c->css_width);
            } else {
                child_w = avail;
            }

            /* <figure> shrink-to-fit. Without this, a Wikipedia article
			 * figure (block with auto width) inherits the full body
			 * content_w (~1080 px on a desktop viewport), and its
			 * <figcaption> child — which is also a block with auto
			 * width — wraps at 1080 px even though the contained <img>
			 * is only ~280 px wide. The visual result is a tall caption
			 * line spanning almost the entire viewport, mixing
			 * visually with adjacent body text — the user perceives
			 * descender letters (p / q / y / g) from one row "leaking"
			 * onto the line above. Real browsers honour
			 * `figure { display: table }` (or the MediaWiki-supplied
			 * `width: <px>`) to size the figure to its image; we don't
			 * model either path reliably (libcss reports CSS_DISPLAY_TABLE
			 * for figure but layout_block bounces it back to BLOCK to
			 * avoid the table-row scanner), so do the shrink-to-fit
			 * here at the geometry boundary: if the figure descendant
			 * has a known-width <img>, clamp the figure width to that
			 * image's natural width. */
            if (c->element != NULL && c->element->node.local_name == LXB_TAG_FIGURE &&
                c->css_width == 0.0f) {
                int budget = 32;
                float fig_img_w = find_descendant_img_width(r, cidx, &budget);
                if (fig_img_w > 0.0f && fig_img_w < child_w) {
                    child_w = fig_img_w;
                }
            }

            float resolved_max = c->css_max_width > 0.0f   ? c->css_max_width
                                 : c->css_max_width < 0.0f ? avail_w * (-c->css_max_width)
                                                           : 0.0f;
            float resolved_min = c->css_min_width > 0.0f   ? c->css_min_width
                                 : c->css_min_width < 0.0f ? avail_w * (-c->css_min_width)
                                                           : 0.0f;
            if (resolved_max > 0.0f && child_w > resolved_max) {
                child_w = resolved_max;
            }
            if (resolved_min > 0.0f && child_w < resolved_min) {
                child_w = resolved_min;
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
            struct float_result child_res =
                layout_block(r, cidx, child_origin_x, cursor_y, child_w);
            YETTY_RETURN_IF_ERR(float, child_res, "layout_block: child block");
            float child_h = child_res.value;
            /* Re-fetch — vector may have relocated. */
            c = &r->boxes.data[cidx];
            /* `height: <px>` from CSS pins; `height: N%` resolves
			 * against the parent's content-area height when known
			 * (we don't have one for a streaming page, so fall
			 * back to content height — same as auto). Otherwise
			 * content height wins. */
            if (c->css_height > 0.0f) {
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
            struct float_result wrap_res =
                wrap_inline_box(r, cidx, avail_x, cursor_y, avail_w, self->text_align);
            YETTY_RETURN_IF_ERR(float, wrap_res, "layout_block: wrap_inline_box");
            cursor_y += wrap_res.value;
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
    return YETTY_OK(float, (cursor_y - origin_y) + pad_bottom);
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

    struct float_result layout_res = layout_block(r, 0, 0, 0, (float)r->viewport_w);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "ylexbor_layout: layout_block");
    float h = layout_res.value;
    r->boxes.data[0].h = h;
    r->content_height = (int)h;

    return YETTY_OK_VOID();
}
