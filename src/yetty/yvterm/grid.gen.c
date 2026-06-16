/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yvterm_grid");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yvterm_grid",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yvterm_grid),
        .data_align = _Alignof(struct yetty_yvterm_grid),
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, NULL, 0, NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yvterm_grid_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yvterm_grid_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yvterm_grid_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yvterm_grid_ptr, "yetty_yvterm_grid_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yvterm_grid_ptr, "yetty_yvterm_grid_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yvterm_grid_ptr, (struct yetty_yvterm_grid *)slice_r.value);
}

struct yetty_yclass_object *yetty_yvterm_grid_to(struct yetty_yvterm_grid *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yvterm_grid_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
