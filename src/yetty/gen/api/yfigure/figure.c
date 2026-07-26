/* GENERATED — do not edit. */
#include <yetty/api/yfigure/figure.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object * obj, struct yetty_ydraw_target * target);
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yfigure_process_input(struct yetty_yclass_object * obj, struct yetty_ywire_wire_statemachine * statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object * obj, const uint8_t * bytes, size_t bytes_len);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object * obj);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object * obj, int indent);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object * obj, float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object * obj, float content_w, float content_h);
struct yetty_ycore_void_result yetty_yfigure_apply_scroll_anchor(struct yetty_yclass_object * obj, int32_t rolling_row_offset, float cell_height);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_object *, struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(struct yetty_yclass_object *, struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_reset_content_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_scroll_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_content_size_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_apply_scroll_anchor_fn)(struct yetty_yclass_object *, int32_t, float);

struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_destroy: dispatch_lookup failed");
    return ((yetty_yfigure_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object * obj, struct yetty_ydraw_target * target)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_render: dispatch_lookup failed");
    return ((yetty_yfigure_render_fn)dispatch_impl_r.value)(obj, target);
}

struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object * obj, int indent)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_dump_state);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump_state: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump_state: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_yfigure_dump_state: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_yfigure_dump_state: dispatch_lookup failed");
    return ((yetty_yfigure_dump_state_fn)dispatch_impl_r.value)(obj, indent);
}

struct yetty_ycore_void_result yetty_yfigure_process_input(struct yetty_yclass_object * obj, struct yetty_ywire_wire_statemachine * statemachine)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_process_input);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_input: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_input: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_process_input: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_process_input: dispatch_lookup failed");
    return ((yetty_yfigure_process_input_fn)dispatch_impl_r.value)(obj, statemachine);
}

struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_object * obj, const uint8_t * bytes, size_t bytes_len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_bytes: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_bytes: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_process_bytes: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_process_bytes: dispatch_lookup failed");
    return ((yetty_yfigure_process_bytes_fn)dispatch_impl_r.value)(obj, bytes, bytes_len);
}

struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_reset_content);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_reset_content: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_reset_content: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_reset_content: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_reset_content: dispatch_lookup failed");
    return ((yetty_yfigure_reset_content_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_object * obj, float scroll_x, float scroll_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_scroll);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_scroll: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_scroll: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_set_scroll: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_set_scroll: dispatch_lookup failed");
    return ((yetty_yfigure_set_scroll_fn)dispatch_impl_r.value)(obj, scroll_x, scroll_y);
}

struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_object * obj, float content_w, float content_h)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_content_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_content_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_content_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_set_content_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_set_content_size: dispatch_lookup failed");
    return ((yetty_yfigure_set_content_size_fn)dispatch_impl_r.value)(obj, content_w, content_h);
}

struct yetty_ycore_void_result yetty_yfigure_apply_scroll_anchor(struct yetty_yclass_object * obj, int32_t rolling_row_offset, float cell_height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_apply_scroll_anchor);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_apply_scroll_anchor: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_apply_scroll_anchor: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yfigure_apply_scroll_anchor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yfigure_apply_scroll_anchor: dispatch_lookup failed");
    return ((yetty_yfigure_apply_scroll_anchor_fn)dispatch_impl_r.value)(obj, rolling_row_offset, cell_height);
}

