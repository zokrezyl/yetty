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
static void abs_descendants_collect(struct yetty_ylexbor *r, uint32_t idx, uint32_t **list,
                                    uint32_t *count, uint32_t *cap);
static struct float_result layout_absolute_child(struct yetty_ylexbor *r, uint32_t cidx, float cb_x,
                                                 float cb_y, float cb_w, float cb_h);
static struct yetty_ycore_void_result flex_layout_absolute_children(struct yetty_ylexbor *r,
                                                                    uint32_t idx, float origin_x,
                                                                    float origin_y, float content_w,
                                                                    float container_h);
static struct float_result layout_grid(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                       float origin_y, float content_w);
static float measure_cell_content_width(struct yetty_ylexbor *r, uint32_t cell_idx, int *budget);
static void resolve_pct_metrics(struct yetty_ylexbor_box *box, float cb_width);
#define YL_CELL_MEASURE_BUDGET 256

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
    /* Computed `line-height` when the cascade set one; otherwise the
     * default `normal` line box (~1.25 × font). */
    float line_height = b->line_height > 0.0f ? b->line_height : font_size * 1.25f;
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

    /* Per-glyph advance: a box that set its own ratio (Ahem = exactly 1.0)
	 * wins; otherwise the engine-global estimate. Snapshot the ratio now — it's
	 * also passed to every naive_text_width below — because `b` is invalidated
	 * once emit_fragment grows the box vector. */
    float advance_ratio =
        b->glyph_advance > 0.0f ? b->glyph_advance : yetty_ylexbor_glyph_advance_ratio(r);
    float per_glyph = font_size * advance_ratio;
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
    /* Only floor the wrap width for GENUINELY tiny containers (fewer than ~2
	 * glyphs), where wrapping would otherwise scatter one glyph per line. A
	 * container that legitimately fits >= 2 glyphs keeps its real width — the
	 * old unconditional `per_glyph * 8` floor scaled with font-size, so a 300px
	 * box in a 100px font (e.g. WPT's Ahem tests, where each glyph is exactly
	 * 1em) got a 800px floor and never wrapped. */
    float min_wrap_w = per_glyph * 8.0f; /* roughly one short word */
    if (content_w < per_glyph * 2.0f && wrap_w < min_wrap_w) {
        wrap_w = min_wrap_w;
    }
    /* `white-space: nowrap` content: no soft wrapping at all — the line
	 * overflows its container exactly as in real browsers (a gnews nav
	 * chip 1px too narrow must overflow by 1px, not wrap its label onto
	 * a second line and double the row height). Explicit '\n' still
	 * breaks via the hard-break path. */
    if (b->nowrap) {
        wrap_w = 1e9f;
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
            /* A TRAILING '\n' (last byte — produced by a trailing `<br>`,
			 * "foo<br>" → "foo\n") leaves cursor == n: there is no following
			 * line, so stop. Without this the fit-loop below runs zero times,
			 * leaves fit/end at 0, and the `end <= cursor` branch resets cursor
			 * to 0 — an infinite loop that emits fragments until the process
			 * OOMs (a <br>-terminated run on Wikipedia drove the box vector past
			 * 20 GB). */
            if (cursor >= n) {
                break;
            }
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
            /* Never let cursor regress (end < cursor) — that re-processes the
			 * whole string forever. Advance at least one byte so the loop
			 * always terminates. */
            cursor = (end > cursor) ? end : cursor + 1;
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
        float line_w =
            yetty_ylexbor_naive_text_width(text + cursor, end - cursor, font_size, advance_ratio);
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
            float frag_w =
                yetty_ylexbor_naive_text_width(text + s0, s1 - s0, font_size, advance_ratio);
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

/* Place a flex container's absolutely-positioned / fixed children once the
 * container's own size is known. Absolute resolves against the container's
 * padding box (the relative-positioned card), fixed against the viewport. */
static struct yetty_ycore_void_result flex_layout_absolute_children(struct yetty_ylexbor *r,
                                                                    uint32_t idx, float origin_x,
                                                                    float origin_y, float content_w,
                                                                    float container_h)
{
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];
    float cb_x = origin_x + self->border_left;
    float cb_y = origin_y + self->border_top;
    float cb_w = content_w;
    float cb_h = container_h - self->border_top - self->border_bottom;
    if (cb_h < 0.0f) {
        cb_h = 0.0f;
    }
    int is_containing_block = (idx == 0) || (r->boxes.data[idx].position != YL_POS_STATIC);
    uint32_t *deferred_absolute = NULL;
    uint32_t deferred_count = 0;
    uint32_t deferred_cap = 0;
    if (is_containing_block) {
        for (uint32_t child_idx = r->boxes.data[idx].first_child; child_idx != 0;
             child_idx = r->boxes.data[child_idx].next_sibling) {
            if (r->boxes.data[child_idx].position == YL_POS_STATIC) {
                abs_descendants_collect(r, child_idx, &deferred_absolute, &deferred_count,
                                        &deferred_cap);
            }
        }
    }
    uint32_t direct_child = r->boxes.data[idx].first_child;
    uint32_t deferred_index = 0;
    for (;;) {
        uint32_t child_idx = 0;
        if (direct_child != 0) {
            child_idx = direct_child;
            direct_child = r->boxes.data[direct_child].next_sibling;
        } else if (deferred_index < deferred_count) {
            child_idx = deferred_absolute[deferred_index++];
        } else {
            break;
        }
        uint8_t child_pos = r->boxes.data[child_idx].position;
        if (child_pos != YL_POS_ABSOLUTE && child_pos != YL_POS_FIXED) {
            continue;
        }
        if (child_pos == YL_POS_ABSOLUTE && !is_containing_block) {
            continue; /* the nearest positioned ancestor places it */
        }
        struct float_result placement_res =
            (child_pos == YL_POS_FIXED)
                ? layout_absolute_child(r, child_idx, 0.0f, 0.0f, (float)r->viewport_w,
                                        (float)r->viewport_h)
                : layout_absolute_child(r, child_idx, cb_x, cb_y, cb_w, cb_h);
        if (YETTY_IS_ERR(placement_res)) {
            free(deferred_absolute);
            return YETTY_ERR(yetty_ycore_void,
                             "flex_layout_absolute_children: layout_absolute_child", placement_res);
        }
        self = &r->boxes.data[idx];
    }
    free(deferred_absolute);
    (void)self;
    return YETTY_OK_VOID();
}

/* Mirror flex items along one axis inside [origin, origin+extent] — the
 * placement half of row-reverse / column-reverse / wrap-reverse. Moved
 * BLOCK/TEXT subtrees are re-laid at the mirrored origin so descendants
 * follow; the caller re-pins any cross-axis size it computed. */
static struct yetty_ycore_void_result flex_mirror_axis(struct yetty_ylexbor *r,
                                                       const uint32_t *children,
                                                       uint32_t n_children, bool mirror_y,
                                                       float origin, float extent)
{
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        float old_pos = mirror_y ? c->y : c->x;
        float size = mirror_y ? c->h : c->w;
        float mirrored = origin + extent - (old_pos - origin) - size;
        if (mirrored < origin) {
            mirrored = origin; /* overflowing content stays inside */
        }
        if (mirrored == old_pos) {
            continue;
        }
        float keep_w = c->w;
        float keep_h = c->h;
        if (mirror_y) {
            c->y = mirrored;
        } else {
            c->x = mirrored;
        }
        if (c->kind == YL_BOX_BLOCK) {
            struct float_result relay_res = layout_block(r, cidx, c->x, c->y, keep_w);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, relay_res, "flex mirror: re-lay block");
            c = &r->boxes.data[cidx];
        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            struct float_result relay_res =
                wrap_inline_box(r, cidx, c->x, c->y, keep_w, /*text_align=*/0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, relay_res, "flex mirror: re-lay inline");
            c = &r->boxes.data[cidx];
        }
        c->w = keep_w;
        c->h = keep_h;
    }
    return YETTY_OK_VOID();
}

/* Longest unbreakable word of a text box, in px — the reduced automatic
 * minimum (min-content) a flex item must not shrink below. Word boundaries
 * only; no hyphenation/break-opportunity analysis. */
static float flex_text_min_content(const struct yetty_ylexbor *r,
                                   const struct yetty_ylexbor_box *box)
{
    if (box->kind != YL_BOX_INLINE_TEXT || !box->text || box->text_len == 0) {
        return 0.0f;
    }
    float advance =
        box->glyph_advance > 0.0f ? box->glyph_advance : yetty_ylexbor_glyph_advance_ratio(r);
    size_t longest_start = 0, longest_len = 0;
    size_t run_start = 0, run_len = 0;
    for (size_t i = 0; i <= box->text_len; i++) {
        unsigned char ch = i < box->text_len ? (unsigned char)box->text[i] : ' ';
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (run_len > longest_len) {
                longest_start = run_start;
                longest_len = run_len;
            }
            run_start = i + 1;
            run_len = 0;
        } else {
            run_len++;
        }
    }
    if (longest_len == 0) {
        return 0.0f;
    }
    return yetty_ylexbor_naive_text_width(box->text + longest_start, longest_len, box->font_size,
                                          advance);
}

/* Recursive (budgeted) min-content width: a replaced item's intrinsic
 * width, a text run's longest word, a nowrap flex row's SUM of item
 * minimums (it cannot wrap, so nothing inside can give up width), any
 * other block's widest in-flow child. Out-of-flow children contribute
 * nothing. */
static float flex_item_min_content_walk(const struct yetty_ylexbor *r, uint32_t idx, int *budget)
{
    if (*budget <= 0) {
        return 0.0f;
    }
    (*budget)--;
    const struct yetty_ylexbor_box *box = &r->boxes.data[idx];
    if (box->kind == YL_BOX_INLINE_IMAGE) {
        /* A nested image is FLEXIBLE: responsive pages give it
		 * max-width:100%, so its intrinsic width must not become a hard
		 * minimum for every flex ancestor (a 1424px hero screenshot
		 * inflated github's whole feature column past the viewport).
		 * Direct image flex items keep their intrinsic minimum via
		 * flex_item_auto_min's image parameter. */
        return 0.0f;
    }
    if (box->kind == YL_BOX_INLINE_TEXT) {
        return flex_text_min_content(r, box);
    }
    int row_nowrap = box->layout_mode == YL_LAYOUT_FLEX_ROW && !box->flex_wrap;
    float sum = 0.0f;
    float widest = 0.0f;
    int in_flow = 0;
    for (uint32_t child = box->first_child; child != 0;
         child = r->boxes.data[child].next_sibling) {
        const struct yetty_ylexbor_box *child_box = &r->boxes.data[child];
        if (child_box->position == YL_POS_ABSOLUTE || child_box->position == YL_POS_FIXED ||
            child_box->float_side != 0) {
            continue;
        }
        /* A definite width pins the box: its min-content equals that width
		 * (padding included), so a fixed-width descendant sets a hard floor
		 * its flex ancestor must not shrink below — the gnews weather
		 * widget's width:180px tile. Percentages stay content-based. */
        float child_min = child_box->css_width > 0.0f
                              ? child_box->css_width + child_box->padding_left +
                                    child_box->padding_right
                              : flex_item_min_content_walk(r, child, budget);
        sum += child_min + child_box->margin_left + child_box->margin_right;
        if (child_min > widest) {
            widest = child_min;
        }
        in_flow++;
    }
    float own = row_nowrap ? sum + (in_flow > 1 ? (float)(in_flow - 1) * box->grid_col_gap : 0.0f)
                           : widest;
    return own + box->padding_left + box->padding_right;
}

/* Automatic minimum main size of a row flex item with min-width:auto
 * (spec: its min-content size, reduced model above). */
static float flex_item_auto_min(const struct yetty_ylexbor *r, uint32_t idx,
                                float image_intrinsic_w)
{
    const struct yetty_ylexbor_box *box = &r->boxes.data[idx];
    if (box->kind == YL_BOX_INLINE_IMAGE) {
        return image_intrinsic_w;
    }
    int budget = 192;
    return flex_item_min_content_walk(r, idx, &budget);
}

/* First-line baseline ascent of a flex item, from the container's top —
 * reduced: 0.8 × the leading text's font-size (no font metrics at layout
 * time); a replaced item's baseline is its bottom edge (`fallback_h`). */
static float flex_item_first_ascent(const struct yetty_ylexbor *r, uint32_t idx, float fallback_h)
{
    const struct yetty_ylexbor_box *box = &r->boxes.data[idx];
    if (box->kind == YL_BOX_INLINE_IMAGE) {
        return fallback_h;
    }
    if (box->kind == YL_BOX_INLINE_TEXT) {
        return box->font_size > 0.0f ? box->font_size * 0.8f : 12.8f;
    }
    for (uint32_t child = box->first_child; child != 0;
         child = r->boxes.data[child].next_sibling) {
        const struct yetty_ylexbor_box *child_box = &r->boxes.data[child];
        if (child_box->kind == YL_BOX_INLINE_TEXT) {
            return child_box->font_size > 0.0f ? child_box->font_size * 0.8f : 12.8f;
        }
    }
    return box->font_size > 0.0f ? box->font_size * 0.8f : 12.8f;
}

/* Per-item scratch for one layout_flex_run invocation: 1 u32 + 10 float
 * arrays + 2 bool + 1 u8 array, all of length `cap`, carved out of one
 * caller-provided block (stack for typical containers, heap beyond that —
 * flex recursion means the scratch cannot live on the engine). */
#define YL_FLEX_SCRATCH_BYTES(cap) ((size_t)(cap) * (11u * sizeof(uint32_t) + 3u))

