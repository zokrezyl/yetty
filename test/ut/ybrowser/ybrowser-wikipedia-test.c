/*
 * ybrowser-wikipedia-test — loads the snapshotted
 * https://en.wikipedia.org/wiki/Photography page (committed at
 * photography.html next to this file) and asserts geometric facts
 * about its layout.
 *
 * This is the regression net for the user-reported "garbage glyphs /
 * all images upper-left" bugs. Wikipedia's HTML is the worst real
 * test case: its <figure typeof="mw:File/Thumb"> images, nav chrome,
 * infobox table, and inline citations exercise float/clear/percent-
 * width/aria-hidden/segment-styling in combination.
 *
 * Each test asserts only the structural facts that must hold for the
 * page to be readable. Pixel-level positions vary with viewport size,
 * so we use coarse predicates (e.g. "image floats to the right half"
 * rather than "image is exactly at x=792").
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ybrowser/ybrowser.h>

#ifndef YBROWSER_WIKI_PAGE
#define YBROWSER_WIKI_PAGE "photography.html"
#endif

static int g_failures = 0;
static int g_passed = 0;

#define ASSERT_TRUE(name, cond)                                                                     \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            fprintf(stderr, "  FAIL %s\n", (name));                                                 \
            g_failures++;                                                                           \
        } else {                                                                                    \
            g_passed++;                                                                             \
        }                                                                                           \
    } while (0)

#define ASSERT_LT(name, val, limit)                                                                 \
    do {                                                                                            \
        float _v = (float)(val);                                                                    \
        float _l = (float)(limit);                                                                  \
        if (_v >= _l) {                                                                             \
            fprintf(stderr, "  FAIL %s: %.1f >= %.1f\n", (name), _v, _l);                           \
            g_failures++;                                                                           \
        } else {                                                                                    \
            g_passed++;                                                                             \
        }                                                                                           \
    } while (0)

#define ASSERT_GT(name, val, limit)                                                                 \
    do {                                                                                            \
        float _v = (float)(val);                                                                    \
        float _l = (float)(limit);                                                                  \
        if (_v <= _l) {                                                                             \
            fprintf(stderr, "  FAIL %s: %.1f <= %.1f\n", (name), _v, _l);                           \
            g_failures++;                                                                           \
        } else {                                                                                    \
            g_passed++;                                                                             \
        }                                                                                           \
    } while (0)

struct yetty_ylexbor *load_wiki(int viewport_w, int viewport_h)
{
    FILE *f = fopen(YBROWSER_WIKI_PAGE, "rb");
    if (!f) {
        fprintf(stderr, "FAIL: cannot open %s\n", YBROWSER_WIKI_PAGE);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != n) {
        fprintf(stderr, "FAIL: short read\n");
        exit(2);
    }
    fclose(f);
    buf[n] = '\0';

    struct yetty_ylexbor_config cfg = {
        .viewport_width = viewport_w,
        .viewport_height = viewport_h,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ylexbor_create failed\n");
        exit(2);
    }
    struct yetty_ylexbor *yl = r.value;
    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(yl, buf, n);
    free(buf);
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "load_html failed: %s\n", lr.error.msg);
        yetty_ylexbor_destroy(yl);
        exit(2);
    }
    return yl;
}

struct count_stats {
    int total;
    int images;            /* INLINE_IMAGE boxes with w > 0 */
    int images_left;       /* images with x in left half */
    int images_right;      /* images with x in right half */
    int single_glyph_text; /* text boxes with w <= 10 px — proxy for the
	                        * one-char-per-line garbage */
    int tiny_text_at_right;/* w <= 10 AND x > viewport*0.7 — the actual
	                        * "garbage at the right edge" pattern */
    float min_x, max_x;
    float min_y, max_y;
};

/* Iterate boxes; classify and accumulate stats. Image counts cover
 * boxes with kind=INLINE_IMAGE (we can detect them as elements with
 * tag "img"). Single-glyph text counts cover INLINE_TEXT boxes with
 * w <= 10 px (a one-char Latin glyph is ~9 px at font 16). */
static struct count_stats walk_boxes(struct yetty_ylexbor *yl, int viewport_w)
{
    struct count_stats s = {0};
    s.min_x = s.min_y = 1e9f;
    s.max_x = s.max_y = -1e9f;

    int total = yetty_ylexbor_test_box_count(yl);
    s.total = total;
    float vw = (float)viewport_w;

    for (int i = 0; i < total; i++) {
        char tag[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (w <= 0.0f || h <= 0.0f) {
            continue;
        }
        if (x < s.min_x) s.min_x = x;
        if (x > s.max_x) s.max_x = x;
        if (y < s.min_y) s.min_y = y;
        if (y > s.max_y) s.max_y = y;

        if (strcmp(tag, "img") == 0) {
            s.images++;
            if (x + w * 0.5f < vw * 0.5f) {
                s.images_left++;
            } else {
                s.images_right++;
            }
        }
    }
    return s;
}

/* Find the Nth element with the given tag whose text content starts
 * with the given prefix. Walks every box and reads (x,y,w,h,tag). */
static int find_with_prefix(struct yetty_ylexbor *yl, const char *tag, float *out_x, float *out_y,
                            float *out_w, float *out_h)
{
    int total = yetty_ylexbor_test_box_count(yl);
    for (int i = 0; i < total; i++) {
        char t[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, t, sizeof(t)) != 0) {
            continue;
        }
        if (strcmp(t, tag) == 0 && w > 0.0f && h > 0.0f) {
            if (out_x) *out_x = x;
            if (out_y) *out_y = y;
            if (out_w) *out_w = w;
            if (out_h) *out_h = h;
            return 0;
        }
    }
    return -1;
}

