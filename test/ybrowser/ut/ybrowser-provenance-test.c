/*
 * ybrowser provenance test — pins the height_source (size-provenance, the hs=
 * column) that the shared used-height helpers assign at the grid/flex/block call
 * sites. Geometry tests (WPT category 34) pin offsetHeight/clientHeight; this
 * pins the PROVENANCE so a helper or call-site can't silently mislabel a box's
 * used-size origin while the numbers stay green. Headless, offline.
 *
 * Contract asserted (grid items exercise grid_item_used_height):
 *   - explicit author height (incl. 0)            -> "css"
 *   - auto block/text                             -> "content"
 *   - bare (intrinsic) image                      -> "img"
 *   - unresolved grid percentage (treated as auto)-> "content"
 * Known limitation, asserted explicitly so it can't drift unnoticed:
 *   - a CSS-height <img> is labelled "img" (NOT "css") because the box builder
 *     does not set css_height_set on the replaced-element path; fixing that is a
 *     separate replaced-element sizing/provenance increment.
 */

#include <yetty/ybrowser/ybrowser.h>

#include "ytest.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static struct yetty_ylexbor *load(struct ytest *test, const char *html)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = 800,
        .viewport_height = 600,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result created = yetty_ylexbor_create(&cfg);
    YTEST_REQUIRE_OK(test, created);
    struct yetty_ycore_void_result loaded =
        yetty_ylexbor_load_html(created.value, html, strlen(html));
    YTEST_REQUIRE_OK(test, loaded);
    return created.value;
}

/* Find the first `tag` box whose height is within 1px of `target_h` (pass a
 * negative target to match on tag alone) and whose width is <= `max_w` (pass a
 * non-positive max_w to ignore width — used to skip a full-width grid container
 * whose height equals its tallest item). On success returns 0 and fills the
 * requested out-params (any may be NULL); returns -1 if no match. The
 * height_source string is static storage inside the accessor — never freed. */
static int find_box_by_height(struct yetty_ylexbor *engine, const char *tag, float target_h,
                              float max_w, float *x_out, const char **height_source_out)
{
    int total = yetty_ylexbor_test_box_count(engine);
    for (int index = 0; index < total; index++) {
        float x, y, w, h;
        char box_tag[16] = {0};
        if (yetty_ylexbor_test_box_at(engine, index, &x, &y, &w, &h, box_tag, sizeof(box_tag)) !=
            0) {
            continue;
        }
        if (strcmp(box_tag, tag) != 0) {
            continue;
        }
        if (target_h >= 0.0f && fabsf(h - target_h) > 1.0f) {
            continue;
        }
        if (max_w > 0.0f && w > max_w) {
            continue;
        }
        const char *width_source = NULL;
        const char *height_source = NULL;
        if (yetty_ylexbor_test_box_sources_at(engine, index, &width_source, &height_source) != 0) {
            continue;
        }
        if (x_out != NULL) {
            *x_out = x;
        }
        if (height_source_out != NULL) {
            *height_source_out = height_source;
        }
        return 0;
    }
    return -1;
}

/* height_source short name for the first matching box, or "(none)". */
static const char *height_source_of(struct yetty_ylexbor *engine, const char *tag, float target_h)
{
    const char *height_source = NULL;
    if (find_box_by_height(engine, tag, target_h, 0.0f, NULL, &height_source) != 0) {
        return "(none)";
    }
    return height_source;
}

