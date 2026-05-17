#ifndef YETTY_YDIAGRAM_GRAPH_IR_H
#define YETTY_YDIAGRAM_GRAPH_IR_H

/*
 * ydiagram graph IR — owned in-memory representation of a parsed diagram.
 *
 * Mermaid (and later DOT) parsers fill this; the layout engine annotates
 * nodes/edges with positions; the renderer walks it once and emits MSD/MSDF
 * primitives into a ydraw buffer.
 *
 * Storage is simple dynamic arrays — diagrams are small (tens to hundreds
 * of nodes), so an open-addressing string map would be overkill. Lookup
 * is linear over the node array.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Shapes / styles
 *===========================================================================*/

enum yetty_ydiagram_node_shape {
    YETTY_YDIAGRAM_SHAPE_RECTANGLE,
    YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT,
    YETTY_YDIAGRAM_SHAPE_CIRCLE,
    YETTY_YDIAGRAM_SHAPE_DIAMOND,
    YETTY_YDIAGRAM_SHAPE_ELLIPSE,
    YETTY_YDIAGRAM_SHAPE_HEXAGON,
    YETTY_YDIAGRAM_SHAPE_PARALLELOGRAM,
    YETTY_YDIAGRAM_SHAPE_CYLINDER,
    YETTY_YDIAGRAM_SHAPE_STADIUM,
    YETTY_YDIAGRAM_SHAPE_TRAPEZOID,
    YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE,
};

enum yetty_ydiagram_arrow_style {
    YETTY_YDIAGRAM_ARROW_NONE,
    YETTY_YDIAGRAM_ARROW_NORMAL,
    YETTY_YDIAGRAM_ARROW_OPEN,
    YETTY_YDIAGRAM_ARROW_DIAMOND,
    YETTY_YDIAGRAM_ARROW_CIRCLE,
    YETTY_YDIAGRAM_ARROW_DOT,
};

enum yetty_ydiagram_line_style {
    YETTY_YDIAGRAM_LINE_SOLID,
    YETTY_YDIAGRAM_LINE_DASHED,
    YETTY_YDIAGRAM_LINE_DOTTED,
    YETTY_YDIAGRAM_LINE_THICK,
};

enum yetty_ydiagram_direction {
    YETTY_YDIAGRAM_DIR_TB,
    YETTY_YDIAGRAM_DIR_BT,
    YETTY_YDIAGRAM_DIR_LR,
    YETTY_YDIAGRAM_DIR_RL,
};

struct yetty_ydiagram_node_style {
    uint32_t fill_color;
    uint32_t stroke_color;
    float    stroke_width;
    uint32_t text_color;
    float    font_size;
    float    corner_radius;
};

struct yetty_ydiagram_edge_style {
    uint32_t                        stroke_color;
    float                           stroke_width;
    enum yetty_ydiagram_line_style  line_style;
    enum yetty_ydiagram_arrow_style source_arrow;
    enum yetty_ydiagram_arrow_style target_arrow;
    uint32_t                        label_color;
    float                           label_font_size;
};

struct yetty_ydiagram_node_style yetty_ydiagram_default_node_style(void);
struct yetty_ydiagram_edge_style yetty_ydiagram_default_edge_style(void);

/*=============================================================================
 * Nodes / edges / clusters
 *===========================================================================*/

struct yetty_ydiagram_node {
    char    *id;     /* heap, NUL-terminated */
    char    *label;  /* heap, may be empty */
    char    *cluster_id; /* heap, NULL if none */
    enum yetty_ydiagram_node_shape shape;
    struct yetty_ydiagram_node_style style;

    /* layout output */
    float x, y;          /* centre */
    float width, height;
    int   layer;
    int   position;
    bool  is_dummy;
};

struct yetty_ydiagram_point {
    float x, y;
};

struct yetty_ydiagram_edge {
    char    *id;
    char    *source_id;
    char    *target_id;
    char    *label;
    struct yetty_ydiagram_edge_style style;

    /* layout output */
    struct yetty_ydiagram_point *control_points;
    size_t                       control_count;
    size_t                       control_capacity;
    struct yetty_ydiagram_point  label_position;
    bool                         reversed;
};

struct yetty_ydiagram_cluster {
    char    *id;
    char    *label;
    char   **node_ids;
    size_t   node_count;
    size_t   node_capacity;
    uint32_t fill_color;
    uint32_t stroke_color;

    float x, y, width, height;
};

struct yetty_ydiagram_graph {
    char *title;
    enum yetty_ydiagram_direction direction;

    struct yetty_ydiagram_node     *nodes;
    size_t                          node_count;
    size_t                          node_capacity;

    struct yetty_ydiagram_edge     *edges;
    size_t                          edge_count;
    size_t                          edge_capacity;

    struct yetty_ydiagram_cluster  *clusters;
    size_t                          cluster_count;
    size_t                          cluster_capacity;

    struct yetty_ydiagram_node_style default_node_style;
    struct yetty_ydiagram_edge_style default_edge_style;

    /* scene bounds — filled by layout */
    float min_x, min_y, max_x, max_y;
};

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydiagram_graph_init(struct yetty_ydiagram_graph *g);
void yetty_ydiagram_graph_destroy(struct yetty_ydiagram_graph *g);

/*=============================================================================
 * Builders
 *
 * Add helpers transfer ownership of all `char *` args via strdup; callers
 * may pass stack/scratch strings. Returns index of the new node/edge/cluster
 * (>= 0) or an error.
 *===========================================================================*/

struct yetty_ycore_int_result yetty_ydiagram_graph_add_node(
    struct yetty_ydiagram_graph *g, const char *id, const char *label,
    enum yetty_ydiagram_node_shape shape);

struct yetty_ycore_int_result yetty_ydiagram_graph_add_edge(
    struct yetty_ydiagram_graph *g, const char *source_id, const char *target_id,
    const char *label, const struct yetty_ydiagram_edge_style *style);

struct yetty_ycore_int_result yetty_ydiagram_graph_add_cluster(
    struct yetty_ydiagram_graph *g, const char *id, const char *label);

struct yetty_ycore_void_result yetty_ydiagram_cluster_add_node(
    struct yetty_ydiagram_cluster *c, const char *node_id);

/* Linear lookup. Returns NULL if not found. */
struct yetty_ydiagram_node *yetty_ydiagram_graph_find_node(
    struct yetty_ydiagram_graph *g, const char *id);

struct yetty_ydiagram_cluster *yetty_ydiagram_graph_find_cluster(
    struct yetty_ydiagram_graph *g, const char *id);

/* Push a single control point onto an edge. */
struct yetty_ycore_void_result yetty_ydiagram_edge_add_control_point(
    struct yetty_ydiagram_edge *e, float x, float y);

void yetty_ydiagram_edge_clear_control_points(struct yetty_ydiagram_edge *e);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_GRAPH_IR_H */
