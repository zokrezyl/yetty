/*
 * yrich geometry + style contract test (#416) — pure, headless.
 *
 * yrich's document/spreadsheet model is a yclass object graph that needs a
 * context, but its geometry and style primitives are header-only static-inline
 * helpers with no dependencies. This pins those: rect containment (half-open)
 * and intersection, spreadsheet cell-address equality and range containment
 * (inclusive), the default text style, and the ABGR colour packing.
 */

#include <yetty/yrich/yrich-types.h>

#include "ytest.h"

#include <stdint.h>

/*---------------------------------------------------------------------------
 * Rect containment is half-open: the min edge is inside, the max edge is not.
 *-------------------------------------------------------------------------*/
static void test_rect_contains(struct ytest *test)
{
    struct yetty_yrich_rect r = {.x = 10.0f, .y = 20.0f, .w = 30.0f, .h = 40.0f};

    YTEST_CHECK(test, yetty_yrich_rect_contains(&r, 25.0f, 40.0f));  /* interior */
    YTEST_CHECK(test, yetty_yrich_rect_contains(&r, 10.0f, 20.0f));  /* min corner: inside */
    YTEST_CHECK(test, !yetty_yrich_rect_contains(&r, 40.0f, 60.0f)); /* max corner: outside */
    YTEST_CHECK(test, !yetty_yrich_rect_contains(&r, 5.0f, 30.0f));  /* left of x */
    YTEST_CHECK(test, !yetty_yrich_rect_contains(&r, 25.0f, 65.0f)); /* below y+h */
}

/*---------------------------------------------------------------------------
 * Rect intersection is strict: sharing only an edge does not intersect.
 *-------------------------------------------------------------------------*/
static void test_rect_intersects(struct ytest *test)
{
    struct yetty_yrich_rect a = {.x = 0.0f, .y = 0.0f, .w = 10.0f, .h = 10.0f};
    struct yetty_yrich_rect overlap = {.x = 5.0f, .y = 5.0f, .w = 10.0f, .h = 10.0f};
    struct yetty_yrich_rect inside = {.x = 2.0f, .y = 2.0f, .w = 2.0f, .h = 2.0f};
    struct yetty_yrich_rect edge = {.x = 10.0f, .y = 0.0f, .w = 5.0f, .h = 5.0f};
    struct yetty_yrich_rect apart = {.x = 100.0f, .y = 100.0f, .w = 5.0f, .h = 5.0f};

    YTEST_CHECK(test, yetty_yrich_rect_intersects(&a, &overlap));
    YTEST_CHECK(test, yetty_yrich_rect_intersects(&a, &inside));
    YTEST_CHECK(test, !yetty_yrich_rect_intersects(&a, &edge)); /* touches at x==10 only */
    YTEST_CHECK(test, !yetty_yrich_rect_intersects(&a, &apart));
}

/*---------------------------------------------------------------------------
 * Cell-address equality and inclusive range containment.
 *-------------------------------------------------------------------------*/
static void test_cell_addressing(struct ytest *test)
{
    struct yetty_yrich_cell_addr a = {.row = 3, .col = 4};
    struct yetty_yrich_cell_addr same = {.row = 3, .col = 4};
    struct yetty_yrich_cell_addr diff_row = {.row = 9, .col = 4};
    struct yetty_yrich_cell_addr diff_col = {.row = 3, .col = 9};

    YTEST_CHECK(test, yetty_yrich_cell_addr_eq(a, same));
    YTEST_CHECK(test, !yetty_yrich_cell_addr_eq(a, diff_row));
    YTEST_CHECK(test, !yetty_yrich_cell_addr_eq(a, diff_col));

    /* Range B2:D5 (rows 1..4, cols 1..3), inclusive on both ends. */
    struct yetty_yrich_cell_range range = {.start = {.row = 1, .col = 1},
                                           .end = {.row = 4, .col = 3}};
    struct yetty_yrich_cell_addr inside = {.row = 2, .col = 2};
    struct yetty_yrich_cell_addr top_left = {.row = 1, .col = 1};     /* inclusive start */
    struct yetty_yrich_cell_addr bottom_right = {.row = 4, .col = 3}; /* inclusive end */
    struct yetty_yrich_cell_addr outside = {.row = 5, .col = 3};

    YTEST_CHECK(test, yetty_yrich_cell_range_contains(&range, inside));
    YTEST_CHECK(test, yetty_yrich_cell_range_contains(&range, top_left));
    YTEST_CHECK(test, yetty_yrich_cell_range_contains(&range, bottom_right));
    YTEST_CHECK(test, !yetty_yrich_cell_range_contains(&range, outside));
}

/*---------------------------------------------------------------------------
 * Default text style and ABGR colour packing.
 *-------------------------------------------------------------------------*/
static void test_style_and_color(struct ytest *test)
{
    struct yetty_yrich_text_style s = yetty_yrich_text_style_default();
    YTEST_CHECK_NEAR(test, s.font_size, 14.0f, 0.001f);
    YTEST_CHECK_EQ_INT(test, s.format, YETTY_YRICH_FMT_NONE);
    YTEST_CHECK_EQ_INT(test, s.font_id, 0);
    YTEST_CHECK(test, s.color == YETTY_YRICH_COLOR_BLACK);
    YTEST_CHECK(test, s.bg_color == YETTY_YRICH_COLOR_TRANSPARENT);

    /* RGBA(r,g,b,a) packs to (A<<24)|(B<<16)|(G<<8)|R. Opaque red. */
    YTEST_CHECK(test, YETTY_YRICH_RGBA(255, 0, 0, 255) == 0xFF0000FFu);
    YTEST_CHECK(test, YETTY_YRICH_COLOR_BLACK == 0xFF000000u);
    YTEST_CHECK(test, YETTY_YRICH_COLOR_WHITE == 0xFFFFFFFFu);
    YTEST_CHECK(test, YETTY_YRICH_COLOR_TRANSPARENT == 0x00000000u);

    /* Format flags are distinct bits. */
    YTEST_CHECK_EQ_INT(test, YETTY_YRICH_FMT_BOLD | YETTY_YRICH_FMT_ITALIC, 3u);
}

int main(void)
{
    struct ytest test = ytest_begin("yrich_geometry");
    YTEST_RUN(&test, test_rect_contains);
    YTEST_RUN(&test, test_rect_intersects);
    YTEST_RUN(&test, test_cell_addressing);
    YTEST_RUN(&test, test_style_and_color);
    return ytest_end(&test);
}
