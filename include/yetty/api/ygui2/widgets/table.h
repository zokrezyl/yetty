/* GENERATED — do not edit. */
/* Object API for regular class(es) `table` (implementation module: ygui2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI2_WIDGETS_TABLE_H
#define YETTY_YCLASSGEN_API_YGUI2_WIDGETS_TABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui2_table_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_table;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_TABLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_TABLE_PTR_RESULT
struct yetty_ygui2_table_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_table *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_table_ptr_result yetty_ygui2_table_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_table_to(struct yetty_ygui2_table *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_table_create(struct yetty_yclass_ctx *ctx);

/* columns: parallel arrays of header text + widths (0 = flexible). */
struct yetty_ycore_void_result yetty_ygui2_table_set_columns(struct yetty_yclass_object *obj,
                                                             const char *const *headers,
                                                             const float *widths, uint32_t count);
struct yetty_ycore_void_result yetty_ygui2_table_clear_rows(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_table_add_row(struct yetty_yclass_object *obj,
                                                         const char *const *cells, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
