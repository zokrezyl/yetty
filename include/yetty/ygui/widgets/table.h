/* GENERATED — do not edit. */
/* Public interface for regular class(es) `table` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_table_class_get(void);

struct yetty_ygui_object;
struct table_data;
YETTY_YRESULT_DECLARE(yetty_ygui_table_data_ptr, struct table_data *);
struct yetty_ygui_table_data_ptr_result yetty_ygui_table_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_table_set_columns(struct yetty_ygui_object *obj, int n_cols, const char *const *headers);
struct yetty_ycore_void_result yetty_ygui_table_add_row(struct yetty_ygui_object *obj, const char *const *cells, int n_cells);
struct yetty_ycore_void_result yetty_ygui_table_clear_rows(struct yetty_ygui_object *obj);

#endif
