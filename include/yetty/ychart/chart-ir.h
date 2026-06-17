#ifndef YETTY_YCHART_CHART_IR_H
#define YETTY_YCHART_CHART_IR_H

/*
 * ychart chart IR — owned in-memory representation of a parsed chart.
 *
 * Data parsers (csv / json / yaml) fill this; the renderer walks it once and
 * emits MSD (SDF shape) and MSDF (text) primitives into a ydraw buffer.
 *
 * The model is the familiar "category × series" table used by every cartesian
 * and polar chart:
 *
 *   - `categories`  are the discrete x-axis labels (or pie/treemap slice
 *     labels): "Apples", "Oranges", "2021", "speed", …
 *   - each `series` carries one value per category (`values[i]` aligns with
 *     `categories[i]`); a short series is zero-padded at render time.
 *
 * Sankey diagrams are flow graphs, not a category table, so they carry a
 * separate `flows` array (source → target with a weight). The renderer's
 * sankey path reads that instead of categories/series.
 *
 * Storage is plain dynamic arrays — charts are small (tens of categories /
 * series), so a map would be overkill.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Chart kinds
 *===========================================================================*/

enum yetty_ychart_kind {
    YETTY_YCHART_KIND_AUTO = 0, /* pick from the data shape */
    YETTY_YCHART_KIND_BAR,      /* horizontal bars (one row per category) */
    YETTY_YCHART_KIND_COLUMN,   /* vertical bars */
    YETTY_YCHART_KIND_LINE,
    YETTY_YCHART_KIND_AREA,
    YETTY_YCHART_KIND_SCATTER,
    YETTY_YCHART_KIND_PIE,
    YETTY_YCHART_KIND_DONUT,
    YETTY_YCHART_KIND_RADAR,
    YETTY_YCHART_KIND_TREEMAP,
    YETTY_YCHART_KIND_SANKEY,
};

/* Name ↔ kind, for directives / CLI flags / the json|yaml `chart:` key.
 * Accepts a few aliases ("hbar" → BAR, "doughnut" → DONUT, …). */
const char *yetty_ychart_kind_name(enum yetty_ychart_kind kind);
enum yetty_ychart_kind yetty_ychart_kind_from_name(const char *name);

/* Forward declaration so the colour helpers below can take a chart pointer;
 * the full definition follows further down. */
struct yetty_ychart_chart;

/*=============================================================================
 * Structural colours (work on the dark brand canvas).
 *===========================================================================*/

#define YETTY_YCHART_COLOR_AXIS 0xFF556162u       /* axis lines */
#define YETTY_YCHART_COLOR_GRID 0xFF263038u       /* gridlines */
#define YETTY_YCHART_COLOR_TEXT 0xFFE0E5E4u       /* primary labels */
#define YETTY_YCHART_COLOR_TEXT_MUTED 0xFF9FA7A8u /* axis tick labels */
#define YETTY_YCHART_COLOR_TITLE 0xFFE0E5E4u      /* title */

/* Categorical series palette. Returns a pointer to a program-lifetime table
 * and writes its length to *out_count. Index callers with `% count`. */
const uint32_t *yetty_ychart_palette(size_t *out_count);

/* Resolve series colour: the series' explicit colour if set, else the palette
 * entry for its index. */
uint32_t yetty_ychart_resolve_color(const struct yetty_ychart_chart *chart, size_t series_index);

/*=============================================================================
 * Series / flows / chart
 *===========================================================================*/

struct yetty_ychart_series {
    char *name;       /* heap, may be empty */
    uint32_t color;   /* 0 = assign from palette at render time */
    double *values;   /* heap, length value_count */
    double *x_values; /* heap, optional; NULL = use category index as x */
    size_t value_count;
    size_t value_capacity;
};

/* One sankey link: a weighted flow from one named node to another. */
struct yetty_ychart_flow {
    char *source; /* heap, NUL-terminated */
    char *target; /* heap, NUL-terminated */
    double value;
};

struct yetty_ychart_chart {
    enum yetty_ychart_kind kind;

    char *title;   /* heap, may be NULL */
    char *x_label; /* heap, may be NULL */
    char *y_label; /* heap, may be NULL */

    char **categories;
    size_t category_count;
    size_t category_capacity;

    struct yetty_ychart_series *series;
    size_t series_count;
    size_t series_capacity;

    struct yetty_ychart_flow *flows; /* sankey only */
    size_t flow_count;
    size_t flow_capacity;

    /* presentation toggles (parsers may flip these from directives) */
    bool show_legend;
    bool show_values; /* draw the numeric value next to each datum */
    bool stacked;     /* bar/column/area: stack series instead of grouping */
};

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_chart_init(struct yetty_ychart_chart *chart);
void yetty_ychart_chart_destroy(struct yetty_ychart_chart *chart);

/*=============================================================================
 * Builders — all `char *` args are copied (strdup); callers may pass
 * stack / scratch strings. Index-returning builders give the new element's
 * index (>= 0) or an error.
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_set_title(struct yetty_ychart_chart *chart,
                                                       const char *title);
struct yetty_ycore_void_result yetty_ychart_set_axis_labels(struct yetty_ychart_chart *chart,
                                                             const char *x_label,
                                                             const char *y_label);

/* Append a category label; returns its index. */
struct yetty_ycore_int_result yetty_ychart_add_category(struct yetty_ychart_chart *chart,
                                                         const char *label);

/* Append an empty series (name may be NULL); returns its index. */
struct yetty_ycore_int_result yetty_ychart_add_series(struct yetty_ychart_chart *chart,
                                                       const char *name, uint32_t color);

/* Push one value onto a series (by index). Grows the series array. */
struct yetty_ycore_void_result yetty_ychart_series_push(struct yetty_ychart_chart *chart,
                                                         size_t series_index, double value);

/* Push an (x, y) pair onto a series — for scatter / x-valued lines. */
struct yetty_ycore_void_result yetty_ychart_series_push_xy(struct yetty_ychart_chart *chart,
                                                            size_t series_index, double x,
                                                            double y);

/* Append a sankey flow. */
struct yetty_ycore_void_result yetty_ychart_add_flow(struct yetty_ychart_chart *chart,
                                                      const char *source, const char *target,
                                                      double value);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHART_CHART_IR_H */
