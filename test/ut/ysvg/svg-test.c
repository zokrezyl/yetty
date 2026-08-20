/*
 * ysvg parser + render golden test.
 *
 * Renders a valid SVG to a non-empty ydraw buffer, rejects malformed XML, and
 * pins render determinism. Headless (no GPU/display).
 */

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysvg/ysvg.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char k_svg[] = "<svg viewBox=\"0 0 100 100\">"
                            "<rect x=\"10\" y=\"10\" width=\"40\" height=\"30\" fill=\"#3050ff\"/>"
                            "<circle cx=\"70\" cy=\"60\" r=\"20\" fill=\"#ff2020\"/>"
                            "<line x1=\"0\" y1=\"0\" x2=\"100\" y2=\"100\" stroke=\"#000000\"/>"
                            "</svg>";

static void test_valid_render_nonempty(struct ytest *test)
{
    struct yetty_ysvg_render_config cfg = {0};
    struct yetty_ysvg_render_result rr = yetty_ysvg_render(k_svg, sizeof(k_svg) - 1, NULL, 0, &cfg);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_REQUIRE_NOT_NULL(test, rr.value.buffer);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(rr.value.buffer) > 0);
    /* Scene bounds are the viewBox scaled to display px; a 1:1 (square) viewBox
     * yields a square scene. */
    YTEST_CHECK(test, rr.value.scene_width > 0.0f);
    YTEST_CHECK_NEAR(test, rr.value.scene_width, rr.value.scene_height, 1.0f);
    yetty_ydraw_drawable_list_destroy(rr.value.buffer);
}

static void test_malformed_rejected(struct ytest *test)
{
    /* Unclosed tag — an XML parse error, not a crash. */
    const char *bad = "<svg viewBox=\"0 0 10 10\"><circle cx=\"5\" cy=\"5\" r=\"3\"";
    struct yetty_ysvg_render_config cfg = {0};
    struct yetty_ysvg_render_result rr = yetty_ysvg_render(bad, strlen(bad), NULL, 0, &cfg);
    if (YETTY_IS_OK(rr)) {
        /* If a lenient parse still returns a buffer, at least it must not crash;
         * free it and fail the expectation so the behaviour is visible. */
        yetty_ydraw_drawable_list_destroy(rr.value.buffer);
        YTEST_CHECK(test, 0 && "malformed SVG should be rejected");
    } else {
        yetty_ycore_error_destroy(rr.error);
        YTEST_CHECK(test, 1);
    }
}

static void test_deterministic(struct ytest *test)
{
    struct yetty_ysvg_render_config cfg = {0};
    struct yetty_ysvg_render_result a = yetty_ysvg_render(k_svg, sizeof(k_svg) - 1, NULL, 0, &cfg);
    struct yetty_ysvg_render_result b = yetty_ysvg_render(k_svg, sizeof(k_svg) - 1, NULL, 0, &cfg);
    YTEST_REQUIRE_OK(test, a);
    YTEST_REQUIRE_OK(test, b);
    size_t sa = yetty_ydraw_drawable_list_size(a.value.buffer);
    size_t sb = yetty_ydraw_drawable_list_size(b.value.buffer);
    YTEST_REQUIRE_EQ_SIZE(test, sa, sb);
    YTEST_CHECK(test, sa > 0);
    YTEST_CHECK_MEM_EQ(test, yetty_ydraw_drawable_list_data(a.value.buffer),
                       yetty_ydraw_drawable_list_data(b.value.buffer), sa);
    yetty_ydraw_drawable_list_destroy(a.value.buffer);
    yetty_ydraw_drawable_list_destroy(b.value.buffer);
}

/* Regression: XML permits "</name S? >" (ETag ::= '</' Name S? '>'), and
 * Inkscape/Sodipodi exports pretty-print the root closer as "</svg\n   >".
 * Those must parse and render, not fail whole-document. */
static void test_endtag_whitespace_parses(struct ytest *test)
{
    const char *svg = "<svg viewBox=\"0 0 10 10\">"
                      "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"#00ff00\"/>"
                      "</svg\n   >";
    struct yetty_ysvg_render_config cfg = {0};
    struct yetty_ysvg_render_result rr = yetty_ysvg_render(svg, strlen(svg), NULL, 0, &cfg);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(rr.value.buffer) > 0);
    yetty_ydraw_drawable_list_destroy(rr.value.buffer);
}

/* Regression: a malformed <path> must not blank the document — per the SVG
 * error-processing model the sibling <rect> still renders. */
static void test_bad_path_recovers(struct ytest *test)
{
    const char *svg = "<svg viewBox=\"0 0 10 10\">"
                      "<path d=\"M0 0 L\"/>" /* truncated command → skipped */
                      "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"#0000ff\"/>"
                      "</svg>";
    struct yetty_ysvg_render_config cfg = {0};
    struct yetty_ysvg_render_result rr = yetty_ysvg_render(svg, strlen(svg), NULL, 0, &cfg);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(rr.value.buffer) > 0);
    yetty_ydraw_drawable_list_destroy(rr.value.buffer);
}

int main(void)
{
    struct ytest test = ytest_begin("ysvg_parse");
    YTEST_RUN(&test, test_valid_render_nonempty);
    YTEST_RUN(&test, test_malformed_rejected);
    YTEST_RUN(&test, test_deterministic);
    YTEST_RUN(&test, test_endtag_whitespace_parses);
    YTEST_RUN(&test, test_bad_path_recovers);
    return ytest_end(&test);
}
