/*
 * ychart parser + render golden test.
 *
 * Pins the low deterministic layer: sniff/parse a valid chart document, render
 * it to a non-empty ydraw buffer, reject malformed input, and prove the render
 * is byte-deterministic (same input → identical output). No GPU/display.
 */

#include <yetty/ychart/chart-ir.h>
#include <yetty/ychart/data-parser.h>
#include <yetty/ychart/renderer.h>
#include <yetty/ydraw-core/drawable-list.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char k_csv[] = "#ychart type=column title=\"Q1 Sales\"\n"
                            "Region,Amount\n"
                            "North,100\n"
                            "South,80\n"
                            "East,120\n";

/* Trivial deterministic text measurement (no font backend needed). */
static float measure(const char *text, size_t text_len, float font_size, void *userdata)
{
    (void)text;
    (void)userdata;
    return 0.6f * font_size * (float)text_len;
}

/* Parse k_csv and render into a fresh buffer; returns the buffer (caller
 * destroys) or aborts the test on failure. */
static struct yetty_ydraw_drawable_list *render_csv(struct ytest *test)
{
    struct yetty_ychart_chart chart;
    YTEST_REQUIRE_OK(test, yetty_ychart_chart_init(&chart));
    struct yetty_ycore_void_result pr = yetty_ychart_parse(k_csv, sizeof(k_csv) - 1, NULL, &chart);
    YTEST_REQUIRE_OK(test, pr);

    struct yetty_ydraw_drawable_list_result buf =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf);
    struct yetty_ychart_render_options opts = yetty_ychart_default_render_options();
    struct yetty_ycore_void_result rr =
        yetty_ychart_render(&chart, buf.value, &opts, measure, NULL);
    YTEST_REQUIRE_OK(test, rr);
    yetty_ychart_chart_destroy(&chart);
    return buf.value;
}

static void test_can_parse(struct ytest *test)
{
    YTEST_CHECK(test, yetty_ychart_can_parse(k_csv, sizeof(k_csv) - 1));
    YTEST_CHECK(test, !yetty_ychart_can_parse("just plain prose", 16));
}

static void test_valid_render_nonempty(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *buf = render_csv(test);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(buf) > 0);
    yetty_ydraw_drawable_list_destroy(buf);
}

static void test_malformed_rejected(struct ytest *test)
{
    /* A JSON chart doc with truncated body — a real parse error, not a crash. */
    const char *bad = "{\"chart\":\"pie\",\"data\":{\"A\":30,";
    struct yetty_ychart_chart chart;
    YTEST_REQUIRE_OK(test, yetty_ychart_chart_init(&chart));
    struct yetty_ycore_void_result pr = yetty_ychart_parse(bad, strlen(bad), NULL, &chart);
    YTEST_CHECK_ERR(test, pr);
    yetty_ychart_chart_destroy(&chart);
}

static void test_deterministic(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *a = render_csv(test);
    struct yetty_ydraw_drawable_list *b = render_csv(test);
    size_t sa = yetty_ydraw_drawable_list_size(a);
    size_t sb = yetty_ydraw_drawable_list_size(b);
    YTEST_REQUIRE_EQ_SIZE(test, sa, sb);
    YTEST_CHECK(test, sa > 0);
    YTEST_CHECK_MEM_EQ(test, yetty_ydraw_drawable_list_data(a), yetty_ydraw_drawable_list_data(b),
                       sa);
    yetty_ydraw_drawable_list_destroy(a);
    yetty_ydraw_drawable_list_destroy(b);
}

int main(void)
{
    struct ytest test = ytest_begin("ychart_parse");
    YTEST_RUN(&test, test_can_parse);
    YTEST_RUN(&test, test_valid_render_nonempty);
    YTEST_RUN(&test, test_malformed_rejected);
    YTEST_RUN(&test, test_deterministic);
    return ytest_end(&test);
}
