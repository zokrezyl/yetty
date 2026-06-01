/*
 * ypdf-positions-test — assert that ypdf produces text spans whose
 * positions match what a real PDF renderer (mutool stext) sees.
 *
 * Ground truth: mutool stext on test/ut/ypdf/pdf-sample.pdf prints, for
 * the title "Adobe Acrobat PDF Files" on page 1:
 *   font Arial-BoldMT size 14.04
 *   first char 'A' at quad-min (213.76, 69.94), baseline y=84.96
 *
 * mutool reports y in TEXT-DOWN coords (origin top-left), same as
 * ypdf's screen-y output. We expect ypdf's text-span x ≈ 213.76 and
 * the span y ≈ 84.96 (baseline) ± a few pixels.
 *
 * The test scans every TEXT_SPAN prim emitted by ypdf, finds the one
 * whose content is the title (or starts with "Adobe"), and asserts:
 *   1. font_size ≈ 14 (PDF text-state Tfs).
 *   2. x is within ±5 px of 213.76.
 *   3. y is within ±5 px of 84.96 (baseline) — wider tolerance because
 *      ypdf might emit at the cap-height instead of strict baseline.
 *
 * Looser asserts catch the "every Tj at the same position" overlap bug:
 *   - At least one span on a *different* line (y ≈ 100..120) exists.
 *   - Spans on different lines must have distinct x positions OR
 *     monotonically increasing y so they don't all stack on (213,85).
 *
 * If ypdf substitutes a system font for the non-embedded Arial, exact
 * advance / inter-char positions will differ. We only assert positions
 * that come from the PDF text matrix (which is font-independent).
 */

#include <pdfio.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ydraw/flyweight.h>
#include <yetty/ypdf/ypdf.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ground truth from mutool stext on test/ut/ypdf/pdf-sample.pdf (MuPDF
 * AGPL — used as an external reference renderer; numbers are facts about
 * the PDF, not derivative code). Two granularities:
 *   - pdf-sample-expected-lines.h: per-baseline leftmost-x + rightmost-x
 *     (catches whole-line shifts and cumulative drift).
 *   - pdf-sample-expected-chars.h: per-character (x, y, font_size, c).
 *     The strongest invariant — matches the canvas's per-glyph layout. */
#include "pdf-sample-expected-lines.h"
#include "pdf-sample-expected-chars.h"

#define REQUIRE(cond, msg)                                                                        \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);                        \
            return 1;                                                                             \
        }                                                                                         \
    } while (0)

#define REQUIRE_NEAR(actual, expected, tol, label)                                                \
    do {                                                                                          \
        float _a = (float)(actual);                                                               \
        float _e = (float)(expected);                                                             \
        if (fabsf(_a - _e) > (float)(tol)) {                                                      \
            fprintf(stderr, "FAIL: %s:%d: %s: got %.3f, expected %.3f ± %.3f\n", __FILE__,        \
                    __LINE__, label, _a, _e, (float)(tol));                                       \
            return 1;                                                                             \
        }                                                                                         \
    } while (0)

#ifndef YPDF_TEST_PDF
#define YPDF_TEST_PDF "pdf-sample.pdf"
#endif

static bool error_cb(struct _pdfio_file_s *f, const char *s, void *d)
{
    (void)f;
    (void)d;
    fprintf(stderr, "pdfio: %s\n", s);
    return true;
}

/* True if `view->text` contains `needle` as a substring. text is NOT
 * NUL-terminated. */