static struct float_result layout_flex_run(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                           float origin_y, float content_w, bool column_dir,
                                           unsigned char *scratch, uint32_t scratch_cap)
{
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];

    /* Carve the per-item arrays out of the scratch block. The zero fill
	 * doubles as every original `= {false}` / `= {YL_SRC_NONE}` (== 0)
	 * initializer. */
    memset(scratch, 0, YL_FLEX_SCRATCH_BYTES(scratch_cap));
    uint32_t *children = (uint32_t *)scratch;
    float *scratch_floats = (float *)(scratch + (size_t)scratch_cap * sizeof(uint32_t));
    float *margin_main_start = scratch_floats + (size_t)scratch_cap * 0;
    float *margin_main_total = scratch_floats + (size_t)scratch_cap * 1;
    float *margin_cross_start = scratch_floats + (size_t)scratch_cap * 2;
    float *margin_cross_total = scratch_floats + (size_t)scratch_cap * 3;
    float *main_size = scratch_floats + (size_t)scratch_cap * 4;
    float *img_w_intr = scratch_floats + (size_t)scratch_cap * 5;
    float *img_h_intr = scratch_floats + (size_t)scratch_cap * 6;
    float *item_content = scratch_floats + (size_t)scratch_cap * 7;
    float *natural_h = scratch_floats + (size_t)scratch_cap * 8;
    float *natural_w = scratch_floats + (size_t)scratch_cap * 9;
    bool *is_auto = (bool *)(scratch + (size_t)scratch_cap * 11u * sizeof(uint32_t));
    bool *min_locked = is_auto + scratch_cap;
    uint8_t *src_main = (uint8_t *)(min_locked + scratch_cap);
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
    uint32_t n_children = 0;
    for (uint32_t cidx = self->first_child; cidx != 0; cidx = r->boxes.data[cidx].next_sibling) {
        if (n_children >= scratch_cap) {
            break; /* wrapper counted — only reachable on a caller bug */
        }
        if (r->boxes.data[cidx].float_side != 0) {
            continue;
        }
        /* Absolutely-positioned / fixed children are out of flow — they are
         * not flex items. Placed against this container in the pass below;
         * the spec's static position treats each as the SOLE flex item, so
         * align-items / justify-content shift the hypothetical box. The
         * horizontal axis is computable here (content_width is known; the
         * child's explicit CSS width stands in for its hypothetical size —
         * gnews centers 1px overlay anchors exactly this way); the
         * container's vertical extent isn't known yet, so the vertical
         * static position stays at the content origin. */
        if (r->boxes.data[cidx].position == YL_POS_ABSOLUTE ||
            r->boxes.data[cidx].position == YL_POS_FIXED) {
            struct yetty_ylexbor_box *out_of_flow = &r->boxes.data[cidx];
            float child_w = out_of_flow->css_width > 0.0f ? out_of_flow->css_width : 0.0f;
            float static_x = content_origin_x + out_of_flow->margin_left;
            int horizontal_align =
                column_dir ? (out_of_flow->align_self != 0 ? out_of_flow->align_self : align)
                           : justify;
            if (column_dir ? (horizontal_align == CSS_ALIGN_ITEMS_CENTER)
                           : (horizontal_align == CSS_JUSTIFY_CONTENT_CENTER)) {
                static_x = content_origin_x + (content_width - child_w) * 0.5f;
            } else if (column_dir ? (horizontal_align == CSS_ALIGN_ITEMS_FLEX_END)
                                  : (horizontal_align == CSS_JUSTIFY_CONTENT_FLEX_END)) {
                static_x = content_origin_x + content_width - child_w - out_of_flow->margin_right;
            }
            out_of_flow->static_x = static_x;
            out_of_flow->static_y = content_origin_y + out_of_flow->margin_top;
            out_of_flow->static_pos_set = true;
            continue;
        }
        children[n_children++] = cidx;
    }
    /* CSS `order`: stable insertion sort — equal orders keep document
	 * order, negative orders move ahead of the default 0. */
    for (uint32_t i = 1; i < n_children; i++) {
        uint32_t moved = children[i];
        int32_t moved_order = r->boxes.data[moved].flex_order;
        uint32_t j = i;
        while (j > 0 && r->boxes.data[children[j - 1]].flex_order > moved_order) {
            children[j] = children[j - 1];
            j--;
        }
        children[j] = moved;
    }
    if (n_children == 0) {
        /* Still place any out-of-flow children before bailing. */
        struct yetty_ycore_void_result abs_res = flex_layout_absolute_children(
            r, idx, origin_x, origin_y, content_w, pad_top + pad_bottom);
        YETTY_RETURN_IF_ERR(float, abs_res, "layout_flex: place out-of-flow (empty)");
        return YETTY_OK(float, pad_top + pad_bottom);
    }

    /* Flex `gap` between items (stored in grid_col_gap by box-build). It
	 * consumes main-axis space before items are sized, so subtract the total
	 * inter-item gap from the budget; the placement loop re-inserts it. */
    float flex_gap = self->grid_col_gap > 0.0f ? self->grid_col_gap : 0.0f;
    float total_flex_gap = n_children > 1 ? (float)(n_children - 1) * flex_gap : 0.0f;

    /* Item margins consume main-axis space and offset each item on both
	 * axes (a gnews nav chip's `margin:4px 16px` is what separates the
	 * chips). Resolve percent metrics first — layout_block does this for
	 * its own in-flow children, but flex items are laid out here. */
    float total_margin_main = 0.0f;
    for (uint32_t i = 0; i < n_children; i++) {
        struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
        resolve_pct_metrics(c, content_width);
        if (column_dir) {
            margin_main_start[i] = c->margin_top;
            margin_main_total[i] = c->margin_top + c->margin_bottom;
            margin_cross_start[i] = c->margin_left;
            margin_cross_total[i] = c->margin_left + c->margin_right;
        } else {
            margin_main_start[i] = c->margin_left;
            margin_main_total[i] = c->margin_left + c->margin_right;
            margin_cross_start[i] = c->margin_top;
            margin_cross_total[i] = c->margin_top + c->margin_bottom;
        }
        total_margin_main += margin_main_total[i];
    }

    /* Main-axis budget. */
    float main_budget;
    if (column_dir) {
        main_budget = css_h > 0 ? css_h : 0;
    } else {
        main_budget = content_width;
    }
    main_budget -= total_flex_gap + total_margin_main;
    if (main_budget < 0.0f) {
        main_budget = 0.0f;
    }

    /* Resolve each child's hypothetical main-axis size. Encoding
	 * (shared with css_width / css_height):
	 *   > 0 = absolute px
	 *   < 0 = percent of main_budget (value is -N/100)
	 *   == 0 = auto / not set → fall back to css_width or
	 *          css_height (same encoding), else 0 and let the
	 *          autobasis branch below distribute space. */
    float total_basis = 0.0f;
    float total_grow = 0.0f;
    int autobasis_count = 0;

    /* Snapshot replaced-element (image) intrinsic sizes BEFORE the placement
     * loop overwrites c->w/c->h. A flex image must keep its own width/height
     * (from box-build: HTML width/height attrs or natural decode) — it must
     * not be auto-distributed across the row or stretched to the cross axis.
     * Without this a 14x14 source-favicon balloons to the column width with a
     * 100px fallback height and covers the article thumbnail (Google News). */
    for (uint32_t i = 0; i < n_children; i++) {
        const struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
        bool is_img = (c->kind == YL_BOX_INLINE_IMAGE);
        img_w_intr[i] = is_img ? c->w : 0.0f;
        img_h_intr[i] = is_img ? c->h : 0.0f;
        /* A flex image must never be WIDER than the container's content box —
		 * responsive images (max-width:100%) shrink to fit, keeping aspect
		 * ratio. Without this a 1600px hero image in a 600px flex column
		 * (github's vertical Stack galleries) overflowed and, stacked 4-high,
		 * inflated the section to ~5600px (3-4x Chrome). */
        if (is_img && img_w_intr[i] > content_width && content_width > 0.0f) {
            img_h_intr[i] *= content_width / img_w_intr[i];
            img_w_intr[i] = content_width;
        }
    }

    /* Main-axis size provenance per item, kept in lockstep with
	 * main_size[] through every branch below and stamped on the box at
	 * placement. */

    for (uint32_t i = 0; i < n_children; i++) {
        struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
        float basis;
        float fbp = c->flex_basis_px;
        if (fbp > 0.0f) {
            basis = fbp;
            src_main[i] = YL_SRC_FLEX_BASIS;
        } else if (fbp < 0.0f) {
            basis = main_budget * (-fbp);
            src_main[i] = YL_SRC_FLEX_BASIS;
        } else {
            float css_main = column_dir ? c->css_height : c->css_width;
            float img_main = column_dir ? img_h_intr[i] : img_w_intr[i];
            if (css_main > 0.0f) {
                basis = css_main;
                src_main[i] = YL_SRC_CSS;
            } else if (css_main < 0.0f) {
                basis = main_budget * (-css_main);
                src_main[i] = YL_SRC_CSS;
            } else if (img_main > 0.0f) {
                /* Image with no flex-basis sizes to its intrinsic main-axis
                 * dimension, not the auto-distributed share. */
                basis = img_main;
                src_main[i] = YL_SRC_IMG_INTRINSIC;
            } else {
                basis = 0.0f;
                is_auto[i] = true;
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
        /* Even split assumes every item is content with its 1/n share. For a
		 * ROW, if any item's max-content is wider than that share, splitting
		 * evenly would wrap its content (the symptom: a 12-item nav crammed into
		 * 48px cells with every label wrapped onto three lines). In that case
		 * the row is content-driven — size each item to its own max-content and
		 * let justify-content place them (flex-start by default), which is what
		 * Chrome produces for a content-sized nav/toolbar. When every item fits
		 * its share, the even split is kept (equal-width header columns that
		 * authors express via bare flex without basis/grow). */
        bool content_driven = false;
        if (!column_dir) {
            for (uint32_t i = 0; i < n_children; i++) {
                int measure_budget = YL_CELL_MEASURE_BUDGET;
                const struct yetty_ylexbor_box *ci = &r->boxes.data[children[i]];
                item_content[i] = measure_cell_content_width(r, children[i], &measure_budget) +
                                  ci->padding_left + ci->padding_right;
                if (item_content[i] > per + 0.5f) {
                    content_driven = true;
                }
            }
            /* A distributing justify-content only means anything when the
			 * items are content-sized: `space-between` over evenly-split
			 * items degenerates to zero visible gaps (the header-bar
			 * symptom: logo and nav each take half the row instead of
			 * shrink-to-fit at the two edges). Chrome sizes auto-basis
			 * items to max-content in every one of these modes. */
            if (justify == CSS_JUSTIFY_CONTENT_SPACE_BETWEEN ||
                justify == CSS_JUSTIFY_CONTENT_SPACE_AROUND ||
                justify == CSS_JUSTIFY_CONTENT_SPACE_EVENLY ||
                justify == CSS_JUSTIFY_CONTENT_CENTER || justify == CSS_JUSTIFY_CONTENT_FLEX_END) {
                content_driven = true;
            }
        }
        if (content_driven) {
            total_basis = 0.0f;
            for (uint32_t i = 0; i < n_children; i++) {
                /* +1px slack: an item sized to EXACTLY its measured
				 * max-content can wrap its last word on float rounding. */
                main_size[i] = item_content[i] + 1.0f;
                is_auto[i] = false;
                src_main[i] = YL_SRC_CONTENT;
                total_basis += main_size[i];
            }
            autobasis_count = 0;
        } else {
            for (uint32_t i = 0; i < n_children; i++) {
                main_size[i] = per;
                src_main[i] = YL_SRC_FLEX_EVEN;
            }
            total_basis = main_budget;
        }
    }

    /* Mixed sized + auto items with no explicit grow: a non-growing
     * auto-basis item is CONTENT-sized (max-content), capped at its fair
     * share of the leftover. This serves both dominant patterns: a card's
     * flexible long-text body measures wider than the leftover and gets
     * capped to fill it, while a short label next to an icon (gnews
     * source-attribution rows) keeps its text width instead of seizing
     * the whole remainder — which is what Chrome produces. Skipped when
     * something grows (that path distributes below) or when ALL items
     * are auto (the even-split fallback above already handled it). */
    if (autobasis_count > 0 && autobasis_count < (int)n_children && total_grow == 0.0f &&
        !column_dir) {
        float leftover_auto = main_budget - total_basis;
        float per = leftover_auto > 0.0f ? leftover_auto / (float)autobasis_count : 0.0f;
        for (uint32_t i = 0; i < n_children; i++) {
            if (!is_auto[i]) {
                continue;
            }
            int measure_budget = YL_CELL_MEASURE_BUDGET;
            const struct yetty_ylexbor_box *auto_item = &r->boxes.data[children[i]];
            /* +1px slack: an item sized to exactly its measured
			 * max-content can wrap its last word on float rounding. */
            float content_main = measure_cell_content_width(r, children[i], &measure_budget) +
                                 auto_item->padding_left + auto_item->padding_right + 1.0f;
            if (leftover_auto > 0.0f) {
                float share = content_main < per ? content_main : per;
                main_size[i] += share;
                src_main[i] = content_main < per ? YL_SRC_CONTENT : YL_SRC_FLEX_SHARE;
                total_basis += share;
            } else {
                /* No leftover (a width:100% sibling already claimed the
				 * row): the auto item STILL gets its content basis — the
				 * shrink pass below resolves the overflow against every
				 * item's flex-shrink and automatic minimum, exactly the
				 * spec order (github header: react-partial nav next to a
				 * width-full search box rendered 0-wide without this). */
                main_size[i] = content_main;
                src_main[i] = YL_SRC_CONTENT;
                total_basis += content_main;
            }
        }
    } else if (autobasis_count > 0 && autobasis_count < (int)n_children && total_grow == 0.0f) {
        /* Column direction: no main-axis content measurer (heights come
		 * from layout) — keep the historical leftover split. */
        float leftover_auto = main_budget - total_basis;
        if (leftover_auto > 0.0f) {
            float per = leftover_auto / (float)autobasis_count;
            for (uint32_t i = 0; i < n_children; i++) {
                if (is_auto[i]) {
                    main_size[i] += per;
                    src_main[i] = YL_SRC_FLEX_SHARE;
                    total_basis += per;
                }
            }
        }
    } else if (autobasis_count > 0 && total_grow > 0.0f && !column_dir) {
        /* Auto items where SOMETHING grows — including the all-auto row
		 * (header pattern: content-sized logo + growing menu; without
		 * this the non-growing logo kept basis 0 and collapsed, since
		 * the even-split fallback requires total_grow == 0 and the
		 * mixed branch required a sized sibling): `flex-basis:auto`
         * resolves to the item's content size — growers then take only the
         * genuine leftover and non-growing auto items keep their content
         * width. Without this every auto item started at 0 and the grower
         * seized the whole row (the gnews header: `flex:1 0 auto` logo +
         * `flex:1 1 100%` search bar + `flex:0 0 auto` buttons rendered
         * 0 / full-width / 0). Overflow is resolved by the shrink pass
         * below, which respects each item's flex-shrink (the search bar's
         * `1 1 100%` gives back exactly what the content-sized siblings
         * need, as in Chrome). */
        for (uint32_t i = 0; i < n_children; i++) {
            if (!is_auto[i]) {
                continue;
            }
            int measure_budget = YL_CELL_MEASURE_BUDGET;
            const struct yetty_ylexbor_box *auto_item = &r->boxes.data[children[i]];
            /* +1px slack: an item sized to exactly its measured
			 * max-content can wrap its last word on float rounding. */
            float content_main = measure_cell_content_width(r, children[i], &measure_budget) +
                                 auto_item->padding_left + auto_item->padding_right + 1.0f;
            if (content_main > main_budget) {
                content_main = main_budget;
            }
            main_size[i] = content_main;
            src_main[i] = YL_SRC_CONTENT;
            total_basis += content_main;
        }
    }

    /* flex-wrap: break the row into multiple flex lines by hypothetical
	 * main sizes, then run flexible-length resolution PER LINE — each
	 * line's leftover distributes across that line's growers (the spec's
	 * per-line algorithm; without it a wrapped card grid keeps its cards
	 * at basis width and leaves ragged right edges Chrome fills). Lines
	 * stack on the cross axis, tallest item drives each line's height.
	 * Row direction only; column wrap is not modelled. */
    if (self->flex_wrap && !column_dir) {
        float line_top = content_origin_y;
        float last_line_h = 0.0f;
        uint32_t line_start = 0;
        float line_used = 0.0f;
        for (uint32_t i = 0; i <= n_children; i++) {
            bool flush_line = (i == n_children) && i > line_start;
            if (i < n_children) {
                float item_main = main_size[i];
                /* An auto-basis item with no resolved main size (no width,
				 * no flex-basis) takes the full line instead of collapsing
				 * to 0 — the dominant wrap idiom is a full-width content
				 * block sharing the container with fixed-size banner/ad
				 * slots, and Chrome wraps each such block onto its own
				 * line. */
                if (item_main <= 0.0f && is_auto[i]) {
                    item_main = content_width;
                    src_main[i] = YL_SRC_AVAIL;
                }
                /* A single item never exceeds the line. */
                if (item_main > content_width) {
                    item_main = content_width;
                }
                main_size[i] = item_main;
                float extent =
                    item_main + margin_main_total[i] + (i > line_start ? flex_gap : 0.0f);
                if (i > line_start && line_used + extent > content_width + 0.5f) {
                    flush_line = true;
                } else {
                    line_used += extent;
                }
            }
            if (flush_line) {
                uint32_t line_end = i; /* exclusive */
                /* Per-line flexible-length resolution: this line's leftover
				 * goes to this line's growers. */
                float line_grow = 0.0f;
                for (uint32_t k = line_start; k < line_end; k++) {
                    float grow = r->boxes.data[children[k]].flex_grow;
                    if (grow > 0.0f) {
                        line_grow += grow;
                    }
                }
                float line_free = content_width - line_used;
                if (line_free > 0.0f && line_grow > 0.0f) {
                    for (uint32_t k = line_start; k < line_end; k++) {
                        float grow = r->boxes.data[children[k]].flex_grow;
                        if (grow > 0.0f) {
                            main_size[k] += line_free * (grow / line_grow);
                            src_main[k] = YL_SRC_FLEX_GROW;
                        }
                    }
                }
                /* Place the line. */
                float line_x = content_origin_x;
                float line_h = 0.0f;
                for (uint32_t k = line_start; k < line_end; k++) {
                    uint32_t cidx = children[k];
                    struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
                    c->x = line_x + margin_main_start[k];
                    c->y = line_top + margin_cross_start[k];
                    c->w = main_size[k];
                    c->width_source = src_main[k];
                    c->h = 0;
                    float h = 0.0f;
                    if (c->kind == YL_BOX_BLOCK) {
                        struct float_result wrap_block_res =
                            layout_block(r, cidx, c->x, c->y, c->w);
                        YETTY_RETURN_IF_ERR(float, wrap_block_res, "layout_flex(wrap): block");
                        h = wrap_block_res.value;
                    } else if (c->kind == YL_BOX_INLINE_TEXT) {
                        struct float_result wrap_text_res =
                            wrap_inline_box(r, cidx, c->x, c->y, c->w, /*text_align=*/0);
                        YETTY_RETURN_IF_ERR(float, wrap_text_res, "layout_flex(wrap): inline");
                        h = wrap_text_res.value;
                    } else if (c->kind == YL_BOX_INLINE_IMAGE) {
                        h = img_h_intr[k] > 0.0f ? img_h_intr[k] : (c->h > 0.0f ? c->h : 100.0f);
                    }
                    c = &r->boxes.data[cidx];
                    if (c->css_height > 0.0f) {
                        h = c->css_height;
                        c->height_source = YL_SRC_CSS;
                    } else {
                        c->height_source = YL_SRC_CONTENT;
                    }
                    c->h = h;
                    if (h + margin_cross_total[k] > line_h) {
                        line_h = h + margin_cross_total[k];
                    }
                    line_x += main_size[k] + margin_main_total[k] + flex_gap;
                }
                last_line_h = line_h;
                if (i < n_children) {
                    line_top += line_h + self->grid_row_gap;
                }
                /* The item that forced the flush opens the next line. */
                line_start = i;
                line_used = i < n_children ? main_size[i] + margin_main_total[i] : 0.0f;
            }
        }
        self = &r->boxes.data[idx];
        if (self->main_reverse && n_children > 0) {
            /* row-reverse inside each line: one whole-array x mirror works
			 * because every line spans the same content box. */
            struct yetty_ycore_void_result mirror_res = flex_mirror_axis(
                r, children, n_children, /*mirror_y=*/false, content_origin_x, content_width);
            YETTY_RETURN_IF_ERR(float, mirror_res, "layout_flex(wrap): reverse mirror");
            self = &r->boxes.data[idx];
        }
        if (self->wrap_reverse && n_children > 0) {
            /* wrap-reverse: the line STACK flips — first line lands at the
			 * bottom. Mirroring every item's y inside the stack extent flips
			 * whole lines while keeping each line intact. */
            float stack_extent = (line_top + last_line_h) - content_origin_y;
            struct yetty_ycore_void_result mirror_res = flex_mirror_axis(
                r, children, n_children, /*mirror_y=*/true, content_origin_y, stack_extent);
            YETTY_RETURN_IF_ERR(float, mirror_res, "layout_flex(wrap): wrap-reverse mirror");
            self = &r->boxes.data[idx];
        }
        float container_h = (line_top + last_line_h) - origin_y + pad_bottom;
        struct yetty_ycore_void_result abs_res =
            flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, container_h);
        YETTY_RETURN_IF_ERR(float, abs_res, "layout_flex(wrap): place out-of-flow");
        return YETTY_OK(float, container_h);
    }

    /* Distribute leftover main-axis space — the spec's resolve-flexible-
	 * lengths grow side. Each round restarts from the FLEX BASE sizes:
	 * distribute the free space by grow ratio, clamp any item that lands
	 * below its min-width or above its max-width, FREEZE the violators at
	 * the clamp, and redo with the remaining free space — until a round
	 * produces no new violation. (A one-shot distribution mis-sizes every
	 * layout where a min/max interacts with grow: `flex:1; min-width:200px`
	 * next to `flex:1` in a 300px row must resolve 200/100, not 150/150.) */
    float leftover = main_budget - total_basis;
    if (leftover > 0.0f && total_grow > 0.0f) {
        /* item_content is dead after the auto-basis section — reuse it as
		 * the base-size snapshot the rounds restart from. */
        for (uint32_t i = 0; i < n_children; i++) {
            item_content[i] = main_size[i];
        }
        for (int round = 0; round < 16; round++) {
            float free_space = main_budget;
            float round_grow = 0.0f;
            for (uint32_t i = 0; i < n_children; i++) {
                const struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
                free_space -= min_locked[i] ? main_size[i] : item_content[i];
                if (!min_locked[i] && c->flex_grow > 0.0f) {
                    round_grow += c->flex_grow;
                }
            }
            bool clamped = false;
            for (uint32_t i = 0; i < n_children; i++) {
                struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
                if (min_locked[i]) {
                    continue;
                }
                float target = item_content[i];
                if (c->flex_grow > 0.0f && round_grow > 0.0f && free_space > 0.0f) {
                    target += free_space * (c->flex_grow / round_grow);
                }
                float min_w = !column_dir ? c->css_min_width : 0.0f;
                float max_w = (!column_dir && c->css_max_width > 0.0f) ? c->css_max_width : 0.0f;
                if (min_w > 0.0f && target < min_w) {
                    main_size[i] = min_w;
                    min_locked[i] = true;
                    src_main[i] = YL_SRC_FLEX_MIN;
                    clamped = true;
                } else if (max_w > 0.0f && target > max_w) {
                    main_size[i] = max_w;
                    min_locked[i] = true;
                    src_main[i] = YL_SRC_FLEX_GROW;
                    clamped = true;
                } else {
                    main_size[i] = target;
                    if (c->flex_grow > 0.0f) {
                        src_main[i] = YL_SRC_FLEX_GROW;
                    }
                }
            }
            if (!clamped) {
                break;
            }
        }
        leftover = 0.0f;
        memset(min_locked, 0, n_children * sizeof(*min_locked)); /* reuse below */
    }

    /* flex-shrink: items overflow the main axis (total basis > budget) — shrink
	 * each by its `flex-shrink × basis` share of the overflow so they fit. This
	 * is what lets a `w-full` content column make room for a fixed-width sidebar
	 * (Tailwind `flex lg:flex-row` content+rail) instead of taking the whole row
	 * and collapsing the sidebar to 0. `shrink-0` (flex_shrink==0) items keep
	 * their size; unset (-1) defaults to the CSS initial 1. Only when the
	 * container has a DEFINITE main size — an auto-height flex COLUMN has
	 * main_budget==0 and grows to fit its content, so it must never shrink (that
	 * collapsed every column item to 0). */
    if (leftover < 0.0f && main_budget > 0.0f) {
        /* Spec resolve-flexible-lengths, shrink side. Rounds restart from
		 * the base sizes: distribute the overflow by SCALED shrink factor
		 * (flex-shrink × base size), clamp any item that would fall below
		 * its min-width floor, FREEZE it there, and redo with the
		 * remaining overflow — the old single pass + one-shot reclaim
		 * under-shrank whenever two or more items hit their floors. */
        for (uint32_t i = 0; i < n_children; i++) {
            item_content[i] = main_size[i]; /* base-size snapshot (see grow) */
        }
        for (int round = 0; round < 16; round++) {
            float free_space = main_budget;
            float total_scaled = 0.0f;
            for (uint32_t i = 0; i < n_children; i++) {
                free_space -= min_locked[i] ? main_size[i] : item_content[i];
                if (!min_locked[i]) {
                    float sh = r->boxes.data[children[i]].flex_shrink;
                    if (sh < 0.0f) {
                        sh = 1.0f;
                    }
                    total_scaled += sh * item_content[i];
                }
            }
            if (free_space >= 0.0f || total_scaled <= 0.0f) {
                break;
            }
            float overflow = -free_space;
            bool clamped = false;
            for (uint32_t i = 0; i < n_children; i++) {
                if (min_locked[i]) {
                    continue;
                }
                float sh = r->boxes.data[children[i]].flex_shrink;
                if (sh < 0.0f) {
                    sh = 1.0f;
                }
                float scaled = sh * item_content[i];
                float target =
                    item_content[i] - (scaled > 0.0f ? overflow * (scaled / total_scaled) : 0.0f);
                float floor_main = !column_dir ? r->boxes.data[children[i]].css_min_width : 0.0f;
                if (floor_main < 0.0f) {
                    floor_main = 0.0f;
                }
                /* min-width:auto — the spec's automatic minimum: don't
				 * shrink below the (reduced) content-based minimum, capped
				 * at the base size so a floor can never GROW an item. */
                if (!column_dir && floor_main <= 0.0f) {
                    floor_main = flex_item_auto_min(r, children[i], img_w_intr[i]);
                    if (floor_main > item_content[i]) {
                        floor_main = item_content[i];
                    }
                }
                if (target < floor_main) {
                    main_size[i] = floor_main;
                    min_locked[i] = true;
                    src_main[i] = YL_SRC_FLEX_MIN;
                    clamped = true;
                } else {
                    main_size[i] = target;
                    if (scaled > 0.0f) {
                        src_main[i] = YL_SRC_FLEX_SHRINK;
                    }
                }
            }
            if (!clamped) {
                break;
            }
        }
        leftover = 0.0f;
    }

    /* Auto margins on the MAIN axis absorb all positive free space before
	 * justify-content sees any — the navbar `margin-left:auto` push-right
	 * idiom. Row direction only: the box producer records auto flags for
	 * the horizontal margins. */
    if (!column_dir && leftover > 0.0f) {
        uint32_t auto_margin_count = 0;
        for (uint32_t i = 0; i < n_children; i++) {
            const struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
            auto_margin_count +=
                (c->margin_left_auto ? 1u : 0u) + (c->margin_right_auto ? 1u : 0u);
        }
        if (auto_margin_count > 0) {
            float share = leftover / (float)auto_margin_count;
            for (uint32_t i = 0; i < n_children; i++) {
                const struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
                if (c->margin_left_auto) {
                    margin_main_start[i] += share;
                    margin_main_total[i] += share;
                }
                if (c->margin_right_auto) {
                    margin_main_total[i] += share;
                }
            }
            leftover = 0.0f;
        }
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
    /* First pass — lay each child at its resolved main-size, learn
	 * natural cross-size. */
    float cursor = (column_dir ? content_origin_y : content_origin_x) + leading;
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        bool is_image = (c->kind == YL_BOX_INLINE_IMAGE);
        if (column_dir) {
            c->x = content_origin_x + margin_cross_start[i];
            c->y = cursor + margin_main_start[i];
            /* A column image keeps its intrinsic width; other items stretch
             * to the content width — UNLESS the resolved alignment is
			 * non-stretch, where the spec gives auto-width items their
			 * fit-content width (a Pillar's `align-items:flex-start`
			 * inline-flex link hugs its label instead of taking the
			 * column, as in Chrome). */
            int item_align_first = c->align_self != 0 ? c->align_self : align;
            int stretches = item_align_first == CSS_ALIGN_ITEMS_STRETCH || item_align_first == 0;
            if (is_image && img_w_intr[i] > 0.0f) {
                c->w = img_w_intr[i];
                c->width_source = YL_SRC_IMG_INTRINSIC;
            } else if (!stretches && c->css_width == 0.0f) {
                int measure_budget = YL_CELL_MEASURE_BUDGET;
                float fit = measure_cell_content_width(r, cidx, &measure_budget) +
                            c->padding_left + c->padding_right + 1.0f;
                float avail_cross = content_width - margin_cross_total[i];
                c->w = fit < avail_cross ? fit : avail_cross;
                c->width_source = YL_SRC_CONTENT;
            } else {
                c->w = content_width - margin_cross_total[i];
                c->width_source = YL_SRC_FLEX_STRETCH;
            }
            if (c->w < 0.0f) {
                c->w = 0.0f;
            }
            c->h = main_size[i];
            c->height_source = src_main[i];
        } else {
            c->x = cursor + margin_main_start[i];
            c->y = content_origin_y + margin_cross_start[i];
            c->w = main_size[i];
            c->width_source = src_main[i];
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
            /* Intrinsic height — NOT the 100px fallback that ballooned small
             * favicons. */
            h = img_h_intr[i] > 0.0f ? img_h_intr[i] : (c->h > 0.0f ? c->h : 100.0f);
        }
        c = &r->boxes.data[cidx];
        if (column_dir) {
            /* Main-axis (height) sizing: an item with an explicit basis /
			 * height keeps main_size[i]; an AUTO item (basis 0, no grow) sizes
			 * to its natural content height `h`. Previously main_size[i] was
			 * forced unconditionally, collapsing text-only items to 0 — which
			 * piled a flex column's items on top of each other (Google News
			 * story cards). */
            if (main_size[i] <= 0.0f) {
                main_size[i] = h;
                c->height_source = YL_SRC_CONTENT;
            }
            c->h = main_size[i];
        } else {
            /* Row direction: an explicit CSS height wins over the laid-out
			 * content height (an empty form control keeps its intrinsic /
			 * author height instead of collapsing to 0). */
            c->h = c->css_height > 0.0f ? c->css_height : h;
            c->height_source = c->css_height > 0.0f ? YL_SRC_CSS : YL_SRC_CONTENT;
        }
        natural_h[i] = c->h;
        natural_w[i] = c->w;
        cursor += main_size[i] + margin_main_total[i] + gap + flex_gap;
    }

    /* Cross-axis: tallest item (margin box) dictates row height (or the
	 * container's css_height when set). For align-items=stretch (default)
	 * we normalise every item to that cross extent. */
    float max_cross = 0.0f;
    for (uint32_t i = 0; i < n_children; i++) {
        float cross = (column_dir ? natural_w[i] : natural_h[i]) + margin_cross_total[i];
        if (cross > max_cross) {
            max_cross = cross;
        }
    }
    if (cross_budget < max_cross) {
        cross_budget = max_cross;
    }
    /* Baseline pre-pass (row direction, reduced model): the line's shared
	 * baseline is the tallest first-line ascent among baseline-aligned
	 * items; each such item then offsets by the ascent difference. */
    float max_baseline_ascent = 0.0f;
    if (!column_dir) {
        for (uint32_t i = 0; i < n_children; i++) {
            const struct yetty_ylexbor_box *c = &r->boxes.data[children[i]];
            int item_align = c->align_self != 0 ? c->align_self : align;
            if (item_align == CSS_ALIGN_ITEMS_BASELINE) {
                float ascent = flex_item_first_ascent(r, children[i], natural_h[i]);
                if (ascent > max_baseline_ascent) {
                    max_baseline_ascent = ascent;
                }
            }
        }
    }
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        /* align-self overrides the container's align-items per item. */
        int item_align = c->align_self != 0 ? c->align_self : align;
        int do_stretch = (item_align == CSS_ALIGN_ITEMS_STRETCH || item_align == 0);
        float cross = column_dir ? natural_w[i] : natural_h[i];
        float cross_origin = column_dir ? content_origin_x : content_origin_y;
        /* Stretch sizes the MARGIN box to the line — the border box gets
		 * the budget minus the item's cross margins. */
        float cross_used = cross_budget - margin_cross_total[i];
        uint8_t cross_source = YL_SRC_FLEX_STRETCH;
        if (cross_used < 0.0f) {
            cross_used = 0.0f;
        }
        if (!do_stretch) {
            cross_used = cross;
            cross_source = 0; /* natural size — keep the first-pass stamp */
        }
        /* Images are replaced elements — never stretch them across the cross
         * axis; keep their intrinsic cross dimension (a row favicon stays its
         * own height, a column image its own width). */
        if (c->kind == YL_BOX_INLINE_IMAGE) {
            float img_cross = column_dir ? img_w_intr[i] : img_h_intr[i];
            if (img_cross > 0.0f) {
                cross_used = img_cross;
                cross_source = YL_SRC_IMG_INTRINSIC;
            }
        }
        /* `align-items: stretch` only applies to items whose cross size is
		 * auto — an explicit CSS cross dimension pins the item (a 14px icon
		 * in a text-height row stays 14px, as in Chrome). */
        if ((column_dir ? c->css_width : c->css_height) > 0.0f) {
            cross_used = cross;
            cross_source = YL_SRC_CSS;
        }
        float cross_pos = cross_origin + margin_cross_start[i];
        if (!do_stretch) {
            if (item_align == CSS_ALIGN_ITEMS_FLEX_END) {
                cross_pos = cross_origin + (cross_budget - cross_used - margin_cross_total[i]) +
                            margin_cross_start[i];
            } else if (item_align == CSS_ALIGN_ITEMS_CENTER) {
                cross_pos = cross_origin +
                            (cross_budget - cross_used - margin_cross_total[i]) * 0.5f +
                            margin_cross_start[i];
            } else if (item_align == CSS_ALIGN_ITEMS_BASELINE && !column_dir) {
                float ascent = flex_item_first_ascent(r, cidx, natural_h[i]);
                cross_pos =
                    cross_origin + (max_baseline_ascent - ascent) + margin_cross_start[i];
            }
        }
        if (column_dir) {
            c->x = cross_pos;
            c->w = cross_used;
            if (cross_source != 0) {
                c->width_source = cross_source;
            }
        } else {
            c->y = cross_pos;
            c->h = cross_used;
            if (cross_source != 0) {
                c->height_source = cross_source;
            }
        }
    }

    float total_main = (column_dir ? cursor - content_origin_y : 0.0f);
    /* row-reverse / column-reverse: the main axis runs backwards — mirror
	 * every item's main position inside the used extent (this flips both
	 * the visual order AND the pack side, exactly the reversed axis). */
    self = &r->boxes.data[idx];
    if (self->main_reverse && n_children > 0) {
        float extent = column_dir ? (cursor - content_origin_y) : content_width;
        struct yetty_ycore_void_result mirror_res =
            flex_mirror_axis(r, children, n_children, column_dir,
                             column_dir ? content_origin_y : content_origin_x, extent);
        YETTY_RETURN_IF_ERR(float, mirror_res, "layout_flex: reverse mirror");
        self = &r->boxes.data[idx];
    }
    float container_h =
        column_dir ? (total_main + pad_bottom) : (cross_budget + pad_top + pad_bottom);
    /* Out-of-flow pass: place absolute / fixed children (e.g. the inset:0
     * click overlays Google News stacks over each card) against this flex
     * container now that its size is known. */
    struct yetty_ycore_void_result abs_res =
        flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, container_h);
    YETTY_RETURN_IF_ERR(float, abs_res, "layout_flex: place out-of-flow");
    return YETTY_OK(float, container_h);
}

