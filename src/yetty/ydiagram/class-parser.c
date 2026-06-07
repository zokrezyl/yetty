/*
 * class-parser.c — Mermaid classDiagram → graph IR (record nodes + UML arrows).
 *
 * Classes become record nodes (title compartment + attributes + methods); the
 * Sugiyama layout + renderer draw them. Relationship operators map to the UML
 * arrow terminals added to the IR:
 *
 *   A <|-- B   inheritance  (hollow triangle at A, the parent)
 *   A <|.. B   realization  (hollow triangle, dashed)
 *   A *-- B    composition  (filled diamond at A)
 *   A o-- B    aggregation  (hollow diamond at A)
 *   A --> B    association  (arrow at B)
 *   A ..> B    dependency   (arrow at B, dashed)
 *   A -- B     link
 *
 * Members:
 *   class Foo { +int age \n +bar() }     block form
 *   Foo : +int age                        per-line form
 *   <<interface>>                         stereotype
 *
 * Attributes (no `()`) render above the methods divider; methods below.
 */

#include <yetty/ydiagram/diagrams.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydiagram/graph-ir.h>
#include <yetty/yplatform/compat.h>

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

static bool is_relop_run(const char *s, size_t n)
{
    bool has_link = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '-' || c == '.') {
            has_link = true;
        } else if (c != '<' && c != '>' && c != '|' && c != '*' && c != 'o') {
            return false;
        }
    }
    return has_link && n >= 2;
}

static struct yetty_ycore_void_result ensure_class(struct yetty_ydiagram_graph *g, const char *name)
{
    struct yetty_ydiagram_node *existing = yetty_ydiagram_graph_find_node(g, name);
    if (existing) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_int_result r =
        yetty_ydiagram_graph_add_node(g, name, name, YETTY_YDIAGRAM_SHAPE_RECTANGLE);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "class: add node failed");
    struct yetty_ydiagram_node *n = &g->nodes[r.value];
    n->is_record = true;
    n->method_start = 0;
    return YETTY_OK_VOID();
}

/* A `<<stereotype>>` line sets the node's stereotype; anything else is a row. */
static struct yetty_ycore_void_result add_member(struct yetty_ydiagram_graph *g, const char *name,
                                                 const char *member)
{
    struct yetty_ycore_void_result er = ensure_class(g, name);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "class: ensure for member failed");
    struct yetty_ydiagram_node *n = yetty_ydiagram_graph_find_node(g, name);
    if (!n) {
        return YETTY_ERR(yetty_ycore_void, "class: node vanished");
    }
    if (!member[0]) {
        return YETTY_OK_VOID();
    }
    if (member[0] == '<' && member[1] == '<') {
        const char *p = member + 2;
        const char *end = strstr(p, ">>");
        size_t slen = end ? (size_t)(end - p) : strlen(p);
        char *copy = malloc(slen + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "class: stereotype oom");
        }
        memcpy(copy, p, slen);
        copy[slen] = '\0';
        free(n->stereotype);
        n->stereotype = copy;
        return YETTY_OK_VOID();
    }
    return yetty_ydiagram_node_add_row(n, member);
}

static void parse_relop(const char *op, struct yetty_ydiagram_edge_style *style)
{
    size_t n = strlen(op);
    style->line_style = strchr(op, '.') ? YETTY_YDIAGRAM_LINE_DASHED : YETTY_YDIAGRAM_LINE_SOLID;
    style->source_arrow = YETTY_YDIAGRAM_ARROW_NONE;
    style->target_arrow = YETTY_YDIAGRAM_ARROW_NONE;

    /* Left (source) terminal. */
    if (n >= 2 && op[0] == '<' && op[1] == '|') {
        style->source_arrow = YETTY_YDIAGRAM_ARROW_TRIANGLE;
    } else if (op[0] == '*') {
        style->source_arrow = YETTY_YDIAGRAM_ARROW_DIAMOND;
    } else if (op[0] == 'o') {
        style->source_arrow = YETTY_YDIAGRAM_ARROW_DIAMOND_OPEN;
    } else if (op[0] == '<') {
        style->source_arrow = YETTY_YDIAGRAM_ARROW_NORMAL;
    }

    /* Right (target) terminal. */
    if (n >= 2 && op[n - 1] == '>' && op[n - 2] == '|') {
        style->target_arrow = YETTY_YDIAGRAM_ARROW_TRIANGLE;
    } else if (op[n - 1] == '*') {
        style->target_arrow = YETTY_YDIAGRAM_ARROW_DIAMOND;
    } else if (op[n - 1] == 'o') {
        style->target_arrow = YETTY_YDIAGRAM_ARROW_DIAMOND_OPEN;
    } else if (op[n - 1] == '>') {
        style->target_arrow = YETTY_YDIAGRAM_ARROW_NORMAL;
    }
}

/* Find the relationship operator run inside `line` and split it into the left
 * id, the operator (copied into `op_out`), and the right id (both ids are
 * NUL-terminated in place). Returns true on a well-formed relationship. */
