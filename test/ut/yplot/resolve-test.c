/*
 * yplot resolver contract test — pure, headless (GPU-less yetty_yplot_core).
 *
 * Exercises yetty_yplot_resolve(): the shared boundary that merges a parsed
 * plot expression with presence-aware caller overrides under an explicit
 * precedence, and validates figure/axis/curve attributes over the whole
 * parsed expression (order-independent). No emission, no GPU.
 */

#include <yetty/yexpr/yexpr.h>
#include <yetty/yplot/resolve.h>

#include "ytest.h"

#include <math.h>
#include <string.h>

/* Parse + resolve, reporting only success/failure (for validation cases —
 * never inspects borrowed title/label pointers, so no lifetime concern). */
static int resolves_ok(const char *source, enum yetty_yplot_attr_precedence precedence)
{
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot(source, strlen(source), &arena);
    if (YETTY_IS_ERR(parse_res)) {
        yetty_ycore_error_destroy(parse_res.error);
        return 0;
    }
    struct yetty_yplot_resolved resolved;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(&parse_res.value, NULL, precedence, &resolved);
    if (YETTY_IS_ERR(resolve_res)) {
        yetty_ycore_error_destroy(resolve_res.error);
        return 0;
    }
    return 1;
}

/* Resolve a parsed expression with explicit options, reporting only whether it
 * errored (and freeing the error chain). */
static int resolve_with_options_fails(const struct yetty_yexpr_plot_expr *expression,
                                      const struct yetty_yplot_options *options,
                                      enum yetty_yplot_attr_precedence precedence)
{
    struct yetty_yplot_resolved resolved;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(expression, options, precedence, &resolved);
    if (YETTY_IS_ERR(resolve_res)) {
        yetty_ycore_error_destroy(resolve_res.error);
        return 1;
    }
    return 0;
}

/* Defaults: an expression with no figure attrs resolves to the documented
 * defaults (decorations on, linear axes, auto legend, viridis, geometry unset). */
static void test_resolve_defaults(struct ytest *test)
{
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot("f = sin(x)", strlen("f = sin(x)"), &arena);
    YTEST_REQUIRE_OK(test, parse_res);

    struct yetty_yplot_resolved resolved;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(&parse_res.value, NULL, YETTY_YPLOT_EXPRESSION_WINS, &resolved);
    YTEST_REQUIRE(test, YETTY_IS_OK(resolve_res));

    YTEST_CHECK(test, resolved.grid && resolved.axes && resolved.labels);
    YTEST_CHECK(test, !resolved.x_log && !resolved.y_log);
    YTEST_CHECK_EQ_INT(test, resolved.legend, YETTY_YPLOT_LEGEND_AUTO);
    YTEST_CHECK_EQ_INT(test, resolved.colormap, YETTY_YPLOT_COLORMAP_VIRIDIS);
    YTEST_CHECK(test, !resolved.has_width && !resolved.has_height && !resolved.has_field);
}

/* Precedence: when the DSL and the caller both name a property, the chosen
 * policy decides the winner — including a real "grid off" override that a
 * zero/default-based config could never express. */
static void test_resolve_precedence(struct ytest *test)
{
    struct yetty_yexpr_arena arena;
    const char *source = "f = sin(x); @plot.width = 640; @plot.grid = on";
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot(source, strlen(source), &arena);
    YTEST_REQUIRE_OK(test, parse_res);

    struct yetty_yplot_options options = {0};
    options.present = YETTY_YPLOT_OPT_WIDTH | YETTY_YPLOT_OPT_GRID;
    options.width = 100.0f;
    options.grid = false;

    struct yetty_yplot_resolved resolved;

    struct yetty_ycore_void_result expr_wins =
        yetty_yplot_resolve(&parse_res.value, &options, YETTY_YPLOT_EXPRESSION_WINS, &resolved);
    YTEST_REQUIRE(test, YETTY_IS_OK(expr_wins));
    YTEST_CHECK(test, resolved.has_width);
    YTEST_CHECK_NEAR(test, resolved.width, 640.0f, 1e-3);
    YTEST_CHECK(test, resolved.grid == true);

    struct yetty_ycore_void_result config_wins =
        yetty_yplot_resolve(&parse_res.value, &options, YETTY_YPLOT_CONFIG_WINS, &resolved);
    YTEST_REQUIRE(test, YETTY_IS_OK(config_wins));
    YTEST_CHECK_NEAR(test, resolved.width, 100.0f, 1e-3);
    YTEST_CHECK(test, resolved.grid == false);
}

