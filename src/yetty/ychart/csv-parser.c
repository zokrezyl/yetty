/*
 * csv-parser.c — CSV / TSV rows → chart IR.
 *
 * Shapes accepted:
 *
 *   label,value                 single implicit series
 *   Apples,30
 *   Oranges,20
 *
 *   region,2020,2021            optional header row names the series; the
 *   North,10,20                 first column is the category label
 *   South,15,25
 *
 * The delimiter is auto-detected (comma, else tab). A header row is assumed
 * when the second column of the first row is non-numeric. Quoted fields
 * ("a,b") are supported. Blank lines and `#` comment lines are skipped.
 */

#include <yetty/ychart/data-parser.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Field / line scanning
 *===========================================================================*/

struct field {
    const char *ptr;
    size_t len;
};

/* Trim ASCII whitespace and one layer of surrounding quotes. */
static struct field trim_field(const char *ptr, size_t len)
{
    while (len > 0 && (*ptr == ' ' || *ptr == '\t' || *ptr == '\r')) {
        ptr++;
        len--;
    }
    while (len > 0 && (ptr[len - 1] == ' ' || ptr[len - 1] == '\t' || ptr[len - 1] == '\r')) {
        len--;
    }
    if (len >= 2 &&
        ((ptr[0] == '"' && ptr[len - 1] == '"') || (ptr[0] == '\'' && ptr[len - 1] == '\''))) {
        ptr++;
        len -= 2;
    }
    return (struct field){ptr, len};
}

/* Split one line into fields by `delim`, respecting double-quotes. Writes up
 * to `max_fields` entries and returns the count. */
static size_t split_line(const char *line, size_t len, char delim, struct field *out,
                         size_t max_fields)
{
    size_t count = 0;
    size_t i = 0;
    while (count < max_fields) {
        const char *start = line + i;
        bool quoted = false;
        size_t field_start = i;
        while (i < len) {
            char c = line[i];
            if (c == '"') {
                quoted = !quoted;
            } else if (c == delim && !quoted) {
                break;
            }
            i++;
        }
        out[count++] = trim_field(start, i - field_start);
        if (i >= len) {
            break;
        }
        i++; /* skip delimiter */
        /* Trailing delimiter → one empty final field. */
        if (i >= len) {
            if (count < max_fields) {
                out[count++] = (struct field){line + i, 0};
            }
            break;
        }
    }
    return count;
}

/* Parse a numeric field. Returns true and writes *out on success. Accepts a
 * leading +/-, digits, decimal point, exponent, and a trailing '%'. */
