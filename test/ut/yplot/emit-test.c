/*
 * yplot emit_into contract test — pure, headless (GPU-less yetty_yplot_core).
 *
 * Locks down the new public emission primitive yetty_yplot_emit_into(): input
 * validation, emitting into an existing list (append, not replace), origin
 * handling, and the log-range failure/partial-list behavior. Records are
 * inspected via the list's serialized byte size (no prim-count API); the deeper
 * derived-flag and record-level assertions land with the shared plan builder in
 * the frontend-unification step.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yplot/resolve.h>

#include "ytest.h"

#include <math.h>
#include <string.h>

/* A minimal, serializable plan: a curve-less plot (grid/axes only) at a known
 * size. Enough to exercise emit_into's contract without the yfsvm pipeline. */
static struct yetty_yplot_render_plan minimal_plan(void)
{
    struct yetty_yplot_render_plan plan = {0};
    plan.uniforms.bounds_w = 400.0f;
    plan.uniforms.bounds_h = 200.0f;
    plan.uniforms.x_min = -1.0f;
    plan.uniforms.x_max = 1.0f;
    plan.uniforms.y_min = -1.0f;
    plan.uniforms.y_max = 1.0f;
    plan.uniforms.flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES;
    plan.uniforms.function_count = 0;
    plan.buffers.bytecode = NULL;
    plan.buffers.bytecode_len = 0;
    plan.buffers.data = NULL;
    plan.buffers.data_count = 0;
    plan.legend_count = 0;
    return plan;
}

static int emit_fails(const struct yetty_yplot_render_plan *plan,
                      struct yetty_ydraw_drawable_list *dest, float origin_x, float origin_y)
{
    struct yetty_ycore_void_result res = yetty_yplot_emit_into(plan, dest, origin_x, origin_y);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
        return 1;
    }
    return 0;
}

/* emit_into APPENDS into an existing list: prior records survive and the plot
 * adds bytes. */
