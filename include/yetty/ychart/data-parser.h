#ifndef YETTY_YCHART_DATA_PARSER_H
#define YETTY_YCHART_DATA_PARSER_H

/*
 * data-parser — turn input bytes (CSV / TSV / JSON / YAML) into a chart IR.
 *
 * Detection mirrors the rest of yetty: an optional self-identifying directive
 * line, then a content/extension sniff. A chart document is one of:
 *
 *   1. A `#ychart` directive line followed by CSV/TSV rows:
 *        #ychart type=column title="Quarterly revenue"
 *        Q1,120
 *        Q2,140,90        (extra columns = extra series)
 *
 *   2. JSON with a top-level "chart" key:
 *        { "chart": "pie", "title": "Share",
 *          "data": { "Chrome": 65, "Safari": 19, "Edge": 5 } }
 *
 *   3. YAML with a top-level `chart:` key (tolerant subset — see yaml-parser.c):
 *        chart: radar
 *        categories: [speed, power, range]
 *        series:
 *          - name: A
 *            values: [3, 5, 2]
 *
 * The directive's `type=` (or the json/yaml `chart:` value) sets
 * `chart->kind`; a caller override still wins. Each parser fills a
 * caller-initialised chart and bails on the first error.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ychart/chart-ir.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_ychart_format {
    YETTY_YCHART_FORMAT_UNKNOWN = 0,
    YETTY_YCHART_FORMAT_CSV, /* comma- or tab-separated rows */
    YETTY_YCHART_FORMAT_JSON,
    YETTY_YCHART_FORMAT_YAML,
};

const char *yetty_ychart_format_name(enum yetty_ychart_format format);
enum yetty_ychart_format yetty_ychart_format_from_name(const char *name);

/* Sniff the data format from content (and optional path extension). Skips a
 * leading `#ychart` directive line before sniffing the body. */
enum yetty_ychart_format yetty_ychart_detect_format(const char *input, size_t len,
                                                      const char *path);

/* Return non-zero if these bytes look like a ychart document — i.e. they
 * carry a `#ychart` directive line, or are JSON/YAML with a top-level chart
 * key. Used by ycat's content sniffer; deliberately conservative so a plain
 * CSV / JSON file is NOT hijacked as a chart. */
int yetty_ychart_can_parse(const char *input, size_t len);

/* Parse a `#ychart key=value ...` directive line into the chart (kind, title,
 * axis labels, toggles). `line`/`line_len` cover the text AFTER the leading
 * "#ychart" token. Unknown keys are ignored. */
struct yetty_ycore_void_result yetty_ychart_parse_directive(const char *line, size_t line_len,
                                                             struct yetty_ychart_chart *chart);

/* Locate a leading `#ychart` directive. On success returns 1 and writes the
 * directive's argument span (after "#ychart") to out_dir and out_dir_len, and
 * the offset of the body (first byte after the directive line) to
 * out_body_off. Returns 0 when there is no directive (body starts at 0). */
int yetty_ychart_find_directive(const char *input, size_t len, const char **out_dir,
                                 size_t *out_dir_len, size_t *out_body_off);

/* Format-specific parsers (body only — no directive). */
struct yetty_ycore_void_result yetty_ychart_parse_csv(const char *input, size_t len,
                                                       struct yetty_ychart_chart *chart);
struct yetty_ycore_void_result yetty_ychart_parse_json(const char *input, size_t len,
                                                        struct yetty_ychart_chart *chart);
struct yetty_ycore_void_result yetty_ychart_parse_yaml(const char *input, size_t len,
                                                        struct yetty_ychart_chart *chart);

/* Detect format, strip any directive, parse the body. Convenience used by the
 * high-level entry. */
struct yetty_ycore_void_result yetty_ychart_parse(const char *input, size_t len, const char *path,
                                                   struct yetty_ychart_chart *chart);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHART_DATA_PARSER_H */
