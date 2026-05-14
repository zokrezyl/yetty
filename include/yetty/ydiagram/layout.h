#ifndef YETTY_YDIAGRAM_LAYOUT_H
#define YETTY_YDIAGRAM_LAYOUT_H

/*
 * Sugiyama layered layout — assigns (x, y) to each node and routes edges
 * by adding straight-line control points. Phases:
 *
 *   1. Cycle removal      (reverse back-edges)
 *   2. Layer assignment   (longest path from sinks)
 *   3. Dummy nodes        (insert per intermediate layer for long edges)
 *   4. Crossing reduction (barycenter, alternating sweep)
 *   5. Positioning        (assign x/y, centre layers)
 *   6. Edge routing       (straight lines, attach points are bottom/top)
 *   7. Direction transform (TB / BT / LR / RL)
 *
 * The text measurement callback is supplied by the caller; it lets us
 * size nodes to their labels at the layer-assignment-known font size.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydiagram/graph-ir.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Measure a UTF-8 string at the given font size, in pixels. The supplied
 * data is NOT NUL-terminated. */
typedef float (*yetty_ydiagram_measure_text_fn)(const char *text, size_t text_len,
                                                float font_size, void *userdata);

struct yetty_ydiagram_layout_params {
    float    node_spacing_x;
    float    node_spacing_y;
    float    node_padding_x;
    float    node_padding_y;
    float    edge_spacing;
    uint32_t max_iterations;
};

struct yetty_ydiagram_layout_params yetty_ydiagram_default_layout_params(void);

struct yetty_ycore_void_result yetty_ydiagram_layout(
    struct yetty_ydiagram_graph *g, const struct yetty_ydiagram_layout_params *params,
    yetty_ydiagram_measure_text_fn measure, void *measure_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_LAYOUT_H */
