/* GENERATED — do not edit. */
/* Public interface for regular class(es) `table` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_table_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_table_set_columns(struct yetty_ygui_object *obj,
                                                            int n_cols, const char *const *headers);
struct yetty_ycore_void_result yetty_ygui_table_add_row(struct yetty_ygui_object *obj,
                                                        const char *const *cells, int n_cells);
/* Drop every data row (headers kept). Used by live monitors (ytop) that
 * rebuild the row set every refresh tick. */
struct yetty_ycore_void_result yetty_ygui_table_clear_rows(struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
