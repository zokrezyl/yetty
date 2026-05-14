/*
 * graph-ir.c — bookkeeping for the diagram graph (nodes/edges/clusters)
 * plus the default style descriptors. No layout or rendering logic.
 */

#include <yetty/ydiagram/graph-ir.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

/*=============================================================================
 * Helpers
 *===========================================================================*/

static char *dup_str(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *out = calloc(n + 1, 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    return out;
}

static struct yetty_ycore_void_result grow_array(void **arr, size_t *cap, size_t needed,
                                                 size_t elem_size)
{
    if (needed <= *cap) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = *cap ? *cap : 8;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    void *n = realloc(*arr, new_cap * elem_size);
    if (!n) {
        return YETTY_ERR(yetty_ycore_void, "ydiagram: out of memory");
    }
    memset((char *)n + (*cap) * elem_size, 0, (new_cap - *cap) * elem_size);
    *arr = n;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Defaults
 *===========================================================================*/

struct yetty_ydiagram_node_style yetty_ydiagram_default_node_style(void)
{
    struct yetty_ydiagram_node_style s = {
        .fill_color    = 0xFFAA5533u,
        .stroke_color  = 0xFFFFFFFFu,
        .stroke_width  = 2.0f,
        .text_color    = 0xFFFFFFFFu,
        .font_size     = 14.0f,
        .corner_radius = 8.0f,
    };
    return s;
}

struct yetty_ydiagram_edge_style yetty_ydiagram_default_edge_style(void)
{
    struct yetty_ydiagram_edge_style s = {
        .stroke_color    = 0xFFFFFFFFu,
        .stroke_width    = 1.5f,
        .line_style      = YETTY_YDIAGRAM_LINE_SOLID,
        .source_arrow    = YETTY_YDIAGRAM_ARROW_NONE,
        .target_arrow    = YETTY_YDIAGRAM_ARROW_NORMAL,
        .label_color     = 0xFFCCCCCCu,
        .label_font_size = 12.0f,
    };
    return s;
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydiagram_graph_init(struct yetty_ydiagram_graph *g)
{
    if (!g) {
        return YETTY_ERR(yetty_ycore_void, "graph_init: NULL graph");
    }
    memset(g, 0, sizeof(*g));
    g->direction          = YETTY_YDIAGRAM_DIR_TB;
    g->default_node_style = yetty_ydiagram_default_node_style();
    g->default_edge_style = yetty_ydiagram_default_edge_style();
    return YETTY_OK_VOID();
}

static void destroy_node(struct yetty_ydiagram_node *n)
{
    free(n->id);
    free(n->label);
    free(n->cluster_id);
}

static void destroy_edge(struct yetty_ydiagram_edge *e)
{
    free(e->id);
    free(e->source_id);
    free(e->target_id);
    free(e->label);
    free(e->control_points);
}

static void destroy_cluster(struct yetty_ydiagram_cluster *c)
{
    free(c->id);
    free(c->label);
    for (size_t i = 0; i < c->node_count; i++) {
        free(c->node_ids[i]);
    }
    free(c->node_ids);
}

void yetty_ydiagram_graph_destroy(struct yetty_ydiagram_graph *g)
{
    if (!g) {
        return;
    }
    free(g->title);
    for (size_t i = 0; i < g->node_count; i++) {
        destroy_node(&g->nodes[i]);
    }
    free(g->nodes);
    for (size_t i = 0; i < g->edge_count; i++) {
        destroy_edge(&g->edges[i]);
    }
    free(g->edges);
    for (size_t i = 0; i < g->cluster_count; i++) {
        destroy_cluster(&g->clusters[i]);
    }
    free(g->clusters);
    memset(g, 0, sizeof(*g));
}

/*=============================================================================
 * Builders
 *===========================================================================*/

struct yetty_ycore_int_result yetty_ydiagram_graph_add_node(
    struct yetty_ydiagram_graph *g, const char *id, const char *label,
    enum yetty_ydiagram_node_shape shape)
{
    if (!g || !id) {
        return YETTY_ERR(yetty_ycore_int, "add_node: NULL graph or id");
    }
    struct yetty_ycore_void_result gr = grow_array(
        (void **)&g->nodes, &g->node_capacity, g->node_count + 1, sizeof(*g->nodes));
    YETTY_RETURN_IF_ERR(yetty_ycore_int, gr, "add_node: grow failed");

    struct yetty_ydiagram_node *n = &g->nodes[g->node_count];
    memset(n, 0, sizeof(*n));
    n->id    = dup_str(id);
    n->label = dup_str(label ? label : "");
    if (!n->id || !n->label) {
        free(n->id);
        free(n->label);
        return YETTY_ERR(yetty_ycore_int, "add_node: oom");
    }
    n->shape    = shape;
    n->style    = g->default_node_style;
    n->layer    = -1;
    n->position = -1;
    n->is_dummy = false;
    int idx     = (int)g->node_count;
    g->node_count++;
    return YETTY_OK(yetty_ycore_int, idx);
}

struct yetty_ycore_int_result yetty_ydiagram_graph_add_edge(
    struct yetty_ydiagram_graph *g, const char *source_id, const char *target_id,
    const char *label, const struct yetty_ydiagram_edge_style *style)
{
    if (!g || !source_id || !target_id) {
        return YETTY_ERR(yetty_ycore_int, "add_edge: NULL graph or endpoints");
    }
    struct yetty_ycore_void_result gr = grow_array(
        (void **)&g->edges, &g->edge_capacity, g->edge_count + 1, sizeof(*g->edges));
    YETTY_RETURN_IF_ERR(yetty_ycore_int, gr, "add_edge: grow failed");

    struct yetty_ydiagram_edge *e = &g->edges[g->edge_count];
    memset(e, 0, sizeof(*e));

    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "e%zu", g->edge_count);
    e->id        = dup_str(id_buf);
    e->source_id = dup_str(source_id);
    e->target_id = dup_str(target_id);
    e->label     = dup_str(label ? label : "");
    if (!e->id || !e->source_id || !e->target_id || !e->label) {
        free(e->id);
        free(e->source_id);
        free(e->target_id);
        free(e->label);
        return YETTY_ERR(yetty_ycore_int, "add_edge: oom");
    }
    e->style = style ? *style : g->default_edge_style;
    int idx  = (int)g->edge_count;
    g->edge_count++;
    return YETTY_OK(yetty_ycore_int, idx);
}

struct yetty_ycore_int_result yetty_ydiagram_graph_add_cluster(
    struct yetty_ydiagram_graph *g, const char *id, const char *label)
{
    if (!g || !id) {
        return YETTY_ERR(yetty_ycore_int, "add_cluster: NULL graph or id");
    }
    struct yetty_ycore_void_result gr = grow_array(
        (void **)&g->clusters, &g->cluster_capacity, g->cluster_count + 1, sizeof(*g->clusters));
    YETTY_RETURN_IF_ERR(yetty_ycore_int, gr, "add_cluster: grow failed");

    struct yetty_ydiagram_cluster *c = &g->clusters[g->cluster_count];
    memset(c, 0, sizeof(*c));
    c->id    = dup_str(id);
    c->label = dup_str(label ? label : "");
    if (!c->id || !c->label) {
        free(c->id);
        free(c->label);
        return YETTY_ERR(yetty_ycore_int, "add_cluster: oom");
    }
    c->fill_color   = 0x40666666u;
    c->stroke_color = 0xFF888888u;
    int idx         = (int)g->cluster_count;
    g->cluster_count++;
    return YETTY_OK(yetty_ycore_int, idx);
}

struct yetty_ycore_void_result yetty_ydiagram_cluster_add_node(
    struct yetty_ydiagram_cluster *c, const char *node_id)
{
    if (!c || !node_id) {
        return YETTY_ERR(yetty_ycore_void, "cluster_add_node: NULL");
    }
    struct yetty_ycore_void_result gr = grow_array(
        (void **)&c->node_ids, &c->node_capacity, c->node_count + 1, sizeof(*c->node_ids));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "cluster_add_node: grow failed");
    char *copy = dup_str(node_id);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "cluster_add_node: oom");
    }
    c->node_ids[c->node_count++] = copy;
    return YETTY_OK_VOID();
}

