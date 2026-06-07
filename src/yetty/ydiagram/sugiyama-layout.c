/*
 * sugiyama-layout.c — Sugiyama layered layout for the diagram graph.
 *
 * All scratch state is per-graph and lives on the heap for the duration of
 * yetty_ydiagram_layout(); we don't carry it in the graph itself. Nodes are
 * referenced by their integer index in `g->nodes` once layering starts —
 * that keeps the inner loops free of string comparisons.
 */

#include <yetty/ydiagram/layout.h>

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>

struct yetty_ydiagram_layout_params yetty_ydiagram_default_layout_params(void)
{
    struct yetty_ydiagram_layout_params p = {
        .node_spacing_x = 60.0f,
        .node_spacing_y = 100.0f,
        .node_padding_x = 15.0f,
        .node_padding_y = 10.0f,
        .edge_spacing = 20.0f,
        .max_iterations = 25,
    };
    return p;
}

/*=============================================================================
 * Helpers — find node index by id (linear)
 *===========================================================================*/

static int node_index(const struct yetty_ydiagram_graph *g, const char *id)
{
    if (!id) {
        return -1;
    }
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id && strcmp(g->nodes[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/*=============================================================================
 * Phase 1: cycle removal (iterative DFS to avoid stack blow-up on huge
 * graphs). Re-orients back edges in place.
 *===========================================================================*/

struct dfs_frame {
    int node_idx;
    /* Next outgoing-edge index to examine for this node. */
    size_t edge_cursor;
};

static struct yetty_ycore_void_result remove_cycles(struct yetty_ydiagram_graph *g)
{
    size_t n = g->node_count;
    char *visited = calloc(n, 1);
    char *in_stack = calloc(n, 1);
    struct dfs_frame *stack = calloc(n, sizeof(*stack));
    if (n && (!visited || !in_stack || !stack)) {
        free(visited);
        free(in_stack);
        free(stack);
        return YETTY_ERR(yetty_ycore_void, "remove_cycles: oom");
    }

    for (size_t root = 0; root < n; root++) {
        if (visited[root]) {
            continue;
        }
        size_t sp = 0;
        stack[sp++] = (struct dfs_frame){(int)root, 0};
        visited[root] = 1;
        in_stack[root] = 1;
        while (sp > 0) {
            struct dfs_frame *fr = &stack[sp - 1];
            const char *cur_id = g->nodes[fr->node_idx].id;
            bool descended = false;
            for (size_t e = fr->edge_cursor; e < g->edge_count; e++) {
                struct yetty_ydiagram_edge *edge = &g->edges[e];
                if (edge->source_id && strcmp(edge->source_id, cur_id) == 0) {
                    int tgt = node_index(g, edge->target_id);
                    if (tgt < 0) {
                        continue;
                    }
                    if (in_stack[tgt]) {
                        /* Back edge — reverse. */
                        char *tmp = edge->source_id;
                        edge->source_id = edge->target_id;
                        edge->target_id = tmp;
                        edge->reversed = !edge->reversed;
                    } else if (!visited[tgt]) {
                        fr->edge_cursor = e + 1;
                        visited[tgt] = 1;
                        in_stack[tgt] = 1;
                        stack[sp++] = (struct dfs_frame){tgt, 0};
                        descended = true;
                        break;
                    }
                }
            }
            if (!descended) {
                in_stack[fr->node_idx] = 0;
                sp--;
            }
        }
    }

    free(visited);
    free(in_stack);
    free(stack);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Phase 2: layer assignment (longest path from sinks). Iterative with a
 * worklist to avoid recursive recompute_max_layer().
 *
 * memo[i] = longest path from node i to any sink (0 for sinks). Final
 * layer = max_memo - memo[i] so sources end up at layer 0.
 *===========================================================================*/

static struct yetty_ycore_void_result assign_layers(struct yetty_ydiagram_graph *g)
{
    size_t n = g->node_count;
    if (n == 0) {
        return YETTY_OK_VOID();
    }

    int *memo = malloc(n * sizeof(*memo));
    char *resolved = calloc(n, sizeof(*resolved));
    if (!memo || !resolved) {
        free(memo);
        free(resolved);
        return YETTY_ERR(yetty_ycore_void, "assign_layers: oom");
    }
    for (size_t i = 0; i < n; i++) {
        memo[i] = -1;
    }

    /* Iterative DFS — for each unresolved node, push children; once all
     * children resolved, memo[v] = 1 + max(child memo). */
    int *stack = malloc(n * sizeof(*stack));
    if (!stack) {
        free(memo);
        free(resolved);
        return YETTY_ERR(yetty_ycore_void, "assign_layers: oom");
    }

    for (size_t root = 0; root < n; root++) {
        if (resolved[root]) {
            continue;
        }
        size_t sp = 0;
        stack[sp++] = (int)root;
        while (sp > 0) {
            int v = stack[sp - 1];
            const char *vid = g->nodes[v].id;
            bool all_resolved = true;
            int best_child = -1;
            for (size_t e = 0; e < g->edge_count; e++) {
                if (g->edges[e].source_id && strcmp(g->edges[e].source_id, vid) == 0) {
                    int c = node_index(g, g->edges[e].target_id);
                    if (c < 0) {
                        continue;
                    }
                    if (!resolved[c]) {
                        /* Avoid pushing the same descendant twice if it's
                         * already on the stack — should be DAG by now but
                         * be defensive. */
                        bool on_stack = false;
                        for (size_t k = 0; k < sp; k++) {
                            if (stack[k] == c) {
                                on_stack = true;
                                break;
                            }
                        }
                        if (!on_stack) {
                            stack[sp++] = c;
                            all_resolved = false;
                            break;
                        }
                    } else if (memo[c] > best_child) {
                        best_child = memo[c];
                    }
                }
            }
            if (all_resolved) {
                memo[v] = best_child + 1;
                resolved[v] = 1;
                sp--;
            }
        }
    }

    int max_memo = 0;
    for (size_t i = 0; i < n; i++) {
        if (memo[i] > max_memo) {
            max_memo = memo[i];
        }
    }
    for (size_t i = 0; i < n; i++) {
        g->nodes[i].layer = max_memo - memo[i];
    }

    free(memo);
    free(resolved);
    free(stack);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Phase 3: dummy nodes for long edges. We append dummies to g->nodes and
 * replace each long edge by a chain of segments. The original edge index
 * is invalidated by appends to g->edges, so we collect a "to remove" list
 * and compact at the end.
 *===========================================================================*/

static struct yetty_ycore_void_result insert_dummy_nodes(struct yetty_ydiagram_graph *g)
{
    size_t orig_edge_count = g->edge_count;
    /* Snapshot original edge bodies so the indices we use against
     * `to_remove` line up with the array even as we append new edges. */
    char *to_remove = calloc(orig_edge_count, 1);
    if (orig_edge_count && !to_remove) {
        return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: oom");
    }

    for (size_t i = 0; i < orig_edge_count; i++) {
        struct yetty_ydiagram_edge *edge = &g->edges[i];
        int src = node_index(g, edge->source_id);
        int tgt = node_index(g, edge->target_id);
        if (src < 0 || tgt < 0) {
            continue;
        }

        int span = g->nodes[tgt].layer - g->nodes[src].layer;
        if (span <= 1) {
            continue;
        }

        /* Snapshot every field we'll need from `edge` before any further
         * add_node/add_edge call: those grow g->edges/g->nodes and may
         * realloc them, invalidating the `edge` pointer. */
        char *orig_id = strdup(edge->id ? edge->id : "");
        char *orig_label = strdup(edge->label ? edge->label : "");
        char *orig_target_id = strdup(edge->target_id ? edge->target_id : "");
        struct yetty_ydiagram_edge_style style = edge->style;
        bool reversed = edge->reversed;
        if (!orig_id || !orig_label || !orig_target_id) {
            free(orig_id);
            free(orig_label);
            free(orig_target_id);
            free(to_remove);
            return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: oom");
        }

        char *prev_id = strdup(edge->source_id);
        if (!prev_id) {
            free(orig_id);
            free(orig_label);
            free(orig_target_id);
            free(to_remove);
            return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: oom");
        }
        /* `edge` is now invalid as soon as we touch g->edges; use the
         * snapshots above for the rest of this iteration. */

        for (int layer = g->nodes[src].layer + 1; layer < g->nodes[tgt].layer; layer++) {
            char dummy_id[128];
            snprintf(dummy_id, sizeof(dummy_id), "_d_%s_%d", orig_id, layer);
            struct yetty_ycore_int_result nr =
                yetty_ydiagram_graph_add_node(g, dummy_id, "", YETTY_YDIAGRAM_SHAPE_RECTANGLE);
            if (YETTY_IS_ERR(nr)) {
                free(prev_id);
                free(orig_id);
                free(orig_label);
                free(orig_target_id);
                free(to_remove);
                return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: add dummy failed", nr);
            }
            struct yetty_ydiagram_node *dn = &g->nodes[nr.value];
            dn->layer = layer;
            dn->is_dummy = true;
            dn->width = 0;
            dn->height = 0;

            /* prev_id → dummy_id */
            struct yetty_ydiagram_edge_style seg_style = style;
            seg_style.source_arrow = YETTY_YDIAGRAM_ARROW_NONE;
            seg_style.target_arrow = YETTY_YDIAGRAM_ARROW_NONE;
            struct yetty_ycore_int_result er =
                yetty_ydiagram_graph_add_edge(g, prev_id, dummy_id, "", &seg_style);
            if (YETTY_IS_ERR(er)) {
                free(prev_id);
                free(orig_id);
                free(orig_label);
                free(orig_target_id);
                free(to_remove);
                return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: add seg failed", er);
            }
            g->edges[er.value].reversed = reversed;

            free(prev_id);
            prev_id = strdup(dummy_id);
            if (!prev_id) {
                free(orig_id);
                free(orig_label);
                free(orig_target_id);
                free(to_remove);
                return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: oom");
            }
        }

        /* Final segment carries the original label and keeps target_arrow. */
        struct yetty_ydiagram_edge_style final_style = style;
        final_style.source_arrow = YETTY_YDIAGRAM_ARROW_NONE;
        struct yetty_ycore_int_result er =
            yetty_ydiagram_graph_add_edge(g, prev_id, orig_target_id, orig_label, &final_style);
        if (YETTY_IS_ERR(er)) {
            free(prev_id);
            free(orig_id);
            free(orig_label);
            free(orig_target_id);
            free(to_remove);
            return YETTY_ERR(yetty_ycore_void, "insert_dummy_nodes: add final failed", er);
        }
        g->edges[er.value].reversed = reversed;

        free(prev_id);
        free(orig_id);
        free(orig_label);
        free(orig_target_id);
        to_remove[i] = 1;
    }

    /* Compact: copy the kept edges over `to_remove` entries. We can't just
     * free the marked edges and skip — destroy_edge would double-free if
     * we left them in place; instead we destroy them and shift. */
    size_t write = 0;
    for (size_t r = 0; r < g->edge_count; r++) {
        if (r < orig_edge_count && to_remove[r]) {
            free(g->edges[r].id);
            free(g->edges[r].source_id);
            free(g->edges[r].target_id);
            free(g->edges[r].label);
            free(g->edges[r].control_points);
            continue;
        }
        if (write != r) {
            g->edges[write] = g->edges[r];
        }
        write++;
    }
    /* Zero out the now-unused tail so destroy doesn't free moved memory. */
    for (size_t r = write; r < g->edge_count; r++) {
        memset(&g->edges[r], 0, sizeof(g->edges[r]));
    }
    g->edge_count = write;

    free(to_remove);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Phase 4: crossing reduction (barycenter, alternating sweep).
 *
 * Layers are stored as int arrays of node indices.
 *===========================================================================*/

struct layer_table {
    int *layer_of;    /* size n: which layer each node belongs to */
    int **layers;     /* layers[L] = malloc'd array of node indices */
    int *layer_sizes; /* count per layer */
    int *layer_caps;  /* alloc per layer */
    int max_layer;
};

static struct yetty_ycore_void_result layer_table_build(const struct yetty_ydiagram_graph *g,
                                                        struct layer_table *t)
{
    size_t n = g->node_count;
    memset(t, 0, sizeof(*t));
    t->layer_of = malloc(n * sizeof(*t->layer_of));
    if (n && !t->layer_of) {
        return YETTY_ERR(yetty_ycore_void, "layer_table: oom");
    }
    int max_layer = 0;
    for (size_t i = 0; i < n; i++) {
        int L = g->nodes[i].layer < 0 ? 0 : g->nodes[i].layer;
        t->layer_of[i] = L;
        if (L > max_layer) {
            max_layer = L;
        }
    }
    t->max_layer = max_layer;

    size_t total_layers = (size_t)max_layer + 1;
    t->layers = calloc(total_layers, sizeof(*t->layers));
    t->layer_sizes = calloc(total_layers, sizeof(*t->layer_sizes));
    t->layer_caps = calloc(total_layers, sizeof(*t->layer_caps));
    if (!t->layers || !t->layer_sizes || !t->layer_caps) {
        free(t->layer_of);
        free(t->layers);
        free(t->layer_sizes);
        free(t->layer_caps);
        return YETTY_ERR(yetty_ycore_void, "layer_table: oom");
    }
    /* Count then alloc. */
    for (size_t i = 0; i < n; i++) {
        t->layer_caps[t->layer_of[i]]++;
    }
    for (size_t L = 0; L < total_layers; L++) {
        if (t->layer_caps[L] > 0) {
            t->layers[L] = malloc(t->layer_caps[L] * sizeof(int));
            if (!t->layers[L]) {
                for (size_t k = 0; k < L; k++) {
                    free(t->layers[k]);
                }
                free(t->layers);
                free(t->layer_sizes);
                free(t->layer_caps);
                free(t->layer_of);
                return YETTY_ERR(yetty_ycore_void, "layer_table: oom");
            }
        }
    }
    /* Fill in declaration order. */
    int *cursor = calloc(total_layers, sizeof(int));
    if (total_layers && !cursor) {
        for (size_t L = 0; L < total_layers; L++) {
            free(t->layers[L]);
        }
        free(t->layers);
        free(t->layer_sizes);
        free(t->layer_caps);
        free(t->layer_of);
        return YETTY_ERR(yetty_ycore_void, "layer_table: oom");
    }
    for (size_t i = 0; i < n; i++) {
        int L = t->layer_of[i];
        t->layers[L][cursor[L]++] = (int)i;
        t->layer_sizes[L]++;
    }
    free(cursor);

    /* Initial positions: order within layer. */
    for (int L = 0; L <= max_layer; L++) {
        for (int k = 0; k < t->layer_sizes[L]; k++) {
            int v = t->layers[L][k];
            ((struct yetty_ydiagram_graph *)g)->nodes[v].position = k;
        }
    }
    return YETTY_OK_VOID();
}

static void layer_table_free(struct layer_table *t)
{
    if (!t) {
        return;
    }
    int total = t->max_layer + 1;
    for (int L = 0; L < total; L++) {
        free(t->layers[L]);
    }
    free(t->layers);
    free(t->layer_sizes);
    free(t->layer_caps);
    free(t->layer_of);
    memset(t, 0, sizeof(*t));
}

/* Barycenter of node v's neighbours in the previous (downward=true) or
 * next (downward=false) layer. */
static float barycenter(const struct yetty_ydiagram_graph *g, int v, bool downward)
{
    const char *vid = g->nodes[v].id;
    float sum = 0.0f;
    int count = 0;
    for (size_t e = 0; e < g->edge_count; e++) {
        const struct yetty_ydiagram_edge *ed = &g->edges[e];
        const char *neighbour = NULL;
        if (downward) {
            if (ed->target_id && strcmp(ed->target_id, vid) == 0) {
                neighbour = ed->source_id;
            }
        } else {
            if (ed->source_id && strcmp(ed->source_id, vid) == 0) {
                neighbour = ed->target_id;
            }
        }
        if (neighbour) {
            int nb = node_index(g, neighbour);
            if (nb >= 0) {
                sum += (float)g->nodes[nb].position;
                count++;
            }
        }
    }
    if (count == 0) {
        return (float)g->nodes[v].position;
    }
    return sum / (float)count;
}

struct bc_pair {
    float bc;
    int node_idx;
};

static int cmp_bc(const void *a, const void *b)
{
    const struct bc_pair *pa = a;
    const struct bc_pair *pb = b;
    if (pa->bc < pb->bc) {
        return -1;
    }
    if (pa->bc > pb->bc) {
        return 1;
    }
    return 0;
}

static void order_layer(struct yetty_ydiagram_graph *g, int L, bool downward, struct layer_table *t)
{
    int sz = t->layer_sizes[L];
    struct bc_pair *pairs = malloc(sz * sizeof(*pairs));
    if (!pairs) {
        return;
    }
    for (int k = 0; k < sz; k++) {
        int v = t->layers[L][k];
        pairs[k].bc = barycenter(g, v, downward);
        pairs[k].node_idx = v;
    }
    qsort(pairs, sz, sizeof(*pairs), cmp_bc);
    for (int k = 0; k < sz; k++) {
        t->layers[L][k] = pairs[k].node_idx;
        g->nodes[pairs[k].node_idx].position = k;
    }
    free(pairs);
}

static void reduce_crossings(struct yetty_ydiagram_graph *g, struct layer_table *t,
                             uint32_t max_iter)
{
    for (uint32_t it = 0; it < max_iter; it++) {
        for (int L = 1; L <= t->max_layer; L++) {
            order_layer(g, L, true, t);
        }
        for (int L = t->max_layer - 1; L >= 0; L--) {
            order_layer(g, L, false, t);
        }
    }
}

/*=============================================================================
 * Phase 5: positioning. We size nodes by their measured label, group by
 * layer, then assign y per layer (largest height) and x within layer.
 *===========================================================================*/

/* Measure one label, falling back to the 0.6 * font_size * len heuristic when
 * no real text-measure callback was supplied. */
static float measure_label(yetty_ydiagram_measure_text_fn measure, const char *text,
                           float font_size, void *userdata)
{
    if (!text || !text[0]) {
        return 0.0f;
    }
    if (measure) {
        return measure(text, strlen(text), font_size, userdata);
    }
    return font_size * 0.6f * (float)strlen(text);
}

static void size_nodes(struct yetty_ydiagram_graph *g, const struct yetty_ydiagram_layout_params *p,
                       yetty_ydiagram_measure_text_fn measure, void *userdata)
{
    const float default_w = 80.0f;
    const float default_h = 40.0f;
    for (size_t i = 0; i < g->node_count; i++) {
        struct yetty_ydiagram_node *n = &g->nodes[i];
        if (n->is_dummy) {
            n->width = 0;
            n->height = 0;
            continue;
        }
        if (n->fixed_size) {
            /* Pseudostate dots / fixed markers carry their own dimensions. */
            continue;
        }
        if (n->is_record) {
            /* UML class / ER entity: title compartment + body rows. Width is
             * the widest of the title and any row; height stacks the title
             * compartment over one line per row. */
            float fs = n->style.font_size;
            float widest = measure_label(measure, n->label, fs, userdata);
            if (n->stereotype && n->stereotype[0]) {
                float sw = measure_label(measure, n->stereotype, fs - 2.0f, userdata);
                if (sw > widest) {
                    widest = sw;
                }
            }
            for (size_t r = 0; r < n->row_count; r++) {
                float rw = measure_label(measure, n->rows[r], fs, userdata);
                if (rw > widest) {
                    widest = rw;
                }
            }
            float pad_x = p->node_padding_x + 6.0f;
            n->width = widest + pad_x * 2.0f;
            if (n->width < default_w) {
                n->width = default_w;
            }
            float header_h = fs + p->node_padding_y * 2.0f;
            if (n->stereotype && n->stereotype[0]) {
                header_h += fs - 2.0f;
            }
            n->header_h = header_h;
            float line_h = fs + 8.0f;
            n->height = header_h + (float)n->row_count * line_h;
            continue;
        }
        if (measure && n->label && n->label[0]) {
            float tw = measure(n->label, strlen(n->label), n->style.font_size, userdata);
            n->width = tw + p->node_padding_x * 2.0f;
            n->height = n->style.font_size + p->node_padding_y * 2.0f;
            if (n->width < default_w) {
                n->width = default_w;
            }
            if (n->height < default_h) {
                n->height = default_h;
            }
            if (n->shape == YETTY_YDIAGRAM_SHAPE_CIRCLE) {
                float d = n->width > n->height ? n->width : n->height;
                n->width = n->height = d;
            } else if (n->shape == YETTY_YDIAGRAM_SHAPE_DIAMOND) {
                n->width *= 1.4f;
                n->height *= 1.4f;
            }
        } else {
            n->width = default_w;
            n->height = default_h;
        }
    }
}

/* Sort layer contents by node->position, then assign y per layer and x
 * within the layer (centred). */
static void place_nodes(struct yetty_ydiagram_graph *g,
                        const struct yetty_ydiagram_layout_params *p, struct layer_table *t)
{
    /* Sort each layer by position. */
    for (int L = 0; L <= t->max_layer; L++) {
        int sz = t->layer_sizes[L];
        /* Tiny insertion sort by position. */
        for (int i = 1; i < sz; i++) {
            int v = t->layers[L][i];
            int pos = g->nodes[v].position;
            int j = i - 1;
            while (j >= 0 && g->nodes[t->layers[L][j]].position > pos) {
                t->layers[L][j + 1] = t->layers[L][j];
                j--;
            }
            t->layers[L][j + 1] = v;
        }
    }

    float current_y = p->node_padding_y;
    for (int L = 0; L <= t->max_layer; L++) {
        float max_h = 0.0f;
        for (int k = 0; k < t->layer_sizes[L]; k++) {
            float h = g->nodes[t->layers[L][k]].height;
            if (h > max_h) {
                max_h = h;
            }
        }
        for (int k = 0; k < t->layer_sizes[L]; k++) {
            g->nodes[t->layers[L][k]].y = current_y + max_h * 0.5f;
        }
        current_y += max_h + p->node_spacing_y;
    }

    for (int L = 0; L <= t->max_layer; L++) {
        int sz = t->layer_sizes[L];
        float total_width = 0.0f;
        for (int k = 0; k < sz; k++) {
            total_width += g->nodes[t->layers[L][k]].width;
        }
        if (sz > 1) {
            total_width += (sz - 1) * p->node_spacing_x;
        }
        float current_x = -total_width * 0.5f;
        for (int k = 0; k < sz; k++) {
            struct yetty_ydiagram_node *n = &g->nodes[t->layers[L][k]];
            n->x = current_x + n->width * 0.5f;
            current_x += n->width + p->node_spacing_x;
        }
    }

    /* Shift to positive coordinates. */
    float min_x = FLT_MAX, min_y = FLT_MAX;
    for (size_t i = 0; i < g->node_count; i++) {
        float lx = g->nodes[i].x - g->nodes[i].width * 0.5f;
        float ly = g->nodes[i].y - g->nodes[i].height * 0.5f;
        if (lx < min_x) {
            min_x = lx;
        }
        if (ly < min_y) {
            min_y = ly;
        }
    }
    if (g->node_count == 0) {
        min_x = 0;
        min_y = 0;
    }
    float off_x = -min_x + p->node_padding_x;
    float off_y = -min_y + p->node_padding_y;
    for (size_t i = 0; i < g->node_count; i++) {
        g->nodes[i].x += off_x;
        g->nodes[i].y += off_y;
    }
}

/*=============================================================================
 * Phase 6: route edges (straight, attach at top/bottom).
 *===========================================================================*/

static struct yetty_ycore_void_result route_edges(struct yetty_ydiagram_graph *g)
{
    for (size_t i = 0; i < g->edge_count; i++) {
        struct yetty_ydiagram_edge *e = &g->edges[i];
        struct yetty_ydiagram_node *s = yetty_ydiagram_graph_find_node(g, e->source_id);
        struct yetty_ydiagram_node *t = yetty_ydiagram_graph_find_node(g, e->target_id);
        if (!s || !t) {
            continue;
        }

        yetty_ydiagram_edge_clear_control_points(e);
        float sx = s->x;
        float sy = s->y + s->height * 0.5f;
        float tx = t->x;
        float ty = t->y - t->height * 0.5f;
        struct yetty_ycore_void_result source_point_res =
            yetty_ydiagram_edge_add_control_point(e, sx, sy);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, source_point_res, "route_edges: source point");
        struct yetty_ycore_void_result target_point_res =
            yetty_ydiagram_edge_add_control_point(e, tx, ty);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, target_point_res, "route_edges: target point");
        e->label_position.x = (sx + tx) * 0.5f;
        e->label_position.y = (sy + ty) * 0.5f;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Bounds + direction transform
 *===========================================================================*/

static void compute_bounds(struct yetty_ydiagram_graph *g)
{
    g->min_x = FLT_MAX;
    g->min_y = FLT_MAX;
    g->max_x = -FLT_MAX;
    g->max_y = -FLT_MAX;
    bool any = false;
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].is_dummy) {
            continue;
        }
        any = true;
        float lx = g->nodes[i].x - g->nodes[i].width * 0.5f;
        float ly = g->nodes[i].y - g->nodes[i].height * 0.5f;
        float rx = g->nodes[i].x + g->nodes[i].width * 0.5f;
        float ry = g->nodes[i].y + g->nodes[i].height * 0.5f;
        if (lx < g->min_x) {
            g->min_x = lx;
        }
        if (ly < g->min_y) {
            g->min_y = ly;
        }
        if (rx > g->max_x) {
            g->max_x = rx;
        }
        if (ry > g->max_y) {
            g->max_y = ry;
        }
    }
    if (!any) {
        g->min_x = g->min_y = g->max_x = g->max_y = 0.0f;
        return;
    }
    g->min_x -= 20.0f;
    g->min_y -= 20.0f;
    g->max_x += 20.0f;
    g->max_y += 20.0f;
}

static void swap_float(float *a, float *b)
{
    float t = *a;
    *a = *b;
    *b = t;
}

static void apply_direction(struct yetty_ydiagram_graph *g)
{
    if (g->direction == YETTY_YDIAGRAM_DIR_TB) {
        return;
    }

    float cx = (g->min_x + g->max_x) * 0.5f;
    float cy = (g->min_y + g->max_y) * 0.5f;

    for (size_t i = 0; i < g->node_count; i++) {
        struct yetty_ydiagram_node *n = &g->nodes[i];
        float dx = n->x - cx;
        float dy = n->y - cy;
        switch (g->direction) {
        case YETTY_YDIAGRAM_DIR_BT:
            n->y = cy - dy;
            break;
        case YETTY_YDIAGRAM_DIR_LR:
            n->x = cx + dy;
            n->y = cy + dx;
            swap_float(&n->width, &n->height);
            break;
        case YETTY_YDIAGRAM_DIR_RL:
            n->x = cx - dy;
            n->y = cy + dx;
            swap_float(&n->width, &n->height);
            break;
        default:
            break;
        }
    }
    for (size_t i = 0; i < g->edge_count; i++) {
        struct yetty_ydiagram_edge *e = &g->edges[i];
        for (size_t k = 0; k < e->control_count; k++) {
            float dx = e->control_points[k].x - cx;
            float dy = e->control_points[k].y - cy;
            switch (g->direction) {
            case YETTY_YDIAGRAM_DIR_BT:
                e->control_points[k].y = cy - dy;
                break;
            case YETTY_YDIAGRAM_DIR_LR:
                e->control_points[k].x = cx + dy;
                e->control_points[k].y = cy + dx;
                break;
            case YETTY_YDIAGRAM_DIR_RL:
                e->control_points[k].x = cx - dy;
                e->control_points[k].y = cy + dx;
                break;
            default:
                break;
            }
        }
        float dx = e->label_position.x - cx;
        float dy = e->label_position.y - cy;
        switch (g->direction) {
        case YETTY_YDIAGRAM_DIR_BT:
            e->label_position.y = cy - dy;
            break;
        case YETTY_YDIAGRAM_DIR_LR:
            e->label_position.x = cx + dy;
            e->label_position.y = cy + dx;
            break;
        case YETTY_YDIAGRAM_DIR_RL:
            e->label_position.x = cx - dy;
            e->label_position.y = cy + dx;
            break;
        default:
            break;
        }
    }
    compute_bounds(g);
}

/* ycat content must flow DOWNWARD from the cursor like `cat` — never above or
 * left of it, and tight to the origin (no large empty offset). compute_bounds()
 * pads the scene box outward by 20px and the direction transform can leave the
 * box far from the origin; either way min_x/min_y end up != 0. Translate the
 * whole scene — nodes, edge control points + labels, clusters, and the bounds —
 * so min_x/min_y land exactly at 0 (the 20px box pad becomes the top/left
 * content margin). */
static void translate_to_non_negative(struct yetty_ydiagram_graph *g)
{
    float sx = -g->min_x;
    float sy = -g->min_y;
    if (sx == 0.0f && sy == 0.0f) {
        return;
    }
    for (size_t i = 0; i < g->node_count; i++) {
        g->nodes[i].x += sx;
        g->nodes[i].y += sy;
    }
    for (size_t i = 0; i < g->edge_count; i++) {
        struct yetty_ydiagram_edge *e = &g->edges[i];
        for (size_t k = 0; k < e->control_count; k++) {
            e->control_points[k].x += sx;
            e->control_points[k].y += sy;
        }
        e->label_position.x += sx;
        e->label_position.y += sy;
    }
    for (size_t i = 0; i < g->cluster_count; i++) {
        g->clusters[i].x += sx;
        g->clusters[i].y += sy;
    }
    g->min_x += sx;
    g->max_x += sx;
    g->min_y += sy;
    g->max_y += sy;
}

/*=============================================================================
 * Public entry
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydiagram_layout(
    struct yetty_ydiagram_graph *g, const struct yetty_ydiagram_layout_params *params,
    yetty_ydiagram_measure_text_fn measure, void *userdata)
{
    if (!g) {
        return YETTY_ERR(yetty_ycore_void, "layout: NULL graph");
    }
    if (g->node_count == 0) {
        compute_bounds(g);
        return YETTY_OK_VOID();
    }
    struct yetty_ydiagram_layout_params p =
        params ? *params : yetty_ydiagram_default_layout_params();

    struct yetty_ycore_void_result r;
    r = remove_cycles(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layout: cycle removal");
    r = assign_layers(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layout: layer assignment");
    r = insert_dummy_nodes(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layout: dummy insertion");

    struct layer_table t;
    r = layer_table_build(g, &t);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layout: layer table");

    reduce_crossings(g, &t, p.max_iterations);
    size_nodes(g, &p, measure, userdata);
    place_nodes(g, &p, &t);
    layer_table_free(&t);

    struct yetty_ycore_void_result route_res = route_edges(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, route_res, "layout: route_edges");
    compute_bounds(g);
    apply_direction(g);
    translate_to_non_negative(g);
    return YETTY_OK_VOID();
}
