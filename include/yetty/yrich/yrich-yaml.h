#ifndef YETTY_YRICH_YRICH_YAML_H
#define YETTY_YRICH_YRICH_YAML_H

/*
 * yrich-yaml — load yrich documents from the POC's YAML serialisation.
 *
 * Supported formats (matching yetty-poc/src/yetty/yrich/yrich-persist.h):
 *   - ydoc:         document.{pageWidth, margin, paragraphs[]}
 *   - yspreadsheet: spreadsheet.{rows, cols, columnWidths[], cells{}}
 *   - yslides:      presentation.{slideWidth, slideHeight, slides[]}
 *
 * Hex colours are accepted as "#AARRGGBB" and re-packed to the ABGR layout
 * used by ydraw.
 *
 * Documents are yclass objects (classes yrich:ydoc / yrich:spreadsheet /
 * yrich:slides). Each loader takes ownership of the document on success —
 * the caller frees it via yetty_yrich_document_destroy(NULL, obj). On error
 * the result carries no document and the caller does nothing.
 */

#include <stddef.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_load_yaml(const char *yaml, size_t len);

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_load_yaml_file(const char *path);

/* Serialise a ydoc back to the same YAML schema the loader reads. */
struct yetty_ycore_void_result yetty_yrich_ydoc_save_yaml_file(struct yetty_yclass_object *doc_obj,
                                                               const char *path);

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_load_yaml(const char *yaml,
                                                                        size_t len);

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_load_yaml_file(const char *path);

struct yetty_yclass_object_ptr_result yetty_yrich_slides_load_yaml(const char *yaml, size_t len);

struct yetty_yclass_object_ptr_result yetty_yrich_slides_load_yaml_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_YAML_H */
