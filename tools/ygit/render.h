/* render.h — render raw file bytes (a git blob) to stdout.
 *
 * Inside yetty: detect the format and draw it as an inline figure (Markdown,
 * SVG, image, PDF, chart, …) or as syntax-highlighted source — reusing ycat's
 * handler registry and OSC envelope encoder. On a plain terminal: emit
 * 24-bit-coloured source, or advise for binary content.
 */

#ifndef YGIT_RENDER_H
#define YGIT_RENDER_H

#include <stddef.h>

/* Render `len` bytes named `name` (the base filename, used for format
 * detection) to stdout. `width_cells` is the terminal width in columns.
 * Returns 0 on success, nonzero on failure. */
int ygit_render_blob(const unsigned char *bytes, size_t len, const char *name, int width_cells);

/* Render a whole commit diff to stdout. Renderable assets (image / SVG / PDF /
 * chart) are drawn as a visual before/after — the old file beside the new one,
 * as figures, not XML. Text/source files are shown as a unified diff with the
 * code content syntax-highlighted per its grammar (beyond git's line-level
 * red/green). `width_cells` is the terminal width in columns. Returns 0 on
 * success, nonzero if any file failed to render. */
struct yetty_ygit_diff;
int ygit_render_diff(const struct yetty_ygit_diff *diff, int width_cells);

/* Draw the commit DAG as an inline GPU figure: lanes as coloured SDF lines,
 * commits as filled circle nodes (merges ringed), and each commit's hash / refs
 * / subject as text — the graph `git log --graph` can only approximate in ASCII.
 * `width_cells` is the terminal width in columns. Returns 0 on success, nonzero
 * on failure (the caller can fall back to the textual lane view). */
struct yetty_ygit_log;
struct yetty_ygit_graph;
int ygit_render_graph_figure(const struct yetty_ygit_log *log, const struct yetty_ygit_graph *graph,
                             int width_cells);

#endif /* YGIT_RENDER_H */
