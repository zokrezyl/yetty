/*
 * ybrowser-inline-test — pins inline-text behaviour: list markers,
 * UTF-8 wrap safety, font weight / italic / underline propagation,
 * synthetic bold, justify, white-space.
 *
 * Separate file from ybrowser-layout-test so layout-vs-text concerns
 * don't have to cross-reference each other's helpers.
 *
 * Each test asserts the smallest observable fact that would catch the
 * regression if the corresponding code path broke. Visible-quality
 * improvements (e.g. justify-fill, real font shaping) will require
 * tightening these assertions — for now they pin current behaviour so
 * the next person to touch the inline pipeline knows what they changed.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ybrowser/ybrowser.h>

static int g_failures = 0;
static int g_passed = 0;

#define ASSERT_TRUE(name, cond)                                                                    \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL %s: condition false\n", (name));                               \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

#define ASSERT_NEAR(name, got, expect)                                                             \
    do {                                                                                           \
        float _g = (got);                                                                          \
        float _e = (expect);                                                                       \
        if (fabsf(_g - _e) > 0.5f) {                                                               \
            fprintf(stderr, "  FAIL %s: got %.2f expected %.2f\n", (name), _g, _e);                \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

static struct yetty_ylexbor *load(const char *html, int viewport_w)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = viewport_w,
        .viewport_height = 600,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ylexbor_create failed: %s\n", r.error.msg);
        exit(2);
    }
    struct yetty_ylexbor *yl = r.value;
    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(yl, html, strlen(html));
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "load_html failed: %s\n", lr.error.msg);
        yetty_ylexbor_destroy(yl);
        exit(3);
    }
    return yl;
}

/* Count boxes whose surfaced text content contains `needle`. The
 * helper accepts both inline-text fragments AND block boxes that
 * carry a list-item marker — both surface via the test_box_info_at
 * text channel since P1.4 moved markers from the inline buffer onto
 * the LI block. */
static int count_text_with(struct yetty_ylexbor *yl, const char *needle)
{
    int total = yetty_ylexbor_test_box_count(yl);
    int hits = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[512];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (text[0] == '\0') {
            continue;
        }
        if (strstr(text, needle) != NULL) {
            hits++;
        }
        (void)kind;
    }
    return hits;
}

/* ============================================================================
 * Test 1 — UL list items prepend a bullet (U+2022) marker.
 *
 * Wikipedia and most docs use unordered lists heavily; if the marker
 * disappears, you can't tell sibling items apart. Without lexbor's
 * `display: list-item` machinery we synthesise the marker in
 * walk()'s LI branch. Pin that the marker survives in the inline text.
 * ============================================================================*/