static void test_grid_height_provenance(struct ytest *test)
{
    /* Distinctive per-box heights so each target box is found unambiguously:
     * explicit 31 (css); auto block 55 = 43px child + 12 padding (content);
     * explicit zero 18 = 0 + 18 padding, content-box (css); bare img 100 (img);
     * css-height img 44 (img, known limitation); grid % item 35 = 27px child +
     * 8 padding, resolves as auto (content). The 43/27 child divs are not asserted. */
    static const char html[] =
        "<html><body>"
        "<div style='display:grid;grid-template-columns:300px'>"
        "<div style='height:31px'>a</div>"
        "<div style='padding:6px 0'><div style='height:43px'></div></div>"
        "<div style='height:0;padding:9px 0'>c</div>"
        "<img src='data:,'>"
        "<img style='height:44px' src='data:,'>"
        "<div style='height:50%;padding:4px 0'><div style='height:27px'></div></div>"
        "</div>"
        "</body></html>";
    struct yetty_ylexbor *engine = load(test, html);

    const char *explicit_source = height_source_of(engine, "div", 31.0f);
    YTEST_CHECK_STR_EQ(test, explicit_source ? explicit_source : "(none)", "css");

    const char *auto_source = height_source_of(engine, "div", 55.0f);
    YTEST_CHECK_STR_EQ(test, auto_source ? auto_source : "(none)", "content");

    const char *zero_source = height_source_of(engine, "div", 18.0f);
    YTEST_CHECK_STR_EQ(test, zero_source ? zero_source : "(none)", "css");

    const char *bare_image_source = height_source_of(engine, "img", 100.0f);
    YTEST_CHECK_STR_EQ(test, bare_image_source ? bare_image_source : "(none)", "img");

    /* KNOWN LIMITATION: a CSS-height image is "img", not "css" — box-build does
     * not set css_height_set on the replaced path. Asserted so the gap is
     * visible and cannot silently change without updating this expectation. */
    const char *css_image_source = height_source_of(engine, "img", 44.0f);
    YTEST_CHECK_STR_EQ(test, css_image_source ? css_image_source : "(none)", "img");

    const char *percent_source = height_source_of(engine, "div", 35.0f);
    YTEST_CHECK_STR_EQ(test, percent_source ? percent_source : "(none)", "content");

    yetty_ylexbor_destroy(engine);
}

/* The layout_grid NAMED-placement sites are duplicated code from auto-flow, so
 * they are covered here separately (the peer flagged that a named site could pass
 * the wrong laid_out_h, omit the assignment, or mislabel provenance even though
 * the shared helper is correct). Named-line placement is triggered by a
 * grid-template-columns with [name] lines AND every child carrying an inline
 * grid-column:<name>. Placing the explicit-height item on the SECOND named track
 * proves the named branch actually ran (auto-flow would place the first child on
 * track 0 at x=0; named placement puts it at x~200). */
static void test_named_grid_placement(struct ytest *test)
{
    static const char html[] =
        "<html><body>"
        "<div style='display:grid;grid-template-columns:[a] 200px [b] 200px [c]'>"
        "<div style='grid-column:b;height:30px;padding:20px 0;"
        "border-top:10px solid #333;border-bottom:10px solid #333'>x</div>"
        "<div style='grid-column:a;padding:6px 0'><div style='height:41px'></div></div>"
        "</div>"
        "</body></html>";
    struct yetty_ylexbor *engine = load(test, html);

    /* Explicit-height named block: used border box 30 + 40 padding + 20 border = 90,
     * placed on track [b] (x ~ 200) — confirms the named branch ran, the named
     * block site applied the used-height helper (border-box), and labelled it css. */
    float explicit_x = -1.0f;
    const char *explicit_source = NULL;
    /* max_w 400 skips the full-width grid container, whose height also = 90. */
    int found_explicit =
        find_box_by_height(engine, "div", 90.0f, 400.0f, &explicit_x, &explicit_source);
    YTEST_CHECK(test, found_explicit == 0);
    YTEST_CHECK_STR_EQ(test, explicit_source ? explicit_source : "(none)", "css");
    YTEST_CHECK(test, explicit_x > 100.0f); /* track [b], not auto-flow's x=0 */

    /* Auto named block: 41px child + 12 padding = 53, on track [a] (x ~ 0), content. */
    float auto_x = -1.0f;
    const char *auto_source = NULL;
    int found_auto = find_box_by_height(engine, "div", 53.0f, 400.0f, &auto_x, &auto_source);
    YTEST_CHECK(test, found_auto == 0);
    YTEST_CHECK_STR_EQ(test, auto_source ? auto_source : "(none)", "content");
    YTEST_CHECK(test, auto_x < 100.0f); /* track [a] */

    yetty_ylexbor_destroy(engine);
}

/* Return the `n`-th <td> box in document (box-vector) order, filling its height
 * and height_source short name. Returns 0 on success, -1 if there is no n-th
 * <td>. The height_source string is static storage in the accessor. */
static int nth_td(struct yetty_ylexbor *engine, int n, float *h_out, const char **height_source_out)
{
    int total = yetty_ylexbor_test_box_count(engine);
    int seen = 0;
    for (int index = 0; index < total; index++) {
        float x, y, w, h;
        char box_tag[16] = {0};
        if (yetty_ylexbor_test_box_at(engine, index, &x, &y, &w, &h, box_tag, sizeof(box_tag)) !=
            0) {
            continue;
        }
        if (strcmp(box_tag, "td") != 0) {
            continue;
        }
        if (seen == n) {
            const char *width_source = NULL;
            const char *height_source = NULL;
            if (yetty_ylexbor_test_box_sources_at(engine, index, &width_source, &height_source) !=
                0) {
                return -1;
            }
            if (h_out != NULL) {
                *h_out = h;
            }
            if (height_source_out != NULL) {
                *height_source_out = height_source;
            }
            return 0;
        }
        seen++;
    }
    return -1;
}

