/*
 * chart-ir.c — chart IR lifecycle, builders, palette, kind-name mapping.
 *
 * Plain dynamic-array storage. Every `char *` builder argument is copied with
 * a local strdup so callers may pass scratch strings; the chart owns all of
 * its strings and arrays and frees them in chart_destroy.
 */

#include <yetty/ychart/chart-ir.h>

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

/*=============================================================================
 * Small helpers
 *===========================================================================*/

static char *dup_string(const char *src)
{
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

/* Grow a dynamic array to hold at least `needed` elements of `elem_size`.
 * Doubles capacity (min 8). Returns 1 on success, 0 on alloc failure. */
static int grow_array(void **data, size_t *capacity, size_t needed, size_t elem_size)
{
    if (needed <= *capacity) {
        return 1;
    }
    size_t new_cap = *capacity ? *capacity : 8;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    void *grown = realloc(*data, new_cap * elem_size);
    if (!grown) {
        return 0;
    }
    *data = grown;
    *capacity = new_cap;
    return 1;
}

/*=============================================================================
 * Kind ↔ name
 *===========================================================================*/

const char *yetty_ychart_kind_name(enum yetty_ychart_kind kind)
{
    switch (kind) {
    case YETTY_YCHART_KIND_AUTO:
        return "auto";
    case YETTY_YCHART_KIND_BAR:
        return "bar";
    case YETTY_YCHART_KIND_COLUMN:
        return "column";
    case YETTY_YCHART_KIND_LINE:
        return "line";
    case YETTY_YCHART_KIND_AREA:
        return "area";
    case YETTY_YCHART_KIND_SCATTER:
        return "scatter";
    case YETTY_YCHART_KIND_PIE:
        return "pie";
    case YETTY_YCHART_KIND_DONUT:
        return "donut";
    case YETTY_YCHART_KIND_RADAR:
        return "radar";
    case YETTY_YCHART_KIND_TREEMAP:
        return "treemap";
    case YETTY_YCHART_KIND_SANKEY:
        return "sankey";
    }
    return "auto";
}

enum yetty_ychart_kind yetty_ychart_kind_from_name(const char *name)
{
    if (!name) {
        return YETTY_YCHART_KIND_AUTO;
    }
    static const struct {
        const char *name;
        enum yetty_ychart_kind kind;
    } table[] = {
        {"auto", YETTY_YCHART_KIND_AUTO},       {"bar", YETTY_YCHART_KIND_BAR},
        {"hbar", YETTY_YCHART_KIND_BAR},        {"column", YETTY_YCHART_KIND_COLUMN},
        {"col", YETTY_YCHART_KIND_COLUMN},      {"vbar", YETTY_YCHART_KIND_COLUMN},
        {"line", YETTY_YCHART_KIND_LINE},       {"area", YETTY_YCHART_KIND_AREA},
        {"scatter", YETTY_YCHART_KIND_SCATTER}, {"points", YETTY_YCHART_KIND_SCATTER},
        {"pie", YETTY_YCHART_KIND_PIE},         {"donut", YETTY_YCHART_KIND_DONUT},
        {"doughnut", YETTY_YCHART_KIND_DONUT},  {"radar", YETTY_YCHART_KIND_RADAR},
        {"spider", YETTY_YCHART_KIND_RADAR},    {"treemap", YETTY_YCHART_KIND_TREEMAP},
        {"sankey", YETTY_YCHART_KIND_SANKEY},   {"flow", YETTY_YCHART_KIND_SANKEY},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name) == 0) {
            return table[i].kind;
        }
    }
    return YETTY_YCHART_KIND_AUTO;
}

/*=============================================================================
 * Palette / colours
 *===========================================================================*/

const uint32_t *yetty_ychart_palette(size_t *out_count)
{
    /* Brand mint leads, followed by a tasteful qualitative set. Program-
     * lifetime constant table — a `static const` local, not a file-scope
     * symbol. ARGB, fully opaque. */
    static const uint32_t palette[] = {
        0xFF6BA892u, /* brand mint */
        0xFF5B8FF9u, /* blue */
        0xFFF6BD16u, /* amber */
        0xFFE8684Au, /* coral */
        0xFF9270CAu, /* violet */
        0xFF78D3F8u, /* sky */
        0xFFF6903Du, /* orange */
        0xFF008685u, /* teal */
        0xFFF08BB4u, /* pink */
        0xFF65789Bu, /* slate */
    };
    if (out_count) {
        *out_count = sizeof(palette) / sizeof(palette[0]);
    }
    return palette;
}

uint32_t yetty_ychart_resolve_color(const struct yetty_ychart_chart *chart, size_t series_index)
{
    if (chart && series_index < chart->series_count && chart->series[series_index].color != 0) {
        return chart->series[series_index].color;
    }
    size_t count = 0;
    const uint32_t *palette = yetty_ychart_palette(&count);
    return palette[series_index % count];
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_chart_init(struct yetty_ychart_chart *chart)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_void, "chart_init: NULL chart");
    }
    memset(chart, 0, sizeof(*chart));
    chart->kind = YETTY_YCHART_KIND_AUTO;
    chart->show_legend = true;
    chart->show_values = false;
    chart->stacked = false;
    return YETTY_OK_VOID();
}

