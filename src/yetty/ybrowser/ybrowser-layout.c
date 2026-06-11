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
static struct float_result layout_absolute_child(struct yetty_ylexbor *r, uint32_t cidx, float cb_x,
                                                 float cb_y, float cb_w, float cb_h);
static void flex_layout_absolute_children(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                          float origin_y, float content_w, float container_h);
static struct float_result layout_grid(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                       float origin_y, float content_w);
static float measure_cell_content_width(struct yetty_ylexbor *r, uint32_t cell_idx, int *budget);
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
 * padding box (the relative-positioned card), fixed against the viewport.
 * Best-effort: an error on one overlay does not abort the layout. */
static void flex_layout_absolute_children(struct yetty_ylexbor *r, uint32_t idx, float origin_x,
                                          float origin_y, float content_w, float container_h)
{
    struct yetty_ylexbor_box *self = &r->boxes.data[idx];
    float cb_x = origin_x + self->border_left;
    float cb_y = origin_y + self->border_top;
    float cb_w = content_w;
    float cb_h = container_h - self->border_top - self->border_bottom;
    if (cb_h < 0.0f) {
        cb_h = 0.0f;
    }
    for (uint32_t child_idx = r->boxes.data[idx].first_child; child_idx != 0;
         child_idx = r->boxes.data[child_idx].next_sibling) {
        uint8_t child_pos = r->boxes.data[child_idx].position;
        if (child_pos != YL_POS_ABSOLUTE && child_pos != YL_POS_FIXED) {
            continue;
        }
        struct float_result placement_res =
            (child_pos == YL_POS_FIXED)
                ? layout_absolute_child(r, child_idx, 0.0f, 0.0f, (float)r->viewport_w,
                                        (float)r->viewport_h)
                : layout_absolute_child(r, child_idx, cb_x, cb_y, cb_w, cb_h);
        if (YETTY_IS_ERR(placement_res)) {
            yetty_ycore_error_destroy(placement_res.error);
        }
        self = &r->boxes.data[idx];
    }
}

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
        /* Absolutely-positioned / fixed children are out of flow — they are
         * not flex items. Placed against this container in the pass below. */
        if (r->boxes.data[cidx].position == YL_POS_ABSOLUTE ||
            r->boxes.data[cidx].position == YL_POS_FIXED) {
            continue;
        }
        children[n_children++] = cidx;
    }
    if (n_children == 0) {
        /* Still place any out-of-flow children before bailing. */
        flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, pad_top + pad_bottom);
        return YETTY_OK(float, pad_top + pad_bottom);
    }

    /* Flex `gap` between items (stored in grid_col_gap by box-build). It
	 * consumes main-axis space before items are sized, so subtract the total
	 * inter-item gap from the budget; the placement loop re-inserts it. */
    float flex_gap = self->grid_col_gap > 0.0f ? self->grid_col_gap : 0.0f;
    float total_flex_gap = n_children > 1 ? (float)(n_children - 1) * flex_gap : 0.0f;

    /* Main-axis budget. */
    float main_budget;
    if (column_dir) {
        main_budget = css_h > 0 ? css_h : 0;
    } else {
        main_budget = content_width;
    }
    main_budget -= total_flex_gap;
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
    float main_size[YL_FLEX_MAX_CHILDREN];
    float total_basis = 0.0f;
    float total_grow = 0.0f;
    int autobasis_count = 0;

    /* Snapshot replaced-element (image) intrinsic sizes BEFORE the placement
     * loop overwrites c->w/c->h. A flex image must keep its own width/height
     * (from box-build: HTML width/height attrs or natural decode) — it must
     * not be auto-distributed across the row or stretched to the cross axis.
     * Without this a 14x14 source-favicon balloons to the column width with a
     * 100px fallback height and covers the article thumbnail (Google News). */
    float img_w_intr[YL_FLEX_MAX_CHILDREN];
    float img_h_intr[YL_FLEX_MAX_CHILDREN];
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
    bool is_auto[YL_FLEX_MAX_CHILDREN] = {false};
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
            float img_main = column_dir ? img_h_intr[i] : img_w_intr[i];
            if (css_main > 0.0f) {
                basis = css_main;
            } else if (css_main < 0.0f) {
                basis = main_budget * (-css_main);
            } else if (img_main > 0.0f) {
                /* Image with no flex-basis sizes to its intrinsic main-axis
                 * dimension, not the auto-distributed share. */
                basis = img_main;
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
        float item_content[YL_FLEX_MAX_CHILDREN];
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
        }
        if (content_driven) {
            total_basis = 0.0f;
            for (uint32_t i = 0; i < n_children; i++) {
                main_size[i] = item_content[i];
                is_auto[i] = false;
                total_basis += item_content[i];
            }
            autobasis_count = 0;
        } else {
            for (uint32_t i = 0; i < n_children; i++) {
                main_size[i] = per;
            }
            total_basis = main_budget;
        }
    }

    /* Mixed sized + auto items with no explicit grow: the auto items share
     * the leftover space (the dominant "fixed sidebar/thumbnail + flexible
     * content" pattern — e.g. a news card's fixed thumbnail + flexible text
     * body). Without this an auto item next to a sized one collapsed to 0.
     * Skipped when something grows (that path distributes below) or when ALL
     * items are auto (the even-split fallback above already handled it). */
    if (autobasis_count > 0 && autobasis_count < (int)n_children && total_grow == 0.0f) {
        float leftover_auto = main_budget - total_basis;
        if (leftover_auto > 0.0f) {
            float per = leftover_auto / (float)autobasis_count;
            for (uint32_t i = 0; i < n_children; i++) {
                if (is_auto[i]) {
                    main_size[i] += per;
                    total_basis += per;
                }
            }
        }
    }

    /* flex-wrap: break the row into multiple flex lines. Each item keeps its
	 * basis main-size; an item that would overflow the current line's main
	 * extent starts a new line, and lines stack on the cross axis (tallest
	 * item per line drives that line's height). Per-line grow is not
	 * distributed — the common wrap idiom uses fixed-size items. Row direction
	 * only; column wrap is not modelled. */
    if (self->flex_wrap && !column_dir) {
        float line_x = content_origin_x;
        float line_top = content_origin_y;
        float line_h = 0.0f;
        bool first_in_line = true;
        for (uint32_t i = 0; i < n_children; i++) {
            uint32_t cidx = children[i];
            struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
            float item_main = main_size[i];
            /* An auto-basis item with no resolved main size (no width, no
			 * flex-basis) takes the full line instead of collapsing to 0 — the
			 * dominant wrap idiom is a full-width content block sharing the
			 * container with fixed-size banner/ad slots, and Chrome wraps each
			 * such block onto its own line. Without this the content block
			 * (e.g. a news site's article grid) gets width 0 and every card
			 * piles up at the right edge. */
            if (item_main <= 0.0f && is_auto[i]) {
                item_main = content_width;
            }
            /* A single item never exceeds the line; clamp so it fits and the
			 * following item wraps below it. */
            if (item_main > content_width) {
                item_main = content_width;
            }
            if (!first_in_line && (line_x - content_origin_x) + item_main > content_width + 0.5f) {
                line_top += line_h + self->grid_row_gap;
                line_x = content_origin_x;
                line_h = 0.0f;
                first_in_line = true;
            }
            c->x = line_x;
            c->y = line_top;
            c->w = item_main;
            c->h = 0;
            float h = 0.0f;
            if (c->kind == YL_BOX_BLOCK) {
                struct float_result wrap_block_res = layout_block(r, cidx, c->x, c->y, c->w);
                YETTY_RETURN_IF_ERR(float, wrap_block_res, "layout_flex(wrap): block");
                h = wrap_block_res.value;
            } else if (c->kind == YL_BOX_INLINE_TEXT) {
                struct float_result wrap_text_res =
                    wrap_inline_box(r, cidx, c->x, c->y, c->w, /*text_align=*/0);
                YETTY_RETURN_IF_ERR(float, wrap_text_res, "layout_flex(wrap): inline");
                h = wrap_text_res.value;
            } else if (c->kind == YL_BOX_INLINE_IMAGE) {
                h = img_h_intr[i] > 0.0f ? img_h_intr[i] : (c->h > 0.0f ? c->h : 100.0f);
            }
            c = &r->boxes.data[cidx];
            if (c->css_height > 0.0f) {
                h = c->css_height;
            }
            c->h = h;
            if (h > line_h) {
                line_h = h;
            }
            line_x += item_main + flex_gap;
            first_in_line = false;
        }
        float container_h = (line_top + line_h) - origin_y + pad_bottom;
        flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, container_h);
        return YETTY_OK(float, container_h);
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
        float overflow = -leftover;
        float total_scaled = 0.0f;
        for (uint32_t i = 0; i < n_children; i++) {
            float sh = r->boxes.data[children[i]].flex_shrink;
            if (sh < 0.0f) {
                sh = 1.0f;
            }
            total_scaled += sh * main_size[i];
        }
        if (total_scaled > 0.0f) {
            for (uint32_t i = 0; i < n_children; i++) {
                float sh = r->boxes.data[children[i]].flex_shrink;
                if (sh < 0.0f) {
                    sh = 1.0f;
                }
                main_size[i] -= overflow * (sh * main_size[i]) / total_scaled;
                if (main_size[i] < 0.0f) {
                    main_size[i] = 0.0f;
                }
            }
        }
        leftover = 0.0f;
    }

    /* Min-width floor (row only): a flex item may not shrink below its
	 * `min-width`. Clamp violators up to their min, then reclaim the overflow
	 * from the still-shrinkable items so the line still fits the container.
	 * This is the min-violation resolution step of the flex algorithm, done in
	 * a single pass — enough for the common one-constrained-item layouts. */
    if (!column_dir) {
        float deficit = 0.0f;
        bool min_locked[YL_FLEX_MAX_CHILDREN] = {false};
        for (uint32_t i = 0; i < n_children; i++) {
            float mn = r->boxes.data[children[i]].css_min_width;
            if (mn > 0.0f && main_size[i] < mn) {
                deficit += mn - main_size[i];
                main_size[i] = mn;
                min_locked[i] = true;
            }
        }
        if (deficit > 0.0f) {
            float pool = 0.0f;
            for (uint32_t i = 0; i < n_children; i++) {
                if (!min_locked[i] && main_size[i] > 0.0f) {
                    pool += main_size[i];
                }
            }
            if (pool > 0.0f) {
                for (uint32_t i = 0; i < n_children; i++) {
                    if (!min_locked[i] && main_size[i] > 0.0f) {
                        main_size[i] -= deficit * (main_size[i] / pool);
                        if (main_size[i] < 0.0f) {
                            main_size[i] = 0.0f;
                        }
                    }
                }
            }
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
    float natural_h[YL_FLEX_MAX_CHILDREN];
    float natural_w[YL_FLEX_MAX_CHILDREN];
    /* First pass — lay each child at its resolved main-size, learn
	 * natural cross-size. */
    float cursor = (column_dir ? content_origin_y : content_origin_x) + leading;
    for (uint32_t i = 0; i < n_children; i++) {
        uint32_t cidx = children[i];
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        bool is_image = (c->kind == YL_BOX_INLINE_IMAGE);
        if (column_dir) {
            c->x = content_origin_x;
            c->y = cursor;
            /* A column image keeps its intrinsic width; other items stretch
             * to the content width. */
            c->w = (is_image && img_w_intr[i] > 0.0f) ? img_w_intr[i] : content_width;
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
            }
            c->h = main_size[i];
        } else {
            c->h = h;
        }
        natural_h[i] = c->h;
        natural_w[i] = c->w;
        cursor += main_size[i] + gap + flex_gap;
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
        /* Images are replaced elements — never stretch them across the cross
         * axis; keep their intrinsic cross dimension (a row favicon stays its
         * own height, a column image its own width). */
        if (c->kind == YL_BOX_INLINE_IMAGE) {
            float img_cross = column_dir ? img_w_intr[i] : img_h_intr[i];
            if (img_cross > 0.0f) {
                cross_used = img_cross;
            }
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
    float container_h =
        column_dir ? (total_main + pad_bottom) : (cross_budget + pad_top + pad_bottom);
    /* Out-of-flow pass: place absolute / fixed children (e.g. the inset:0
     * click overlays Google News stacks over each card) against this flex
     * container now that its size is known. */
    flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, container_h);
    return YETTY_OK(float, container_h);
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
            float adv =
                c->glyph_advance > 0.0f ? c->glyph_advance : yetty_ylexbor_glyph_advance_ratio(r);
            sum += yetty_ylexbor_naive_text_width(c->text, c->text_len, c->font_size, adv);
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

    /* Width: explicit width wins; else left+right both pin it; else
     * shrink toward the inset edges (no intrinsic-size pass, so this is an
     * approximation that still respects a single anchoring inset). */
    float width;
    if (c->css_width > 0.0f) {
        width = c->css_width;
    } else if (c->css_width < 0.0f) {
        width = cb_w * (-c->css_width); /* percent encoded as negative ratio */
    } else if (left_set && right_set) {
        width = cb_w - left - right;
    } else {
        width = cb_w - (left_set ? left : 0.0f) - (right_set ? right : 0.0f);
    }
    if (width < 0.0f) {
        width = 0.0f;
    }

    float x;
    if (left_set) {
        x = cb_x + left;
    } else if (right_set) {
        x = cb_x + cb_w - right - width;
    } else {
        x = cb_x; /* static-position approximation */
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
    float height = (c->css_height > 0.0f) ? c->css_height : measure.value;
    /* top + bottom both set with no explicit height stretches the box to the
     * containing block — the `inset:0` full-cover overlay pattern. */
    if (top_set && bottom_set && c->css_height <= 0.0f) {
        float stretched = cb_h - top - bottom;
        if (stretched > height) {
            height = stretched;
        }
    }

    float y;
    if (top_set) {
        y = cb_y + top;
    } else if (bottom_set) {
        y = cb_y + cb_h - bottom - height;
    } else {
        y = cb_y; /* static-position approximation */
    }

    /* If y moved, re-lay the subtree so descendants land at the final
     * position (their coords were computed against the provisional origin). */
    if (y != cb_y) {
        struct float_result place = layout_block(r, cidx, x, y, content_w);
        YETTY_RETURN_IF_ERR(float, place, "layout_absolute_child: place");
        c = &r->boxes.data[cidx];
        height = (c->css_height > 0.0f) ? c->css_height : place.value;
    }

    c->x = x;
    c->y = y;
    c->w = width;
    c->h = height;
    apply_transform(r, cidx);
    return YETTY_OK(float, height);
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

    /* `auto` / min-content / max-content tracks size to the max-content of the
	 * items placed in them. Pre-pass: simulate row-major placement (honoring
	 * spans) and measure each single-span item's intrinsic content width into
	 * its column. */
    float auto_w[YL_GRID_MAX_TRACKS] = {0};
    bool any_auto = false;
    for (int c = 0; c < ncols; c++) {
        if (self->grid_tracks[c].is_auto) {
            any_auto = true;
        }
    }
    if (any_auto) {
        int acol = 0;
        for (uint32_t cc = self->first_child; cc != 0; cc = r->boxes.data[cc].next_sibling) {
            struct yetty_ylexbor_box *ch = &r->boxes.data[cc];
            if (ch->position == YL_POS_ABSOLUTE || ch->position == YL_POS_FIXED ||
                ch->float_side != 0) {
                continue;
            }
            int sp = ch->grid_col_span > 0 ? ch->grid_col_span : 1;
            if (sp > ncols) {
                sp = ncols;
            }
            if (acol + sp > ncols) {
                acol = 0;
            }
            if (sp == 1 && self->grid_tracks[acol].is_auto) {
                float iw;
                if (ch->css_width > 0.0f) {
                    iw = ch->css_width;
                } else {
                    int budget = YL_CELL_MEASURE_BUDGET;
                    iw = measure_cell_content_width(r, cc, &budget) + ch->padding_left +
                         ch->padding_right + ch->border_left + ch->border_right;
                    if (ch->kind == YL_BOX_INLINE_IMAGE && ch->w > 0.0f) {
                        iw = ch->w;
                    }
                }
                if (iw > auto_w[acol]) {
                    auto_w[acol] = iw;
                }
            }
            acol += sp;
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
	 * track, so it is unaffected. */
    if (self->grid_line_spec == NULL) {
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
                struct float_result gres = layout_block(r, cidx, cell_x, row_top, cell_w);
                YETTY_RETURN_IF_ERR(float, gres, "layout_grid(named): child block");
                c = &r->boxes.data[cidx];
                child_h = (c->css_height > 0.0f) ? c->css_height : gres.value;
                c->h = child_h;
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
        flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, named_h);
        return YETTY_OK(float, named_h);
    }

    /* Auto-flow children into cells, row-major. */
    float row_y = content_origin_y;
    float row_h = 0.0f;
    int col = 0;
    int placed_any = 0;
    for (uint32_t cidx = self->first_child; cidx != 0; cidx = r->boxes.data[cidx].next_sibling) {
        struct yetty_ylexbor_box *c = &r->boxes.data[cidx];
        if (c->position == YL_POS_ABSOLUTE || c->position == YL_POS_FIXED || c->float_side != 0) {
            continue;
        }
        /* Column span: the item occupies `span` consecutive columns (github's
		 * 12-col cards: `grid-column: span N`). Clamp to the track count. */
        int span = c->grid_col_span > 0 ? c->grid_col_span : 1;
        if (span > ncols) {
            span = ncols;
        }
        /* Wrap to a new row when this item won't fit in the columns left. */
        if (col + span > ncols) {
            col = 0;
            row_y += row_h + row_gap;
            row_h = 0.0f;
        }
        float cell_x = col_x[col];
        /* Width spans `span` tracks plus the inter-track gaps between them. */
        float cell_w = 0.0f;
        for (int sc = 0; sc < span && (col + sc) < ncols; sc++) {
            cell_w += col_w[col + sc];
        }
        cell_w += (float)(span - 1) * col_gap;
        resolve_pct_metrics(c, cell_w);
        float child_h = 0.0f;
        if (c->kind == YL_BOX_BLOCK) {
            c->x = cell_x;
            c->y = row_y;
            c->w = cell_w;
            struct float_result block_res = layout_block(r, cidx, cell_x, row_y, cell_w);
            YETTY_RETURN_IF_ERR(float, block_res, "layout_grid: child block");
            c = &r->boxes.data[cidx];
            child_h = (c->css_height > 0.0f) ? c->css_height : block_res.value;
            c->h = child_h;
        } else if (c->kind == YL_BOX_INLINE_TEXT) {
            struct float_result wrap_res = wrap_inline_box(r, cidx, cell_x, row_y, cell_w, 0);
            YETTY_RETURN_IF_ERR(float, wrap_res, "layout_grid: child text");
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
            child_h = img_h;
        }
        if (child_h > row_h) {
            row_h = child_h;
        }
        apply_transform(r, cidx);
        placed_any = 1;
        col += span;
    }

    float block_height = (placed_any ? (row_y + row_h) : content_origin_y) - origin_y + pad_bottom;
    self = &r->boxes.data[idx];
    flex_layout_absolute_children(r, idx, origin_x, origin_y, content_w, block_height);
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
         * block. */
        if (c->position == YL_POS_ABSOLUTE || c->position == YL_POS_FIXED) {
            cidx = c->next_sibling;
            continue;
        }

        /* Resolve this child's percent margins/paddings against our
         * content width — the child's containing block — before any of
         * them are read for flow, collapsing, or float placement. */
        resolve_pct_metrics(c, content_width);

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
            if (c->css_width > 0.0f) {
                child_w = c->css_width + box_extra;
            } else if (c->css_width < 0.0f) {
                child_w = avail_w * (-c->css_width) + box_extra;
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

    /* Out-of-flow pass: place absolute / fixed children now that this
	 * block's content box is known. `absolute` resolves against this block's
	 * padding box; `fixed` against the viewport. Their heights do not feed
	 * back into `block_height` — they are out of flow. */
    for (uint32_t child_idx = r->boxes.data[idx].first_child; child_idx != 0;
         child_idx = r->boxes.data[child_idx].next_sibling) {
        uint8_t child_pos = r->boxes.data[child_idx].position;
        if (child_pos != YL_POS_ABSOLUTE && child_pos != YL_POS_FIXED) {
            continue;
        }
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
        YETTY_RETURN_IF_ERR(float, placement_res, "layout_block: absolute child");
        /* self / vector may have relocated during the recursion. */
        self = &r->boxes.data[idx];
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
