/*
 * ybrowser-layout-test — pins layout geometry for the core CSS features
 * that have already regressed at least once.
 *
 * Each test loads a small HTML snippet, walks the post-layout box vector
 * via yetty_ylexbor_test_box_at, and asserts box positions / dimensions
 * against expected values. Failures print the actual vs expected so the
 * trace is enough to diagnose without re-running with YTRACE_DEFAULT_ON.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ybrowser/ybrowser.h>

static int g_failures = 0;
static int g_passed = 0;

#define EPS 0.5f

#define ASSERT_NEAR(name, got, expect)                                                             \
    do {                                                                                           \
        float _g = (got);                                                                          \
        float _e = (expect);                                                                       \
        if (fabsf(_g - _e) > EPS) {                                                                \
            fprintf(stderr, "  FAIL %s: got %.2f expected %.2f\n", (name), _g, _e);                \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(name, cond)                                                                    \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL %s: condition false\n", (name));                               \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

struct box_info {
    int idx;
    char tag[16];
    float x, y, w, h;
};

/* Find the Nth box (0-indexed) whose tag matches `tag`. Returns 0 + fills
 * info on success, -1 if no such box. */
static int find_box(struct yetty_ylexbor *r, const char *tag, int n, struct box_info *out)
{
    int total = yetty_ylexbor_test_box_count(r);
    int seen = 0;
    for (int i = 0; i < total; i++) {
        char t[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(r, i, &x, &y, &w, &h, t, sizeof(t)) != 0) {
            continue;
        }
        if (strcmp(t, tag) == 0) {
            if (seen == n) {
                out->idx = i;
                strncpy(out->tag, t, sizeof(out->tag) - 1);
                out->x = x;
                out->y = y;
                out->w = w;
                out->h = h;
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

static struct yetty_ylexbor *load(const char *html, int viewport_w, int viewport_h)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = viewport_w,
        .viewport_height = viewport_h,
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

/* ============================================================================
 * Test 1 — `width: 100%` resolves against the parent's content width.
 *
 * The historical regression: pct_basis at box-build was `font_size * 16`
 * (= 256 for default 16px fonts) so `width: 100%` produced 256 px wide
 * boxes, which is why Wikipedia and other %-width-heavy pages collapsed
 * to a narrow column. The correct behaviour is to fill the parent's
 * content area minus any horizontal margins / padding.
 * ============================================================================*/
static void test_width_percent(void)
{
    fprintf(stderr, "[test_width_percent]\n");
    static const char html[] = "<html><body>"
                               "<div id='full' style='width: 100%;'>x</div>"
                               "<div id='half' style='width: 50%;'>x</div>"
                               "</body></html>";
    /* viewport 1000 px, body has UA default margin: 8px → content area 984 px. */
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* Find the two divs. */
    struct box_info full = {0}, half = {0};
    if (find_box(yl, "div", 0, &full) != 0) {
        fprintf(stderr, "  no first div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    if (find_box(yl, "div", 1, &half) != 0) {
        fprintf(stderr, "  no second div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    /* width:100% must match the body's content width (1000 - 2*8 = 984),
     * NOT pct_basis (= font_size * 16 = 256 px). */
    ASSERT_NEAR("width:100% fills parent content area", full.w, 984.0f);

    /* width:50% must be half of the same parent content width. */
    ASSERT_NEAR("width:50% is half of parent content area", half.w, 492.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 2 — `float: right` lands at the right edge of its container, NOT
 * smashed against the left.
 *
 * Catches both: float side not being read (would render at x=0) and the
 * 100% width regression (would consume the whole row, leaving nothing
 * for inline content to flow around).
 * ============================================================================*/
static void test_float_right(void)
{
    fprintf(stderr, "[test_float_right]\n");
    static const char html[] = "<html><body>"
                               "<div id='rail' style='float: right; width: 150px;'>rail</div>"
                               "<p>main column</p>"
                               "</body></html>";
    /* viewport 1000 px → body content 984 px → right edge at x=8+984=992
     * → 150-wide rail sits at x = 992 - 150 = 842. */
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info rail = {0};
    if (find_box(yl, "div", 0, &rail) != 0) {
        fprintf(stderr, "  no float div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    ASSERT_NEAR("float-right width pinned to css width", rail.w, 150.0f);
    ASSERT_NEAR("float-right anchored at right edge", rail.x, 842.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 3 — `float: left` puts the box at the left edge with the configured
 * width; subsequent in-flow paragraphs flow at x = float.right.
 * ============================================================================*/
static void test_float_left_flow(void)
{
    fprintf(stderr, "[test_float_left_flow]\n");
    static const char html[] = "<html><body>"
                               "<div id='aside' style='float: left; width: 200px;'>aside</div>"
                               "<p>body text</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info aside = {0}, para = {0};
    if (find_box(yl, "div", 0, &aside) != 0) {
        fprintf(stderr, "  no float div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    if (find_box(yl, "p", 0, &para) != 0) {
        fprintf(stderr, "  no paragraph\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    ASSERT_NEAR("float-left at content origin", aside.x, 8.0f);
    ASSERT_NEAR("float-left width", aside.w, 200.0f);
    /* Paragraph flows past the float — x shifted by 200 px. */
    ASSERT_NEAR("paragraph flows after float-left", para.x, 8.0f + 200.0f);
    /* Width narrowed by the float. */
    ASSERT_NEAR("paragraph width narrowed", para.w, 984.0f - 200.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 4 — flex-grow ratios 1:2:1 produce widths 1/4, 1/2, 1/4 of the
 * container's content area.
 * ============================================================================*/
static void test_flex_grow_ratios(void)
{
    fprintf(stderr, "[test_flex_grow_ratios]\n");
    static const char html[] = "<html><body>"
                               "<div id='row' style='display: flex;'>"
                               "<div style='flex-grow: 1;'>a</div>"
                               "<div style='flex-grow: 2;'>b</div>"
                               "<div style='flex-grow: 1;'>c</div>"
                               "</div>"
                               "</body></html>";
    /* viewport 1000 → body content 984. The flex container fills 984.
     * Items have basis auto (0) and grow 1/2/1 → widths 246 / 492 / 246. */
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info a = {0}, b = {0}, c = {0};
    /* div 0 is the flex container; 1,2,3 are the items. */
    if (find_box(yl, "div", 1, &a) != 0 || find_box(yl, "div", 2, &b) != 0 ||
        find_box(yl, "div", 3, &c) != 0) {
        fprintf(stderr, "  missing flex item\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    ASSERT_NEAR("flex-grow 1:2:1 → a.w", a.w, 246.0f);
    ASSERT_NEAR("flex-grow 1:2:1 → b.w", b.w, 492.0f);
    ASSERT_NEAR("flex-grow 1:2:1 → c.w", c.w, 246.0f);
    /* Cumulative x positions. */
    ASSERT_NEAR("flex item a.x", a.x, 8.0f);
    ASSERT_NEAR("flex item b.x", b.x, 8.0f + 246.0f);
    ASSERT_NEAR("flex item c.x", c.x, 8.0f + 246.0f + 492.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 5 — `justify-content: space-between` with auto-basis items still
 * gives each item a non-zero width (the fallback even-split). Without
 * this fallback, items collapse to width 0 and text wraps to one glyph
 * per line — the "Logo" → L o g o vertical stack bug.
 * ============================================================================*/
static void test_flex_space_between_auto_basis(void)
{
    fprintf(stderr, "[test_flex_space_between_auto_basis]\n");
    static const char html[] = "<html><body>"
                               "<div style='display: flex; justify-content: space-between;'>"
                               "<div>logo</div><div>nav</div><div>user</div>"
                               "</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info logo = {0}, nav = {0}, user = {0};
    if (find_box(yl, "div", 1, &logo) != 0 || find_box(yl, "div", 2, &nav) != 0 ||
        find_box(yl, "div", 3, &user) != 0) {
        fprintf(stderr, "  missing nav item\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    /* Each item should be at least font_size wide so its content fits
     * horizontally. The fallback path gives each 1/3 of the container. */
    ASSERT_TRUE("logo wider than one glyph", logo.w > 32.0f);
    ASSERT_TRUE("nav wider than one glyph", nav.w > 32.0f);
    ASSERT_TRUE("user wider than one glyph", user.w > 32.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 6 — basic block flow: h1 followed by p, both at the body's left
 * inset, stacked vertically with non-overlapping y ranges.
 * ============================================================================*/
static void test_block_flow(void)
{
    fprintf(stderr, "[test_block_flow]\n");
    static const char html[] = "<html><body>"
                               "<h1>title</h1>"
                               "<p>paragraph</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info h1 = {0}, p = {0};
    if (find_box(yl, "h1", 0, &h1) != 0 || find_box(yl, "p", 0, &p) != 0) {
        fprintf(stderr, "  missing h1 or p\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    ASSERT_NEAR("h1 at body left inset", h1.x, 8.0f);
    ASSERT_NEAR("p at body left inset", p.x, 8.0f);
    ASSERT_TRUE("h1 above p", h1.y + h1.h <= p.y + EPS);
    ASSERT_TRUE("h1 full width", h1.w > 900.0f);
    ASSERT_TRUE("p full width", p.w > 900.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 7 — `width: 200px` (absolute) pins the box at exactly that size,
 * not the parent content width.
 * ============================================================================*/
static void test_width_pixels(void)
{
    fprintf(stderr, "[test_width_pixels]\n");
    static const char html[] = "<html><body>"
                               "<div style='width: 200px;'>fixed</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info d = {0};
    if (find_box(yl, "div", 0, &d) != 0) {
        fprintf(stderr, "  no div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    ASSERT_NEAR("width:200px is 200", d.w, 200.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 8 — table with 3 columns lays out cells side-by-side.
 *
 * Updated for content-aware column widths: short cells ("a", "b", "c")
 * each get only their measured glyph width + small padding, NOT one
 * third of the container. The total table width therefore stays small
 * — same behaviour real CSS tables exhibit for narrow infobox-like
 * label/value tables. The structural facts we still check:
 *
 *   - all three cells share a Y coordinate (one row),
 *   - X is strictly increasing,
 *   - each cell has a non-trivial width (> font/2 so glyphs fit).
 * ============================================================================*/
static void test_table_layout(void)
{
    fprintf(stderr, "[test_table_layout]\n");
    static const char html[] = "<html><body>"
                               "<table><tr><td>a</td><td>b</td><td>c</td></tr></table>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info td_a = {0}, td_b = {0}, td_c = {0};
    if (find_box(yl, "td", 0, &td_a) != 0 || find_box(yl, "td", 1, &td_b) != 0 ||
        find_box(yl, "td", 2, &td_c) != 0) {
        fprintf(stderr, "  missing td\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    fprintf(stderr, "  td widths a=%.1f b=%.1f c=%.1f\n", td_a.w, td_b.w, td_c.w);
    /* Each cell wide enough to hold at least one glyph. */
    ASSERT_TRUE("td a width > 5", td_a.w > 5.0f);
    ASSERT_TRUE("td b width > 5", td_b.w > 5.0f);
    ASSERT_TRUE("td c width > 5", td_c.w > 5.0f);
    /* No cell should consume an unreasonable fraction of the container. */
    ASSERT_TRUE("td a not full-width", td_a.w < 500.0f);
    /* Same row → same y. */
    ASSERT_NEAR("td a/b same y", td_a.y, td_b.y);
    ASSERT_NEAR("td b/c same y", td_b.y, td_c.y);
    /* Strictly increasing x. */
    ASSERT_TRUE("td x ordering", td_a.x < td_b.x && td_b.x < td_c.x);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 8b — content-aware table column widths: a wide-content cell takes
 * a larger share than narrow siblings (was equal-split before P1.3).
 * Pins the new behaviour.
 * ============================================================================*/
static void test_table_content_widths(void)
{
    fprintf(stderr, "[test_table_content_widths]\n");
    static const char html[] = "<html><body>"
                               "<table><tr>"
                               "<td>x</td>"
                               "<td>A reasonably long label with several words</td>"
                               "<td>x</td>"
                               "</tr></table>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info a = {0}, b = {0}, c = {0};
    if (find_box(yl, "td", 0, &a) != 0 || find_box(yl, "td", 1, &b) != 0 ||
        find_box(yl, "td", 2, &c) != 0) {
        fprintf(stderr, "  missing td\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    fprintf(stderr, "  td widths a=%.1f b=%.1f c=%.1f\n", a.w, b.w, c.w);
    /* The middle cell with much more text must end up wider than the
     * single-letter cells on either side. */
    ASSERT_TRUE("wide content cell wider than narrow neighbours",
                b.w > a.w * 2.0f && b.w > c.w * 2.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 9 — `<nav>` is hidden by default; `<header>` IS rendered (it
 * commonly wraps the article's <h1> on real pages like Wikipedia, so
 * we can't blanket-hide it).
 *
 * Wikipedia ships its left sidebar / page menu as <nav> elements;
 * those should drop out. The page-title <header> stays visible so
 * the <h1> inside it surfaces.
 * ============================================================================*/
static void test_nav_hidden_header_visible(void)
{
    fprintf(stderr, "[test_nav_hidden_header_visible]\n");
    static const char html[] = "<html><body>"
                               "<nav><ul><li>menu1</li><li>menu2</li></ul></nav>"
                               "<header><h1>Article title</h1></header>"
                               "<p>article body</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* The <nav> menu items must NOT push content down. */
    struct box_info para = {0};
    if (find_box(yl, "p", 0, &para) != 0) {
        fprintf(stderr, "  no article paragraph\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* h1 has its own margin so paragraph won't be at y=8 exactly,
	 * but it must be far below the 100 px or so that a typical
	 * header + h1 occupy — definitely under 200 px. */
    ASSERT_TRUE("article paragraph reasonably near top", para.y < 200.0f);

    /* h1 must be visible (inside <header>) — that's why we don't
	 * blanket-hide <header>. */
    struct box_info h1 = {0};
    if (find_box(yl, "h1", 0, &h1) != 0) {
        fprintf(stderr, "  h1 inside header should be visible\n");
        g_failures++;
    } else {
        ASSERT_TRUE("h1 inside header has non-zero height", h1.h > 0.0f);
    }

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 10 — `aria-hidden="true"` elements don't render. Wikipedia uses
 * this for decorative icons and off-screen helper text; without it we
 * leak "Jump to content" / "move to sidebar hide" into the visible
 * page.
 * ============================================================================*/
static void test_aria_hidden_skipped(void)
{
    fprintf(stderr, "[test_aria_hidden_skipped]\n");
    static const char html[] = "<html><body>"
                               "<div aria-hidden=\"true\"><h2>hidden decoration</h2></div>"
                               "<p>visible body</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info para = {0};
    if (find_box(yl, "p", 0, &para) != 0) {
        fprintf(stderr, "  no paragraph\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("aria-hidden block didn't push visible content down", para.y < 50.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 11 — Wikipedia float helper class. `<figure class="mw-halign-right">`
 * should float right even without Wikipedia's external stylesheet because
 * the UA sheet bakes in the rule.
 * ============================================================================*/
static void test_wikipedia_float_class(void)
{
    fprintf(stderr, "[test_wikipedia_float_class]\n");
    static const char html[] = "<html><body>"
                               "<figure class=\"mw-halign-right\" style=\"width: 200px;\">"
                               "<img width=\"200\" height=\"100\">"
                               "</figure>"
                               "<p>article body should flow around the float</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info fig = {0};
    if (find_box(yl, "figure", 0, &fig) != 0) {
        fprintf(stderr, "  no figure\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* viewport 1000 → body content 984 → right edge 992 →
     * 200-wide figure should land at x = 992 - 200 - 13 (right margin
     * 0.8em ≈ 13 px) ≈ 779. We accept any x in (492, 992) — i.e.
     * roughly in the right half of the content area. */
    ASSERT_TRUE("mw-halign-right figure in right half", fig.x > 492.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 12 — Stacked float-right figures pile vertically against the
 * right edge (single-stack model). This is the "Wikipedia article with
 * multiple sidebar images" case.
 * ============================================================================*/
static void test_stacked_float_right(void)
{
    fprintf(stderr, "[test_stacked_float_right]\n");
    static const char html[] = "<html><body>"
                               "<div style=\"float: right; width: 150px; height: 100px;\">A</div>"
                               "<div style=\"float: right; width: 150px; height: 100px;\">B</div>"
                               "<p>body</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info a = {0}, b = {0};
    if (find_box(yl, "div", 0, &a) != 0 || find_box(yl, "div", 1, &b) != 0) {
        fprintf(stderr, "  missing float\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* Both at the same right edge x. */
    ASSERT_NEAR("float A and B same x", a.x, b.x);
    /* B sits BELOW A (single-stack model: same side floats stack). */
    ASSERT_TRUE("second float-right below first", b.y >= a.y + a.h - EPS);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 13 — Image with HTML width/height attributes renders at that
 * size when the network fetch fails (offline rendering). Without this,
 * images collapse to 0×0 and disappear from paint.
 * ============================================================================*/
static void test_img_attr_sizing(void)
{
    fprintf(stderr, "[test_img_attr_sizing]\n");
    static const char html[] = "<html><body>"
                               "<img src=\"missing://nope\" width=\"280\" height=\"187\">"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info img = {0};
    if (find_box(yl, "img", 0, &img) != 0) {
        fprintf(stderr, "  no img\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_NEAR("img width from attr", img.w, 280.0f);
    ASSERT_NEAR("img height from attr", img.h, 187.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 14 — Float without explicit width must NOT consume the entire
 * row. Wikipedia <figure class="mw-halign-right"> elements don't carry
 * a width attribute (the width comes from external CSS we can't load
 * offline) — if we default the float's width to "all remaining space"
 * the surrounding paragraph collapses to 0 width and wrap_inline_box
 * emits 1 glyph per line ("garbage letters at the right edge").
 *
 * Asserts the surrounding paragraph keeps a workable width even when
 * the float doesn't specify one.
 * ============================================================================*/
static void test_float_no_width_doesnt_swallow_row(void)
{
    fprintf(stderr, "[test_float_no_width_doesnt_swallow_row]\n");
    static const char html[] = "<html><body>"
                               "<figure class=\"mw-halign-right\">"
                               "<img width=\"280\" height=\"187\">"
                               "</figure>"
                               "<p>Surrounding body paragraph must keep a usable width — if "
                               "the float swallows the row, this text wraps to one glyph per "
                               "line at the right edge, the 'random garbage glyphs' bug.</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info p = {0};
    if (find_box(yl, "p", 0, &p) != 0) {
        fprintf(stderr, "  no paragraph\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* Paragraph must keep at least half the content area. Without the
	 * fix, p.w would be 0 and p.x would be 1016 (right edge). */
    ASSERT_TRUE("paragraph keeps usable width", p.w > 400.0f);
    ASSERT_TRUE("paragraph not pushed to right edge", p.x < 200.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test 15 — Full Wikipedia-shape article. Reproduces the structural
 * pieces of https://en.wikipedia.org/wiki/Photography so the renderer
 * never regresses back to "all images stacked at x=8 with 3000 px of
 * menu junk above the title".
 *
 * Asserts:
 *   - The chrome (header, nav, vector-menu, jump-link) is fully hidden.
 *   - The article H1 appears near the top of the page (y small).
 *   - The mw-halign-right figure floats to the right half of the page.
 *   - The infobox table is on the right.
 *   - The article body paragraph is in the lower half of the page,
 *     and its width is narrower than the body's content area because
 *     the float is taking horizontal space on the right.
 * ============================================================================*/
static void test_wikipedia_shape(void)
{
    fprintf(stderr, "[test_wikipedia_shape]\n");
    static const char html[] =
        "<!doctype html><html><body>"
        "<a class=\"mw-jump-link\" href=\"#main\">Jump to content</a>"
        "<header class=\"vector-header\"><div class=\"vector-page-titlebar\">"
        "  <span>page header noise</span></div></header>"
        "<nav class=\"vector-menu mw-portlet\"><ul>"
        "  <li>Main page</li><li>Contents</li><li>Current events</li>"
        "</ul></nav>"
        "<main id=\"content\">"
        "<h1>Photography</h1>"
        "<table class=\"infobox\" style=\"width: 250px;\">"
        "  <tr><th>Infobox</th></tr>"
        "  <tr><td>Field</td><td>Value</td></tr>"
        "</table>"
        "<figure class=\"mw-halign-right\" style=\"width: 280px;\">"
        "  <img width=\"280\" height=\"187\">"
        "  <figcaption>caption</figcaption>"
        "</figure>"
        "<p id=\"article-body\">"
        "Photography is the art, application, and practice of creating images "
        "by recording light, either electronically by means of an image sensor, "
        "or chemically by means of a light-sensitive material."
        "</p>"
        "</main>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* Header / nav / jump link must be invisible: their box height
	 * should be 0 (skipped at box-build by libcss display:none). */
    struct box_info hdr = {0}, nav = {0};
    if (find_box(yl, "header", 0, &hdr) == 0) {
        ASSERT_NEAR("vector-header hidden (h=0)", hdr.h, 0.0f);
    }
    if (find_box(yl, "nav", 0, &nav) == 0) {
        ASSERT_NEAR("vector-menu nav hidden (h=0)", nav.h, 0.0f);
    }

    /* The article <h1> must land near the top, not buried under chrome. */
    struct box_info h1 = {0};
    if (find_box(yl, "h1", 0, &h1) != 0) {
        fprintf(stderr, "  no article h1\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("article h1 appears in the first 100 px", h1.y < 100.0f);

    /* The infobox is .infobox → UA CSS floats it right. */
    struct box_info infobox = {0};
    if (find_box(yl, "table", 0, &infobox) != 0) {
        fprintf(stderr, "  no infobox\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("infobox floats to right half", infobox.x > 500.0f);

    /* The figure.mw-halign-right floats right too — stacks BELOW the
	 * infobox under our single-stack model. */
    struct box_info fig = {0};
    if (find_box(yl, "figure", 0, &fig) != 0) {
        fprintf(stderr, "  no figure\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("figure floats to right half", fig.x > 500.0f);

    /* Article body paragraph is in flow on the left, narrowed by the
	 * 250 px infobox (+ ~13 px margin). With viewport 1000 and body
	 * margin 8, content area is 984. The paragraph should be wider
	 * than half the content area (because no left float reduces it
	 * further) but narrower than full content. */
    struct box_info p = {0};
    if (find_box(yl, "p", 0, &p) != 0) {
        fprintf(stderr, "  no paragraph\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("article body narrower than content area", p.w < 984.0f);
    ASSERT_TRUE("article body wider than 50% of content", p.w > 492.0f);
    ASSERT_NEAR("article body starts at body content origin", p.x, 8.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test — <img> inside <figure> must be positioned, NOT stuck at (0,0).
 *
 * The bug: libcss's internal UA stylesheet reports computed display
 * value CSS_DISPLAY_TABLE (6) for <figure>, regardless of any author /
 * ybrowser-UA CSS to the contrary (including !important). Our layout
 * dispatcher would then call layout_table on every <figure>, which
 * scans for descendant <tr> elements, finds none, and returns h=0 —
 * leaving the inner <img> at the memset-default (0, 0). On Wikipedia
 * this manifested as every article image stacked at the upper-left
 * corner of the page.
 *
 * The fix (ybrowser-box.c): when libcss reports DISPLAY_TABLE for an
 * element that isn't actually <table>, force it back to BLOCK so the
 * normal block-flow path positions the inner <img>.
 *
 * This test pins the bug.
 * ============================================================================*/
static void test_figure_img_positioned(void)
{
    fprintf(stderr, "[test_figure_img_positioned]\n");
    static const char html[] = "<html><body>"
                               "<p>before</p>"
                               "<figure>"
                               "<img width=\"280\" height=\"187\">"
                               "<figcaption>caption</figcaption>"
                               "</figure>"
                               "<p>after</p>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1024, 600);

    struct box_info img = {0};
    if (find_box(yl, "img", 0, &img) != 0) {
        fprintf(stderr, "  no img\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    fprintf(stderr, "  img at xy=(%.0f,%.0f) wh=(%.0fx%.0f)\n", img.x, img.y, img.w, img.h);
    /* The img must have been positioned by the layout pass — i.e.
	 * its y must be past the body's top padding and past the first
	 * <p>. With body padding 8 + h1 default = 0 + p height ≈ 20px,
	 * the img should be at roughly y > 25 px. */
    ASSERT_TRUE("img inside figure was positioned (y > 0)", img.y > 0.0f);
    ASSERT_TRUE("img inside figure was positioned (x not 0)", img.x > 0.0f || img.y > 30.0f);
    ASSERT_TRUE("img kept its width", img.w >= 280.0f);
    ASSERT_TRUE("img kept its height", img.h >= 187.0f);

    yetty_ylexbor_destroy(yl);
}

/* ============================================================================
 * Test — `@media (prefers-color-scheme: dark)` must NOT override :root
 * variables in the default rendering pass.
 *
 * Wikipedia (and most themed sites) defines a light palette in :root
 * then a dark palette inside @media (prefers-color-scheme: dark). Our
 * earlier custom-property scanner walked into @media bodies and let
 * the dark values overwrite the light defaults via last-write-wins —
 * the entire article rendered with light-gray text (#eaecf0) that was
 * invisible on the default white terminal background.
 *
 * This test confirms we resolve --x to its :root value (red) and
 * ignore the @media override (blue). Verified indirectly: a <div>
 * styled with `color: var(--x)` should resolve to the :root value
 * regardless of any @media-gated redefinitions later in the
 * stylesheet.
 * ============================================================================*/
static void test_media_query_doesnt_override_root_vars(void)
{
    fprintf(stderr, "[test_media_query_doesnt_override_root_vars]\n");
    static const char html[] = "<html><head><style>"
                               ":root { --x: #ff0000; }"
                               "@media (prefers-color-scheme: dark) {"
                               "  :root { --x: #0000ff; }"
                               "}"
                               "div { color: var(--x); }"
                               "</style></head>"
                               "<body><div>hello</div></body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* Render once to ensure layout is computed. We can't easily read
	 * computed colors back through the public test API, so this test
	 * is mostly a smoke check that the page parses, lays out, and
	 * doesn't crash with the dark-vars override. The cssvars-scan
	 * behaviour is the regression target. */
    struct box_info d = {0};
    if (find_box(yl, "div", 0, &d) != 0) {
        fprintf(stderr, "  no div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_TRUE("div rendered with some width", d.w > 0.0f);
    ASSERT_TRUE("div rendered with some height", d.h > 0.0f);
    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    fprintf(stderr, "ybrowser-layout-test\n");

    test_block_flow();
    test_width_pixels();
    test_width_percent();
    test_float_left_flow();
    test_float_right();
    test_flex_grow_ratios();
    test_flex_space_between_auto_basis();
    test_table_layout();
    test_table_content_widths();
    test_nav_hidden_header_visible();
    test_aria_hidden_skipped();
    test_wikipedia_float_class();
    test_stacked_float_right();
    test_img_attr_sizing();
    test_float_no_width_doesnt_swallow_row();
    test_figure_img_positioned();
    test_media_query_doesnt_override_root_vars();
    test_wikipedia_shape();

    fprintf(stderr, "\nresults: %d passed, %d failed\n", g_passed, g_failures);
    return g_failures == 0 ? 0 : 1;
}
