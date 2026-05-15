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
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yslides.h>
#include <yetty/yrich/yspreadsheet.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each loader takes ownership of the document on success — caller frees it
 * via yetty_yrich_document_destroy(). On error the result carries no
 * document and the caller does nothing. */

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_load_yaml(const char *yaml, size_t len);

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_load_yaml_file(const char *path);

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_load_yaml(const char *yaml,
                                                                            size_t len);

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_load_yaml_file(const char *path);

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_load_yaml(const char *yaml, size_t len);

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_load_yaml_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_YAML_H */