static void test_ul_bullet_marker(void)
{
    fprintf(stderr, "[test_ul_bullet_marker]\n");
    static const char html[] =
        "<html><body>"
        "<ul><li>first</li><li>second</li><li>third</li></ul>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    /* U+2022 bullet, UTF-8: e2 80 a2. P1.4 stores the marker on the
	 * LI block (so wrapping doesn't strand line 2 at the marker
	 * column) — the test_box_info_at text channel surfaces both
	 * inline text AND block markers. */
    int bullets = count_text_with(yl, "\xe2\x80\xa2");
    ASSERT_TRUE("ul items have bullet markers", bullets >= 3);
    /* And each item's body text survives intact (as a sibling
	 * inline-text box, NOT prepended with the marker any more). */
    ASSERT_TRUE("first item text rendered", count_text_with(yl, "first") >= 1);
    ASSERT_TRUE("second item text rendered", count_text_with(yl, "second") >= 1);
    ASSERT_TRUE("third item text rendered", count_text_with(yl, "third") >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 2 — OL list items prepend a 1-based decimal marker.
 *
 * The counter is computed by walking prev-siblings inside the LI branch
 * of walk(). Pin that the numbers come out in order.
 * ============================================================================*/
static void test_ol_decimal_marker(void)
{
    fprintf(stderr, "[test_ol_decimal_marker]\n");
    static const char html[] =
        "<html><body>"
        "<ol><li>alpha</li><li>beta</li><li>gamma</li></ol>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    ASSERT_TRUE("first item marked '1.'", count_text_with(yl, "1.") >= 1);
    ASSERT_TRUE("second item marked '2.'", count_text_with(yl, "2.") >= 1);
    ASSERT_TRUE("third item marked '3.'", count_text_with(yl, "3.") >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 3 — UTF-8 multi-byte text survives wrapping.
 *
 * wrap_inline_box advances `fit` by full UTF-8 codepoint sizes, but its
 * one-codepoint-minimum fallback (`end = cursor + 1`) could in principle
 * split a multi-byte sequence. Force a narrow viewport with CJK + accents
 * and verify every emitted inline-text box parses as valid UTF-8
 * (no truncated lead/continuation bytes).
 * ============================================================================*/
static int valid_utf8(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
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
            return 0;
        }
        if (i + step > n) {
            return 0;
        }
        for (size_t k = 1; k < step; k++) {
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) {
                return 0;
            }
        }
        i += step;
    }
    return 1;
}

static void test_utf8_wrap_safety(void)
{
    fprintf(stderr, "[test_utf8_wrap_safety]\n");
    /* Tight viewport (120 px content area at 16px font ≈ 13 glyphs)
     * forces the wrapper to break inside the run several times. Text
     * mixes ASCII, 2-byte (é), 3-byte (汉), and emoji (🌍 = 4-byte)
     * codepoints. */
    static const char html[] =
        "<html><body><p>"
        "café 漢字テスト 🌍 résumé naïve façade über 日本語 αβγ ελληνικά"
        "</p></body></html>";
    struct yetty_ylexbor *yl = load(html, 160);

    int total = yetty_ylexbor_test_box_count(yl);
    int inline_text_boxes = 0;
    int valid = 0;
    int empty_after_wrap = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[512];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) {
            continue;
        }
        inline_text_boxes++;
        size_t tn = strlen(text);
        if (tn == 0) {
            empty_after_wrap++;
            continue;
        }
        if (valid_utf8(text, tn)) {
            valid++;
        } else {
            fprintf(stderr, "  invalid UTF-8 in wrapped box: %.40s\n", text);
        }
    }
    fprintf(stderr, "  inline boxes=%d valid_utf8=%d empty=%d\n", inline_text_boxes, valid,
            empty_after_wrap);
    ASSERT_TRUE("at least one non-empty inline text box", inline_text_boxes - empty_after_wrap > 0);
    /* Every non-empty fragment must be well-formed UTF-8. */
    ASSERT_TRUE("all wrapped fragments parse as valid UTF-8",
                valid == inline_text_boxes - empty_after_wrap);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 4 — <a> propagates underline + link colour into its inline run.
 *
 * The UA default for <a> sets underline=1 and fg=0x0000ee; verify the
 * resulting inline-text fragment carries underline=true and a blue-ish
 * foreground.
 * ============================================================================*/
