/*
 * er-parser.c — Mermaid erDiagram → graph IR (entity records + crow's-foot).
 *
 * Entities become single-compartment record nodes (title + attribute rows).
 * Relationship cardinality runs map to the ER crow's-foot terminals added to
 * the IR:
 *
 *   CUSTOMER ||--o{ ORDER : places
 *   ORDER    ||--|{ LINE-ITEM : contains
 *   CUSTOMER { string name \n string id PK }
 *
 * Cardinality tokens (each 2 chars, one per side):
 *   ||  exactly one        |o / o|  zero or one
 *   }| / |{  one or many    }o / o{  zero or many
 * Link `--` solid (identifying) / `..` dashed (non-identifying).
 */

#include <yetty/ydiagram/diagrams.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydiagram/graph-ir.h>

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

static bool is_er_char(char c)
{
    return c == '|' || c == 'o' || c == '{' || c == '}' || c == '-' || c == '.';
}

/* Map a 2-char cardinality token to a crow's-foot terminal. */
static enum yetty_ydiagram_arrow_style card_to_arrow(char a, char b)
{
    bool many = (a == '{' || a == '}' || b == '{' || b == '}');
    bool optional = (a == 'o' || b == 'o');
    if (many) {
        return optional ? YETTY_YDIAGRAM_ARROW_CROW_MANY_OPT : YETTY_YDIAGRAM_ARROW_CROW_MANY;
    }
    return optional ? YETTY_YDIAGRAM_ARROW_CROW_ONE_OPT : YETTY_YDIAGRAM_ARROW_CROW_ONE;
}

static struct yetty_ycore_void_result ensure_entity(struct yetty_ydiagram_graph *g,
                                                    const char *name)
{
    if (yetty_ydiagram_graph_find_node(g, name)) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result r =
        yetty_ydiagram_graph_add_node(g, name, name, YETTY_YDIAGRAM_SHAPE_RECTANGLE);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "er: add entity failed");
    g->nodes[r.value].is_record = true; /* method_start stays 0 → no inner divider */
    return YETTY_OK_VOID();
}

/* Find the cardinality run `<card><link><card>` (e.g. "||--o{"). A run is a
 * contiguous block of ER chars, length >= 5, containing "--" or "..". This
 * keeps hyphenated entity names (LINE-ITEM) from matching. */
static bool split_relationship(char *line, char **left, char *op_out, size_t op_cap, char **right)
{
    size_t len = strlen(line);
    for (size_t i = 0; i < len; i++) {
        if (!is_er_char(line[i])) {
            continue;
        }
        size_t j = i;
        while (j < len && is_er_char(line[j])) {
            j++;
        }
        size_t oplen = j - i;
        bool has_link = false;
        for (size_t k = i + 1; k < j; k++) {
            if ((line[k] == '-' && line[k - 1] == '-') || (line[k] == '.' && line[k - 1] == '.')) {
                has_link = true;
                break;
            }
        }
        if (oplen < 5 || !has_link) {
            continue;
        }
        if (oplen >= op_cap) {
            oplen = op_cap - 1;
        }
        memcpy(op_out, line + i, oplen);
        op_out[oplen] = '\0';
        line[i] = '\0';
        *left = trim(line);
        *right = trim(line + j);
        return (*left)[0] != '\0' && (*right)[0] != '\0';
    }
    return false;
}

struct yetty_ycore_void_result yetty_ydiagram_er_parse(const char *input, size_t len,
                                                       struct yetty_ydiagram_graph *out_graph)
{
    if (!input || !out_graph) {
        return YETTY_ERR(yetty_ycore_void, "er_parse: NULL input or graph");
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "er_parse: oom");
    }
    memcpy(buf, input, len);
    buf[len] = '\0';

    out_graph->direction = YETTY_YDIAGRAM_DIR_TB;

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    char current_entity[128] = {0};
    char *save = NULL;

    for (char *raw = strtok_r(buf, "\n", &save); raw; raw = strtok_r(NULL, "\n", &save)) {
        char *line = trim(raw);
        if (!line[0] || (line[0] == '%' && line[1] == '%')) {
            continue;
        }
        if (strncmp(line, "erDiagram", 9) == 0 || strncmp(line, "direction", 9) == 0) {
            continue;
        }

        if (current_entity[0]) {
            if (line[0] == '}') {
                current_entity[0] = '\0';
                continue;
            }
            result = ensure_entity(out_graph, current_entity);
            if (YETTY_IS_OK(result)) {
                struct yetty_ydiagram_node *n =
                    yetty_ydiagram_graph_find_node(out_graph, current_entity);
                if (n) {
                    result = yetty_ydiagram_node_add_row(n, line);
                }
            }
            if (YETTY_IS_ERR(result)) {
                break;
            }
            continue;
        }

        /* `ENTITY {` opens an attribute block. */
        char *brace = strchr(line, '{');
        char *relstart = NULL;
        for (char *p = line; *p; p++) {
            if (is_er_char(*p) && (*p == '|' || *p == '{' || *p == '}')) {
                relstart = p;
                break;
            }
        }
        if (brace && (!relstart || brace < relstart)) {
            *brace = '\0';
            char *name = trim(line);
            if (name[0]) {
                snprintf(current_entity, sizeof(current_entity), "%s", name);
                result = ensure_entity(out_graph, name);
                if (YETTY_IS_ERR(result)) {
                    break;
                }
            }
            continue;
        }

        /* Relationship: A <card> B [: label]. */
        char *label = NULL;
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            label = trim(colon + 1);
        }
        char *left = NULL, *right = NULL;
        char op[16];
        if (split_relationship(line, &left, op, sizeof(op), &right)) {
            result = ensure_entity(out_graph, left);
            if (YETTY_IS_OK(result)) {
                result = ensure_entity(out_graph, right);
            }
            if (YETTY_IS_ERR(result)) {
                break;
            }
            struct yetty_ydiagram_edge_style style = out_graph->default_edge_style;
            size_t oplen = strlen(op);
            style.line_style =
                strchr(op, '.') ? YETTY_YDIAGRAM_LINE_DASHED : YETTY_YDIAGRAM_LINE_SOLID;
            style.source_arrow = card_to_arrow(op[0], op[1]);
            style.target_arrow = card_to_arrow(op[oplen - 1], op[oplen - 2]);
            struct yetty_ycore_int_result er = yetty_ydiagram_graph_add_edge(
                out_graph, left, right, (label && label[0]) ? label : NULL, &style);
            if (YETTY_IS_ERR(er)) {
                result = YETTY_ERR(yetty_ycore_void, "er: add edge failed", er);
                break;
            }
        }
    }

    free(buf);
    return result;
}
