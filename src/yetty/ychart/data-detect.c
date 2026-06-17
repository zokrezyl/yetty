/*
 * data-detect.c — format detection, the `#ychart` directive, and the
 * detect → parse dispatch.
 *
 * A ychart document self-identifies one of three ways:
 *   - a leading `#ychart ...` directive line (then CSV/TSV body), or
 *   - JSON with a top-level "chart" key, or
 *   - YAML with a top-level `chart:` key.
 *
 * The sniffers are deliberately conservative: a plain CSV / JSON / YAML file
 * with no chart marker is NOT claimed (ycat would otherwise hijack every data
 * file). The standalone tool can still force a chart via an explicit kind.
 */

#include <yetty/ychart/data-parser.h>

#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

/*=============================================================================
 * Format name mapping
 *===========================================================================*/

const char *yetty_ychart_format_name(enum yetty_ychart_format format)
{
    switch (format) {
    case YETTY_YCHART_FORMAT_CSV:
        return "csv";
    case YETTY_YCHART_FORMAT_JSON:
        return "json";
    case YETTY_YCHART_FORMAT_YAML:
        return "yaml";
    case YETTY_YCHART_FORMAT_UNKNOWN:
        break;
    }
    return "unknown";
}

enum yetty_ychart_format yetty_ychart_format_from_name(const char *name)
{
    if (!name) {
        return YETTY_YCHART_FORMAT_UNKNOWN;
    }
    if (strcasecmp(name, "csv") == 0 || strcasecmp(name, "tsv") == 0) {
        return YETTY_YCHART_FORMAT_CSV;
    }
    if (strcasecmp(name, "json") == 0) {
        return YETTY_YCHART_FORMAT_JSON;
    }
    if (strcasecmp(name, "yaml") == 0 || strcasecmp(name, "yml") == 0) {
        return YETTY_YCHART_FORMAT_YAML;
    }
    return YETTY_YCHART_FORMAT_UNKNOWN;
}

/*=============================================================================
 * Directive
 *===========================================================================*/

int yetty_ychart_find_directive(const char *input, size_t len, const char **out_dir,
                                 size_t *out_dir_len, size_t *out_body_off)
{
    if (out_dir) {
        *out_dir = NULL;
    }
    if (out_dir_len) {
        *out_dir_len = 0;
    }
    if (out_body_off) {
        *out_body_off = 0;
    }
    if (!input) {
        return 0;
    }

    size_t i = 0;
    /* Skip leading blank lines / whitespace before the first content line. */
    while (i < len &&
           (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r')) {
        i++;
    }
    if (i >= len || input[i] != '#') {
        return 0;
    }
    size_t pos = i + 1;
    while (pos < len && (input[pos] == ' ' || input[pos] == '\t')) {
        pos++; /* allow "# ychart" */
    }
    static const char keyword[] = "ychart";
    size_t keyword_len = sizeof(keyword) - 1;
    if (pos + keyword_len > len || strncasecmp(input + pos, keyword, keyword_len) != 0) {
        return 0;
    }
    pos += keyword_len;
    /* Must be followed by whitespace or end-of-line (so "#ychart-foo" is not
     * mistaken for a directive). */
    if (pos < len && input[pos] != ' ' && input[pos] != '\t' && input[pos] != '\n' &&
        input[pos] != '\r') {
        return 0;
    }
    /* The directive arguments run to end of line. */
    size_t arg_start = pos;
    while (arg_start < len && (input[arg_start] == ' ' || input[arg_start] == '\t')) {
        arg_start++;
    }
    size_t line_end = arg_start;
    while (line_end < len && input[line_end] != '\n' && input[line_end] != '\r') {
        line_end++;
    }
    size_t body = line_end;
    while (body < len && (input[body] == '\n' || input[body] == '\r')) {
        body++;
    }
    if (out_dir) {
        *out_dir = input + arg_start;
    }
    if (out_dir_len) {
        *out_dir_len = line_end - arg_start;
    }
    if (out_body_off) {
        *out_body_off = body;
    }
    return 1;
}

/* Read a directive token: either `key=value` or `key="quoted value"`. Advances
 * *pos. Writes key/val spans. Returns 1 if a token was read, 0 at end. */
static int next_directive_token(const char *line, size_t len, size_t *pos, const char **key,
                                size_t *key_len, const char **val, size_t *val_len)
{
    size_t i = *pos;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i >= len) {
        *pos = i;
        return 0;
    }
    const char *kstart = line + i;
    while (i < len && line[i] != '=' && line[i] != ' ' && line[i] != '\t') {
        i++;
    }
    *key = kstart;
    *key_len = (size_t)(line + i - kstart);
    *val = NULL;
    *val_len = 0;
    if (i < len && line[i] == '=') {
        i++;
        if (i < len && (line[i] == '"' || line[i] == '\'')) {
            char quote = line[i];
            i++;
            const char *vstart = line + i;
            while (i < len && line[i] != quote) {
                i++;
            }
            *val = vstart;
            *val_len = (size_t)(line + i - vstart);
            if (i < len) {
                i++; /* closing quote */
            }
        } else {
            const char *vstart = line + i;
            while (i < len && line[i] != ' ' && line[i] != '\t') {
                i++;
            }
            *val = vstart;
            *val_len = (size_t)(line + i - vstart);
        }
    }
    *pos = i;
    return 1;
}

