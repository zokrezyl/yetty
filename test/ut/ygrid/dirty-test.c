/*
 * ygrid dirty / clear model contract test (#419).
 *
 * Complements ygrid-wire-test.c (entity-tree dump goldens) and robustness-test.c
 * (malformed input + dump stability) by pinning the figure dirty-flag lifecycle
 * that neither touches: a fresh grid is dirty, a processed record re-dirties a
 * clean grid, and clear_local both empties the model and re-dirties it.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/api/yfigure/container.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/ygrid/grid.h>
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

static int grid_dirty(struct ytest *test, struct yetty_ygrid_grid *grid)
{
    struct yetty_ycore_int_result d = yetty_yfigure_figure_dirty_get(grid_obj(grid));
    YTEST_REQUIRE_OK(test, d);
    return d.value;
}

static void clear_dirty(struct ytest *test, struct yetty_ygrid_grid *grid)
{
    YTEST_REQUIRE_OK(test, yetty_yfigure_figure_dirty_set(grid_obj(grid), 0));
}

/* Feed a single SDF box at (x, y) to the grid receiver. */
static void feed_box(struct ytest *test, struct yetty_ygrid_grid *grid, float x, float y)
{
    struct yetty_ydraw_drawable_list_result buf_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, buf_res);
    struct yetty_ydraw_drawable_list *buf = buf_res.value;

    struct yetty_ysdf_box box = {
        .center_x = x, .center_y = y, .half_width = 5, .half_height = 5, .corner_radius = 0};
    YTEST_REQUIRE_OK(
        test, yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, 0, 0xff00ff00u, 0, 0.0f, &box));
    YTEST_REQUIRE_OK(test, yetty_yfigure_process_bytes(grid_obj(grid),
                                                       yetty_ydraw_drawable_list_data(buf),
                                                       yetty_ydraw_drawable_list_size(buf)));
    yetty_ydraw_drawable_list_destroy(buf);
}

static char *dump_grid(struct ytest *test, struct yetty_ygrid_grid *grid)
{
    struct yetty_ycore_char_ptr_result r = yetty_yfigure_dump(yetty_ygrid_as_figure(grid), 0);
    YTEST_REQUIRE_OK(test, r);
    YTEST_REQUIRE_NOT_NULL(test, r.value);
    return r.value;
}

/*---------------------------------------------------------------------------
 * Fresh grid is dirty; a processed record re-dirties a cleaned grid.
 *-------------------------------------------------------------------------*/
static void test_dirty_lifecycle(struct ytest *test)
{
    struct yetty_ygrid_grid *grid = make_grid(test);

    YTEST_CHECK(test, grid_dirty(test, grid)); /* dirty at birth */

    clear_dirty(test, grid);
    YTEST_CHECK(test, !grid_dirty(test, grid));

    feed_box(test, grid, 20, 20);
    YTEST_CHECK(test, grid_dirty(test, grid)); /* a processed record re-dirties */

    destroy_grid(grid);
}

/*---------------------------------------------------------------------------
 * clear_local empties the model (prim_count → 0) and re-dirties the grid.
 *-------------------------------------------------------------------------*/
static void test_clear_local_empties_and_dirties(struct ytest *test)
{
    struct yetty_ygrid_grid *grid = make_grid(test);
    feed_box(test, grid, 30, 30);

    char *before = dump_grid(test, grid);
    YTEST_CHECK(test, strstr(before, "\nprim_count: 1\n") != NULL);
    free(before);

    clear_dirty(test, grid);
    YTEST_REQUIRE_OK(test, yetty_ygrid_clear_local(grid));
    YTEST_CHECK(test, grid_dirty(test, grid)); /* clear re-dirties */

    char *after = dump_grid(test, grid);
    YTEST_CHECK(test, strstr(after, "\nprim_count: 0\n") != NULL);
    free(after);

    destroy_grid(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("ygrid_dirty");
    YTEST_RUN(&test, test_dirty_lifecycle);
    YTEST_RUN(&test, test_clear_local_empties_and_dirties);
    return ytest_end(&test);
}