static bool parse_number(struct field f, double *out)
{
    if (f.len == 0) {
        return false;
    }
    char buf[64];
    size_t n = f.len < sizeof(buf) - 1 ? f.len : sizeof(buf) - 1;
    /* Drop a trailing percent sign and thousands separators. */
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        char c = f.ptr[i];
        if (c == ',' || c == '%' || c == ' ') {
            continue;
        }
        buf[w++] = c;
    }
    buf[w] = '\0';
    if (w == 0) {
        return false;
    }
    char *end = NULL;
    double value = strtod(buf, &end);
    if (end == buf || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

#define YCHART_MAX_COLUMNS 64

/*=============================================================================
 * Parser
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_parse_csv(const char *input, size_t len,
                                                       struct yetty_ychart_chart *chart)
{
    if (!input || !chart) {
        return YETTY_ERR(yetty_ycore_void, "parse_csv: NULL input or chart");
    }

    char delim = (memchr(input, ',', len) != NULL) ? ',' : '\t';

    /* Find the first content line (skip blanks and # comments). */
    struct field first[YCHART_MAX_COLUMNS];
    size_t first_cols = 0;
    size_t pos = 0;
    size_t first_line_end = 0;
    bool have_first = false;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && input[pos] != '\n') {
            pos++;
        }
        size_t line_len = pos - line_start;
        if (pos < len) {
            pos++; /* step past newline */
        }
        /* trim trailing CR for length test */
        const char *line = input + line_start;
        size_t trimmed = line_len;
        while (trimmed > 0 && (line[trimmed - 1] == '\r')) {
            trimmed--;
        }
        size_t lead = 0;
        while (lead < trimmed && (line[lead] == ' ' || line[lead] == '\t')) {
            lead++;
        }
        if (lead >= trimmed) {
            continue; /* blank */
        }
        if (line[lead] == '#') {
            continue; /* comment */
        }
        first_cols = split_line(line, trimmed, delim, first, YCHART_MAX_COLUMNS);
        first_line_end = line_start; /* remember where the first content line began */
        have_first = true;
        break;
    }
    if (!have_first || first_cols == 0) {
        return YETTY_ERR(yetty_ycore_void, "parse_csv: no data rows");
    }

    /* Header detection: a header row's value columns are non-numeric. With a
     * single column, a non-numeric first cell is the series name. */
    bool has_header;
    if (first_cols >= 2) {
        double probe;
        has_header = !parse_number(first[1], &probe);
    } else {
        double probe;
        has_header = !parse_number(first[0], &probe);
    }

    size_t series_count = first_cols >= 2 ? first_cols - 1 : 1;

    /* Create series with names from the header (or defaults). */
    for (size_t s = 0; s < series_count; s++) {
        char name_buf[128];
        const char *name = NULL;
        if (has_header) {
            struct field hf = first_cols >= 2 ? first[1 + s] : first[0];
            size_t n = hf.len < sizeof(name_buf) - 1 ? hf.len : sizeof(name_buf) - 1;
            memcpy(name_buf, hf.ptr, n);
            name_buf[n] = '\0';
            name = name_buf;
        } else {
            snprintf(name_buf, sizeof(name_buf), "Series %zu", s + 1);
            name = name_buf;
        }
        struct yetty_ycore_int_result sr = yetty_ychart_add_series(chart, name, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "parse_csv: add series");
    }

    /* Walk the data rows. If the first content line was a header, start after
     * it; otherwise re-include it as data. */
    size_t row_pos = has_header ? pos : first_line_end;
    size_t category_index = 0;

    while (row_pos < len) {
        size_t line_start = row_pos;
        while (row_pos < len && input[row_pos] != '\n') {
            row_pos++;
        }
        const char *line = input + line_start;
        size_t line_len = row_pos - line_start;
        if (row_pos < len) {
            row_pos++;
        }
        while (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
        }
        size_t lead = 0;
        while (lead < line_len && (line[lead] == ' ' || line[lead] == '\t')) {
            lead++;
        }
        if (lead >= line_len || line[lead] == '#') {
            continue;
        }

        struct field cells[YCHART_MAX_COLUMNS];
        size_t cols = split_line(line, line_len, delim, cells, YCHART_MAX_COLUMNS);
        if (cols == 0) {
            continue;
        }

        /* Category label + per-series values. */
        if (first_cols >= 2) {
            char label[256];
            size_t n = cells[0].len < sizeof(label) - 1 ? cells[0].len : sizeof(label) - 1;
            memcpy(label, cells[0].ptr, n);
            label[n] = '\0';
            struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "parse_csv: add category");

            for (size_t s = 0; s < series_count; s++) {
                double value = 0.0;
                if (1 + s < cols) {
                    parse_number(cells[1 + s], &value);
                }
                struct yetty_ycore_void_result pv = yetty_ychart_series_push(chart, s, value);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "parse_csv: push value");
            }
        } else {
            /* Single column: auto category index, one series. */
            char label[32];
            snprintf(label, sizeof(label), "%zu", category_index + 1);
            struct yetty_ycore_int_result cr = yetty_ychart_add_category(chart, label);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "parse_csv: add category");
            double value = 0.0;
            parse_number(cells[0], &value);
            struct yetty_ycore_void_result pv = yetty_ychart_series_push(chart, 0, value);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pv, "parse_csv: push value");
        }
        category_index++;
    }

    if (chart->category_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "parse_csv: no data rows after header");
    }
    return YETTY_OK_VOID();
}
