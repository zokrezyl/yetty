/*
 * state-parser.c — Mermaid stateDiagram / stateDiagram-v2 → graph IR.
 *
 * A state machine is a node+edge graph, so it reuses the shared graph IR and
 * the Sugiyama layout + renderer wholesale. The only state-specific pieces:
 *
 *   - states render as rounded rectangles;
 *   - `[*]` is a pseudostate: as a transition SOURCE it is the (filled-dot)
 *     initial state, as a TARGET it is the (ringed) final state. All initial
 *     uses share one `__start` node, all final uses one `__end` node;
 *   - transition labels use `A --> B : text` (colon), not `A -->|text| B`.
 *
 * Supported lines:
 *   stateDiagram / stateDiagram-v2          (header, ignored)
 *   direction TB | BT | LR | RL
 *   A --> B
 *   A --> B : label
 *   [*] --> A        /  A --> [*]
 *   state "long description" as A           (alias: node A, label = desc)
 *   StateName                               (bare declaration)
 *   composite `state X { ... }` braces are flattened (inner states become
 *   siblings); `note ...`, `%%` comments are ignored.
 */

#include <yetty/ydiagram/diagrams.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydiagram/graph-ir.h>

/* Trim ASCII whitespace in place; returns a pointer into `s`. */
static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

static struct yetty_ycore_void_result ensure_plain_state(struct yetty_ydiagram_graph *g,
                                                         const char *id)
{
    if (yetty_ydiagram_graph_find_node(g, id)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result r =
        yetty_ydiagram_graph_add_node(g, id, id, YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "state: add state node failed");
    return YETTY_OK_VOID();
}

/* Initial (`final == false`) → small filled dot; final → ringed circle. */
static struct yetty_ycore_void_result ensure_pseudo(struct yetty_ydiagram_graph *g, const char *id,
                                                    bool final)
{
    if (yetty_ydiagram_graph_find_node(g, id)) {
        return YETTY_OK_VOID();
    }
    enum yetty_ydiagram_node_shape shape =
        final ? YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE : YETTY_YDIAGRAM_SHAPE_CIRCLE;
    struct yetty_ycore_int_result r = yetty_ydiagram_graph_add_node(g, id, "", shape);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "state: add pseudostate failed");

    struct yetty_ydiagram_node *n = &g->nodes[r.value];
    n->fixed_size = true;
    if (final) {
        n->width = n->height = 24.0f;
        n->style.fill_color = 0;
        n->style.stroke_color = 0xFFFFFFFFu;
        n->style.stroke_width = 2.0f;
    } else {
        n->width = n->height = 16.0f;
        n->style.fill_color = 0xFFFFFFFFu;
        n->style.stroke_color = 0;
        n->style.stroke_width = 0.0f;
    }
    return YETTY_OK_VOID();
}

/* Resolve a transition endpoint token to a node id, creating the node if
 * needed. `[*]` resolves to the shared start (source) / end (target) node. */
static struct yetty_ycore_void_result resolve_endpoint(struct yetty_ydiagram_graph *g,
                                                       const char *token, bool as_source,
                                                       const char **out_id)
{
    if (strcmp(token, "[*]") == 0) {
        const char *id = as_source ? "__start" : "__end";
        struct yetty_ycore_void_result r = ensure_pseudo(g, id, /*final=*/!as_source);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "state: pseudostate failed");
        *out_id = id;
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result r = ensure_plain_state(g, token);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "state: state failed");
    *out_id = token;
    return YETTY_OK_VOID();
}

