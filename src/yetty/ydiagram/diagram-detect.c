/*
 * diagram-detect.c — sniff the diagram family from its header keyword.
 *
 * Mirrors Mermaid's own dispatch: the first non-blank, non-comment line names
 * the diagram type. `graph` / `flowchart` are the flowchart family; the rest
 * route to the dedicated parsers in diagrams.h.
 */

#include <yetty/ydiagram/diagrams.h>

#include <string.h>

static bool starts_with(const char *s, size_t s_len, const char *prefix)
{
    size_t pl = strlen(prefix);
    return s_len >= pl && memcmp(s, prefix, pl) == 0;
}

enum yetty_ydiagram_kind yetty_ydiagram_detect(const char *input, size_t len)
{
    if (!input) {
        return YETTY_YDIAGRAM_KIND_UNKNOWN;
    }

    size_t i = 0;
    while (i < len) {
        /* Start of a line — skip leading whitespace. */
        while (i < len && (input[i] == ' ' || input[i] == '\t')) {
            i++;
        }
        size_t line_start = i;
        while (i < len && input[i] != '\n' && input[i] != '\r') {
            i++;
        }
        size_t line_len = i - line_start;
        /* Step past the newline for the next iteration. */
        while (i < len && (input[i] == '\n' || input[i] == '\r')) {
            i++;
        }

        if (line_len == 0) {
            continue; /* blank line */
        }
        const char *line = input + line_start;
        if (line_len >= 2 && line[0] == '%' && line[1] == '%') {
            continue; /* Mermaid comment */
        }

        if (starts_with(line, line_len, "stateDiagram")) {
            return YETTY_YDIAGRAM_KIND_STATE;
        }
        if (starts_with(line, line_len, "classDiagram")) {
            return YETTY_YDIAGRAM_KIND_CLASS;
        }
        if (starts_with(line, line_len, "erDiagram")) {
            return YETTY_YDIAGRAM_KIND_ER;
        }
        if (starts_with(line, line_len, "sequenceDiagram")) {
            return YETTY_YDIAGRAM_KIND_SEQUENCE;
        }
        if (starts_with(line, line_len, "graph") || starts_with(line, line_len, "flowchart")) {
            return YETTY_YDIAGRAM_KIND_FLOWCHART;
        }
        return YETTY_YDIAGRAM_KIND_UNKNOWN;
    }
    return YETTY_YDIAGRAM_KIND_UNKNOWN;
}