/* Table row equalization must report a table-specific source, NOT grid-stretch:
 * a cell shorter than its row's tallest cell is grown to the row's border-box
 * height and labelled "table-stretch"; the taller cell that SET the row height
 * keeps its own content/css provenance (it is not stretched). This pins the
 * provenance the peer required so the shared source contract stays truthful. */
static void test_table_cell_stretch_provenance(struct ytest *test)
{
    static const char html[] = "<html><body>"
                               "<table style='border-collapse:separate;border-spacing:0'>"
                               "<tr>"
                               "<td style='width:120px;padding:0;border-top:10px solid "
                               "#333;border-bottom:10px solid #333'>"
                               "<div style='height:20px'></div></td>"
                               "<td style='width:120px;padding:0;border-top:10px solid "
                               "#333;border-bottom:10px solid #333'>"
                               "<div style='height:60px'></div></td>"
                               "</tr></table>"
                               "</body></html>";
    struct yetty_ylexbor *engine = load(test, html);

    /* Cell 0: natural content 20 + 20 border = 40; equalized UP to the row's
     * 80 -> height 80, provenance "table-stretch" (not grid-stretch). */
    float short_h = -1.0f;
    const char *short_source = NULL;
    YTEST_CHECK(test, nth_td(engine, 0, &short_h, &short_source) == 0);
    YTEST_CHECK(test, fabsf(short_h - 80.0f) <= 1.0f);
    YTEST_CHECK_STR_EQ(test, short_source ? short_source : "(none)", "table-stretch");

    /* Cell 1: natural content 60 + 20 border = 80 == row height -> the
     * row-height winner is NOT stretched and keeps its content provenance. */
    float tall_h = -1.0f;
    const char *tall_source = NULL;
    YTEST_CHECK(test, nth_td(engine, 1, &tall_h, &tall_source) == 0);
    YTEST_CHECK(test, fabsf(tall_h - 80.0f) <= 1.0f);
    YTEST_CHECK_STR_EQ(test, tall_source ? tall_source : "(none)", "content");

    yetty_ylexbor_destroy(engine);
}

/* Absolutely-positioned boxes route their used border-box height through
 * abs_used_height, which picks provenance by the same rules as grid/flex/block:
 * explicit author height -> css; auto -> content (+ own border); percentage
 * (definite against the CB) -> css; a top+bottom auto-stretch -> the new
 * abs-stretch source; a replaced <img> keeps the documented img limitation.
 * Distinctive per-box heights (inside a definite 400x200 zero-border CB) locate
 * each unambiguously; max_w skips the 400px-wide CB itself. */
static void test_abs_height_provenance(struct ytest *test)
{
    static const char html[] =
        "<html><body>"
        "<div style='position:relative;width:400px;height:200px;margin:0;padding:0;border:0'>"
        "<div style='position:absolute;top:0;left:0;width:90px;height:31px'>a</div>"
        "<div style='position:absolute;top:0;left:100px;width:90px;"
        "border-top:10px solid #333;border-bottom:10px solid #333'><div "
        "style='height:43px'></div></div>"
        "<div style='position:absolute;top:0;left:200px;width:90px;height:50%'>p</div>"
        "<div style='position:absolute;top:10px;bottom:10px;left:300px;width:90px'>s</div>"
        "<img style='position:absolute;top:0;left:0;width:90px;height:44px' src='data:,'>"
        "</div>"
        "</body></html>";
    struct yetty_ylexbor *engine = load(test, html);

    /* explicit height:31 -> css. */
    float x = -1.0f;
    const char *explicit_source = NULL;
    YTEST_CHECK(test, find_box_by_height(engine, "div", 31.0f, 350.0f, &x, &explicit_source) == 0);
    YTEST_CHECK_STR_EQ(test, explicit_source ? explicit_source : "(none)", "css");

    /* auto: content 43 + own border 20 = 63 -> content (and proves the border add). */
    const char *auto_source = NULL;
    YTEST_CHECK(test, find_box_by_height(engine, "div", 63.0f, 350.0f, &x, &auto_source) == 0);
    YTEST_CHECK_STR_EQ(test, auto_source ? auto_source : "(none)", "content");

    /* percentage 50% of the definite CB height 200 = 100 -> css (definite, not auto). */
    const char *percent_source = NULL;
    YTEST_CHECK(test, find_box_by_height(engine, "div", 100.0f, 350.0f, &x, &percent_source) == 0);
    YTEST_CHECK_STR_EQ(test, percent_source ? percent_source : "(none)", "css");

    /* top+bottom auto stretch: 200 - 10 - 10 = 180 -> abs-stretch. */
    const char *stretch_source = NULL;
    YTEST_CHECK(test, find_box_by_height(engine, "div", 180.0f, 350.0f, &x, &stretch_source) == 0);
    YTEST_CHECK_STR_EQ(test, stretch_source ? stretch_source : "(none)", "abs-stretch");

    /* replaced <img height:44> -> img (documented author-vs-intrinsic limitation). */
    const char *image_source = NULL;
    YTEST_CHECK(test, find_box_by_height(engine, "img", 44.0f, 350.0f, &x, &image_source) == 0);
    YTEST_CHECK_STR_EQ(test, image_source ? image_source : "(none)", "img");

    yetty_ylexbor_destroy(engine);
}