struct yetty_ydiagram_node *yetty_ydiagram_graph_find_node(
    struct yetty_ydiagram_graph *g, const char *id)
{
    if (!g || !id) {
        return NULL;
    }
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id && strcmp(g->nodes[i].id, id) == 0) {
            return &g->nodes[i];
        }
    }
    return NULL;
}

struct yetty_ydiagram_cluster *yetty_ydiagram_graph_find_cluster(
    struct yetty_ydiagram_graph *g, const char *id)
{
    if (!g || !id) {
        return NULL;
    }
    for (size_t i = 0; i < g->cluster_count; i++) {
        if (g->clusters[i].id && strcmp(g->clusters[i].id, id) == 0) {
            return &g->clusters[i];
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_ydiagram_edge_add_control_point(
    struct yetty_ydiagram_edge *e, float x, float y)
{
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "edge_add_control_point: NULL");
    }
    struct yetty_ycore_void_result gr = grow_array(
        (void **)&e->control_points, &e->control_capacity, e->control_count + 1,
        sizeof(*e->control_points));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "edge_add_control_point: grow failed");
    e->control_points[e->control_count++] = (struct yetty_ydiagram_point){x, y};
    return YETTY_OK_VOID();
}

void yetty_ydiagram_edge_clear_control_points(struct yetty_ydiagram_edge *e)
{
    if (e) {
        e->control_count = 0;
    }
}
