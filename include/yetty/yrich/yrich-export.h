#ifndef YETTY_YRICH_YRICH_EXPORT_H
#define YETTY_YRICH_YRICH_EXPORT_H

/*
 * yrich-export — plain-text, Markdown, HTML, and RTF import/export for ydoc.
 *
 * Compatibility layers over the native model (roadmap Phase 5). Export walks
 * the paragraph tree and emits block markers (headings, lists, checklists,
 * horizontal rules) with inline bold / italic / strike; import parses the same
 * back into paragraphs + style runs. Plain text is the lossy text-only path.
 * These use only the public ydoc API — no model internals.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write every paragraph's text, one per line (formatting dropped). */
struct yetty_ycore_void_result yetty_yrich_ydoc_export_text_file(
    struct yetty_yclass_object *doc_obj, const char *path);

/* Write the document as Markdown: `#`..`######` headings, `- `/`1. ` lists
 * (indented by nesting level), `- [ ]`/`- [x]` checklists, `---` rules, and
 * inline `**bold**` / `_italic_` / `~~strike~~`. */
struct yetty_ycore_void_result yetty_yrich_ydoc_export_markdown_file(
    struct yetty_yclass_object *doc_obj, const char *path);

/* Load a plain-text file, one paragraph per line, into a fresh ydoc. */
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_text_file(const char *path);

/* Parse a Markdown file (the subset export emits) into a fresh ydoc. */
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_markdown_file(const char *path);

/* Write the document as HTML: <h1>..<h6> headings, <p> paragraphs, <hr>
 * rules, and inline <b>/<i>/<s>, with &lt;/&gt;/&amp; escaping. */
struct yetty_ycore_void_result yetty_yrich_ydoc_export_html_file(
    struct yetty_yclass_object *doc_obj, const char *path);

/* Parse an HTML file (the subset export emits) into a fresh ydoc. */
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_html_file(const char *path);

/* Write the document as RTF: `\s1`..`\s6` heading paragraph styles, a bottom
 * border paragraph for horizontal rules, and inline `\b`/`\i`/`\strike`, with
 * `\`/`{`/`}` escaping. */
struct yetty_ycore_void_result yetty_yrich_ydoc_export_rtf_file(struct yetty_yclass_object *doc_obj,
                                                                const char *path);

/* Parse an RTF file (the subset export emits) into a fresh ydoc. */
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_import_rtf_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_EXPORT_H */