static int span_eq(const char *span, size_t span_len, const char *literal)
{
    return strlen(literal) == span_len && strncasecmp(span, literal, span_len) == 0;
}

static bool truthy(const char *val, size_t val_len)
{
    return span_eq(val, val_len, "1") || span_eq(val, val_len, "on") ||
           span_eq(val, val_len, "yes") || span_eq(val, val_len, "true");
}

struct yetty_ycore_void_result yetty_ychart_parse_directive(const char *line, size_t line_len,
                                                             struct yetty_ychart_chart *chart)
{
    if (!chart) {
        return YETTY_ERR(yetty_ycore_void, "parse_directive: NULL chart");
    }
    if (!line) {
        return YETTY_OK_VOID();
    }
    size_t pos = 0;
    const char *key, *val;
    size_t key_len, val_len;
    char scratch[256];

    while (next_directive_token(line, line_len, &pos, &key, &key_len, &val, &val_len)) {
        if (!val) {
            continue;
        }
        /* NUL-terminate the value into scratch for the string setters. */
        size_t copy_len = val_len < sizeof(scratch) - 1 ? val_len : sizeof(scratch) - 1;
        memcpy(scratch, val, copy_len);
        scratch[copy_len] = '\0';

        if (span_eq(key, key_len, "type") || span_eq(key, key_len, "kind")) {
            chart->kind = yetty_ychart_kind_from_name(scratch);
        } else if (span_eq(key, key_len, "title")) {
            struct yetty_ycore_void_result set = yetty_ychart_set_title(chart, scratch);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set, "parse_directive: title");
        } else if (span_eq(key, key_len, "x") || span_eq(key, key_len, "xlabel")) {
            struct yetty_ycore_void_result set =
                yetty_ychart_set_axis_labels(chart, scratch, NULL);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set, "parse_directive: xlabel");
        } else if (span_eq(key, key_len, "y") || span_eq(key, key_len, "ylabel")) {
            struct yetty_ycore_void_result set =
                yetty_ychart_set_axis_labels(chart, NULL, scratch);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set, "parse_directive: ylabel");
        } else if (span_eq(key, key_len, "legend")) {
            chart->show_legend = truthy(val, val_len);
        } else if (span_eq(key, key_len, "values")) {
            chart->show_values = truthy(val, val_len);
        } else if (span_eq(key, key_len, "stacked")) {
            chart->stacked = truthy(val, val_len);
        }
        /* unknown keys ignored */
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Format detection
 *===========================================================================*/

static const char *path_ext(const char *path)
{
    if (!path) {
        return NULL;
    }
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && slash > dot)) {
        return NULL;
    }
    return dot + 1;
}

/* Detect format of the BODY (directive already stripped by the caller). */
static enum yetty_ychart_format detect_body_format(const char *body, size_t len, const char *path)
{
    const char *ext = path_ext(path);
    if (ext) {
        if (strcasecmp(ext, "json") == 0) {
            return YETTY_YCHART_FORMAT_JSON;
        }
        if (strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0) {
            return YETTY_YCHART_FORMAT_YAML;
        }
        if (strcasecmp(ext, "csv") == 0 || strcasecmp(ext, "tsv") == 0) {
            return YETTY_YCHART_FORMAT_CSV;
        }
    }