void yetty_ychart_chart_destroy(struct yetty_ychart_chart *chart)
{
    if (!chart) {
        return;
    }
    free(chart->title);
    free(chart->x_label);
    free(chart->y_label);

    for (size_t i = 0; i < chart->category_count; i++) {
        free(chart->categories[i]);
    }
    free(chart->categories);

    for (size_t i = 0; i < chart->series_count; i++) {
        free(chart->series[i].name);
        free(chart->series[i].values);
        free(chart->series[i].x_values);
    }
    free(chart->series);

    for (size_t i = 0; i < chart->flow_count; i++) {
        free(chart->flows[i].source);
        free(chart->flows[i].target);
    }
    free(chart->flows);

    memset(chart, 0, sizeof(*chart));
}

/*=============================================================================
 * Builders
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_set_title(struct yetty_ychart_chart *chart,
                                                      const char *title)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_void, "set_title: NULL chart");
    }
    char *copy = dup_string(title);
    if (title && !copy) {
        return YETTY_ERR(yetty_ycore_void, "set_title: out of memory");
    }
    free(chart->title);
    chart->title = copy;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_set_axis_labels(struct yetty_ychart_chart *chart,
                                                            const char *x_label,
                                                            const char *y_label)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_void, "set_axis_labels: NULL chart");
    }
    if (x_label) {
        char *copy = dup_string(x_label);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "set_axis_labels: out of memory");
        }
        free(chart->x_label);
        chart->x_label = copy;
    }
    if (y_label) {
        char *copy = dup_string(y_label);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "set_axis_labels: out of memory");
        }
        free(chart->y_label);
        chart->y_label = copy;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_ychart_add_category(struct yetty_ychart_chart *chart,
                                                        const char *label)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_int, "add_category: NULL chart");
    }
    if (!grow_array((void **)&chart->categories, &chart->category_capacity,
                    chart->category_count + 1, sizeof(chart->categories[0]))) {
        return YETTY_ERR(yetty_ycore_int, "add_category: out of memory");
    }
    char *copy = dup_string(label ? label : "");
    if (!copy) {
        return YETTY_ERR(yetty_ycore_int, "add_category: out of memory");
    }
    int index = (int)chart->category_count;
    chart->categories[chart->category_count++] = copy;
    return YETTY_OK(yetty_ycore_int, index);
}

struct yetty_ycore_int_result yetty_ychart_add_series(struct yetty_ychart_chart *chart,
                                                      const char *name, uint32_t color)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_int, "add_series: NULL chart");
    }
    if (!grow_array((void **)&chart->series, &chart->series_capacity, chart->series_count + 1,
                    sizeof(chart->series[0]))) {
        return YETTY_ERR(yetty_ycore_int, "add_series: out of memory");
    }
    struct yetty_ychart_series *series = &chart->series[chart->series_count];
    memset(series, 0, sizeof(*series));
    series->color = color;
    if (name) {
        series->name = dup_string(name);
        if (!series->name) {
            return YETTY_ERR(yetty_ycore_int, "add_series: out of memory");
        }
    }
    int index = (int)chart->series_count;
    chart->series_count++;
    return YETTY_OK(yetty_ycore_int, index);
}

struct yetty_ycore_void_result yetty_ychart_series_push(struct yetty_ychart_chart *chart,
                                                        size_t series_index, double value)
{
    if (!chart || series_index >= chart->series_count) {
        return YETTY_ERR(yetty_ycore_void, "series_push: bad series index");
    }
    struct yetty_ychart_series *series = &chart->series[series_index];
    if (!grow_array((void **)&series->values, &series->value_capacity, series->value_count + 1,
                    sizeof(series->values[0]))) {
        return YETTY_ERR(yetty_ycore_void, "series_push: out of memory");
    }
    series->values[series->value_count++] = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_series_push_xy(struct yetty_ychart_chart *chart,
                                                           size_t series_index, double x, double y)
{
    if (!chart || series_index >= chart->series_count) {
        return YETTY_ERR(yetty_ycore_void, "series_push_xy: bad series index");
    }
    struct yetty_ychart_series *series = &chart->series[series_index];
    /* `values` owns the capacity; `x_values` runs strictly parallel to it. */
    if (!grow_array((void **)&series->values, &series->value_capacity, series->value_count + 1,
                    sizeof(series->values[0]))) {
        return YETTY_ERR(yetty_ycore_void, "series_push_xy: out of memory (y)");
    }
    /* Size x_values to match the (possibly just-grown) capacity. realloc to an
     * unchanged size is a cheap no-op, so calling this each push is fine. */
    double *grown_x = realloc(series->x_values, series->value_capacity * sizeof(double));
    if (!grown_x) {
        return YETTY_ERR(yetty_ycore_void, "series_push_xy: out of memory (x)");
    }
    series->x_values = grown_x;
    series->x_values[series->value_count] = x;
    series->values[series->value_count] = y;
    series->value_count++;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_add_flow(struct yetty_ychart_chart *chart,
                                                     const char *source, const char *target,
                                                     double value)
{
    if (!chart || !source || !target) {
        return YETTY_ERR(yetty_ycore_void, "add_flow: NULL chart/source/target");
    }
    if (!grow_array((void **)&chart->flows, &chart->flow_capacity, chart->flow_count + 1,
                    sizeof(chart->flows[0]))) {
        return YETTY_ERR(yetty_ycore_void, "add_flow: out of memory");
    }
    struct yetty_ychart_flow *flow = &chart->flows[chart->flow_count];
    flow->source = dup_string(source);
    flow->target = dup_string(target);
    flow->value = value;
    if (!flow->source || !flow->target) {
        free(flow->source);
        free(flow->target);
        return YETTY_ERR(yetty_ycore_void, "add_flow: out of memory");
    }
    chart->flow_count++;
    return YETTY_OK_VOID();
}