static void test_emit_into_appends(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();
    struct yetty_ycore_void_result first = yetty_yplot_emit_into(&plan, dest, 0.0f, 0.0f);
    YTEST_REQUIRE(test, YETTY_IS_OK(first));
    size_t after_first = yetty_ydraw_drawable_list_size(dest);
    YTEST_CHECK(test, after_first > 0);

    /* A second emit into the same list must not shrink or replace the first. */
    struct yetty_ycore_void_result second = yetty_yplot_emit_into(&plan, dest, 0.0f, 0.0f);
    YTEST_REQUIRE(test, YETTY_IS_OK(second));
    size_t after_second = yetty_ydraw_drawable_list_size(dest);
    YTEST_CHECK(test, after_second > after_first);

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* Input validation rejects bad plans/args before mutating dest. */
static void test_emit_into_validation(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();

    YTEST_CHECK(test, emit_fails(NULL, dest, 0.0f, 0.0f));
    YTEST_CHECK(test, emit_fails(&plan, NULL, 0.0f, 0.0f));
    YTEST_CHECK(test, emit_fails(&plan, dest, INFINITY, 0.0f));   /* non-finite origin */
    YTEST_CHECK(test, emit_fails(&plan, dest, 0.0f, NAN));

    struct yetty_yplot_render_plan bad_dims = minimal_plan();
    bad_dims.uniforms.bounds_w = 0.0f; /* non-positive figure dimension */
    YTEST_CHECK(test, emit_fails(&bad_dims, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan bad_buffers = minimal_plan();
    bad_buffers.buffers.data = NULL;
    bad_buffers.buffers.data_count = 3; /* count > 0 with NULL data */
    YTEST_CHECK(test, emit_fails(&bad_buffers, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan bad_bytecode = minimal_plan();
    bad_bytecode.buffers.bytecode = NULL;
    bad_bytecode.buffers.bytecode_len = 4; /* len > 0 with NULL bytecode */
    YTEST_CHECK(test, emit_fails(&bad_bytecode, dest, 0.0f, 0.0f));

    struct yetty_yplot_data_buffer null_samples = {0};
    null_samples.samples = NULL;
    null_samples.count = 5; /* per-buffer count > 0 with NULL samples */
    struct yetty_yplot_render_plan bad_databuf = minimal_plan();
    bad_databuf.buffers.data = &null_samples;
    bad_databuf.buffers.data_count = 1;
    YTEST_CHECK(test, emit_fails(&bad_databuf, dest, 0.0f, 0.0f));

    struct yetty_yplot_render_plan big_legend = minimal_plan();
    big_legend.legend_count = 10000u; /* over capacity -> reject, not clamp */
    YTEST_CHECK(test, emit_fails(&big_legend, dest, 0.0f, 0.0f));

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* A log axis over a non-positive range is rejected at the shared choke point. */
static void test_emit_into_log_range(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_ydraw_drawable_list *dest = list_res.value;

    struct yetty_yplot_render_plan plan = minimal_plan();
    plan.uniforms.flags |= YETTY_YPLOT_FLAG_XLOG; /* x range spans <= 0 */
    YTEST_CHECK(test, emit_fails(&plan, dest, 0.0f, 0.0f));

    (void)yetty_ydraw_drawable_list_destroy(dest);
}

/* The shared expression path (yetty_yplot_emit_expression) is deterministic and
 * origin-consistent — the parity guarantee that yecho / yplot-yaml / the shell
 * render identically when they hand it the same source + config. */
static void test_emit_expression_shared_path(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 400.0f,
        .bounds_h = 200.0f,
        .x_min = -3.14159f,
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    const char *source = "f=sin(x); g=cos(x); @plot.title=\"t\"; @plot.legend=on";
    size_t source_len = strlen(source);

    struct yetty_ydraw_drawable_list_result la =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    struct yetty_ydraw_drawable_list_result lb =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, la);
    YTEST_REQUIRE_OK(test, lb);

    float figure_w = 0.0f, figure_h = 0.0f;
    YTEST_REQUIRE(test, YETTY_IS_OK(yetty_yplot_emit_expression(
                            source, source_len, NULL, 0, &config, la.value, 0.0f, 0.0f,
                            &figure_w, &figure_h)));
    YTEST_REQUIRE(test, YETTY_IS_OK(yetty_yplot_emit_expression(
                            source, source_len, NULL, 0, &config, lb.value, 0.0f, 0.0f, NULL,
                            NULL)));

    size_t size_a = yetty_ydraw_drawable_list_size(la.value);
    size_t size_b = yetty_ydraw_drawable_list_size(lb.value);
    YTEST_CHECK(test, size_a > 0 && size_a == size_b);
    YTEST_CHECK(test, memcmp(yetty_ydraw_drawable_list_data(la.value),
                             yetty_ydraw_drawable_list_data(lb.value), size_a) == 0);
    YTEST_CHECK_NEAR(test, figure_w, 400.0f, 1e-3);
    YTEST_CHECK_NEAR(test, figure_h, 200.0f, 1e-3);

    /* Emitting at a non-zero origin shifts coordinates but not the byte size. */
    struct yetty_ydraw_drawable_list_result lc =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, lc);
    YTEST_REQUIRE(test, YETTY_IS_OK(yetty_yplot_emit_expression(
                            source, source_len, NULL, 0, &config, lc.value, 100.0f, 50.0f, NULL,
                            NULL)));
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(lc.value) == size_a);

    (void)yetty_ydraw_drawable_list_destroy(la.value);
    (void)yetty_ydraw_drawable_list_destroy(lb.value);
    (void)yetty_ydraw_drawable_list_destroy(lc.value);
}

/* A plot that declares only a data buffer and no function renders the buffer as
 * a curve (compiling a zero-function program is skipped, not treated as an
 * error). Regression pin for the buffer-language demo. */
static void test_emit_expression_buffer_only(struct ytest *test)
{
    struct yetty_yplot_render_config config = {
        .bounds_w = 400.0f,
        .bounds_h = 200.0f,
        .x_min = 0.0f,
        .x_max = 1.0f,
        .y_min = -1.0f,
        .y_max = 1.0f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    const char *source = "data=buffer; @data.size=8; @data.values=0,0.3,0.6,0.9,0.6,0,-0.4,-0.2";

    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, list);
    struct yetty_ycore_void_result emit_res = yetty_yplot_emit_expression(
        source, strlen(source), NULL, 0, &config, list.value, 0.0f, 0.0f, NULL, NULL);
    YTEST_CHECK(test, YETTY_IS_OK(emit_res));
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_error_destroy(emit_res.error);
    }
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(list.value) > 0);
    (void)yetty_ydraw_drawable_list_destroy(list.value);
}

int main(void)
{
    struct ytest test = ytest_begin("yplot_emit");
    YTEST_RUN(&test, test_emit_into_appends);
    YTEST_RUN(&test, test_emit_into_validation);
    YTEST_RUN(&test, test_emit_into_log_range);
    YTEST_RUN(&test, test_emit_expression_shared_path);
    YTEST_RUN(&test, test_emit_expression_buffer_only);
    return ytest_end(&test);
}