    /* First non-whitespace byte. */
    size_t i = 0;
    while (i < len && (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' || body[i] == '\r')) {
        i++;
    }
    if (i >= len) {
        return YETTY_YCHART_FORMAT_CSV;
    }
    if (body[i] == '{' || body[i] == '[') {
        return YETTY_YCHART_FORMAT_JSON;
    }

    /* Distinguish YAML from CSV on the first content line: a "key:" head with
     * no comma before the colon reads as YAML; a comma-bearing line reads as
     * CSV. */
    size_t line_end = i;
    while (line_end < len && body[line_end] != '\n' && body[line_end] != '\r') {
        line_end++;
    }
    const char *colon = memchr(body + i, ':', line_end - i);
    const char *comma = memchr(body + i, ',', line_end - i);
    if (colon && (!comma || colon < comma)) {
        return YETTY_YCHART_FORMAT_YAML;
    }
    return YETTY_YCHART_FORMAT_CSV;
}

enum yetty_ychart_format yetty_ychart_detect_format(const char *input, size_t len,
                                                      const char *path)
{
    if (!input) {
        return YETTY_YCHART_FORMAT_UNKNOWN;
    }
    size_t body_off = 0;
    yetty_ychart_find_directive(input, len, NULL, NULL, &body_off);
    return detect_body_format(input + body_off, len - body_off, path);
}

/*=============================================================================
 * can_parse — conservative chart sniff for ycat
 *===========================================================================*/

/* Search the first `scan` bytes for a needle (case-sensitive). */
static int contains(const char *hay, size_t hay_len, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hay_len) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= hay_len; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

int yetty_ychart_can_parse(const char *input, size_t len)
{
    if (!input || len == 0) {
        return 0;
    }
    if (yetty_ychart_find_directive(input, len, NULL, NULL, NULL)) {
        return 1;
    }
    size_t scan = len < 4096u ? len : 4096u;

    /* JSON with a top-level "chart" key. */
    size_t i = 0;
    while (i < scan &&
           (input[i] == ' ' || input[i] == '\t' || input[i] == '\n' || input[i] == '\r')) {
        i++;
    }
    if (i < scan && input[i] == '{') {
        if (contains(input + i, scan - i, "\"chart\"")) {
            return 1;
        }
    }

    /* YAML with a `chart:` key at the start of some line. */
    for (size_t pos = 0; pos < scan;) {
        size_t ls = pos;
        while (ls < scan && (input[ls] == ' ' || input[ls] == '\t')) {
            ls++;
        }
        if (ls + 6 <= scan && strncasecmp(input + ls, "chart:", 6) == 0) {
            return 1;
        }
        while (pos < scan && input[pos] != '\n') {
            pos++;
        }
        pos++;
    }
    return 0;
}

/*=============================================================================
 * detect → parse
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_parse(const char *input, size_t len, const char *path,
                                                   struct yetty_ychart_chart *chart)
{
    if (!input || !chart) {
        return YETTY_ERR(yetty_ycore_void, "parse: NULL input or chart");
    }

    const char *dir = NULL;
    size_t dir_len = 0;
    size_t body_off = 0;
    if (yetty_ychart_find_directive(input, len, &dir, &dir_len, &body_off)) {
        struct yetty_ycore_void_result dr = yetty_ychart_parse_directive(dir, dir_len, chart);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "parse: directive");
    }

    const char *body = input + body_off;
    size_t body_len = len - body_off;
    enum yetty_ychart_format format = detect_body_format(body, body_len, path);

    switch (format) {
    case YETTY_YCHART_FORMAT_JSON:
        return yetty_ychart_parse_json(body, body_len, chart);
    case YETTY_YCHART_FORMAT_YAML:
        return yetty_ychart_parse_yaml(body, body_len, chart);
    case YETTY_YCHART_FORMAT_CSV:
    case YETTY_YCHART_FORMAT_UNKNOWN:
    default:
        return yetty_ychart_parse_csv(body, body_len, chart);
    }
}
