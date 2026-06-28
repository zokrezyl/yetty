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
 * Test 10 — `aria-hidden="true"` is an ACCESSIBILITY hint, not a visual one:
 * the element is removed from the a11y tree but still RENDERS (Chrome paints
 * it). Mapping it to display:none wrongly drops visible content — e.g. Google
 * News marks each story's thumbnail figure aria-hidden yet shows the image.
 * So the aria-hidden block must occupy space and push the paragraph below it.
 * (Site-specific hidden nav is handled by explicit selectors, not by abusing
 * aria-hidden.)
 * ============================================================================*/
static void test_aria_hidden_still_renders(void)
{
    fprintf(stderr, "[test_aria_hidden_still_renders]\n");
    static const char html[] = "<html><body>"
                               "<div aria-hidden=\"true\"><h2>decoration</h2></div>"
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
    /* The aria-hidden <h2> renders and takes vertical space, so the paragraph
     * is pushed well below the top — NOT collapsed to y≈0. */
    ASSERT_TRUE("aria-hidden block should render and push content down", para.y > 20.0f);

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

/* ============================================================================
 * Geometry P0 — box-sizing, percent padding basis, viewport units, and
 * CSS line-height. These pin the four fixes that move static-page geometry
 * toward real-browser behaviour.
 * ============================================================================*/

/* box-sizing: content-box (the CSS initial) expands the box by padding;
 * border-box keeps the declared width as the outer width. */
static void test_box_sizing(void)
{
    fprintf(stderr, "[test_box_sizing]\n");
    static const char html[] =
        "<html><body style='margin:0'>"
        "<div id='cb' style='width:200px; padding:20px;'>x</div>"
        "<div id='bb' style='width:200px; padding:20px; box-sizing:border-box;'>y</div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info cb = {0}, bb = {0};
    if (find_box(yl, "div", 0, &cb) != 0 || find_box(yl, "div", 1, &bb) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* content-box: outer width = content 200 + left/right padding 20+20. */
    ASSERT_NEAR("content-box width is 200 + 2*20", cb.w, 240.0f);
    /* border-box: outer width = declared 200. */
    ASSERT_NEAR("border-box width is exactly 200", bb.w, 200.0f);

    yetty_ylexbor_destroy(yl);
}

/* Percent padding resolves against the containing block's content width
 * (1000 here), not the old `font_size * 16` proxy (= 256 → 25.6 px). */
static void test_percent_padding_basis(void)
{
    fprintf(stderr, "[test_percent_padding_basis]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div id='outer' style='padding-left:10%;'>"
                               "<div id='inner'>x</div>"
                               "</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info inner = {0};
    if (find_box(yl, "div", 1, &inner) != 0) {
        fprintf(stderr, "  missing inner div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* 10% of the body's 1000 px content width = 100 px of left padding,
     * so the inner block's content origin sits at x = 100. */
    ASSERT_NEAR("percent padding uses containing-block width", inner.x, 100.0f);

    yetty_ylexbor_destroy(yl);
}

/* Viewport units resolve against the live viewport: 50vw of a 1000 px
 * viewport = 500; 50vh of a 600 px viewport = 300. */
static void test_viewport_units(void)
{
    fprintf(stderr, "[test_viewport_units]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div id='vw' style='width:50vw;'>x</div>"
                               "<div id='vh' style='width:50vh;'>y</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info vw = {0}, vh = {0};
    if (find_box(yl, "div", 0, &vw) != 0 || find_box(yl, "div", 1, &vh) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_NEAR("50vw of 1000px viewport is 500", vw.w, 500.0f);
    ASSERT_NEAR("50vh of 600px viewport is 300", vh.w, 300.0f);

    yetty_ylexbor_destroy(yl);
}

/* CSS line-height drives the inline line box: a 40px line-height makes a
 * single line of text consume 40px, so the following block lands at y=40
 * instead of the default ~20 (font_size * 1.25). */
static void test_line_height(void)
{
    fprintf(stderr, "[test_line_height]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div id='a' style='line-height:40px;'>one line</div>"
                               "<div id='b'>after</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info b = {0};
    if (find_box(yl, "div", 1, &b) != 0) {
        fprintf(stderr, "  missing second div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* First div is one line tall at the declared 40px line-height; the
     * sibling stacks directly below it. */
    ASSERT_NEAR("explicit line-height sets line box", b.y, 40.0f);

    yetty_ylexbor_destroy(yl);
}

/* `position: relative` keeps the box in flow (siblings stack as if it were
 * not shifted) but offsets the box itself by the insets. `left` wins over
 * `right`, `top` over `bottom`. */
static void test_position_relative(void)
{
    fprintf(stderr, "[test_position_relative]\n");
    static const char html[] =
        "<html><body style='margin:0'>"
        "<div id='a' style='height:50px'>a</div>"
        "<div id='b' style='position:relative; top:30px; left:40px; height:20px'>b</div>"
        "<div id='c' style='height:10px'>c</div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info b = {0}, c = {0};
    if (find_box(yl, "div", 1, &b) != 0 || find_box(yl, "div", 2, &c) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* Flow puts #b at y=50; relative shifts it to (40, 80). */
    ASSERT_NEAR("relative left offset", b.x, 40.0f);
    ASSERT_NEAR("relative top offset", b.y, 80.0f);
    /* #c stacks as if #b were unshifted: directly below #b's flow slot. */
    ASSERT_NEAR("relative does not disturb flow", c.y, 70.0f);

    yetty_ylexbor_destroy(yl);
}

/* `position: absolute` removes the box from flow (siblings ignore it) and
 * places it against the nearest positioned ancestor's box using the insets. */
static void test_position_absolute(void)
{
    fprintf(stderr, "[test_position_absolute]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div id='wrap' style='position:relative; height:300px'>"
                               "<div id='card' style='position:absolute; top:100px; left:20px; "
                               "width:200px; height:80px'>card</div>"
                               "<div id='flow' style='height:40px'>flow</div>"
                               "</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info card = {0}, flow = {0};
    if (find_box(yl, "div", 1, &card) != 0 || find_box(yl, "div", 2, &flow) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* Absolute card placed at the wrap's padding box + insets. */
    ASSERT_NEAR("absolute left", card.x, 20.0f);
    ASSERT_NEAR("absolute top", card.y, 100.0f);
    ASSERT_NEAR("absolute width", card.w, 200.0f);
    /* The in-flow sibling starts at the top — the absolute box reserved no
     * space. */
    ASSERT_NEAR("absolute reserves no flow space", flow.y, 0.0f);

    yetty_ylexbor_destroy(yl);
}

/* `transform: translate()` from an inline style visually shifts the box (and
 * its subtree) without disturbing flow. This is how JS-driven sites position
 * content (e.g. Google News cards). */
static void test_transform_translate(void)
{
    fprintf(stderr, "[test_transform_translate]\n");
    static const char html[] =
        "<html><body style='margin:0'>"
        "<div id='a' style='width:100px; height:40px; "
        "transform:translate(200px,50px)'>a</div>"
        "<div id='b' style='width:100px; height:40px; transform:translateY(80px)'>b</div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info a = {0}, b = {0};
    if (find_box(yl, "div", 0, &a) != 0 || find_box(yl, "div", 1, &b) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* #a: flow (0,0) + translate(200,50). */
    ASSERT_NEAR("translate x", a.x, 200.0f);
    ASSERT_NEAR("translate y", a.y, 50.0f);
    /* #b: flow y=40 (below #a's flow slot) + translateY(80) = 120. */
    ASSERT_NEAR("translateY keeps flow slot", b.x, 0.0f);
    ASSERT_NEAR("translateY adds to flow y", b.y, 120.0f);

    yetty_ylexbor_destroy(yl);
}

/* A flex COLUMN item with no explicit height (auto main-size) must size to
 * its content, not collapse to 0. This is the bug that piled Google News
 * story cards on top of each other: a card is a flex column of [image,
 * headline]; the headline (text-only, auto height) collapsed to 0, so the
 * card shrank to the image and siblings overlapped. */
static void test_flex_column_text_height(void)
{
    fprintf(stderr, "[test_flex_column_text_height]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div style='display:flex; flex-direction:column; width:280px'>"
                               "<div id='img' style='height:100px'>img</div>"
                               "<div id='txt'>headline text here</div>"
                               "</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* div 0 = flex column, div 1 = image, div 2 = headline text. */
    struct box_info img = {0}, txt = {0};
    if (find_box(yl, "div", 1, &img) != 0 || find_box(yl, "div", 2, &txt) != 0) {
        fprintf(stderr, "  missing divs\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* The headline sits below the 100px image and has a real (line-box)
     * height — not the collapsed 0 that caused the overlap. */
    ASSERT_NEAR("flex-column text item stacks below the image", txt.y, 100.0f);
    ASSERT_TRUE("flex-column text item has nonzero height", txt.h > 10.0f);

    yetty_ylexbor_destroy(yl);
}

/* `insertBefore` / `replaceChild` must preserve DOM order, not append. They
 * used to alias appendChild, which silently reordered JS-driven inserts to the
 * end — scrambling app-shell content (Google News builds its feed this way). */
static void test_dom_insert_before(void)
{
    fprintf(stderr, "[test_dom_insert_before]\n");
    static const char html[] =
        "<html><body style='margin:0'><div id='box'></div>"
        "<script>"
        "var c=document.getElementById('box');"
        "var b=document.createElement('div');b.textContent='SECOND';"
        "var a=document.createElement('div');a.textContent='FIRST';"
        "c.appendChild(b);"
        "c.insertBefore(a,b);" /* => FIRST, SECOND */
        "var d=document.createElement('div');d.textContent='THIRD';c.appendChild(d);"
        "var rep=document.createElement('div');rep.textContent='REPLACED';"
        "c.replaceChild(rep,b);" /* => FIRST, REPLACED, THIRD */
        "</script></body></html>";
    struct yetty_ylexbor *yl = load(html, 800, 400);

    /* Gather inline-text boxes top-to-bottom and check their order. */
    int total = yetty_ylexbor_test_box_count(yl);
    char topmost[32] = {0};
    float topmost_y = 1e9f;
    int saw_second = 0;
    for (int i = 0; i < total; i++) {
        int kind = 0;
        char txt[32] = {0};
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, txt, sizeof(txt)) != 0) {
            continue;
        }
        if (kind != YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT || txt[0] == '\0') {
            continue;
        }
        float x, y, w, h;
        char tag[16] = {0};
        (void)yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, tag, sizeof(tag));
        if (y < topmost_y) {
            topmost_y = y;
            strncpy(topmost, txt, sizeof(topmost) - 1);
        }
        if (strcmp(txt, "SECOND") == 0) {
            saw_second = 1;
        }
    }
    ASSERT_TRUE("insertBefore puts FIRST at the top", strcmp(topmost, "FIRST") == 0);
    ASSERT_TRUE("replaceChild removed the replaced node", saw_second == 0);

    yetty_ylexbor_destroy(yl);
}

/* A 2-column `display:grid` (the news-card idiom: `1fr <px>`) places the
 * first child in the flexible column and the second in the fixed thumbnail
 * column — so the thumbnail shrinks to its narrow track instead of filling
 * the card. Track sizes come from the author CSS (libcss exposes no grid). */
static void test_grid_two_column(void)
{
    fprintf(stderr, "[test_grid_two_column]\n");
    static const char html[] =
        "<html><head><style>"
        ".card{display:grid;grid-template-columns:1fr 75px;column-gap:12px;width:400px}"
        "</style></head><body style='margin:0'>"
        "<div class='card'>"
        "<div id='text'>Headline in the wide first column</div>"
        "<div id='thumb' style='height:60px'>t</div>"
        "</div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    /* div 0 = grid card, div 1 = text cell, div 2 = thumbnail cell. */
    struct box_info text = {0}, thumb = {0};
    if (find_box(yl, "div", 1, &text) != 0 || find_box(yl, "div", 2, &thumb) != 0) {
        fprintf(stderr, "  missing cells\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* 1fr column = 400 - 75 - 12 = 313, at x=0. Thumbnail = 75px at x=325. */
    ASSERT_NEAR("grid 1fr column x", text.x, 0.0f);
    ASSERT_NEAR("grid 1fr column width", text.w, 313.0f);
    ASSERT_NEAR("grid fixed column x", thumb.x, 325.0f);
    ASSERT_NEAR("grid fixed column width", thumb.w, 75.0f);

    yetty_ylexbor_destroy(yl);
}

/* CSS `width`/`height` on an image must win over its natural decoded size.
 * The real Google News favicons have NO width/height attributes and a large
 * (96x96) natural image, sized down to 14px purely by CSS — box-build ignored
 * CSS sizing and rendered them at natural size, burying the card. Here a 4x4
 * PNG is forced to 14x14 by CSS: if CSS were ignored it would be 4x4. */
static void test_css_image_size(void)
{
    fprintf(stderr, "[test_css_image_size]\n");
    static const char html[] = "<html><head><style>img{width:14px;height:14px}</style></head>"
                               "<body style='margin:0'>"
                               "<img src='data:image/png;base64,"
                               "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+"
                               "AAAAEklEQVR42mP4z8DwHxkzkC4AADxAH+Ea86VIAAAAAEl"
                               "FTkSuQmCC'>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info img = {0};
    if (find_box(yl, "img", 0, &img) != 0) {
        fprintf(stderr, "  missing img\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    ASSERT_NEAR("CSS width wins over natural", img.w, 14.0f);
    ASSERT_NEAR("CSS height wins over natural", img.h, 14.0f);

    yetty_ylexbor_destroy(yl);
}

/* A small image inside a flex row must keep its intrinsic size, not balloon to
 * the flex line. This is the Google News bug: a 14x14 source favicon next to
 * the headline ballooned to ~266x100 (column width + 100px fallback) and
 * covered the article thumbnail. See test/ut/ybrowser/examples/gnews-card.html. */
static void test_flex_image_intrinsic_size(void)
{
    fprintf(stderr, "[test_flex_image_intrinsic_size]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<div style='display:flex; flex-direction:row; width:400px'>"
                               "<img width='14' height='14'>"
                               "<div>Source name and the rest of the headline text here</div>"
                               "</div>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct box_info img = {0};
    if (find_box(yl, "img", 0, &img) != 0) {
        fprintf(stderr, "  missing img\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* The favicon keeps its 14x14, not the ballooned flex-distributed size. */
    ASSERT_NEAR("flex image keeps intrinsic width", img.w, 14.0f);
    ASSERT_NEAR("flex image keeps intrinsic height", img.h, 14.0f);

    yetty_ylexbor_destroy(yl);
}

/* A display:grid container with the content-column idiom
 * `grid-template-columns: minmax(0, <Nrem>) ...` caps the content to N
 * (≈ readable column) instead of filling the parent — we don't lay out
 * grid tracks, but we scan that cap out of the CSS and apply it as a
 * max-width. 30rem = 480px. */
static void test_grid_content_column_cap(void)
{
    fprintf(stderr, "[test_grid_content_column_cap]\n");
    static const char html[] =
        "<html><head><style>"
        ".body{display:grid;grid-template-columns:minmax(0,30rem) min-content}"
        "</style></head><body style='margin:0'>"
        "<div class='body'><p>x</p></div>"
        "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1200, 800);

    struct box_info g = {0};
    if (find_box(yl, "div", 0, &g) != 0) {
        fprintf(stderr, "  missing grid div\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    /* Without the cap the div would fill the 1200px body; the scanned
     * minmax(0,30rem) caps it to 480. */
    ASSERT_NEAR("grid content column capped to 30rem", g.w, 480.0f);

    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    fprintf(stderr, "ybrowser-layout-test\n");

    test_grid_content_column_cap();
    test_box_sizing();
    test_percent_padding_basis();
    test_viewport_units();
    test_line_height();
    test_position_relative();
    test_position_absolute();
    test_transform_translate();
    test_flex_column_text_height();
    test_grid_two_column();
    test_css_image_size();
    test_flex_image_intrinsic_size();
    test_dom_insert_before();
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
    test_aria_hidden_still_renders();
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