/* Scratch provisioning around layout_flex_run: typical containers use one
 * stack block; a container with more in-flow children than the stack cap
 * gets a heap block sized to its exact count — NO silent truncation (the
 * old fixed 256-slot arrays dropped every child past the cap). */
static struct float_result layout_flex(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                       float origin_y, float content_w, bool column_dir)
{
    uint32_t in_flow_count = 0;
    for (uint32_t cidx = r->boxes.data[idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        const struct yetty_ylexbor_box *child = &r->boxes.data[cidx];
        if (child->float_side != 0 || child->position == YL_POS_ABSOLUTE ||
            child->position == YL_POS_FIXED) {
            continue;
        }
        in_flow_count++;
    }
    unsigned char stack_scratch[YL_FLEX_SCRATCH_BYTES(YL_FLEX_MAX_CHILDREN)];
    unsigned char *scratch = stack_scratch;
    unsigned char *heap_scratch = NULL;
    uint32_t scratch_cap = YL_FLEX_MAX_CHILDREN;
    if (in_flow_count > YL_FLEX_MAX_CHILDREN) {
        heap_scratch = malloc(YL_FLEX_SCRATCH_BYTES(in_flow_count));
        if (!heap_scratch) {
            return YETTY_ERR(float, "layout_flex: scratch alloc");
        }
        scratch = heap_scratch;
        scratch_cap = in_flow_count;
    }
    struct float_result run_res =
        layout_flex_run(r, idx, origin_x, origin_y, content_w, column_dir, scratch, scratch_cap);
    free(heap_scratch);
    return run_res;
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

/* Horizontal margins of a measured child, skipping percent-flagged values
 * (they resolve against a containing block the measure pass doesn't
 * have). */
static float measure_horizontal_margins(const struct yetty_ylexbor_box *box)
{
    float sum = 0.0f;
    if (!(box->pct_mask & YL_PCT_MARGIN_LEFT)) {
        sum += box->margin_left;
    }
    if (!(box->pct_mask & YL_PCT_MARGIN_RIGHT)) {
        sum += box->margin_right;
    }
    return sum;
}

static float measure_cell_content_width(struct yetty_ylexbor *r, uint32_t cell_idx, int *budget)
{
    if (*budget <= 0) {
        return 0;
    }
    (*budget)--;
    /* A bare text node can be a cell in its own right — most importantly a
     * direct text child of a flex container becomes an anonymous flex item
     * (`<div style=display:flex><svg/>More headlines</div>`). Its content
     * width is its OWN text advance, not its children (a text node has none),
     * so the child-summing loop below would return 0 and the flex item would
     * collapse to the 1px slack, wrapping the label one fragment per line
     * (the Google News "More headlines & perspectives" header). Measure the
     * text directly. */
    if (r->boxes.data[cell_idx].kind == YL_BOX_INLINE_TEXT) {
        const struct yetty_ylexbor_box *text_cell = &r->boxes.data[cell_idx];
        float adv = text_cell->glyph_advance > 0.0f ? text_cell->glyph_advance
                                                     : yetty_ylexbor_glyph_advance_ratio(r);
        return yetty_ylexbor_naive_text_width(text_cell->text, text_cell->text_len,
                                              text_cell->font_size, adv);
    }
    float sum = 0;
    float max_line = 0;
    /* A flex ROW lays its children side-by-side: its max-content is the
	 * SUM of the children plus the inter-item gaps, not the widest line.
	 * (A nav of four links with gap:24px is 3 gaps wider than its text.) */
    int row_flex = (r->boxes.data[cell_idx].layout_mode == YL_LAYOUT_FLEX_ROW);
    /* A single-implicit-row grid (auto-flow column menus: child count <=
	 * track count) also lays children side-by-side. Multi-row grids keep
	 * the block treatment (max line), which under-estimates but never
	 * explodes. */
    if (!row_flex && r->boxes.data[cell_idx].layout_mode == YL_LAYOUT_GRID &&
        r->boxes.data[cell_idx].grid_ntracks > 1) {
        int grid_children = 0;
        for (uint32_t scan = r->boxes.data[cell_idx].first_child; scan != 0;
             scan = r->boxes.data[scan].next_sibling) {
            grid_children++;
        }
        if (grid_children <= (int)r->boxes.data[cell_idx].grid_ntracks) {
            row_flex = 1;
        }
    }
    float item_gap = row_flex ? r->boxes.data[cell_idx].grid_col_gap : 0.0f;
    int in_flow_items = 0;
    for (uint32_t cidx = r->boxes.data[cell_idx].first_child; cidx != 0;
         cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        /* Out-of-flow children contribute nothing to the parent's content
		 * size — and a dropdown's ABSOLUTE panel subtree is often huge
		 * enough to exhaust the recursion budget, zeroing the whole
		 * measurement (github's nav collapsed to width 0 through it). */
        if (c->position == YL_POS_ABSOLUTE || c->position == YL_POS_FIXED ||
            c->float_side != 0) {
            continue;
        }
        if (c->kind == YL_BOX_INLINE_TEXT) {
            float adv =
                c->glyph_advance > 0.0f ? c->glyph_advance : yetty_ylexbor_glyph_advance_ratio(r);
            sum += yetty_ylexbor_naive_text_width(c->text, c->text_len, c->font_size, adv);
            in_flow_items++;
        } else if (c->kind == YL_BOX_INLINE_IMAGE) {
            if (row_flex) {
                sum += (c->w > 0 ? c->w : 0) + measure_horizontal_margins(c);
            } else if (c->w > 0 && c->w > sum) {
                sum = c->w;
            }
            in_flow_items++;
        } else if (c->kind == YL_BOX_BLOCK) {
            /* A definite width makes min- and max-content BOTH equal to that
			 * width — the box clamps its contents — so a fixed-width child
			 * contributes its own width, not a recursive content measure.
			 * Without this a `width:180px` forecast tile measured as its
			 * (near-empty) text, its flex parent shrank below 180, and the
			 * tile overflowed the column (the Google News weather widget).
			 * A percentage (css_width < 0) resolves against an indefinite
			 * intrinsic container as auto, so keep measuring its content. */
            float nested = c->css_width > 0.0f ? c->css_width
                                               : measure_cell_content_width(r, cidx, budget);
            if (row_flex) {
                /* Flex items sit side-by-side — they add horizontally,
				 * padding and margins included (a gnews nav chip's
				 * `margin:4px 16px` widens the tablist's max-content). */
                sum += nested + c->padding_left + c->padding_right + measure_horizontal_margins(c);
            } else {
                /* Block children inside a cell (nested layout); use the
				 * larger of: sum of inline run so far, recursive measure
				 * of the nested block's content. A row of two block
				 * siblings stacks them vertically — each contributes its
				 * own max-line, but they don't add horizontally. */
                if (sum > max_line) {
                    max_line = sum;
                }
                sum = 0;
                if (nested > max_line) {
                    max_line = nested;
                }
            }
            in_flow_items++;
        }
    }
    if (row_flex && in_flow_items > 1) {
        sum += (float)(in_flow_items - 1) * item_gap;
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
    if (self->table_fixed && content_width > 0.0f) {
        /* `table-layout: fixed`: columns share the table's width equally,
		 * content-blind (the CSS fixed algorithm with no per-column widths
		 * authored). */
        for (uint32_t i = 0; i < cols; i++) {
            col_w[i] = content_width / (float)cols;
        }
    } else if (total_max <= content_width || total_max == 0.0f) {
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
            c->width_source = YL_SRC_TABLE_COLS;
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
            c->height_source = YL_SRC_CONTENT;
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
            if (c->h != row_h) {
                c->height_source = YL_SRC_GRID_STRETCH;
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

/* Resolve any percent-flagged margins/paddings on `b` against the
 * containing block's content width `cb_width`. The box pass stores the
 * percent as a ratio (e.g. 0.10) and records which fields are percentages
 * in `pct_mask`; here we multiply through and clear the bit so the call is
 * idempotent — the box can be visited again (e.g. its own layout_block
 * entry) without double-applying. Percent margins AND paddings both use
 * the containing block width per CSS, so one basis covers all eight. */
static void resolve_pct_metrics(struct yetty_ylexbor_box *b, float cb_width)
{
    if (b->pct_mask == 0) {
        return;
    }
    if (b->pct_mask & YL_PCT_MARGIN_TOP) {
        b->margin_top *= cb_width;
    }
    if (b->pct_mask & YL_PCT_MARGIN_RIGHT) {
        b->margin_right *= cb_width;
    }
    if (b->pct_mask & YL_PCT_MARGIN_BOTTOM) {
        b->margin_bottom *= cb_width;
    }
    if (b->pct_mask & YL_PCT_MARGIN_LEFT) {
        b->margin_left *= cb_width;
    }
    if (b->pct_mask & YL_PCT_PADDING_TOP) {
        b->padding_top *= cb_width;
    }
    if (b->pct_mask & YL_PCT_PADDING_RIGHT) {
        b->padding_right *= cb_width;
    }
    if (b->pct_mask & YL_PCT_PADDING_BOTTOM) {
        b->padding_bottom *= cb_width;
    }
    if (b->pct_mask & YL_PCT_PADDING_LEFT) {
        b->padding_left *= cb_width;
    }
    b->pct_mask = 0;
}

/* True iff inset `side` (0=top,1=right,2=bottom,3=left) is specified. */
static bool inset_is_set(const struct yetty_ylexbor_box *b, int side)
{
    return (b->pos_set_mask >> side) & 1u;
}

/* Resolved inset value in px for `side`, percentages multiplied against the
 * containing-block width (left/right) or height (top/bottom). */
static float inset_value(const struct yetty_ylexbor_box *b, int side, float cb_w, float cb_h)
{
    float raw = (side == 0)   ? b->pos_top
                : (side == 1) ? b->pos_right
                : (side == 2) ? b->pos_bottom
                              : b->pos_left;
    if (b->pos_pct_mask & (1u << side)) {
        return raw * ((side == 0 || side == 2) ? cb_h : cb_w);
    }
    return raw;
}

/* Visual shift applied to a `position: relative` box (and its whole subtree).
 * `left` wins over `right`, `top` over `bottom`, matching CSS. */
static void relative_offset(const struct yetty_ylexbor_box *b, float cb_w, float cb_h,
                            float *out_dx, float *out_dy)
{
    float dx = 0.0f, dy = 0.0f;
    if (inset_is_set(b, 3)) {
        dx = inset_value(b, 3, cb_w, cb_h);
    } else if (inset_is_set(b, 1)) {
        dx = -inset_value(b, 1, cb_w, cb_h);
    }
    if (inset_is_set(b, 0)) {
        dy = inset_value(b, 0, cb_w, cb_h);
    } else if (inset_is_set(b, 2)) {
        dy = -inset_value(b, 2, cb_w, cb_h);
    }
    *out_dx = dx;
    *out_dy = dy;
}

/* Translate an already-laid-out box and its whole subtree by (dx, dy). Used
 * to apply `position: relative`-after-the-fact and `transform: translate`,
 * which move painted geometry without disturbing siblings' flow. */
static void shift_subtree(struct yetty_ylexbor *r, uint32_t idx, float dx, float dy)
{
    struct yetty_ylexbor_box *b = &r->boxes.data[idx];
    b->x += dx;
    b->y += dy;
    for (uint32_t c = b->first_child; c != 0; c = r->boxes.data[c].next_sibling) {
        shift_subtree(r, c, dx, dy);
    }
}

/* Apply a box's `transform: translate(...)` once it (and its subtree) has been
 * laid out. Percent offsets resolve against the box's own size — the
 * `translate(-50%,-50%)` centering idiom. Visual only: flow is unaffected. */
static void apply_transform(struct yetty_ylexbor *r, uint32_t idx)
{
    struct yetty_ylexbor_box *b = &r->boxes.data[idx];
    if (!b->has_transform) {
        return;
    }
    float dx = b->tf_tx_pct ? (b->tf_tx * 0.01f * b->w) : b->tf_tx;
    float dy = b->tf_ty_pct ? (b->tf_ty * 0.01f * b->h) : b->tf_ty;
    if (dx != 0.0f || dy != 0.0f) {
        shift_subtree(r, idx, dx, dy);
    }
}

/* Used height of an absolutely-positioned box. Explicit px wins; a
 * percentage resolves against the containing-block height `cb_h` (an
 * abspos %-height DOES have a definite basis — the CB is the padding box
 * of its positioned ancestor, whose height we were handed, so unlike an
 * in-flow block it needn't fall back to auto); else the measured content
 * height. Percentages are stored as a negative ratio (`height:100%` ->
 * -1.0), the same convention `css_width` uses — this is exactly why
 * `width:100%` already worked on these boxes and `height:100%` did not
 * (the MDC ripple overlay `{position:absolute;height:100%;width:100%}`
 * was content-sized instead of filling its button). */
static float abs_used_height(const struct yetty_ylexbor_box *c, float cb_h, float content_height,
                             uint8_t *out_source)
{
    if (c->css_height > 0.0f) {
        *out_source = YL_SRC_CSS;
        return c->css_height;
    }
    if (c->css_height < 0.0f) {
        *out_source = YL_SRC_CSS;
        float resolved = cb_h * (-c->css_height);
        return resolved > 0.0f ? resolved : 0.0f;
    }
    *out_source = YL_SRC_CONTENT;
    return content_height;
}

/* Place a `position: absolute` / `fixed` child against its containing block
 * (padding box `(cb_x, cb_y, cb_w, cb_h)`) and lay out its subtree there.
 *
 * Simplification: the containing block is the box that owns this child in the
 * tree, not the nearest *positioned* ancestor the spec calls for. That covers
 * the dominant real-world idiom — `position:absolute` children of a
 * `position:relative` container (news cards, dropdowns, badges) — and is far
 * better than the previous behaviour of stacking them in normal flow. */
static struct float_result layout_absolute_child(struct yetty_ylexbor *r, uint32_t cidx, float cb_x,
                                                 float cb_y, float cb_w, float cb_h)
{
    struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
    resolve_pct_metrics(c, cb_w);

    const bool left_set = inset_is_set(c, 3);
    const bool right_set = inset_is_set(c, 1);
    const bool top_set = inset_is_set(c, 0);
    const bool bottom_set = inset_is_set(c, 2);
    const float left = inset_value(c, 3, cb_w, cb_h);
    const float right = inset_value(c, 1, cb_w, cb_h);
    const float top = inset_value(c, 0, cb_w, cb_h);
    const float bottom = inset_value(c, 2, cb_w, cb_h);

    /* Width: explicit width wins; left+right together pin it; a box
     * anchored on one side (or none) shrinks to fit its content — capped
     * at the space the anchoring inset leaves. Without the intrinsic
     * measurement a corner widget (card kebab button, right:0) stretched
     * across the whole containing block. */
    float width;
    uint8_t width_source;
    if (c->css_width > 0.0f) {
        width = c->css_width;
        width_source = YL_SRC_CSS;
    } else if (c->css_width < 0.0f) {
        width = cb_w * (-c->css_width); /* percent encoded as negative ratio */
        width_source = YL_SRC_CSS;
    } else if (c->width_keyword != 0) {
        /* width:max-content / fit-content / min-content — libcss dropped
		 * the keyword, the side table preserved it. An explicit width
		 * beats insets even when both are set (over-constrained: the
		 * inline-end inset gives way), which is exactly the dropdown-
		 * panel pattern (`left:0;right:0` drawer base + `width:
		 * max-content` desktop override). */
        int measure_budget = YL_CELL_MEASURE_BUDGET;
        if (c->width_keyword == 2) {
            width = flex_item_min_content_walk(r, cidx, &measure_budget);
        } else {
            width = measure_cell_content_width(r, cidx, &measure_budget) + c->padding_left +
                    c->padding_right + 1.0f;
        }
        width_source = YL_SRC_CONTENT;
    } else if (left_set && right_set) {
        width = cb_w - left - right;
        width_source = YL_SRC_ABS_INSET;
    } else {
        float space = cb_w - (left_set ? left : 0.0f) - (right_set ? right : 0.0f);
        int measure_budget = YL_CELL_MEASURE_BUDGET;
        float fit_w = measure_cell_content_width(r, cidx, &measure_budget) + c->padding_left +
                      c->padding_right + c->border_left + c->border_right + 1.0f;
        width = fit_w < space ? fit_w : space;
        width_source = fit_w < space ? YL_SRC_ABS_FIT : YL_SRC_ABS_INSET;
    }
    if (width < 0.0f) {
        width = 0.0f;
    }

    float x;
    if (left_set) {
        x = cb_x + left;
    } else if (right_set) {
        x = cb_x + cb_w - right - width;
    } else if (c->static_pos_set) {
        x = c->static_x; /* static-position rectangle (flow-recorded) */
    } else {
        x = cb_x; /* no flow record — containing-block fallback */
    }

    /* The recursion subtracts only padding from the width it is handed; for
     * a content-box explicit width, pre-subtract the border (mirrors the
     * in-flow block path). */
    float content_w = width;
    if (!c->border_box && c->css_width != 0.0f) {
        content_w -= (c->border_left + c->border_right);
    }

    /* Measure the subtree at a provisional origin to learn its height, then
     * resolve y. A bottom-anchored box needs the height first. */
    struct float_result measure = layout_block(r, cidx, x, cb_y, content_w);
    YETTY_RETURN_IF_ERR(float, measure, "layout_absolute_child: measure");
    c = &r->boxes.data[cidx];
    uint8_t height_source;
    float height = abs_used_height(c, cb_h, measure.value, &height_source);
    /* top + bottom both set with no explicit height stretches the box to the
     * containing block — the `inset:0` full-cover overlay pattern. Only when
     * height is auto (== 0): a definite px OR percentage height already fixed
     * the size, so the inset pair just positions it, it does not re-stretch. */
    if (top_set && bottom_set && c->css_height == 0.0f) {
        float stretched = cb_h - top - bottom;
        if (stretched > height) {
            height = stretched;
            height_source = YL_SRC_ABS_INSET;
        }
    }

    float y;
    if (top_set) {
        y = cb_y + top;
    } else if (bottom_set) {
        y = cb_y + cb_h - bottom - height;
    } else if (c->static_pos_set) {
        y = c->static_y; /* static-position rectangle (flow-recorded) */
    } else {
        y = cb_y; /* no flow record — containing-block fallback */
    }

    /* If y moved, re-lay the subtree so descendants land at the final
     * position (their coords were computed against the provisional origin). */
    if (y != cb_y) {
        struct float_result place = layout_block(r, cidx, x, y, content_w);
        YETTY_RETURN_IF_ERR(float, place, "layout_absolute_child: place");
        c = &r->boxes.data[cidx];
        height = abs_used_height(c, cb_h, place.value, &height_source);
        /* Re-apply the inset stretch — the re-lay recomputed content
         * height and would otherwise drop it. */
        if (top_set && bottom_set && c->css_height == 0.0f) {
            float stretched = cb_h - top - bottom;
            if (stretched > height) {
                height = stretched;
                height_source = YL_SRC_ABS_INSET;
            }
        }
    }

    /* Nested absolute children were placed by the measure pass above,
     * BEFORE this box's inset-stretched height was known — a nested
     * inset:0 overlay resolved top/bottom against in-flow height 0 and
     * collapsed (the gnews card pattern: abs link inside abs wrapper).
     * Re-place them against the final padding box. */
    for (uint32_t nested_idx = r->boxes.data[cidx].first_child; nested_idx != 0;
         nested_idx = r->boxes.data[nested_idx].next_sibling) {
        if (r->boxes.data[nested_idx].position != YL_POS_ABSOLUTE) {
            continue; /* fixed resolves against the viewport — already right */
        }
        struct yetty_ylexbor_box *final_self = &r->boxes.data[cidx];
        float pad_x = x + final_self->border_left;
        float pad_y = y + final_self->border_top;
        float pad_h = height - final_self->border_top - final_self->border_bottom;
        if (pad_h < 0.0f) {
            pad_h = 0.0f;
        }
        struct float_result nested_res =
            layout_absolute_child(r, nested_idx, pad_x, pad_y, content_w, pad_h);
        YETTY_RETURN_IF_ERR(float, nested_res, "layout_absolute_child: nested absolute");
    }
    /* Re-fetch — the nested re-placement recursion may have grown the
     * box vector. */
    c = &r->boxes.data[cidx];

    c->x = x;
    c->y = y;
    c->w = width;
    c->h = height;
    c->width_source = width_source;
    c->height_source = height_source;
    apply_transform(r, cidx);
    return YETTY_OK(float, height);
}

/* Grid default `align-items: stretch`: block items without an explicit
 * height fill their row (the tallest cell sets the row height). Text and
 * replaced items keep their content height, matching Chrome. */
static void grid_stretch_row_members(struct yetty_ylexbor *r, const uint32_t *members,
                                     int member_count, float row_height)
{
    for (int i = 0; i < member_count; i++) {
        struct yetty_ylexbor_box *member = &r->boxes.data[members[i]];
        if (member->kind == YL_BOX_BLOCK && member->css_height <= 0.0f && member->h < row_height) {
            member->h = row_height;
            member->height_source = YL_SRC_GRID_STRETCH;
        }
    }
}

/* Append every ABSOLUTE box under `idx` whose nearest positioned ancestor
 * is the caller: direct absolute children plus recursion through children
 * that are unpositioned (a positioned child is the containing block for
 * its own subtree — its pass handles it). Best-effort: OOM stops the
 * collection, leaving the remainder where the legacy per-parent pass put
 * them. */
static void abs_descendants_collect(struct yetty_ylexbor *r, uint32_t idx, uint32_t **list,
                                    uint32_t *count, uint32_t *cap)
{
    for (uint32_t child_idx = r->boxes.data[idx].first_child; child_idx != 0;
         child_idx = r->boxes.data[child_idx].next_sibling) {
        uint8_t child_pos = r->boxes.data[child_idx].position;
        if (child_pos == YL_POS_ABSOLUTE) {
            if (*count == *cap) {
                uint32_t new_cap = *cap ? *cap * 2 : 8;
                uint32_t *grown = realloc(*list, new_cap * sizeof(**list));
                if (!grown) {
                    return;
                }
                *list = grown;
                *cap = new_cap;
            }
            (*list)[(*count)++] = child_idx;
            continue; /* its subtree resolves against it */
        }
        if (child_pos == YL_POS_STATIC) {
            abs_descendants_collect(r, child_idx, list, count, cap);
        }
        /* RELATIVE/FIXED child: containing block for its own subtree. */
    }
}

/* 2D cell-occupancy map for grid placement. Rows grow on demand — a
 * 500-item product grid places every child instead of overlapping past a
 * fixed cap. YL_GRID_FLOW_MAX_ROWS is only the sane upper bound that
 * keeps a pathological span from looping forever. */
enum { YL_GRID_FLOW_MAX_ROWS = 65536 };
struct yl_grid_occupancy {
    bool *cell; /* row_cap x YL_GRID_MAX_TRACKS, row-major; heap */
    int row_cap;
};

/* Grow the map to cover `rows_needed` rows. Returns 0 on OOM — the
 * caller treats unmapped rows as free (degrades to overlap, no crash). */
static int grid_occupancy_ensure(struct yl_grid_occupancy *occupancy, int rows_needed)
{
    if (rows_needed <= occupancy->row_cap) {
        return 1;
    }
    int new_cap = occupancy->row_cap ? occupancy->row_cap : 96;
    while (new_cap < rows_needed) {
        new_cap *= 2;
    }
    bool *grown = calloc((size_t)new_cap * YL_GRID_MAX_TRACKS, sizeof(bool));
    if (!grown) {
        return 0;
    }
    if (occupancy->cell) {
        memcpy(grown, occupancy->cell,
               (size_t)occupancy->row_cap * YL_GRID_MAX_TRACKS * sizeof(bool));
        free(occupancy->cell);
    }
    occupancy->cell = grown;
    occupancy->row_cap = new_cap;
    return 1;
}

static bool grid_cell_occupied(const struct yl_grid_occupancy *occupancy, int row, int col)
{
    if (row >= occupancy->row_cap) {
        return false; /* unmapped rows read as free */
    }
    return occupancy->cell[(size_t)row * YL_GRID_MAX_TRACKS + col];
}

/* One grid child with its resolved cell (0-based) and spans. */
struct yl_grid_item {
    uint32_t box;
    int col;
    int row; /* -1 until placed */
    int col_span;
    int row_span;
    float bottom; /* laid-out bottom edge — folds multi-row overhang */
};

static bool grid_area_free(const struct yl_grid_occupancy *occupancy, int row, int col,
                           int row_span, int col_span)
{
    for (int rr = row; rr < row + row_span && rr < YL_GRID_FLOW_MAX_ROWS; rr++) {
        for (int cc = col; cc < col + col_span && cc < YL_GRID_MAX_TRACKS; cc++) {
            if (grid_cell_occupied(occupancy, rr, cc)) {
                return false;
            }
        }
    }
    return true;
}

static void grid_area_mark(struct yl_grid_occupancy *occupancy, int row, int col, int row_span,
                           int col_span)
{
    if (!grid_occupancy_ensure(occupancy, row + row_span)) {
        return; /* OOM — later placements read these rows as free */
    }
    for (int rr = row; rr < row + row_span && rr < YL_GRID_FLOW_MAX_ROWS; rr++) {
        for (int cc = col; cc < col + col_span && cc < YL_GRID_MAX_TRACKS; cc++) {
            occupancy->cell[(size_t)rr * YL_GRID_MAX_TRACKS + cc] = true;
        }
    }
}

/* display:grid with a parsed grid-template-columns. Resolves the column track
 * widths (fr tracks share leftover space after fixed px + gaps), then
 * auto-flows the in-flow children row-major into the cells. Spans / explicit
 * placement are NOT modelled — this is activated only for small-track card
 * grids (text + thumbnail), where one-child-per-cell is the correct result
 * and the win is huge (thumbnails shrink to their narrow column). */
static struct float_result layout_grid(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
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
    if (content_width < 0.0f) {
        content_width = 0.0f;
    }

    int ncols = self->grid_ntracks;
    if (ncols < 1) {
        ncols = 1;
    }
    if (ncols > YL_GRID_MAX_TRACKS) {
        ncols = YL_GRID_MAX_TRACKS;
    }
    float col_gap = self->grid_col_gap;
    float row_gap = self->grid_row_gap > 0.0f ? self->grid_row_gap : 0.0f;

    /* Collect the in-flow children and resolve every child's CELL up
	 * front — placement is pure occupancy bookkeeping and needs no track
	 * widths. Explicitly-placed items (numeric `grid-column` /
	 * `grid-row`, the gnews card's thumbnail at column 3 spanning the
	 * text rows) claim their cells first; the rest auto-flow row-major
	 * around them with a forward-only cursor (CSS sparse packing). */
    struct yl_grid_item *items = NULL;
    uint32_t item_count = 0;
    bool any_explicit = false;
    if (self->child_count > 0) {
        items = calloc(self->child_count, sizeof(struct yl_grid_item));
        if (items == NULL) {
            /* OOM: degrade to block flow rather than dropping children. */
            r->boxes.data[idx].layout_mode = YL_LAYOUT_BLOCK;
            return layout_block(r, idx, origin_x, origin_y, content_w);
        }
    }
    if (items != NULL) {
        for (uint32_t cc = self->first_child; cc != 0 && item_count < (uint32_t)self->child_count;
             cc = r->boxes.data[cc].next_sibling) {
            struct yetty_ylexbor_box *ch = &r->boxes.data[cc];
            if (ch->position == YL_POS_ABSOLUTE || ch->position == YL_POS_FIXED ||
                ch->float_side != 0) {
                continue;
            }
            struct yl_grid_item *item = &items[item_count++];
            item->box = cc;
            item->col_span = ch->grid_col_span > 0 ? ch->grid_col_span : 1;
            if (item->col_span > ncols) {
                item->col_span = ncols;
            }
            item->row_span = ch->grid_row_span > 0 ? ch->grid_row_span : 1;
            if (item->row_span > YL_GRID_FLOW_MAX_ROWS) {
                item->row_span = YL_GRID_FLOW_MAX_ROWS;
            }
            item->col = ch->grid_col_start > 0 ? (int)ch->grid_col_start - 1 : -1;
            if (item->col >= 0 && item->col + item->col_span > ncols) {
                item->col = ncols - item->col_span;
            }
            item->row = ch->grid_row_start > 0 ? (int)ch->grid_row_start - 1 : -1;
            if (item->row >= 0 && item->row >= YL_GRID_FLOW_MAX_ROWS) {
                item->row = YL_GRID_FLOW_MAX_ROWS - 1;
            }
            if (item->col >= 0 || item->row >= 0) {
                any_explicit = true;
            }
        }
        struct yl_grid_occupancy occupancy = {0};
        /* Pass 1: fully-explicit items claim their cells. */
        for (uint32_t i = 0; i < item_count; i++) {
            if (items[i].col >= 0 && items[i].row >= 0) {
                grid_area_mark(&occupancy, items[i].row, items[i].col, items[i].row_span,
                               items[i].col_span);
            }
        }
        /* Pass 2: flow the rest in document order. */
        int cursor_row = 0;
        int cursor_col = 0;
        for (uint32_t i = 0; i < item_count; i++) {
            struct yl_grid_item *item = &items[i];
            if (item->col >= 0 && item->row >= 0) {
                continue;
            }
            if (item->col >= 0) {
                /* Column locked, row auto: first row where the area fits. */
                int rr = 0;
                while (rr < YL_GRID_FLOW_MAX_ROWS - 1 &&
                       !grid_area_free(&occupancy, rr, item->col, item->row_span, item->col_span)) {
                    rr++;
                }
                item->row = rr;
            } else if (item->row >= 0) {
                /* Row locked, column auto: first fitting column. */
                int cc_col = 0;
                while (cc_col + item->col_span <= ncols &&
                       !grid_area_free(&occupancy, item->row, cc_col, item->row_span,
                                       item->col_span)) {
                    cc_col++;
                }
                if (cc_col + item->col_span > ncols) {
                    cc_col = 0; /* nothing free — overlap at the left edge */
                }
                item->col = cc_col;
            } else {
                /* Fully auto: forward-only row-major cursor. */
                int rr = cursor_row;
                int cc_col = cursor_col;
                for (;;) {
                    if (cc_col + item->col_span > ncols) {
                        cc_col = 0;
                        rr++;
                    }
                    if (rr >= YL_GRID_FLOW_MAX_ROWS - 1 ||
                        grid_area_free(&occupancy, rr, cc_col, item->row_span, item->col_span)) {
                        break;
                    }
                    cc_col++;
                }
                item->row = rr;
                item->col = cc_col;
                cursor_row = rr;
                cursor_col = cc_col + item->col_span;
            }
            grid_area_mark(&occupancy, item->row, item->col, item->row_span, item->col_span);
        }
        free(occupancy.cell);
    }

    /* `auto` / min-content / max-content tracks size to the max-content of the
	 * items placed in them: measure each single-span item's intrinsic content
	 * width into its resolved column. */
    float auto_w[YL_GRID_MAX_TRACKS] = {0};
    bool any_auto = false;
    for (int c = 0; c < ncols; c++) {
        if (self->grid_tracks[c].is_auto) {
            any_auto = true;
        }
    }
    if (any_auto && items != NULL) {
        for (uint32_t i = 0; i < item_count; i++) {
            const struct yl_grid_item *item = &items[i];
            struct yetty_ylexbor_box *ch = &r->boxes.data[item->box];
            if (item->col_span != 1 || item->col < 0 || item->col >= ncols ||
                !self->grid_tracks[item->col].is_auto) {
                continue;
            }
            float iw;
            if (ch->css_width > 0.0f) {
                iw = ch->css_width;
            } else {
                int budget = YL_CELL_MEASURE_BUDGET;
                iw = measure_cell_content_width(r, item->box, &budget) + ch->padding_left +
                     ch->padding_right + ch->border_left + ch->border_right;
                if (ch->kind == YL_BOX_INLINE_IMAGE && ch->w > 0.0f) {
                    iw = ch->w;
                }
            }
            if (iw > auto_w[item->col]) {
                auto_w[item->col] = iw;
            }
        }
    }

    /* Resolve column widths: fixed px tracks keep their size; auto tracks take
     * their measured content size; fr tracks share the leftover after fixed +
     * auto tracks + inter-column gaps. */
    float fixed_sum = 0.0f, fr_sum = 0.0f;
    for (int c = 0; c < ncols; c++) {
        if (self->grid_tracks[c].is_fr) {
            fr_sum += self->grid_tracks[c].value;
        } else if (self->grid_tracks[c].is_auto) {
            fixed_sum += auto_w[c];
        } else if (self->grid_tracks[c].is_pct) {
            fixed_sum += self->grid_tracks[c].value * 0.01f * content_width;
        } else {
            fixed_sum += self->grid_tracks[c].value;
        }
    }
    float total_gap = (float)(ncols - 1) * col_gap;
    float free_space = content_width - fixed_sum - total_gap;
    if (free_space < 0.0f) {
        free_space = 0.0f;
    }
    float fr_unit = fr_sum > 0.0f ? free_space / fr_sum : 0.0f;
    float col_w[YL_GRID_MAX_TRACKS];
    float col_x[YL_GRID_MAX_TRACKS];
    float cursor_x = content_origin_x;
    for (int c = 0; c < ncols; c++) {
        if (self->grid_tracks[c].is_fr) {
            col_w[c] = self->grid_tracks[c].value * fr_unit;
        } else if (self->grid_tracks[c].is_auto) {
            col_w[c] = auto_w[c];
        } else if (self->grid_tracks[c].is_pct) {
            col_w[c] = self->grid_tracks[c].value * 0.01f * content_width;
        } else {
            col_w[c] = self->grid_tracks[c].value;
        }
        col_x[c] = cursor_x;
        cursor_x += col_w[c] + col_gap;
    }

    /* Named-placement guard. We only model row-major auto-flow; sites whose
	 * grid places the content by name/area (Wikipedia's Vector shell:
	 * sidebar | content | toc) would have their content auto-flowed into the
	 * narrow sidebar track and collapsed. Detect it: simulate the row-major
	 * walk and, if a content-bearing child (>=3 of its own children) lands in
	 * a track far narrower than the widest one, the grid isn't really
	 * auto-flow — fall back to block so the content keeps full width. A real
	 * card grid (text + thumbnail) puts only a leaf-ish image in the narrow
	 * track, so it is unaffected. Skipped when any child is explicitly
	 * placed — the site is using real placement, not auto-flow, and the
	 * row-major probe below would be meaningless. */
    if (self->grid_line_spec == NULL && !any_explicit) {
        float max_track = 0.0f;
        for (int c = 0; c < ncols; c++) {
            if (col_w[c] > max_track) {
                max_track = col_w[c];
            }
        }
        int probe_col = 0;
        for (uint32_t cc = self->first_child; cc != 0; cc = r->boxes.data[cc].next_sibling) {
            const struct yetty_ylexbor_box *ch = &r->boxes.data[cc];
            if (ch->position == YL_POS_ABSOLUTE || ch->position == YL_POS_FIXED ||
                ch->float_side != 0) {
                continue;
            }
            int narrow = (col_w[probe_col] < 0.45f * max_track && col_w[probe_col] < 400.0f);
            if (narrow) {
                /* Count this child's descendants (capped). A page's content
				 * column has a large subtree; a card's thumbnail in a narrow
				 * track is a small leaf-ish image. >25 descendants in a narrow
				 * track = misplaced content → fall back to block. */
                int desc = 0;
                uint32_t stack[64];
                int sp = 0;
                if (ch->child_count > 0) {
                    stack[sp++] = ch->first_child;
                }
                while (sp > 0 && desc < 40) {
                    uint32_t node = stack[--sp];
                    while (node != 0 && desc < 40) {
                        desc++;
                        if (r->boxes.data[node].child_count > 0 && sp < 64) {
                            stack[sp++] = r->boxes.data[node].first_child;
                        }
                        node = r->boxes.data[node].next_sibling;
                    }
                }
                if (desc > 25) {
                    free(items);
                    r->boxes.data[idx].layout_mode = YL_LAYOUT_BLOCK;
                    return layout_block(r, idx, origin_x, origin_y, content_w);
                }
            }
            probe_col = (probe_col + 1) % ncols;
        }
    }

    /* Named-line placement. When box-build flagged this container as a named
	 * grid (`grid_line_spec` set + every child carries `grid-column:<name>`),
	 * place each child by resolving its line name to a start track rather than
	 * row-major auto-flow. A start-only `grid-column:<name>` occupies the single
	 * track that begins at that line; an unresolved name falls back to track 0.
	 * All children share one row here (the single-row idiom these shells use). */
    if (self->grid_line_spec != NULL) {
        free(items); /* named placement ignores the numeric-cell resolution */
        items = NULL;
        size_t spec_len = strlen(self->grid_line_spec);
        float row_top = content_origin_y;
        float band_h = 0.0f;
        for (uint32_t cidx = self->first_child; cidx != 0;
             cidx = r->boxes.data[cidx].next_sibling) {
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            if (c->position == YL_POS_ABSOLUTE || c->position == YL_POS_FIXED ||
                c->float_side != 0) {
                continue;
            }
            int track = 0;
            if (c->grid_col_name != NULL) {
                int line = yetty_ylexbor_grid_resolve_line(
                    self->grid_line_spec, spec_len, c->grid_col_name, strlen(c->grid_col_name));
                if (line >= 0 && line < ncols) {
                    track = line;
                }
            }
            float cell_x = col_x[track];
            float cell_w = col_w[track];
            resolve_pct_metrics(c, cell_w);
            float child_h = 0.0f;
            if (c->kind == YL_BOX_BLOCK) {
                c->x = cell_x;
                c->y = row_top;
                c->w = cell_w;
                c->width_source = YL_SRC_GRID_TRACKS;
                struct float_result gres = layout_block(r, cidx, cell_x, row_top, cell_w);
                YETTY_RETURN_IF_ERR(float, gres, "layout_grid(named): child block");
                c = &r->boxes.data[cidx];
                child_h = (c->css_height > 0.0f) ? c->css_height : gres.value;
                c->h = child_h;
                c->height_source = (c->css_height > 0.0f) ? YL_SRC_CSS : YL_SRC_CONTENT;
            } else if (c->kind == YL_BOX_INLINE_TEXT) {
                struct float_result gres = wrap_inline_box(r, cidx, cell_x, row_top, cell_w, 0);
                YETTY_RETURN_IF_ERR(float, gres, "layout_grid(named): child text");
                c = &r->boxes.data[cidx];
                c->x = cell_x;
                c->y = row_top;
                c->w = cell_w;
                child_h = gres.value;
                c->h = child_h;
            } else if (c->kind == YL_BOX_INLINE_IMAGE) {
                float img_w = c->w > 0.0f ? c->w : 100.0f;
                float img_h = c->h > 0.0f ? c->h : 100.0f;
                if (img_w > cell_w && cell_w > 0.0f) {
                    img_h *= cell_w / img_w;
                    img_w = cell_w;
                }
                c->x = cell_x;
                c->y = row_top;
                c->w = img_w;
                c->h = img_h;
                child_h = img_h;
            }
            if (child_h > band_h) {
                band_h = child_h;
            }
            apply_transform(r, cidx);
        }
        float named_h =
            (band_h > 0.0f ? (row_top + band_h) : content_origin_y) - origin_y + pad_bottom;
        self = &r->boxes.data[idx];
        struct yetty_ycore_void_result abs_res =
            flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, named_h);
        YETTY_RETURN_IF_ERR(float, abs_res, "layout_grid(named): place out-of-flow");
        return YETTY_OK(float, named_h);
    }

    /* Lay the resolved cells out row by row. Each row's height is the max
	 * of its single-row members; a multi-row member (a card thumbnail
	 * spanning the text rows) folds any height still sticking out into
	 * the LAST row of its range when that row closes. Rows close in
	 * order, so every row_y is final by the time its members are laid
	 * out — each child is laid out exactly once (re-running a child's
	 * layout is not safe: the inline wrapper consumes its style segments
	 * and emits fragment boxes). Members of a row are stretched to the
	 * row height when it closes — the grid default `align-items:
	 * stretch`. */
    int stretch_items = (self->align_items == 0 || self->align_items == CSS_ALIGN_ITEMS_STRETCH);
    int max_row_end = 0;
    for (uint32_t i = 0; i < item_count; i++) {
        int row_end = items[i].row + items[i].row_span;
        if (row_end > max_row_end) {
            max_row_end = row_end;
        }
    }
    if (max_row_end > YL_GRID_FLOW_MAX_ROWS) {
        max_row_end = YL_GRID_FLOW_MAX_ROWS;
    }
    float row_y = content_origin_y;
    float last_row_bottom = content_origin_y;
    uint32_t row_members[YL_GRID_MAX_TRACKS];
    for (int rr = 0; rr < max_row_end; rr++) {
        float row_h = 0.0f;
        int row_member_count = 0;
        for (uint32_t i = 0; i < item_count; i++) {
            struct yl_grid_item *item = &items[i];
            if (item->row != rr) {
                continue;
            }
            uint32_t cidx = item->box;
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            float cell_x = col_x[item->col];
            /* Width spans the item's tracks plus the inter-track gaps
			 * between them. */
            float cell_w = 0.0f;
            for (int sc = 0; sc < item->col_span && (item->col + sc) < ncols; sc++) {
                cell_w += col_w[item->col + sc];
            }
            cell_w += (float)(item->col_span - 1) * col_gap;
            resolve_pct_metrics(c, cell_w);
            float child_h = 0.0f;
            if (c->kind == YL_BOX_BLOCK) {
                /* A grid item defaults to justify-self:stretch — it fills its
				 * track. But an explicit `width` overrides stretch: the item
				 * takes that width and its horizontal auto-margins distribute
				 * the leftover track space (`width:840px; margin:0 auto` centres
				 * an 840 box in a 1040 track — gnews' article column). */
                float item_x = cell_x;
                float item_w = cell_w;
                float explicit_w = 0.0f;
                if (c->css_width > 0.0f) {
                    explicit_w = c->css_width;
                } else if (c->css_width < 0.0f) {
                    explicit_w = cell_w * (-c->css_width);
                }
                if (explicit_w > 0.0f && explicit_w < cell_w) {
                    item_w = explicit_w;
                    float leftover = cell_w - explicit_w;
                    if (c->margin_left_auto && c->margin_right_auto) {
                        item_x = cell_x + leftover * 0.5f;
                    } else if (c->margin_left_auto) {
                        item_x = cell_x + leftover;
                    } else {
                        item_x = cell_x + c->margin_left;
                    }
                }
                c->x = item_x;
                c->y = row_y;
                c->w = item_w;
                c->width_source = explicit_w > 0.0f && explicit_w < cell_w ? YL_SRC_CSS
                                                                           : YL_SRC_GRID_TRACKS;
                struct float_result block_res = layout_block(r, cidx, item_x, row_y, item_w);
                if (YETTY_IS_ERR(block_res)) {
                    free(items);
                    return YETTY_ERR(float, "layout_grid: child block", block_res);
                }
                c = &r->boxes.data[cidx];
                child_h = (c->css_height > 0.0f) ? c->css_height : block_res.value;
                c->h = child_h;
                c->height_source = (c->css_height > 0.0f) ? YL_SRC_CSS : YL_SRC_CONTENT;
            } else if (c->kind == YL_BOX_INLINE_TEXT) {
                struct float_result wrap_res = wrap_inline_box(r, cidx, cell_x, row_y, cell_w, 0);
                if (YETTY_IS_ERR(wrap_res)) {
                    free(items);
                    return YETTY_ERR(float, "layout_grid: child text", wrap_res);
                }
                c = &r->boxes.data[cidx];
                c->x = cell_x;
                c->y = row_y;
                c->w = cell_w;
                child_h = wrap_res.value;
                c->h = child_h;
            } else if (c->kind == YL_BOX_INLINE_IMAGE) {
                float img_w = c->w > 0.0f ? c->w : 100.0f;
                float img_h = c->h > 0.0f ? c->h : 100.0f;
                if (img_w > cell_w && cell_w > 0.0f) {
                    img_h *= cell_w / img_w;
                    img_w = cell_w;
                }
                c->x = cell_x;
                c->y = row_y;
                c->w = img_w;
                c->h = img_h;
                c->width_source = YL_SRC_IMG_INTRINSIC;
                c->height_source = YL_SRC_IMG_INTRINSIC;
                child_h = img_h;
            }
            item->bottom = row_y + child_h;
            if (item->row_span == 1) {
                if (child_h > row_h) {
                    row_h = child_h;
                }
                if (row_member_count < YL_GRID_MAX_TRACKS) {
                    row_members[row_member_count++] = cidx;
                }
            }
            apply_transform(r, cidx);
        }
        /* Multi-row members ending here fold their overhang into this
		 * row. */
        for (uint32_t i = 0; i < item_count; i++) {
            if (items[i].row_span > 1 && items[i].row + items[i].row_span - 1 == rr) {
                float needed = items[i].bottom - row_y;
                if (needed > row_h) {
                    row_h = needed;
                }
            }
        }
        if (stretch_items) {
            grid_stretch_row_members(r, row_members, row_member_count, row_h);
        }
        last_row_bottom = row_y + row_h;
        row_y = last_row_bottom + row_gap;
    }
    free(items);

    float block_height =
        (item_count > 0 ? last_row_bottom : content_origin_y) - origin_y + pad_bottom;
    struct yetty_ycore_void_result abs_res =
        flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, block_height);
    YETTY_RETURN_IF_ERR(float, abs_res, "layout_grid: place out-of-flow");
    return YETTY_OK(float, block_height);
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
    if (r->boxes.data[idx].layout_mode == YL_LAYOUT_GRID) {
        return layout_grid(r, idx, origin_x, origin_y, content_w);
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

        /* Absolutely-positioned / fixed children are out of normal flow:
         * they neither advance the cursor nor reserve space. They are placed
         * in a second pass below, against this block as the containing
         * block. Record where the box WOULD have landed — the static
         * position an inset-less axis resolves to (margin collapse with the
         * previous sibling approximated the same way the in-flow path
         * collapses). */
        if (c->position == YL_POS_ABSOLUTE || c->position == YL_POS_FIXED) {
            float collapsed_top =
                has_prev ? (c->margin_top > prev_margin_bottom ? c->margin_top : prev_margin_bottom)
                         : c->margin_top;
            c->static_x = content_origin_x + c->margin_left;
            c->static_y = cursor_y + collapsed_top;
            c->static_pos_set = true;
            cidx = c->next_sibling;
            continue;
        }

        /* Resolve this child's percent margins/paddings against our
         * content width — the child's containing block — before any of
         * them are read for flow, collapsing, or float placement. */
        resolve_pct_metrics(c, content_width);

        if (c->float_side != 0 && c->kind == YL_BOX_INLINE_IMAGE) {
            /* Expire floats the cursor has scrolled past, then register
			 * this one against the fresh state. */
            float_advance_y(&fl, cursor_y);
            /* Floated replaced element (an <img>/<svg> pulled out of
				 * its paragraph's inline flow): the box producer already
				 * resolved its pixel size, so place it directly — no
				 * block recursion. Margins widen the band the in-flow
				 * text wraps around and offset the box inside it. */
            if (c->kind == YL_BOX_INLINE_IMAGE) {
                float replaced_w = c->w > 0.0f ? c->w : 100.0f;
                float replaced_h = c->h > 0.0f ? c->h : 100.0f;
                if (replaced_w > content_width && content_width > 0.0f) {
                    replaced_h *= content_width / replaced_w;
                    replaced_w = content_width;
                }
                float band_w = replaced_w + c->margin_left + c->margin_right;
                float replaced_y = cursor_y;
                float replaced_x;
                if (c->float_side == 1) {
                    if (fl.left_width > 0) {
                        replaced_y = fl.left_bottom;
                    }
                    replaced_x = content_origin_x + c->margin_left;
                } else {
                    if (fl.right_width > 0) {
                        replaced_y = fl.right_bottom;
                    }
                    replaced_x = content_origin_x + content_width - replaced_w - c->margin_right;
                }
                c->x = replaced_x;
                c->y = replaced_y + c->margin_top;
                c->w = replaced_w;
                c->h = replaced_h;
                float band_bottom = c->y + replaced_h + c->margin_bottom;
                if (c->float_side == 1) {
                    fl.left_width = band_w;
                    fl.left_bottom = band_bottom;
                } else {
                    fl.right_width = band_w;
                    fl.right_bottom = band_bottom;
                }
                cidx = r->boxes.data[cidx].next_sibling;
                continue;
            }
        }

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
            /* box-sizing. Under content-box (the CSS initial) an explicit
             * width is the CONTENT width, so padding + border expand the
             * box; `child_w` below is always the border-box (visual)
             * width, so add the extra. border-box widths already include
             * padding + border — which is what this engine has always
             * assumed — so the extra is zero there and behaviour is
             * unchanged. Auto width (css_width == 0) adds nothing. */
            float pad_h = c->padding_left + c->padding_right;
            float border_h = c->border_left + c->border_right;
            float box_extra = c->border_box ? 0.0f : (pad_h + border_h);
            float child_w;
            uint8_t child_width_source;
            if (c->css_width > 0.0f) {
                child_w = c->css_width + box_extra;
                child_width_source = YL_SRC_CSS;
            } else if (c->css_width < 0.0f) {
                child_w = avail_w * (-c->css_width) + box_extra;
                child_width_source = YL_SRC_CSS;
            } else if (c->shrink_to_fit) {
                /* Promoted inline-block / inline-flex widget: max-content
				 * width capped at the available area, never the full
				 * parent content width. +1px wrap-rounding slack. */
                int measure_budget = YL_CELL_MEASURE_BUDGET;
                float fit_w =
                    measure_cell_content_width(r, cidx, &measure_budget) + pad_h + border_h + 1.0f;
                child_w = fit_w < avail ? fit_w : avail;
                child_width_source = fit_w < avail ? YL_SRC_SHRINK_TO_FIT : YL_SRC_AVAIL;
            } else {
                child_w = avail;
                child_width_source = YL_SRC_AVAIL;
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
            /* Clamp to the available rectangle only for AUTO widths. An
				 * explicit `width: <px>` (or `min-width`) larger than the
				 * container overflows in real browsers rather than being
				 * shrunk; clamping it broke e.g. a `width:1000px` grid laid out
				 * in an 800px viewport (its fr track came out 200px short). */
            bool width_is_explicit = c->css_width > 0.0f || resolved_min > avail;
            if (!width_is_explicit && child_w > avail) {
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
            /* `position: relative` shifts the box (and its subtree) by the
             * insets, but leaves the in-flow cursor where it would have been
             * — so siblings still stack as if the box were unshifted. We pass
             * the shifted origin to the recursion so descendants follow, and
             * keep advancing `cursor_y` from the unshifted position below. */
            float place_x = child_origin_x;
            float place_y = cursor_y;
            if (c->position == YL_POS_RELATIVE) {
                float rel_dx = 0.0f, rel_dy = 0.0f;
                relative_offset(c, content_width, 0.0f, &rel_dx, &rel_dy);
                place_x += rel_dx;
                place_y += rel_dy;
            }
            c->x = place_x;
            c->y = place_y;
            c->w = child_w;
            c->width_source = child_width_source;
            /* The recursion subtracts only padding (not border) from the
             * width it is handed to derive the children's content area.
             * For a content-box explicit width, pre-subtract the border so
             * the resulting content area lands exactly on the specified
             * width. Auto width and border-box keep the historical width
             * (content area = width − padding). */
            float child_content_w = child_w;
            if (!c->border_box && c->css_width != 0.0f) {
                child_content_w -= border_h;
            }
            struct float_result child_res =
                layout_block(r, cidx, place_x, place_y, child_content_w);
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
            } else if (c->css_height < 0.0f) {
                /* `height: N%` — resolve against the containing block's
				 * content-area height, but only when this parent block has a
				 * definite height; otherwise percentage height is `auto`. */
                self = &r->boxes.data[idx];
                float parent_content_h = 0.0f;
                if (self->css_height > 0.0f) {
                    parent_content_h = self->border_box ? (self->css_height - self->padding_top -
                                                           self->padding_bottom - self->border_top -
                                                           self->border_bottom)
                                                        : self->css_height;
                }
                if (parent_content_h > 0.0f) {
                    child_h = parent_content_h * (-c->css_height);
                }
                c = &r->boxes.data[cidx];
            }
            c->h = child_h;
            c->height_source =
                (c->css_height != 0.0f && child_h != child_res.value) ? YL_SRC_CSS : YL_SRC_CONTENT;
            cursor_y += child_h;
            prev_margin_bottom = c->margin_bottom;
            has_prev = 1;
            /* Visual `transform: translate` shift — applied after the box and
             * its subtree are placed, and after cursor_y has advanced (flow
             * is unaffected by the transform). */
            apply_transform(r, cidx);

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
            /* Read text_align through the vector, not `self` — earlier
             * iterations may have grown the box vector and moved it. */
            struct float_result wrap_res =
                wrap_inline_box(r, cidx, avail_x, cursor_y, avail_w, r->boxes.data[idx].text_align);
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
            c->width_source = YL_SRC_IMG_INTRINSIC;
            c->height_source = YL_SRC_IMG_INTRINSIC;
            cursor_y += img_h;
            prev_margin_bottom = 0;
            has_prev = 1;
            apply_transform(r, cidx);
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

    /* Final in-flow height of this block (padding-box) — the containing
	 * block height for any absolute children placed below. */
    float block_height = (cursor_y - origin_y) + pad_bottom;

    /* Out-of-flow pass. CSS: an ABSOLUTE box resolves against its nearest
	 * POSITIONED ancestor, not its parent. An unpositioned block therefore
	 * leaves absolute children for an ancestor pass; a positioned block —
	 * or the root, the final fallback — places its own absolute children
	 * PLUS every absolute descendant reachable through unpositioned
	 * intermediates. FIXED children resolve against the viewport and are
	 * placed at any level. Out-of-flow heights do not feed back into
	 * `block_height`. */
    int is_containing_block = (idx == 0) || (r->boxes.data[idx].position != YL_POS_STATIC);
    uint32_t *deferred_absolute = NULL;
    uint32_t deferred_count = 0;
    uint32_t deferred_cap = 0;
    if (is_containing_block) {
        /* Descendants first (through unpositioned chains) — direct
		 * children are handled by the loop below. */
        for (uint32_t child_idx = r->boxes.data[idx].first_child; child_idx != 0;
             child_idx = r->boxes.data[child_idx].next_sibling) {
            if (r->boxes.data[child_idx].position == YL_POS_STATIC) {
                abs_descendants_collect(r, child_idx, &deferred_absolute, &deferred_count,
                                        &deferred_cap);
            }
        }
    }
    uint32_t direct_child = r->boxes.data[idx].first_child;
    uint32_t deferred_index = 0;
    for (;;) {
        uint32_t child_idx = 0;
        if (direct_child != 0) {
            child_idx = direct_child;
            direct_child = r->boxes.data[direct_child].next_sibling;
        } else if (deferred_index < deferred_count) {
            child_idx = deferred_absolute[deferred_index++];
        } else {
            break;
        }
        uint8_t child_pos = r->boxes.data[child_idx].position;
        if (child_pos != YL_POS_ABSOLUTE && child_pos != YL_POS_FIXED) {
            continue;
        }
        if (child_pos == YL_POS_ABSOLUTE && !is_containing_block) {
            continue; /* the nearest positioned ancestor places it */
        }
        /* Re-fetch on every pass: the in-flow loop above (and each
         * layout_absolute_child recursion below) can grow the box vector
         * and relocate it, leaving `self` dangling. */
        self = &r->boxes.data[idx];
        float cb_x, cb_y, cb_w, cb_h;
        if (child_pos == YL_POS_FIXED) {
            cb_x = 0.0f;
            cb_y = 0.0f;
            cb_w = (float)r->viewport_w;
            cb_h = (float)r->viewport_h;
        } else {
            /* Padding box of this block (border-box origin inset by the
			 * border). content_w is the width inside the border already. */
            cb_x = origin_x + self->border_left;
            cb_y = origin_y + self->border_top;
            cb_w = content_w;
            cb_h = block_height - self->border_top - self->border_bottom;
            /* An explicit height defines the containing block even when no
			 * in-flow child contributed height — e.g. a sized
			 * position:relative box holding only an absolutely-positioned
			 * overlay, whose bottom/right insets resolve against it. */
            if (self->css_height > 0.0f) {
                float pad_box_h =
                    self->border_box
                        ? (self->css_height - self->border_top - self->border_bottom)
                        : (self->css_height + self->padding_top + self->padding_bottom);
                if (pad_box_h > cb_h) {
                    cb_h = pad_box_h;
                }
            }
            if (cb_h < 0.0f) {
                cb_h = 0.0f;
            }
        }
        struct float_result placement_res =
            layout_absolute_child(r, child_idx, cb_x, cb_y, cb_w, cb_h);
        if (YETTY_IS_ERR(placement_res)) {
            free(deferred_absolute);
            return YETTY_ERR(float, "layout_block: absolute child", placement_res);
        }
    }
    free(deferred_absolute);

    /* Total consumed height includes our own padding-top + content +
	 * padding-bottom. The caller stored origin_y as our top, so
	 * (cursor_y - origin_y) already counts pad_top + content; just
	 * add pad_bottom. */
    float total_height = (cursor_y - origin_y) + pad_bottom;

    /* -webkit-line-clamp: cap the content box to N text lines. The
	 * clamp-to-N idiom truncates a title/snippet; without the cap the
	 * box grows to the full untruncated text and everything below it
	 * drifts (news-card feeds). Only when height is auto — an explicit
	 * height already fixed the box. Uses the same line-height the wrap
	 * pass used, so N of our lines line up with the cap. */
    self = &r->boxes.data[idx];
    if (self->line_clamp > 0 && self->css_height == 0.0f) {
        float clamp_font_size = self->font_size > 0.0f ? self->font_size : 16.0f;
        float clamp_line_height =
            self->line_height > 0.0f ? self->line_height : clamp_font_size * 1.25f;
        float content_cap = (float)self->line_clamp * clamp_line_height;
        float content_height = total_height - pad_top - pad_bottom;
        if (content_height > content_cap) {
            total_height = content_cap + pad_top + pad_bottom;
        }
    }
    return YETTY_OK(float, total_height);
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
    root->width_source = YL_SRC_VIEWPORT;
    root->height_source = YL_SRC_CONTENT;
    /* The root's containing block is the viewport — resolve any percent
     * margins/paddings on it before layout_block reads them (children are
     * resolved inside the block loop against their own parent). */
    resolve_pct_metrics(root, (float)r->viewport_w);

    struct float_result layout_res = layout_block(r, 0, 0, 0, (float)r->viewport_w);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "ylexbor_layout: layout_block");
    float h = layout_res.value;
    r->boxes.data[0].h = h;
    r->content_height = (int)h;

    return YETTY_OK_VOID();
}