/* `state "desc" as ID` — declare ID with a human label. */
static struct yetty_ycore_void_result parse_alias(struct yetty_ydiagram_graph *g, char *rest)
{
    char *q1 = strchr(rest, '"');
    if (!q1) {
        return YETTY_OK_VOID(); /* not the quoted form; ignore */
    }
    char *q2 = strchr(q1 + 1, '"');
    if (!q2) {
        return YETTY_OK_VOID();
    }
    *q2 = '\0';
    const char *desc = q1 + 1;
    char *as = strstr(q2 + 1, " as ");
    if (!as) {
        return YETTY_OK_VOID();
    }
    char *id = trim(as + 4);
    if (!id[0]) {
        return YETTY_OK_VOID();
    }
    if (yetty_ydiagram_graph_find_node(g, id)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result r =
        yetty_ydiagram_graph_add_node(g, id, desc, YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "state: alias add failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result parse_transition(struct yetty_ydiagram_graph *g, char *line,
                                                       char *arrow)
{
    *arrow = '\0';
    char *left = trim(line);
    char *right = trim(arrow + 3); /* skip "-->" */

    /* Optional ` : label` on the target side. */
    char *label = NULL;
    char *colon = strchr(right, ':');
    if (colon) {
        *colon = '\0';
        label = trim(colon + 1);
        right = trim(right);
    }
    if (!left[0] || !right[0]) {
        return YETTY_OK_VOID();
    }

    const char *src_id = NULL;
    const char *tgt_id = NULL;
    struct yetty_ycore_void_result sr = resolve_endpoint(g, left, /*as_source=*/true, &src_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "state: resolve source failed");
    struct yetty_ycore_void_result tr = resolve_endpoint(g, right, /*as_source=*/false, &tgt_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "state: resolve target failed");

    struct yetty_ycore_int_result er = yetty_ydiagram_graph_add_edge(
        g, src_id, tgt_id, (label && label[0]) ? label : NULL, &g->default_edge_style);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "state: add edge failed");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ydiagram_state_parse(const char *input, size_t len,
                                                          struct yetty_ydiagram_graph *out_graph)
{
    if (!input || !out_graph) {
        return YETTY_ERR(yetty_ycore_void, "state_parse: NULL input or graph");
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "state_parse: oom");
    }
    memcpy(buf, input, len);
    buf[len] = '\0';

    out_graph->direction = YETTY_YDIAGRAM_DIR_TB;

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    char *save = NULL;
    for (char *raw = strtok_r(buf, "\n", &save); raw; raw = strtok_r(NULL, "\n", &save)) {
        char *line = trim(raw);
        if (!line[0] || (line[0] == '%' && line[1] == '%')) {
            continue;
        }
        if (strncmp(line, "stateDiagram", 12) == 0) {
            continue;
        }
        if (strncmp(line, "note", 4) == 0 || strcmp(line, "}") == 0 ||
            strcmp(line, "end note") == 0) {
            continue;
        }
        if (strncmp(line, "direction", 9) == 0) {
            char *d = trim(line + 9);
            if (strcmp(d, "LR") == 0) {
                out_graph->direction = YETTY_YDIAGRAM_DIR_LR;
            } else if (strcmp(d, "RL") == 0) {
                out_graph->direction = YETTY_YDIAGRAM_DIR_RL;
            } else if (strcmp(d, "BT") == 0) {
                out_graph->direction = YETTY_YDIAGRAM_DIR_BT;
            } else {
                out_graph->direction = YETTY_YDIAGRAM_DIR_TB;
            }
            continue;
        }

        char *arrow = strstr(line, "-->");
        if (arrow) {
            result = parse_transition(out_graph, line, arrow);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            continue;
        }

        if (strncmp(line, "state", 5) == 0 && (line[5] == ' ' || line[5] == '\t')) {
            /* `state "desc" as ID` alias, or `state X {` composite header. */
            char *rest = trim(line + 5);
            if (strchr(rest, '"')) {
                result = parse_alias(out_graph, rest);
                if (YETTY_IS_ERR(result)) {
                    break;
                }
            } else {
                /* `state X {` / `state X <<fork>>` — register the bare id. */
                char *brace = strchr(rest, '{');
                if (brace) {
                    *brace = '\0';
                }
                char *angle = strstr(rest, "<<");
                if (angle) {
                    *angle = '\0';
                }
                char *id = trim(rest);
                if (id[0]) {
                    result = ensure_plain_state(out_graph, id);
                    if (YETTY_IS_ERR(result)) {
                        break;
                    }
                }
            }
            continue;
        }

        /* Bare identifier — a standalone state declaration. */
        if (!strchr(line, ' ') && !strchr(line, ':')) {
            result = ensure_plain_state(out_graph, line);
            if (YETTY_IS_ERR(result)) {
                break;
            }
        }
    }

    free(buf);
    return result;
}