/* A percentage MAIN-axis height on a COLUMN flex item resolves only against a
 * DEFINITE container main size, and the provenance must reflect which path ran:
 *   - INDEFINITE (auto) column  -> the percentage behaves as auto -> "content"
 *     (it must NOT collapse to 0 against the 0 main budget)
 *   - DEFINITE (author height)   -> resolved percentage -> "css"
 *   - EXPLICIT height:0 column   -> still DEFINITE: resolves against 0, a
 *     content-box child adds its own P+B -> "css" (must NOT expand to content)
 * Definiteness is derived from provenance, not the number, so an explicit
 * height:0 (numeric zero but definite) stays "css" and does not become auto —
 * the same explicit-zero-is-definite invariant the flex/grid/table/abspos sites
 * hold. The percentage child is a <section> so it is located unambiguously by
 * tag; the three cases use distinct heights (53/200/24). */
static void test_flex_column_pct_height_provenance(struct ytest *test)
{
    static const char html[] =
        "<html><body>"
        /* 1 INDEFINITE (auto) column: height:100% -> auto -> content 53. */
        "<div style='display:flex;flex-direction:column;width:300px'>"
        "<section style='height:100%'><div style='height:53px'></div></section></div>"
        /* 2 DEFINITE-200 column: height:100% resolves against 200 -> css. */
        "<div style='display:flex;flex-direction:column;width:300px;height:200px'>"
        "<section style='height:100%'><div style='height:53px'></div></section></div>"
        /* 3 EXPLICIT height:0 column (definite): content-box height:100% resolves
         * against 0, adds own P16 + B8 -> 24 border box, css (NOT content 53). */
        "<div style='display:flex;flex-direction:column;width:300px;height:0'>"
        "<section style='box-sizing:content-box;height:100%;padding:8px 0;"
        "border-top:4px solid #333;border-bottom:4px solid #333'>"
        "<div style='height:53px'></div></section></div>"
        "</body></html>";
    struct yetty_ylexbor *engine = load(test, html);

    /* 1: auto column % height falls back to content (53). This is the fix: an
     * indefinite column main size must not collapse the percentage to 0. */
    const char *auto_source = height_source_of(engine, "section", 53.0f);
    YTEST_CHECK_STR_EQ(test, auto_source ? auto_source : "(none)", "content");

    /* 2: definite column resolves the percentage to 200 -> css (not auto). */
    const char *definite_source = height_source_of(engine, "section", 200.0f);
    YTEST_CHECK_STR_EQ(test, definite_source ? definite_source : "(none)", "css");

    /* 3: explicit height:0 is definite -> percentage resolves against 0, own
     * P+B -> 24, css. Must NOT expand to the 53px content. */
    float zero_x = -1.0f;
    const char *zero_source = NULL;
    YTEST_CHECK(test,
                find_box_by_height(engine, "section", 24.0f, 0.0f, &zero_x, &zero_source) == 0);
    YTEST_CHECK_STR_EQ(test, zero_source ? zero_source : "(none)", "css");

    yetty_ylexbor_destroy(engine);
}

int main(void)
{
    struct ytest test = ytest_begin("ybrowser_provenance");
    YTEST_RUN(&test, test_grid_height_provenance);
    YTEST_RUN(&test, test_named_grid_placement);
    YTEST_RUN(&test, test_table_cell_stretch_provenance);
    YTEST_RUN(&test, test_abs_height_provenance);
    YTEST_RUN(&test, test_flex_column_pct_height_provenance);
    return ytest_end(&test);
}