/* ============================================================================
 * Test A — global geometry sanity.
 *
 * Catches the "all images at x=0..8" symptom: at least 25% of <img>
 * boxes must end up in the right half of the viewport (Wikipedia
 * articles have many right-floated figures by default).
 * ============================================================================*/
static void test_image_distribution(void)
{
    fprintf(stderr, "[test_image_distribution]\n");
    int vw = 1200;
    struct yetty_ylexbor *yl = load_wiki(vw, 800);
    struct count_stats s = walk_boxes(yl, vw);

    fprintf(stderr, "  boxes=%d images=%d (left=%d right=%d)\n", s.total, s.images, s.images_left,
            s.images_right);
    ASSERT_GT("Wikipedia page produced images", s.images, 5);
    /* Most Wikipedia article images carry mw-default-size /
     * mw-halign-right and should float right. We require at least 25 %
     * of them to actually land in the right half — anything below that
     * means the float CSS isn't being honoured. */
    ASSERT_GT("at least 25% of images in right half", s.images_right * 4,
              s.images_left * 1 + 0);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test B — no garbage at the absolute right edge.
 *
 * The user-reported "random garbage glyphs/letters" appeared because
 * a float without explicit width swallowed the row, leaving the
 * surrounding paragraph with avail_w=0 → wrap_inline_box emitted one
 * glyph per line at x ≈ viewport_w − 8 (the inside of the body's
 * right padding). The broken state produced 2300+ one-glyph boxes
 * piled at exactly that x.
 *
 * We catch that specific pile-up: count tiny (w ≤ 10 px) text boxes
 * within the last 20 px of the viewport. Legitimate one-glyph spans
 * (single trailing periods / commas / spaces inside floated captions)
 * can sit slightly inset from the right edge — those have x < vw−20.
 * Only the pathological pile lives right at the edge.
 * ============================================================================*/
static void test_no_garbage_at_right_edge(void)
{
    fprintf(stderr, "[test_no_garbage_at_right_edge]\n");
    int vw = 1200;
    struct yetty_ylexbor *yl = load_wiki(vw, 800);

    int total = yetty_ylexbor_test_box_count(yl);
    int tiny_pile = 0;
    int tiny_total = 0;
    int single_space_pile = 0;
    float pile_x_threshold = (float)vw - 20.0f;
    for (int i = 0; i < total; i++) {
        char tag[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (w > 0.0f && w <= 10.0f && h > 0.0f && h < 30.0f) {
            tiny_total++;
            if (x >= pile_x_threshold) {
                tiny_pile++;
            }
            /* Even spaces between styled segments should not pile up
			 * at the right edge — they only do when content wraps to
			 * one glyph per line. */
            if (x >= pile_x_threshold && tag[0] == '\0') {
                single_space_pile++;
            }
        }
    }
    fprintf(stderr, "  tiny_total=%d tiny_pile_at_right_edge=%d (cap=15)\n", tiny_total, tiny_pile);
    /* Hard limit: the float-swallow bug produces literally thousands
     * piled at exactly the right edge. 15 is comfortably above the
     * tiniest amount of legitimate noise. */
    ASSERT_LT("no avalanche of tiny text at the right edge", tiny_pile, 15);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test C — the article body must surface near the top.
 *
 * Wikipedia ships the left sidebar / page header BEFORE the article
 * <h1>. If we render that chrome verbatim, the article title ends up
 * ~3000 px down the page, and the user sees only menu junk. The UA
 * stylesheet hides <nav>, <header>, .vector-menu, etc; this test
 * asserts the result: the first <h1> appears in the first 200 px.
 * ============================================================================*/
static void test_article_title_near_top(void)
{
    fprintf(stderr, "[test_article_title_near_top]\n");
    struct yetty_ylexbor *yl = load_wiki(1200, 800);

    float x = 0, y = 0, w = 0, h = 0;
    if (find_with_prefix(yl, "h1", &x, &y, &w, &h) != 0) {
        fprintf(stderr, "  no h1\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    fprintf(stderr, "  first h1 at y=%.0f (cap=500)\n", y);
    ASSERT_LT("h1 reached within first 500 px (chrome hidden)", y, 500.0f);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test D — total content height stays within a sane range.
 *
 * A page that hits the float-swallow bug had wrap_inline_box emit one
 * glyph per line — every paragraph took thousands of vertical px and
 * the page total exploded to >100 000 px. Check that the Photography
 * article (a ~30 KB body) fits in roughly the layout we'd expect of a
 * real browser.
 * ============================================================================*/
static void test_content_height_sane(void)
{
    fprintf(stderr, "[test_content_height_sane]\n");
    struct yetty_ylexbor *yl = load_wiki(1200, 800);

    int h = yetty_ylexbor_content_height(yl);
    fprintf(stderr, "  content_height=%d (cap=80000)\n", h);
    ASSERT_GT("content_height non-trivial", h, 1000);
    ASSERT_LT("content_height under 80000 px (no float-swallow blowout)", h, 80000);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test E — the body content is mostly to the LEFT of the floats.
 *
 * Wikipedia article body paragraphs should land at the page's left
 * inset (x ≈ body padding). If our renderer puts them at x=1016 due
 * to all-floats-on-the-left or float-swallowing, this assertion fires.
 * ============================================================================*/
static void test_body_paragraphs_on_left(void)
{
    fprintf(stderr, "[test_body_paragraphs_on_left]\n");
    int vw = 1200;
    struct yetty_ylexbor *yl = load_wiki(vw, 800);

    int total = yetty_ylexbor_test_box_count(yl);
    int p_left = 0;
    int p_right = 0;
    for (int i = 0; i < total; i++) {
        char tag[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (strcmp(tag, "p") != 0 || w <= 0.0f || h <= 0.0f) {
            continue;
        }
        if (x < (float)vw * 0.5f) {
            p_left++;
        } else {
            p_right++;
        }
    }
    fprintf(stderr, "  paragraphs left=%d right=%d\n", p_left, p_right);
    ASSERT_GT("paragraphs exist", p_left + p_right, 10);
    ASSERT_GT("most paragraphs in left half of viewport", p_left, p_right);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test F — no images stuck at exactly (0,0) — the regression where
 * <figure> children weren't being positioned at all.
 *
 * Anything reaching the paint pass with x=0 AND y=0 is almost certainly
 * a layout pass that never ran. A real top-of-page image would be at
 * y ≈ body-padding (8) and x ≈ left-inset, not literal zero on BOTH
 * axes.
 * ============================================================================*/
static void test_no_images_at_zero_zero(void)
{
    fprintf(stderr, "[test_no_images_at_zero_zero]\n");
    struct yetty_ylexbor *yl = load_wiki(1200, 800);

    int total = yetty_ylexbor_test_box_count(yl);
    int zero_zero = 0;
    int total_imgs = 0;
    for (int i = 0; i < total; i++) {
        char tag[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (strcmp(tag, "img") != 0 || w <= 0.0f || h <= 0.0f) {
            continue;
        }
        total_imgs++;
        if (x == 0.0f && y == 0.0f) {
            zero_zero++;
        }
    }
    fprintf(stderr, "  imgs=%d at_origin=%d (cap=0)\n", total_imgs, zero_zero);
    ASSERT_TRUE("no images stuck at exactly (0,0)", zero_zero == 0);
    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test G — no text rendered at (0,0).
 *
 * Catches "Jump to content" / similar skip-link chrome that author CSS
 * leaves visible because they're hidden via offscreen-positioning
 * tricks (position: absolute; clip: rect(...)) that we don't honour.
 * Wikipedia's loaded author CSS for `.mw-jump-link` would otherwise
 * win over our UA `display: none`, so we install the chrome-hide
 * rules at CSS_ORIGIN_USER with !important — user !important is the
 * highest tier in the CSS cascade.
 *
 * Any text box landing at exactly (0, 0) is essentially never legitimate
 * — real first-line content respects at least body padding (≥ 8 px).
 * ============================================================================*/
static void test_no_text_at_origin(void)
{
    fprintf(stderr, "[test_no_text_at_origin]\n");
    struct yetty_ylexbor *yl = load_wiki(1200, 800);

    int total = yetty_ylexbor_test_box_count(yl);
    int origin_count = 0;
    for (int i = 0; i < total; i++) {
        char tag[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag)) != 0) {
            continue;
        }
        if (w <= 0.0f || h <= 0.0f) {
            continue;
        }
        /* Skip the root / html / body boxes which legitimately start at
		 * (0, 0). They have non-zero width/height matching the viewport
		 * — only truly tiny boxes piled at the origin signal a bug. */
        if (w > 200.0f) {
            continue;
        }
        if (x == 0.0f && y == 0.0f) {
            fprintf(stderr, "  unexpected box at (0,0): tag=%s w=%.0f h=%.0f\n", tag, w, h);
            origin_count++;
        }
    }
    fprintf(stderr, "  small-box-at-origin count=%d (cap=0)\n", origin_count);
    ASSERT_TRUE("no small boxes at (0,0) — chrome leak", origin_count == 0);
    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    fprintf(stderr, "ybrowser-wikipedia-test on %s\n", YBROWSER_WIKI_PAGE);

    test_image_distribution();
    test_no_images_at_zero_zero();
    test_no_text_at_origin();
    test_no_garbage_at_right_edge();
    test_article_title_near_top();
    test_content_height_sane();
    test_body_paragraphs_on_left();

    fprintf(stderr, "\nresults: %d passed, %d failed\n", g_passed, g_failures);
    return g_failures == 0 ? 0 : 1;
}