/* Presence: an option absent from the mask never touches the resolved value —
 * the DSL (or default) stands. */
static void test_resolve_presence(struct ytest *test)
{
    struct yetty_yexpr_arena arena;
    const char *source = "f = sin(x); @plot.height = 320";
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot(source, strlen(source), &arena);
    YTEST_REQUIRE_OK(test, parse_res);

    /* Options carry a width but NOT a height; height must come from the DSL. */
    struct yetty_yplot_options options = {0};
    options.present = YETTY_YPLOT_OPT_WIDTH;
    options.width = 200.0f;

    struct yetty_yplot_resolved resolved;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(&parse_res.value, &options, YETTY_YPLOT_CONFIG_WINS, &resolved);
    YTEST_REQUIRE(test, YETTY_IS_OK(resolve_res));
    YTEST_CHECK(test, resolved.has_width);
    YTEST_CHECK_NEAR(test, resolved.width, 200.0f, 1e-3);
    YTEST_CHECK(test, resolved.has_height);
    YTEST_CHECK_NEAR(test, resolved.height, 320.0f, 1e-3);
}

/* Validation: unknown reserved / curve / target attributes and bad keywords are
 * rejected under both precedence policies; a valid attribute preceding its
 * curve declaration is accepted (order-independent). */
static void test_resolve_validation(struct ytest *test)
{
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @plot.bogus=1", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @f.bogus=1", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @z.color=#ffffff", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @plot.legend=nope", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @x.scale=diagonal", YETTY_YPLOT_EXPRESSION_WINS));

    /* Same verdict regardless of precedence. */
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @plot.bogus=1", YETTY_YPLOT_CONFIG_WINS));

    /* Declaration-order independence: @f.color before `f=` is valid. */
    YTEST_CHECK(test, resolves_ok("@f.color=#6BA892; f=sin(x)", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, resolves_ok("f=sin(x); @f.color=#6BA892", YETTY_YPLOT_EXPRESSION_WINS));
}

/* Caller options are validated as strictly as expression attributes (the
 * facade passes them directly): bad dimensions/ranges/enums/presence bits and
 * an invalid precedence value are all rejected; a clean options set resolves. */
static void test_resolve_option_validation(struct ytest *test)
{
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot("f = sin(x)", strlen("f = sin(x)"), &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_plot_expr *expr = &parse_res.value;

    struct yetty_yplot_options bad;

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_WIDTH;
    bad.width = -5.0f; /* non-positive dimension */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_HEIGHT;
    bad.height = 0.0f; /* zero dimension */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_X_RANGE;
    bad.x_min = 5.0f; /* reversed range */
    bad.x_max = 1.0f;
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_LEGEND;
    bad.legend = (enum yetty_yplot_legend_mode)99; /* out-of-range enum */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = 1u << 30; /* unknown presence bit */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_WIDTH;
    bad.width = NAN; /* non-finite dimension */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_WIDTH;
    bad.width = INFINITY;
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_Y_RANGE; /* reversed y range */
    bad.y_min = 2.0f;
    bad.y_max = -2.0f;
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_FIELD; /* reversed field range */
    bad.field_min = 3.0f;
    bad.field_max = 1.0f;
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    bad = (struct yetty_yplot_options){0};
    bad.present = YETTY_YPLOT_OPT_COLORMAP;
    bad.colormap = (enum yetty_yplot_colormap)42; /* out-of-range enum */
    YTEST_CHECK(test, resolve_with_options_fails(expr, &bad, YETTY_YPLOT_CONFIG_WINS));

    struct yetty_yplot_options empty = {0};
    YTEST_CHECK(test, resolve_with_options_fails(expr, &empty,
                                                 (enum yetty_yplot_attr_precedence)7));

    struct yetty_yplot_options good = {0};
    good.present = YETTY_YPLOT_OPT_WIDTH | YETTY_YPLOT_OPT_X_RANGE;
    good.width = 320.0f;
    good.x_min = -1.0f;
    good.x_max = 1.0f;
    YTEST_CHECK(test, !resolve_with_options_fails(expr, &good, YETTY_YPLOT_CONFIG_WINS));
}

/* Curve color values are validated with the emission parser — an unparseable
 * color errors in the resolver instead of silently becoming a palette default. */
static void test_resolve_curve_color(struct ytest *test)
{
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @f.color=garbage", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, resolves_ok("f=sin(x); @f.color=#6BA892", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, resolves_ok("f=sin(x); @f.color=#FF0000", YETTY_YPLOT_EXPRESSION_WINS));
}

/* A failed resolve leaves the caller's output object untouched (transactional). */
static void test_resolve_transactional(struct ytest *test)
{
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot("f = sin(x)", strlen("f = sin(x)"), &arena);
    YTEST_REQUIRE_OK(test, parse_res);

    struct yetty_yplot_resolved resolved, sentinel;
    memset(&resolved, 0xAB, sizeof(resolved));
    memcpy(&sentinel, &resolved, sizeof(sentinel));

    struct yetty_yplot_options bad = {0};
    bad.present = YETTY_YPLOT_OPT_WIDTH;
    bad.width = -1.0f;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(&parse_res.value, &bad, YETTY_YPLOT_CONFIG_WINS, &resolved);
    YTEST_REQUIRE(test, YETTY_IS_ERR(resolve_res));
    yetty_ycore_error_destroy(resolve_res.error);

    /* The whole output object is byte-for-byte untouched. */
    YTEST_CHECK(test, memcmp(&resolved, &sentinel, sizeof(resolved)) == 0);
}

/* Merged domain ranges are validated regardless of source: a reversed
 * expression range or @view is rejected, and a CONFIG_WINS override can correct
 * one that the expression got wrong. */
static void test_resolve_range_validation(struct ytest *test)
{
    YTEST_CHECK(test, !resolves_ok("f=sin(x); x=5..1", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); y=3..-3", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, !resolves_ok("f=sin(x); @view=5..1, -1..1", YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, resolves_ok("f=sin(x); x=-3..3; y=-1..1", YETTY_YPLOT_EXPRESSION_WINS));

    struct yetty_yexpr_arena arena;
    const char *source = "f=sin(x); x=5..1";
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot(source, strlen(source), &arena);
    YTEST_REQUIRE_OK(test, parse_res);

    struct yetty_yplot_options fix = {0};
    fix.present = YETTY_YPLOT_OPT_X_RANGE;
    fix.x_min = -1.0f;
    fix.x_max = 1.0f;

    struct yetty_yplot_resolved resolved;
    /* CONFIG_WINS: the valid option range overwrites the reversed expression. */
    struct yetty_ycore_void_result corrected =
        yetty_yplot_resolve(&parse_res.value, &fix, YETTY_YPLOT_CONFIG_WINS, &resolved);
    YTEST_CHECK(test, YETTY_IS_OK(corrected));
    /* EXPRESSION_WINS: the reversed expression range stands and is rejected. */
    YTEST_CHECK(test, resolve_with_options_fails(&parse_res.value, &fix, YETTY_YPLOT_EXPRESSION_WINS));
}

/* Field bounds require a strict range; equal bounds are rejected (not passed
 * through as a degenerate explicit range the shader would treat as unset). */
static void test_resolve_field_bounds(struct ytest *test)
{
    YTEST_CHECK(test, !resolves_ok("f=sin(x)*cos(y); x=-2..2; y=-2..2; @plot.field=2..2",
                                   YETTY_YPLOT_EXPRESSION_WINS));
    YTEST_CHECK(test, resolves_ok("f=sin(x)*cos(y); x=-2..2; y=-2..2; @plot.field=-1..1",
                                  YETTY_YPLOT_EXPRESSION_WINS));

    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot("f = sin(x)", strlen("f = sin(x)"), &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    struct yetty_yplot_options equal_field = {0};
    equal_field.present = YETTY_YPLOT_OPT_FIELD;
    equal_field.field_min = 2.0f;
    equal_field.field_max = 2.0f;
    YTEST_CHECK(test, resolve_with_options_fails(&parse_res.value, &equal_field,
                                                 YETTY_YPLOT_CONFIG_WINS));
}

int main(void)
{
    struct ytest test = ytest_begin("yplot_resolve");
    YTEST_RUN(&test, test_resolve_defaults);
    YTEST_RUN(&test, test_resolve_precedence);
    YTEST_RUN(&test, test_resolve_presence);
    YTEST_RUN(&test, test_resolve_validation);
    YTEST_RUN(&test, test_resolve_option_validation);
    YTEST_RUN(&test, test_resolve_curve_color);
    YTEST_RUN(&test, test_resolve_transactional);
    YTEST_RUN(&test, test_resolve_range_validation);
    YTEST_RUN(&test, test_resolve_field_bounds);
    return ytest_end(&test);
}
