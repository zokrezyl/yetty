/* GENERATED — do not edit. */
/* Object API for regular class(es) `14_todo_list` (implementation module: demoygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_DEMOYGUI_14_TODO_LIST_MAIN_H
#define YETTY_YCLASSGEN_API_DEMOYGUI_14_TODO_LIST_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_14_todo_list;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_14_TODO_LIST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_14_TODO_LIST_PTR_RESULT
struct yetty_demoygui_14_todo_list_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_14_todo_list *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_demoygui_14_todo_list_ptr_result yetty_demoygui_14_todo_list_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_14_todo_list_to(
    struct yetty_demoygui_14_todo_list *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_14_todo_list_create(
    struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
