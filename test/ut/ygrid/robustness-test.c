/*
 * ygrid robustness contract test — malformed command buffers + dump stability.
 *
 * Complements ygrid-wire-test.c (add/group/nested/reopen/delete goldens) by
 * pinning the two properties that test does not: the receiver survives
 * malformed / truncated command bytes and still processes a valid buffer
 * afterwards, and the dump is deterministic across identical inputs and
 * grid instances.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct yetty_ygrid_grid *make_grid(struct ytest *test)
{
    struct yetty_ycore_rectangle rect = {{0, 0}, {100, 100}};
    struct yetty_ygrid_grid_ptr_result r = yetty_ygrid_create(rect, 1, 1, NULL);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static struct yetty_yclass_object *grid_obj(struct yetty_ygrid_grid *grid)
{
    return (struct yetty_yclass_object *)yetty_ygrid_as_figure(grid) - 1;
}

static void destroy_grid(struct yetty_ygrid_grid *grid)
{
    yetty_yfigure_destroy(grid_obj(grid));
}

static struct yetty_ydraw_drawable_list *make_buf(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result r =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void add_box(struct ytest *test, struct yetty_ydraw_drawable_list *buf, float x, float y)
{
    struct yetty_ysdf_box box = {
        .center_x = x, .center_y = y, .half_width = 5, .half_height = 5, .corner_radius = 0};
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_box(
        buf, 0, 0, 0xff00ff00u, 0, 0.0f, &box);
    YTEST_REQUIRE_OK(test, r);
}

/* Feed raw bytes to the grid receiver, returning the Result so the caller can
 * assert on success/error. */
static struct yetty_ycore_void_result feed_bytes(struct yetty_ygrid_grid *grid,
                                                 const uint8_t *bytes, size_t len)
{
    return yetty_yfigure_process_bytes(grid_obj(grid), bytes, len);
}

static char *dump_grid(struct ytest *test, struct yetty_ygrid_grid *grid)
{
    struct yetty_ycore_char_ptr_result r = yetty_yfigure_dump(yetty_ygrid_as_figure(grid), 0);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

/*---------------------------------------------------------------------------
 * Malformed buffers: no crash / no over-read, and recovery afterwards.
 *-------------------------------------------------------------------------*/
static void test_malformed_then_recover(struct ytest *test)
{
    struct yetty_ygrid_grid *grid = make_grid(test);

    /* 1) Unaligned garbage (not even a whole 4-byte word). */
    const uint8_t junk[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03};
    struct yetty_ycore_void_result junk_res = feed_bytes(grid, junk, sizeof(junk));
    if (YETTY_IS_ERR(junk_res)) {
        yetty_ycore_error_destroy(junk_res.error);
    }

    /* 2) A record whose type word claims a box but supplies no fields
     * (truncated mid-record). Exact-size alloc so an over-read trips ASAN. */
    uint32_t *trunc = malloc(sizeof(uint32_t));
    YTEST_REQUIRE_NOT_NULL(test, trunc);
    trunc[0] = YETTY_YSDF_BOX;
    struct yetty_ycore_void_result trunc_res =
        feed_bytes(grid, (const uint8_t *)trunc, sizeof(uint32_t));
    if (YETTY_IS_ERR(trunc_res)) {
        yetty_ycore_error_destroy(trunc_res.error);
    }
    free(trunc);

    /* 3) Recovery: a valid buffer after the garbage still processes, and the
     * grid is intact enough to dump. */
    struct yetty_ydraw_drawable_list *buf = make_buf(test);
    add_box(test, buf, 20, 20);
    struct yetty_ycore_void_result good = feed_bytes(
        grid, yetty_ydraw_drawable_list_data(buf), yetty_ydraw_drawable_list_size(buf));
    YTEST_CHECK_OK(test, good);
    char *dump = dump_grid(test, grid);
    YTEST_CHECK_NOT_NULL(test, dump);
    free(dump);

    yetty_ydraw_drawable_list_destroy(buf);
    destroy_grid(grid);
}

/*---------------------------------------------------------------------------
 * Dump stability: identical inputs → identical dumps, within one grid and
 * across two grid instances.
 *-------------------------------------------------------------------------*/
static void test_dump_stable_across_instances(struct ytest *test)
{
    struct yetty_ygrid_grid *grid_a = make_grid(test);
    struct yetty_ygrid_grid *grid_b = make_grid(test);

    struct yetty_ydraw_drawable_list *buf = make_buf(test);
    add_box(test, buf, 10, 10);
    add_box(test, buf, 30, 40);
    const uint8_t *bytes = yetty_ydraw_drawable_list_data(buf);
    size_t len = yetty_ydraw_drawable_list_size(buf);

    YTEST_REQUIRE_OK(test, feed_bytes(grid_a, bytes, len));
    YTEST_REQUIRE_OK(test, feed_bytes(grid_b, bytes, len));

    char *dump_a = dump_grid(test, grid_a);
    char *dump_b = dump_grid(test, grid_b);
    /* Two independent grids fed identical bytes dump identically. */
    YTEST_CHECK_STR_EQ(test, dump_a, dump_b);
    free(dump_a);
    free(dump_b);

    yetty_ydraw_drawable_list_destroy(buf);
    destroy_grid(grid_a);
    destroy_grid(grid_b);
}

static void test_dump_stable_repeat(struct ytest *test)
{
    struct yetty_ygrid_grid *grid = make_grid(test);
    struct yetty_ydraw_drawable_list *buf = make_buf(test);
    add_box(test, buf, 15, 15);
    YTEST_REQUIRE_OK(test, feed_bytes(grid, yetty_ydraw_drawable_list_data(buf),
                                      yetty_ydraw_drawable_list_size(buf)));

    /* Dumping twice with no intervening mutation yields the same text. */
    char *first = dump_grid(test, grid);
    char *second = dump_grid(test, grid);
    YTEST_CHECK_STR_EQ(test, first, second);
    free(first);
    free(second);

    yetty_ydraw_drawable_list_destroy(buf);
    destroy_grid(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("ygrid_robustness");
    YTEST_RUN(&test, test_malformed_then_recover);
    YTEST_RUN(&test, test_dump_stable_across_instances);
    YTEST_RUN(&test, test_dump_stable_repeat);
    return ytest_end(&test);
}