static int text_contains(const struct yetty_ydraw_text_span_drawable_view *view,
                         const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || view->text_len < nlen) {
        return 0;
    }
    for (uint32_t i = 0; i + nlen <= view->text_len; i++) {
        if (memcmp(view->text + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

struct span_summary {
    uint32_t total_spans;
    uint32_t spans_with_text;
    uint32_t spans_at_same_xy;     /* count of spans whose (x, y) collides */
    float min_x, max_x;
    float min_y, max_y;
    float min_font_size, max_font_size;
};

int main(void)
{
    struct _pdfio_file_s *pdf = pdfioFileOpen(YPDF_TEST_PDF, NULL, NULL, error_cb, NULL);
    REQUIRE(pdf, "pdfioFileOpen failed");

    struct yetty_ypdf_render_result rr = yetty_ypdf_render_pdf(pdf);
    REQUIRE(rr.ok, "yetty_ypdf_render_pdf failed");

    struct yetty_ypdf_render_output *out = &rr.value;
    REQUIRE(out->buffer, "buffer is NULL");
    REQUIRE(out->page_count == 1, "expected 1 page");

    struct yetty_ydraw_flyweight_registry_ptr_result rfr = yetty_ydraw_flyweight_create();
    REQUIRE(rfr.ok, "flyweight_create failed");
    struct yetty_ydraw_flyweight_registry *reg = rfr.value;

    struct span_summary sum = {0};
    sum.min_x = sum.min_y = sum.min_font_size = 1e9f;
    sum.max_x = sum.max_y = sum.max_font_size = -1e9f;

    /* Track positions to detect "all spans at same x,y" overlap bug. */
    float prev_x = -1, prev_y = -1;

    /* Capture key landmarks for explicit position asserts.
     *
     * The PDF emits text via TJ with kerning, so each "word" is split
     * across many adjacent spans on the same baseline. The TITLE
     * "Adobe Acrobat PDF Files" lives on a single y line at ~84.96 in
     * 14pt; the first body line "Adobe® Portable Document Format ..."
     * lives at ~112.68 in 12pt. We pick the LEFTMOST span on each line
     * as the anchor — it must match mutool's first-char x. */
    float title_y_target = 84.96f;
    float body_y_target = 112.68f;
    int found_title = 0;
    float title_min_x = 1e9f;
    float title_size = 0;
    int found_body = 0;
    float body_min_x = 1e9f;
    float body_size = 0;
    int found_print = 0;            /* second-half text on page */
    float print_y = 0;

    struct yetty_ydraw_primitive_iter_result ir =
        yetty_ydraw_draw_list_drawable_first(out->buffer, reg);
    REQUIRE(ir.ok, "iterator first failed");
    struct yetty_ydraw_primitive_iter it = ir.value;

    int dump_count = 0;
    for (;;) {
        uint32_t t = it.fw.data[0];
        if (t == YETTY_YDRAW_TYPE_TEXT_SPAN) {
            struct yetty_ydraw_text_span_drawable_view v;
            if (yetty_ydraw_text_span_drawable_parse(it.fw.data, &v) == 0) {
                if (dump_count++ < 320) {
                    char snippet[64];
                    size_t n = v.text_len < sizeof(snippet) - 1 ? v.text_len : sizeof(snippet) - 1;
                    memcpy(snippet, v.text, n);
                    snippet[n] = '\0';
                    fprintf(stderr,
                            "  span[%2d] x=%7.2f y=%7.2f sz=%5.2f font=%2d text='%s'\n",
                            dump_count - 1, v.x, v.y, v.font_size, v.font_id, snippet);
                }
                sum.total_spans++;
                if (v.text_len > 0) {
                    sum.spans_with_text++;
                }
                if (v.x < sum.min_x) sum.min_x = v.x;
                if (v.x > sum.max_x) sum.max_x = v.x;
                if (v.y < sum.min_y) sum.min_y = v.y;
                if (v.y > sum.max_y) sum.max_y = v.y;
                if (v.font_size < sum.min_font_size) sum.min_font_size = v.font_size;
                if (v.font_size > sum.max_font_size) sum.max_font_size = v.font_size;
                if (prev_x >= 0 && fabsf(v.x - prev_x) < 0.001f &&
                    fabsf(v.y - prev_y) < 0.001f) {
                    sum.spans_at_same_xy++;
                }
                prev_x = v.x;
                prev_y = v.y;

                /* Title row: leftmost span at y ≈ 84.96 in size ~14. */
                if (fabsf(v.y - title_y_target) < 1.0f) {
                    found_title = 1;
                    if (v.x < title_min_x) {
                        title_min_x = v.x;
                    }
                    title_size = v.font_size;
                }
                /* First body row: leftmost span at y ≈ 112.68 in size ~12. */
                if (fabsf(v.y - body_y_target) < 1.0f) {
                    found_body = 1;
                    if (v.x < body_min_x) {
                        body_min_x = v.x;
                    }
                    body_size = v.font_size;
                }
                if (!found_print && text_contains(&v, "PDF files always print")) {
                    found_print = 1;
                    print_y = v.y;
                }
            }
        }
        struct yetty_ydraw_primitive_iter_result nx =
            yetty_ydraw_draw_list_drawable_next(out->buffer, reg, &it);
        if (!nx.ok) break;
        it = nx.value;
    }

    fprintf(stderr,
            "ypdf-positions: %u text spans (%u non-empty)\n"
            "                x range [%.2f, %.2f] (span %.2f)\n"
            "                y range [%.2f, %.2f] (span %.2f)\n"
            "                font_size range [%.2f, %.2f]\n"
            "                spans at same (x,y) as previous: %u\n",
            sum.total_spans, sum.spans_with_text, sum.min_x, sum.max_x, sum.max_x - sum.min_x,
            sum.min_y, sum.max_y, sum.max_y - sum.min_y, sum.min_font_size, sum.max_font_size,
            sum.spans_at_same_xy);

    /* === Sanity ============================================================ */
    REQUIRE(sum.total_spans >= 5, "ypdf emitted <5 text spans for sample PDF");
    REQUIRE(sum.spans_with_text >= 5, "ypdf emitted <5 non-empty text spans");

    /* === Title row anchor ================================================= */
    /* The PDF emits the title via TJ with kerning, so "Adobe Acrobat PDF
     * Files" ends up split across many spans on the same baseline. We
     * find the leftmost one — it must start where mutool says 'A' lives. */
    REQUIRE(found_title, "no text span found at title baseline (y≈84.96)");
    REQUIRE_NEAR(title_min_x, 213.76f, 5.0f, "title leftmost-span x");
    REQUIRE_NEAR(title_size, 14.0f, 1.5f, "title font size");

    /* === First body row anchor ============================================ */
    REQUIRE(found_body, "no text span found at first body line (y≈112.68)");
    REQUIRE_NEAR(body_min_x, 89.92f, 5.0f, "body leftmost-span x");
    REQUIRE_NEAR(body_size, 12.0f, 1.5f, "body font size");

    /* === Layout sanity: spans must span >1 row ============================ */
    /* If every Tj overlapped at the same y, max_y - min_y would be tiny.
     * Real layout puts text on multiple lines spanning hundreds of pixels. */
    REQUIRE(sum.max_y - sum.min_y > 50.0f,
            "text spans span <50px vertically — likely all overlapping at same y");

    /* === Layout sanity: spans must span the page horizontally ============= */
    REQUIRE(sum.max_x - sum.min_x > 50.0f,
            "text spans span <50px horizontally — every Tj at the same x");

    /* === Layout sanity: not every span at the same (x,y) ================== */
    /* If 80%+ of spans collide at the same (x,y) as their neighbour, the
     * text matrix isn't advancing — this catches the "no measure_text"
     * bug where text_emit_cb's error stops the parser from updating
     * text_matrix. */
    REQUIRE(sum.spans_at_same_xy * 5 < sum.total_spans * 4,
            "≥80% of consecutive text spans collide at identical (x,y) — "
            "text matrix not advancing (likely measure_text returning error)");

    /* === Found a span on a later line ===================================== */
    if (found_print) {
        /* "PDF files always print correctly" appears later on the page —
         * its y must be below the title. */
        REQUIRE(print_y > title_y_target + 20.0f,
                "second-half text 'PDF files always print' has y too close to title");
    }

    /* === Per-line position match against mutool ground truth ============ *
     *
     * For each text baseline mutool sees, find ypdf's spans at that
     * baseline and assert that:
     *   leftmost ypdf-span x  ≈ leftmost mutool-char x   (line start)
     *   rightmost ypdf-span (x + estimated_width) ≈ rightmost mutool
     *     char's right edge                              (line end)
     *
     * If both endpoints match, every ypdf segment in between landed at
     * a sensible position (cumulative drift would push the right edge
     * out of tolerance). This is the tightest assertion that's robust
     * against ypdf emitting one span per Tj-segment vs mutool's
     * per-character view.
     *
     * Estimating right-edge from a span: use 0.5 * font_size * char_count
     * as an upper bound for proportional fonts (typical char advance is
     * 0.4–0.6 em). We compare to mutool's right edge ± 15 px to absorb
     * the estimation noise + font substitution differences (Liberation
     * Sans is metric-compatible with Arial but not character-identical). */

    /* Re-scan: the first iter consumed everything; iterate again. */
    struct yetty_ydraw_primitive_iter_result ir2 =
        yetty_ydraw_draw_list_drawable_first(out->buffer, reg);
    REQUIRE(ir2.ok, "iterator first (rescan) failed");

    const float Y_TOL = 2.0f;
    const float X_LEFT_TOL = 5.0f;     /* leftmost x — ypdf's text matrix  */
    const float X_RIGHT_TOL = 25.0f;   /* right edge — looser, font-dependent */
    const float SZ_TOL = 1.5f;
    const size_t n_lines = sizeof(EXPECTED_LINES) / sizeof(EXPECTED_LINES[0]);
    size_t matched_left = 0;
    size_t matched_right = 0;
    size_t found_lines = 0;
    int first_fail = 0;

    for (size_t li = 0; li < n_lines; li++) {
        const struct mutool_line *ml = &EXPECTED_LINES[li];

        /* Walk every span on this baseline. */
        struct yetty_ydraw_primitive_iter it3 = ir2.value;
        float ypdf_left = 1e9f, ypdf_right_estimate = -1e9f;
        int found = 0;
        for (;;) {
            if (it3.fw.data[0] == YETTY_YDRAW_TYPE_TEXT_SPAN) {
                struct yetty_ydraw_text_span_drawable_view v;
                if (yetty_ydraw_text_span_drawable_parse(it3.fw.data, &v) == 0 &&
                    v.text_len > 0 && fabsf(v.y - ml->y) <= Y_TOL &&
                    fabsf(v.font_size - ml->font_size) <= SZ_TOL) {
                    found = 1;
                    if (v.x < ypdf_left) {
                        ypdf_left = v.x;
                    }
                    /* Upper-bound right edge estimate. */
                    float est_right = v.x + 0.55f * v.font_size * (float)v.text_len;
                    if (est_right > ypdf_right_estimate) {
                        ypdf_right_estimate = est_right;
                    }
                }
            }
            struct yetty_ydraw_primitive_iter_result nx3 =
                yetty_ydraw_draw_list_drawable_next(out->buffer, reg, &it3);
            if (!nx3.ok) break;
            it3 = nx3.value;
        }

        if (!found) {
            if (first_fail++ < 5) {
                fprintf(stderr, "  line MISS: y=%.2f sz=%.2f '%s' — no spans\n", ml->y,
                        ml->font_size, ml->first_char);
            }
            continue;
        }
        found_lines++;

        int left_ok = fabsf(ypdf_left - ml->x_left) <= X_LEFT_TOL;
        /* For right: ypdf's estimated right edge must REACH the mutool
         * right edge (within tolerance). Underestimate = drift / missing
         * spans. Overestimate is OK (estimate is an upper bound). */
        int right_ok = ypdf_right_estimate + X_RIGHT_TOL >= ml->x_right;

        if (left_ok) matched_left++;
        if (right_ok) matched_right++;

        if (!left_ok || !right_ok) {
            if (first_fail++ < 10) {
                fprintf(stderr,
                        "  line[%zu] y=%.2f sz=%.2f '%s' "
                        "left expected=%.2f got=%.2f %s | "
                        "right expected=%.2f estimate=%.2f %s\n",
                        li, ml->y, ml->font_size, ml->first_char, ml->x_left, ypdf_left,
                        left_ok ? "OK" : "FAIL", ml->x_right, ypdf_right_estimate,
                        right_ok ? "OK" : "FAIL");
            }
        }
    }

    fprintf(stderr,
            "per-line match: %zu/%zu lines found, %zu/%zu left-x ok, %zu/%zu right-x ok\n",
            found_lines, n_lines, matched_left, n_lines, matched_right, n_lines);

    /* Every line must be present, every line's left-x must match within
     * tolerance, every line's right edge must reach the mutool right
     * edge (within wider tolerance). All three are required. */
    REQUIRE(found_lines == n_lines, "some text lines missing from ypdf output");
    REQUIRE(matched_left == n_lines, "some text lines have wrong leftmost-x");
    REQUIRE(matched_right == n_lines,
            "some text lines fall short on the right (cumulative drift)");

    /* === Per-character position match (strongest invariant) ============== *
     *
     * For each TEXT_SPAN, materialise its FONT prim into a raster_font,
     * then walk char-by-char accumulating each glyph's advance to
     * compute the on-screen position the canvas WILL render at. Compare
     * against mutool's per-character ground truth.
     *
     * This catches *any* drift the user would see on screen:
     *   - wrong glyph advance (hmtx patch incorrect, font substitution
     *     metrics off)
     *   - wrong span anchor x (already covered by the leftmost-x test
     *     above, but caught here too for completeness)
     *   - missing glyphs (canvas would skip → cumulative offset wrong)
     *
     * Tolerance: ±3 px per char. Substituted fonts (Liberation Sans for
     * Arial, Noto Sans for Times) have very similar metrics but not
     * pixel-identical; mutool may itself be substituting. 90% match
     * required — failures pinpoint where the line "goes shit".
     */
    /* First, build a font_id → raster_font* map by walking FONT prims. */
    enum { MAX_FONTS_LOCAL = 32 };
    struct yetty_yfont_font *fonts[MAX_FONTS_LOCAL] = {0};
    struct yetty_ydraw_primitive_iter_result ir3 =
        yetty_ydraw_draw_list_drawable_first(out->buffer, reg);
    REQUIRE(ir3.ok, "iterator first (font scan) failed");
    struct yetty_ydraw_primitive_iter it4 = ir3.value;
    int loaded_fonts = 0;
    for (;;) {
        if (it4.fw.data[0] == YETTY_YDRAW_TYPE_FONT) {
            struct yetty_yfont_font_drawable_view fv;
            if (yetty_yfont_font_drawable_parse(it4.fw.data, &fv) == 0 && fv.font_id >= 0 &&
                fv.font_id < MAX_FONTS_LOCAL && fv.ttf && fv.ttf_len > 0) {
                struct yetty_font_font_result rf =
                    yetty_yfont_raster_font_create_from_data(fv.ttf, fv.ttf_len, "test", NULL,
                                                             32.0f);
                if (rf.ok) {
                    fonts[fv.font_id] = rf.value;
                    loaded_fonts++;
                }
            }
        }
        struct yetty_ydraw_primitive_iter_result nx4 =
            yetty_ydraw_draw_list_drawable_next(out->buffer, reg, &it4);
        if (!nx4.ok) break;
        it4 = nx4.value;
    }
    fprintf(stderr, "loaded %d fonts from FONT prims for per-char layout sim\n", loaded_fonts);

    /* Walk TEXT_SPANs and emit (computed_x, y, c) for each char. Compare
     * each to mutool's expected per-char position. */
    const float CHAR_X_TOL = 3.0f;
    const float CHAR_Y_TOL = 2.0f;
    size_t total_chars = 0, matched_chars = 0;
    int char_misses_dumped = 0;

    struct yetty_ydraw_primitive_iter_result ir4 =
        yetty_ydraw_draw_list_drawable_first(out->buffer, reg);
    REQUIRE(ir4.ok, "iterator first (char layout sim) failed");
    struct yetty_ydraw_primitive_iter it5 = ir4.value;
    for (;;) {
        if (it5.fw.data[0] == YETTY_YDRAW_TYPE_TEXT_SPAN) {
            struct yetty_ydraw_text_span_drawable_view v;
            if (yetty_ydraw_text_span_drawable_parse(it5.fw.data, &v) == 0 && v.text_len > 0 &&
                v.font_id >= 0 && v.font_id < MAX_FONTS_LOCAL && fonts[v.font_id]) {
                struct yetty_yfont_font *f = fonts[v.font_id];
                float cursor_x = v.x;
                /* Walk UTF-8: decode each codepoint, advance the cursor
                 * by the FULL multi-byte sequence's measure plus the PDF
                 * Tc/Tw spacing the producer baked into the prim. The
                 * canvas's expand_text_span_to_glyphs uses the same
                 * formula, so this simulation matches what gets rendered. */
                uint32_t k = 0;
                while (k < v.text_len) {
                    unsigned char c0 = (unsigned char)v.text[k];
                    int seqlen;
                    if (c0 < 0x80) seqlen = 1;
                    else if ((c0 & 0xE0) == 0xC0) seqlen = 2;
                    else if ((c0 & 0xF0) == 0xE0) seqlen = 3;
                    else if ((c0 & 0xF8) == 0xF0) seqlen = 4;
                    else { k++; continue; }
                    if (k + (uint32_t)seqlen > v.text_len) break;

                    struct float_result r =
                        f->ops->measure_text(f, v.text + k, (size_t)seqlen, v.font_size);
                    float adv = r.ok ? r.value : 0.0f;
                    adv += v.char_spacing;
                    if (seqlen == 1 && c0 == 0x20) {
                        adv += v.word_spacing;
                    }

                    /* Match only printable ASCII against mutool. */
                    if (seqlen == 1 && c0 >= 0x21 && c0 < 0x7f) {
                        total_chars++;
                        int hit = 0;
                        const size_t n_chars = sizeof(EXPECTED_CHARS) / sizeof(EXPECTED_CHARS[0]);
                        for (size_t mi = 0; mi < n_chars; mi++) {
                            if ((unsigned char)EXPECTED_CHARS[mi].c != c0) continue;
                            if (fabsf(EXPECTED_CHARS[mi].y - v.y) > CHAR_Y_TOL) continue;
                            if (fabsf(EXPECTED_CHARS[mi].x - cursor_x) <= CHAR_X_TOL) {
                                hit = 1;
                                break;
                            }
                        }
                        if (hit) {
                            matched_chars++;
                        } else if (char_misses_dumped < 12) {
                            fprintf(stderr,
                                    "  char miss '%c' computed (x=%.2f, y=%.2f) — no mutool "
                                    "char within ±%.1f x at this y\n",
                                    (char)c0, cursor_x, v.y, CHAR_X_TOL);
                            char_misses_dumped++;
                        }
                    }
                    cursor_x += adv;
                    k += (uint32_t)seqlen;
                }
            }
        }
        struct yetty_ydraw_primitive_iter_result nx5 =
            yetty_ydraw_draw_list_drawable_next(out->buffer, reg, &it5);
        if (!nx5.ok) break;
        it5 = nx5.value;
    }
    fprintf(stderr, "per-char layout sim: %zu/%zu chars match mutool (%.1f%%)\n", matched_chars,
            total_chars, total_chars ? 100.0 * matched_chars / total_chars : 0.0);

    /* Diagnostic dump: per-character cursor positions for any line whose
     * spans concatenate to contain "smaller". Lets us see exactly where
     * each glyph lands relative to mutool's ground truth — useful when
     * the user reports a visible gap that the per-char match doesn't
     * catch (e.g. wrong font_id pointing at a TTF with patched-to-zero
     * advances). */
    fprintf(stderr, "\n=== smaller-line diagnostic (per-char canvas-side cursor) ===\n");
    struct yetty_ydraw_primitive_iter_result ir5 =
        yetty_ydraw_draw_list_drawable_first(out->buffer, reg);
    if (ir5.ok) {
        struct yetty_ydraw_primitive_iter dit = ir5.value;
        for (;;) {
            if (dit.fw.data[0] == YETTY_YDRAW_TYPE_TEXT_SPAN) {
                struct yetty_ydraw_text_span_drawable_view dv;
                if (yetty_ydraw_text_span_drawable_parse(dit.fw.data, &dv) == 0 &&
                    dv.text_len > 0 && dv.font_id >= 0 && dv.font_id < MAX_FONTS_LOCAL &&
                    fonts[dv.font_id]) {
                    int has_sma = 0;
                    for (uint32_t k = 0; k + 3 <= dv.text_len; k++) {
                        if (dv.text[k] == 's' && dv.text[k+1] == 'm' && dv.text[k+2] == 'a') {
                            has_sma = 1;
                            break;
                        }
                    }
                    if (has_sma) {
                        fprintf(stderr, "  span x=%.3f y=%.3f font_id=%d sz=%.2f Tc=%.4f Tw=%.4f text='",
                                dv.x, dv.y, dv.font_id, dv.font_size, dv.char_spacing, dv.word_spacing);
                        fwrite(dv.text, 1, dv.text_len, stderr);
                        fprintf(stderr, "'\n");
                        struct yetty_yfont_font *f = fonts[dv.font_id];
                        float cx = dv.x;
                        for (uint32_t k = 0; k < dv.text_len; k++) {
                            unsigned char c0 = (unsigned char)dv.text[k];
                            if (c0 >= 0x80) continue;
                            struct float_result mr =
                                f->ops->measure_text(f, dv.text + k, 1, dv.font_size);
                            float adv = mr.ok ? mr.value : 0.0f;
                            fprintf(stderr, "    '%c' x=%.3f adv=%.3f Tc=%.3f%s\n",
                                    (char)c0, cx, adv, dv.char_spacing,
                                    c0 == 0x20 ? " (+Tw)" : "");
                            cx += adv + dv.char_spacing + (c0 == 0x20 ? dv.word_spacing : 0.0f);
                        }
                    }
                }
            }
            struct yetty_ydraw_primitive_iter_result nxd =
                yetty_ydraw_draw_list_drawable_next(out->buffer, reg, &dit);
            if (!nxd.ok) break;
            dit = nxd.value;
        }
    }
    fprintf(stderr, "=== end smaller-line diagnostic ===\n\n");

    /* Free the loaded fonts. */
    for (int i = 0; i < MAX_FONTS_LOCAL; i++) {
        if (fonts[i] && fonts[i]->ops && fonts[i]->ops->destroy) {
            fonts[i]->ops->destroy(fonts[i]);
        }
    }

    /* Require ≥98% per-char match — when ypdf forwards Tc/Tw and the
     * canvas applies them, this hits 100% on the sample PDF; the 2%
     * margin absorbs occasional rounding outliers from substituted
     * fonts. Anything below means a real position regression in ypdf
     * or in the per-glyph layout, the kind that produces visible
     * "first word OK, rest of line off". */
    if (total_chars > 0 && matched_chars * 100 < total_chars * 98) {
        fprintf(stderr, "FAIL: per-char layout match %.1f%% < 98%% threshold\n",
                100.0 * matched_chars / total_chars);
        return 1;
    }

    printf("OK: %u spans, %zu/%zu lines (left+right both match), "
           "%zu/%zu chars match mutool, "
           "title x=%.2f sz=%.1f, body x=%.2f sz=%.1f\n",
           sum.total_spans, n_lines, n_lines, matched_chars, total_chars, title_min_x, title_size,
           body_min_x, body_size);

    yetty_ydraw_flyweight_registry_destroy(reg);
    yetty_ydraw_draw_list_destroy(out->buffer);
    pdfioFileClose(pdf);
    return 0;
}
