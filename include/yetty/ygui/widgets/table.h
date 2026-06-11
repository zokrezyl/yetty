/* GENERATED — do not edit. */
/* Public interface for regular class(es) `table` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_table_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_table;
YETTY_YRESULT_DECLARE(yetty_ygui_table_ptr, struct yetty_ygui_table *);
struct yetty_ygui_table_ptr_result yetty_ygui_table_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_table_to(struct yetty_ygui_table *data);

struct yetty_yclass_object_ptr_result yetty_ygui_table_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_table_set_columns(struct yetty_yclass_object *obj,
                                                            int n_cols, const char *const *headers);
struct yetty_ycore_void_result yetty_ygui_table_add_row(struct yetty_yclass_object *obj,
                                                        const char *const *cells, int n_cells);
struct yetty_ycore_void_result yetty_ygui_table_clear_rows(struct yetty_yclass_object *obj);

#endif