static bool split_relationship(char *line, char **left, char *op_out, size_t op_cap, char **right)
{
    size_t len = strlen(line);
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        /* Start a run on any relop char except 'o' (ambiguous with ids). */
        if (c != '<' && c != '>' && c != '|' && c != '*' && c != '-' && c != '.') {
            continue;
        }
        size_t j = i;
        while (j < len) {
            char d = line[j];
            if (d == '<' || d == '>' || d == '|' || d == '*' || d == 'o' || d == '-' || d == '.') {
                j++;
            } else {
                break;
            }
        }
        size_t start = i;
        if (start > 0 && line[start - 1] == 'o') {
            start--; /* pull in a leading aggregation 'o' */
        }
        size_t oplen = j - start;
        if (!is_relop_run(line + start, oplen)) {
            continue;
        }
        if (oplen >= op_cap) {
            oplen = op_cap - 1;
        }
        memcpy(op_out, line + start, oplen);
        op_out[oplen] = '\0';

        line[start] = '\0'; /* terminate the left id */
        *left = trim(line);
        *right = trim(line + j); /* the rest of the (NUL-terminated) line */
        return (*left)[0] != '\0' && (*right)[0] != '\0';
    }
    return false;
}

struct yetty_ycore_void_result yetty_ydiagram_class_parse(const char *input, size_t len,
                                                          struct yetty_ydiagram_graph *out_graph)
{
    if (!input || !out_graph) {
        return YETTY_ERR(yetty_ycore_void, "class_parse: NULL input or graph");
    }

    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "class_parse: oom");
    }
    memcpy(buf, input, len);
    buf[len] = '\0';

    out_graph->direction = YETTY_YDIAGRAM_DIR_TB;

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    char current_class[128] = {0}; /* non-empty while inside `class X { ... }` */
    char *save = NULL;

    for (char *raw = strtok_r(buf, "\n", &save); raw; raw = strtok_r(NULL, "\n", &save)) {
        char *line = trim(raw);
        if (!line[0] || (line[0] == '%' && line[1] == '%')) {
            continue;
        }
        if (strncmp(line, "classDiagram", 12) == 0 || strncmp(line, "direction", 9) == 0) {
            continue;
        }

        /* Inside a class block: members until the closing brace. */
        if (current_class[0]) {
            if (line[0] == '}') {
                current_class[0] = '\0';
                continue;
            }
            result = add_member(out_graph, current_class, line);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            continue;
        }

        if (strncmp(line, "class ", 6) == 0) {
            char *rest = trim(line + 6);
            char *brace = strchr(rest, '{');
            if (brace) {
                *brace = '\0';
            }
            char *stereo = strstr(rest, "<<"); /* only an inline `class Foo <<x>>` */
            char *stereo_text = NULL;
            if (stereo) {
                stereo_text = strdup(stereo);
                *stereo = '\0';
            }
            char *name = trim(rest);
            if (name[0]) {
                result = ensure_class(out_graph, name);
                if (YETTY_IS_OK(result) && stereo_text) {
                    result = add_member(out_graph, name, stereo_text);
                }
                if (YETTY_IS_OK(result) && brace) {
                    snprintf(current_class, sizeof(current_class), "%s", name);
                }
            }
            free(stereo_text);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            continue;
        }

        /* `ClassName : member` shorthand. */
        char *colon = strchr(line, ':');
        char *relstart = strpbrk(line, "<>|*-.");
        if (colon && (!relstart || colon < relstart)) {
            *colon = '\0';
            char *name = trim(line);
            char *member = trim(colon + 1);
            if (name[0]) {
                result = add_member(out_graph, name, member);
                if (YETTY_IS_ERR(result)) {
                    break;
                }
            }
            continue;
        }

        /* Relationship: LEFT <op> RIGHT [: label]. */
        char *label = NULL;
        char *lbl_colon = strchr(line, ':');
        if (lbl_colon) {
            *lbl_colon = '\0';
            label = trim(lbl_colon + 1);
        }
        char *left = NULL, *right = NULL;
        char op[16];
        if (split_relationship(line, &left, op, sizeof(op), &right)) {
            result = ensure_class(out_graph, left);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            result = ensure_class(out_graph, right);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            struct yetty_ydiagram_edge_style style = out_graph->default_edge_style;
            parse_relop(op, &style);
            struct yetty_ycore_int_result er = yetty_ydiagram_graph_add_edge(
                out_graph, left, right, (label && label[0]) ? label : NULL, &style);
            if (YETTY_IS_ERR(er)) {
                result = YETTY_ERR(yetty_ycore_void, "class: add edge failed", er);
                break;
            }
        }
    }

    /* Place the attributes/methods divider: first row that looks like a method
     * (contains '('). */
    if (YETTY_IS_OK(result)) {
        for (size_t i = 0; i < out_graph->node_count; i++) {
            struct yetty_ydiagram_node *n = &out_graph->nodes[i];
            if (!n->is_record) {
                continue;
            }
            n->method_start = n->row_count;
            for (size_t r = 0; r < n->row_count; r++) {
                if (strchr(n->rows[r], '(')) {
                    n->method_start = r;
                    break;
                }
            }
        }
    }

    free(buf);
    return result;
}