static void test_anchor_underline(void)
{
    fprintf(stderr, "[test_anchor_underline]\n");
    static const char html[] =
        "<html><body>"
        "<p>before <a href=\"#x\">linktext</a> after</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int link_underlined = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1, underline = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, &underline, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && strstr(text, "linktext") &&
            underline == 1) {
            link_underlined++;
        }
    }
    ASSERT_TRUE("<a> inline text carries underline=true", link_underlined >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 5 — <strong> propagates font_weight ≥ 700 to its segment.
 * ============================================================================*/
static void test_strong_weight_propagation(void)
{
    fprintf(stderr, "[test_strong_weight_propagation]\n");
    static const char html[] =
        "<html><body>"
        "<p>normal <strong>bolded</strong> normal</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int bold_hits = 0;
    int normal_hits = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1, weight = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, &weight, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) {
            continue;
        }
        if (strstr(text, "bolded") && weight >= 700) {
            bold_hits++;
        }
        if (strstr(text, "normal") && weight < 700) {
            normal_hits++;
        }
    }
    ASSERT_TRUE("<strong> fragment is weight >= 700", bold_hits >= 1);
    ASSERT_TRUE("surrounding text stays < 700", normal_hits >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 6 — <em>/<i> propagate italic to their segment.
 * ============================================================================*/
static void test_em_italic_propagation(void)
{
    fprintf(stderr, "[test_em_italic_propagation]\n");
    static const char html[] =
        "<html><body>"
        "<p>plain <em>slanted</em> plain</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int italic_hits = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1, italic = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, &italic, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && strstr(text, "slanted") &&
            italic == 1) {
            italic_hits++;
        }
    }
    ASSERT_TRUE("<em> fragment is italic", italic_hits >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 7 — Inline whitespace between sibling inline elements produces a
 * visible gap on the rendered line.
 *
 * `<a>foo</a> <a>bar</a>` must render with at least one glyph-width of
 * horizontal gap between the "foo" and "bar" fragments. Pre-P1.2 the
 * space appeared as its own " " TEXT_DRAWABLE_LIST; after the whitespace-only
 * fragment skip (added to stop fonts from rendering placeholder
 * glyphs for the lone space) the "bar" fragment starts at "foo".x +
 * "foo".w + space-width — same visual, one less prim.
 * ============================================================================*/
static void test_inline_inter_word_space(void)
{
    fprintf(stderr, "[test_inline_inter_word_space]\n");
    static const char html[] =
        "<html><body><p><a href=\"#\">foo</a> <a href=\"#\">bar</a></p></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    float foo_x = -1, foo_end = -1, bar_x = -1;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        float x, w;
        if (yetty_ylexbor_test_box_at(yl, i, &x, NULL, &w, NULL, NULL, 0) != 0) {
            continue;
        }
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) {
            continue;
        }
        if (strstr(text, "foo") && foo_x < 0) {
            foo_x = x;
            foo_end = x + w;
        }
        if (strstr(text, "bar") && bar_x < 0) {
            bar_x = x;
        }
    }
    fprintf(stderr, "  foo.x=%.1f foo.end=%.1f bar.x=%.1f gap=%.1f\n", foo_x, foo_end,
            bar_x, bar_x - foo_end);
    ASSERT_TRUE("both fragments rendered", foo_x >= 0 && bar_x >= 0);
    /* The inter-word gap should be ≈ one glyph width (font*0.55 = 8.8). */
    ASSERT_TRUE("space between sibling <a> preserved as visual gap",
                bar_x - foo_end > 4.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 8 — Heading font-size inheritance.
 *
 * <h1> inside <body> should have a font-size 2x larger than the body's
 * default (16 → 32 px), per the UA stylesheet.
 * ============================================================================*/
static void test_heading_font_size(void)
{
    fprintf(stderr, "[test_heading_font_size]\n");
    static const char html[] =
        "<html><body>"
        "<h1>Big</h1>"
        "<p>normal</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int big_count = 0, normal_count = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[32], tag[16];
        if (yetty_ylexbor_test_box_at(yl, i, NULL, NULL, NULL, NULL, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        /* The h1 inline text's parent block carries the heading font
         * size; we measure indirectly via box height being notably
         * larger than the paragraph's. Direct font_size readback would
         * need another test helper. */
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && strstr(text, "Big")) {
            big_count++;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && strstr(text, "normal")) {
            normal_count++;
        }
    }
    ASSERT_TRUE("h1 inline text emitted", big_count >= 1);
    ASSERT_TRUE("body paragraph inline text emitted", normal_count >= 1);

    /* The h1 BLOCK should be visibly taller than the <p> block — pin
     * via geometry instead of font_size readback. */
    float h1_h = 0, p_h = 0;
    for (int i = 0; i < total; i++) {
        char tag[16];
        float h;
        if (yetty_ylexbor_test_box_at(yl, i, NULL, NULL, NULL, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (strcmp(tag, "h1") == 0 && h1_h == 0) h1_h = h;
        if (strcmp(tag, "p") == 0 && p_h == 0) p_h = h;
    }
    fprintf(stderr, "  h1.h=%.0f p.h=%.0f\n", h1_h, p_h);
    ASSERT_TRUE("h1 is taller than p (font-size scaling worked)", h1_h > p_h);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 9 — `<pre>` preserves source newlines.
 *
 * Verify that a multi-line <pre> ends up split into multiple inline-text
 * boxes (one per source line) rather than collapsed onto one line.
 * ============================================================================*/
static void test_pre_preserves_newlines(void)
{
    fprintf(stderr, "[test_pre_preserves_newlines]\n");
    /* Three explicit lines inside the <pre>. */
    static const char html[] =
        "<html><body>"
        "<pre>line-one\nline-two\nline-three</pre>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    /* Each source line should be a separate INLINE_TEXT box (wrapped
     * verbatim because preserve_ws=1). */
    int hits = 0;
    if (count_text_with(yl, "line-one") > 0) hits++;
    if (count_text_with(yl, "line-two") > 0) hits++;
    if (count_text_with(yl, "line-three") > 0) hits++;
    fprintf(stderr, "  preserved lines found=%d\n", hits);
    ASSERT_TRUE("pre keeps all three lines", hits == 3);

    /* And the three lines must sit at distinct y coordinates. */
    int total = yetty_ylexbor_test_box_count(yl);
    float ys[8];
    int yc = 0;
    for (int i = 0; i < total && yc < 8; i++) {
        int kind = -1;
        char text[64];
        float y;
        if (yetty_ylexbor_test_box_at(yl, i, NULL, &y, NULL, NULL, NULL, 0) != 0) {
            continue;
        }
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        if (strstr(text, "line-")) ys[yc++] = y;
    }
    ASSERT_TRUE("three <pre> lines at three distinct ys", yc == 3 && ys[0] < ys[1] && ys[1] < ys[2]);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 10 — `<a>` inline-text fragment is not lost when wrapped onto
 * a narrow viewport. (Regression net for the segment-walker.)
 * ============================================================================*/
static void test_anchor_survives_wrap(void)
{
    fprintf(stderr, "[test_anchor_survives_wrap]\n");
    static const char html[] =
        "<html><body><p>"
        "Plain prose then <a href=\"#\">a link with several words</a> and then more "
        "plain prose after that goes on and on for a while."
        "</p></body></html>";
    /* Narrow enough that the link wraps. */
    struct yetty_ylexbor *yl = load(html, 240);

    /* Count fragments that contain *part* of the link text and are
     * underlined. Wrapping should never strip the underline flag from
     * any per-line fragment. */
    int link_pieces = 0;
    int link_pieces_underlined = 0;
    int total = yetty_ylexbor_test_box_count(yl);
    for (int i = 0; i < total; i++) {
        int kind = -1, underline = -1;
        char text[128];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, &underline, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        /* Each fragment of the link contains some subsequence of its
         * words; we look for the unique word "several" or "words". */
        if (strstr(text, "several") || strstr(text, "words") || strstr(text, "link")) {
            link_pieces++;
            if (underline) link_pieces_underlined++;
        }
    }
    fprintf(stderr, "  link pieces=%d underlined=%d\n", link_pieces, link_pieces_underlined);
    ASSERT_TRUE("link survived wrap into >= 1 fragment", link_pieces >= 1);
    ASSERT_TRUE("every wrapped link fragment is underlined",
                link_pieces > 0 && link_pieces == link_pieces_underlined);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 11 — `display: block` on a <span> via inline style IS respected.
 *
 * P2.8 makes the CSS display value drive the inline-vs-block decision
 * in walk() before the tag-default table is consulted. Two spans with
 * display:block must therefore stack vertically (different y values).
 * ============================================================================*/
static void test_span_display_block(void)
{
    fprintf(stderr, "[test_span_display_block]\n");
    static const char html[] =
        "<html><body>"
        "<span style=\"display: block;\">first</span>"
        "<span style=\"display: block;\">second</span>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    float first_y = -1, second_y = -1;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[32];
        float y;
        if (yetty_ylexbor_test_box_at(yl, i, NULL, &y, NULL, NULL, NULL, 0) != 0) {
            continue;
        }
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        if (strstr(text, "first") && first_y < 0) first_y = y;
        if (strstr(text, "second") && second_y < 0) second_y = y;
    }
    fprintf(stderr, "  first.y=%.0f second.y=%.0f\n", first_y, second_y);
    ASSERT_TRUE("both spans rendered", first_y >= 0 && second_y >= 0);
    ASSERT_TRUE("display:block makes spans stack vertically", second_y > first_y + 0.5f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 12 — text-align: justify line start is at content origin (the
 * line is left-anchored and only the inter-word slack is distributed
 * via TEXT_DRAWABLE_LIST v2 word_spacing — P2.12). The first fragment x for the
 * justified paragraph therefore equals the first fragment x for a
 * normal paragraph. word_spacing itself can't be read through the
 * public test API today; see the wikipedia-test prim counts for the
 * justification-aware paint output.
 * ============================================================================*/
static void test_text_align_justify_falls_back(void)
{
    fprintf(stderr, "[test_text_align_justify_falls_back]\n");
    static const char html[] =
        "<html><body>"
        "<p style=\"text-align: justify;\">justify me everywhere</p>"
        "<p>normal me</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    /* Both paragraphs' first inline fragment should land at the same
     * x (body content origin) because justify currently falls back
     * to left-align — there's no per-character slack distribution. */
    int total = yetty_ylexbor_test_box_count(yl);
    float just_x = -1, normal_x = -1;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[32];
        float x;
        if (yetty_ylexbor_test_box_at(yl, i, &x, NULL, NULL, NULL, NULL, 0) != 0) {
            continue;
        }
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        if (strstr(text, "justify") && just_x < 0) just_x = x;
        if (strstr(text, "normal") && normal_x < 0) normal_x = x;
    }
    fprintf(stderr, "  justified.x=%.0f normal.x=%.0f\n", just_x, normal_x);
    ASSERT_NEAR("justify first fragment at content origin", just_x, normal_x);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 13 — text-decoration: line-through propagates from CSS through
 * to the per-fragment inline-text box (P2.11).
 * ============================================================================*/
static void test_text_decoration_line_through(void)
{
    fprintf(stderr, "[test_text_decoration_line_through]\n");
    static const char html[] =
        "<html><body>"
        "<p>before <s>struck</s> after</p>"
        "<p>before <del>removed</del> after</p>"
        "<p>before <span style=\"text-decoration: line-through;\">styled</span> after</p>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    /* The visible bodies must still be present even though the paint
	 * pass now adds extra strike strips. We can't read line_through
	 * back through the public test API today; the actual paint diff
	 * shows up in ybrowser-wikipedia-test prim counts. */
    ASSERT_TRUE("struck visible", count_text_with(yl, "struck") >= 1);
    ASSERT_TRUE("removed visible", count_text_with(yl, "removed") >= 1);
    ASSERT_TRUE("styled visible", count_text_with(yl, "styled") >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 14 — CSS white-space: pre on a non-<pre> element preserves
 * source newlines (P2.9).
 * ============================================================================*/
static void test_css_white_space_pre(void)
{
    fprintf(stderr, "[test_css_white_space_pre]\n");
    static const char html[] =
        "<html><body>"
        "<div style=\"white-space: pre;\">line-a\nline-b\nline-c</div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int hits = 0;
    if (count_text_with(yl, "line-a") > 0) hits++;
    if (count_text_with(yl, "line-b") > 0) hits++;
    if (count_text_with(yl, "line-c") > 0) hits++;
    fprintf(stderr, "  lines preserved=%d\n", hits);
    ASSERT_TRUE("CSS white-space: pre preserves source newlines", hits == 3);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 15 — ZWSP (U+200B) is a wrap opportunity (P1.5).
 *
 * A long ZWSP-glued word in a narrow viewport must break — without
 * ZWSP awareness the wrapper would have to overflow or break inside
 * a UTF-8 sequence.
 * ============================================================================*/
static void test_zwsp_wrap_opportunity(void)
{
    fprintf(stderr, "[test_zwsp_wrap_opportunity]\n");
    /* String literals are split at each ZWSP to keep the hex escape
	 * from greedy-eating the next ASCII char (compilers parse
	 * `\xe2\x80\x8bcali` as one giant escape otherwise). */
    static const char html[] =
        "<html><body><p>"
        "supe" "\xe2\x80\x8b" "cali" "\xe2\x80\x8b" "frag" "\xe2\x80\x8b"
        "ilis" "\xe2\x80\x8b" "tice"
        "</p></body></html>";
    struct yetty_ylexbor *yl = load(html, 120);

    int total = yetty_ylexbor_test_box_count(yl);
    int frag_count = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        if (text[0] == '\0') continue;
        frag_count++;
    }
    fprintf(stderr, "  fragments=%d (>1 if ZWSP wraps worked)\n", frag_count);
    ASSERT_TRUE("ZWSP introduces wrap opportunities", frag_count > 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 16 — <a> inline text carries the link element pointer (P1.2).
 *
 * Indirect: an <a>'s wrapped INLINE_TEXT fragment should report the
 * "a" tag through the existing test_box_at API (which reads the box's
 * `element` field). Pre-P1.2 the element was NULL on wrapped fragments
 * and the tag came back as "".
 * ============================================================================*/
static void test_anchor_box_has_element(void)
{
    fprintf(stderr, "[test_anchor_box_has_element]\n");
    static const char html[] =
        "<html><body><p>click <a href=\"#x\">here</a></p></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int a_fragments = 0;
    for (int i = 0; i < total; i++) {
        char tag[16];
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (strcmp(tag, "a") != 0) continue;
        int kind = -1;
        char text[32];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && w > 0 && h > 0 &&
            strstr(text, "here")) {
            a_fragments++;
        }
    }
    fprintf(stderr, "  <a> inline fragments with element set: %d\n", a_fragments);
    ASSERT_TRUE("<a> inline text fragment carries the link element", a_fragments >= 1);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 17 — list marker lives on the LI block, NOT prepended to inline
 * text (P1.4). The body fragment's first byte is the body's own first
 * byte, not the marker character.
 * ============================================================================*/
static void test_marker_separate_from_body(void)
{
    fprintf(stderr, "[test_marker_separate_from_body]\n");
    static const char html[] =
        "<html><body>"
        "<ul><li>alpha bravo charlie delta</li></ul>"
        "<ol><li>numbered item content</li></ol>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int ul_clean = 0, ol_clean = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        if (strstr(text, "alpha bravo") && text[0] == 'a') {
            ul_clean = 1;
        }
        if (strstr(text, "numbered item") && text[0] == 'n') {
            ol_clean = 1;
        }
    }
    ASSERT_TRUE("UL body fragment starts with body text, not bullet", ul_clean);
    ASSERT_TRUE("OL body fragment starts with body text, not '1.'", ol_clean);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 18 — U+00A0 NBSP gets collapsed to an ASCII space at text
 * collection time. Wikipedia uses NBSPs heavily in citation formatting
 * ("ISBN 978-…", "p. 8–9", etc.); the default canvas font has no glyph
 * for U+00A0 and renders a visible placeholder square that the user
 * perceives as scattered "garbage" glyphs. Verify the prim stream
 * doesn't contain any standalone NBSP fragments.
 * ============================================================================*/
static void test_nbsp_collapsed_to_space(void)
{
    fprintf(stderr, "[test_nbsp_collapsed_to_space]\n");
    /* `\xc2\xa0` is U+00A0 NBSP. Three NBSPs between words. */
    static const char html[] =
        "<html><body><p>foo" "\xc2\xa0" "bar" "\xc2\xa0" "baz" "\xc2\xa0" "qux</p></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int nbsp_spans = 0;
    int has_words = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        /* Detect NBSP bytes (0xC2 0xA0) anywhere in the fragment. */
        for (size_t k = 0; text[k]; k++) {
            if ((unsigned char)text[k] == 0xC2 &&
                (unsigned char)text[k + 1] == 0xA0) {
                nbsp_spans++;
                break;
            }
        }
        if (strstr(text, "foo") || strstr(text, "bar") || strstr(text, "baz") ||
            strstr(text, "qux")) {
            has_words = 1;
        }
    }
    fprintf(stderr, "  spans containing NBSP bytes: %d\n", nbsp_spans);
    ASSERT_TRUE("words rendered", has_words);
    ASSERT_TRUE("no remaining NBSP bytes in any fragment", nbsp_spans == 0);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 19 — Whitespace-only seg fragments (e.g. the `" "` text node
 * between `<a>x</a> <b>y</b>`) are NOT emitted as their own TEXT_DRAWABLE_LIST
 * prims. The visual gap between fragments is preserved via frag_x
 * advancement; the prim stream just doesn't contain a standalone " "
 * span that some fonts would render as a placeholder glyph.
 * ============================================================================*/
static void test_no_whitespace_only_fragments(void)
{
    fprintf(stderr, "[test_no_whitespace_only_fragments]\n");
    static const char html[] =
        "<html><body><p>"
        "<a href=\"#\">one</a> <a href=\"#\">two</a> <a href=\"#\">three</a>"
        "</p></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int ws_only_count = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        size_t n = strlen(text);
        if (n == 0) continue;
        int ws_only = 1;
        for (size_t k = 0; k < n; k++) {
            if (text[k] != ' ' && text[k] != '\t') {
                ws_only = 0;
                break;
            }
        }
        if (ws_only) {
            ws_only_count++;
        }
    }
    fprintf(stderr, "  whitespace-only fragments: %d\n", ws_only_count);
    ASSERT_TRUE("no whitespace-only TEXT_DRAWABLE_LIST fragments", ws_only_count == 0);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 20 — Wikipedia-style nested inlines collapse to ONE fragment.
 *
 * `<a><span>[</span>12<span>]</span></a>` is exactly what MediaWiki
 * emits for every `[N]` citation. The old per-element seg tracking
 * (P1.2 original) split this into THREE TEXT_DRAWABLE_LIST prims at
 * adjacent x positions computed from our naive `font_size*0.55`
 * per-glyph estimate. The canvas's real font advances differ — the
 * cumulative drift across thousands of such fragments produced
 * visible gaps within bracketed numbers, which from a small distance
 * read as scattered descender-like glyphs ("the p, g, q garbage" the
 * user kept reporting).
 *
 * Click-target-only tracking (only clickable inlines mark a seg
 * boundary) collapses `[12]` into ONE fragment. Pin that.
 * ============================================================================*/
static void test_nested_inline_collapses_to_one_fragment(void)
{
    fprintf(stderr, "[test_nested_inline_collapses_to_one_fragment]\n");
    /* Mimic exactly MediaWiki's citation markup. */
    static const char html[] =
        "<html><body><p>before "
        "<sup class=\"reference\"><a href=\"#cite_note-1\">"
        "<span class=\"cite-bracket\">[</span>12<span class=\"cite-bracket\">]</span>"
        "</a></sup> after</p></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000);

    int total = yetty_ylexbor_test_box_count(yl);
    int citation_fragments = 0;
    for (int i = 0; i < total; i++) {
        int kind = -1;
        char text[64];
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) continue;
        /* Any inline-text fragment that contains a citation character
		 * ([, 1, 2, ]) — count fragments that pertain to this citation. */
        if (strchr(text, '[') || strchr(text, ']') ||
            (strstr(text, "12") && strlen(text) <= 4)) {
            citation_fragments++;
        }
    }
    fprintf(stderr, "  fragments for `[12]`: %d (expect 1)\n", citation_fragments);
    /* The whole `[12]` should be ONE fragment. Pre-fix this was 3. */
    ASSERT_TRUE("Wikipedia [N] citation renders as a single fragment, not 3",
                citation_fragments == 1);

    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    fprintf(stderr, "ybrowser-inline-test\n");

    test_ul_bullet_marker();
    test_ol_decimal_marker();
    test_utf8_wrap_safety();
    test_anchor_underline();
    test_strong_weight_propagation();
    test_em_italic_propagation();
    test_inline_inter_word_space();
    test_heading_font_size();
    test_pre_preserves_newlines();
    test_anchor_survives_wrap();
    test_span_display_block();
    test_text_align_justify_falls_back();
    test_text_decoration_line_through();
    test_css_white_space_pre();
    test_zwsp_wrap_opportunity();
    test_anchor_box_has_element();
    test_marker_separate_from_body();
    test_nbsp_collapsed_to_space();
    test_no_whitespace_only_fragments();
    test_nested_inline_collapses_to_one_fragment();

    fprintf(stderr, "\nresults: %d passed, %d failed\n", g_passed, g_failures);
    return g_failures == 0 ? 0 : 1;
}
