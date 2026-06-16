/* GENERATED — do not edit. */
#include "yetty/ygui/mixins/clickable.h"
#include "yetty/ygui/mixins/draggable.h"
#include "yetty/ygui/primitive-widget.h"
#include "yetty/ygui/widget.h"
#include "yetty/ygui/widgets/breadcrumbs.h"
#include "yetty/ygui/widgets/button.h"
#include "yetty/ygui/widgets/checkbox.h"
#include "yetty/ygui/widgets/chip.h"
#include "yetty/ygui/widgets/choicebox.h"
#include "yetty/ygui/widgets/collapsing_header.h"
#include "yetty/ygui/widgets/colorpicker.h"
#include "yetty/ygui/widgets/combobox.h"
#include "yetty/ygui/widgets/datepicker.h"
#include "yetty/ygui/widgets/dialog.h"
#include "yetty/ygui/widgets/dropdown.h"
#include "yetty/ygui/widgets/filepicker.h"
#include "yetty/ygui/widgets/hbox.h"
#include "yetty/ygui/widgets/label.h"
#include "yetty/ygui/widgets/list.h"
#include "yetty/ygui/widgets/menubar.h"
#include "yetty/ygui/widgets/panel.h"
#include "yetty/ygui/widgets/popup_menu.h"
#include "yetty/ygui/widgets/progress.h"
#include "yetty/ygui/widgets/radio.h"
#include "yetty/ygui/widgets/rich.h"
#include "yetty/ygui/widgets/scrollarea.h"
#include "yetty/ygui/widgets/selectable.h"
#include "yetty/ygui/widgets/separator.h"
#include "yetty/ygui/widgets/slider.h"
#include "yetty/ygui/widgets/spinner.h"
#include "yetty/ygui/widgets/splitter.h"
#include "yetty/ygui/widgets/statusbar.h"
#include "yetty/ygui/widgets/stepper.h"
#include "yetty/ygui/widgets/tabbar.h"
#include "yetty/ygui/widgets/table.h"
#include "yetty/ygui/widgets/textarea.h"
#include "yetty/ygui/widgets/textinput.h"
#include "yetty/ygui/widgets/toggle.h"
#include "yetty/ygui/widgets/tooltip.h"
#include "yetty/ygui/widgets/tree_node.h"
#include "yetty/ygui/widgets/vbox.h"
#include "yetty/ygui/widgets/window.h"
#include "yetty/ygui/widgets/ybrowser.h"
#include "yetty/ygui/widgets/ydiagram.h"
#include "yetty/ygui/widgets/ydraw_embed.h"
#include "yetty/ygui/widgets/yimage.h"
#include "yetty/ygui/widgets/yjungle.h"
#include "yetty/ygui/widgets/ymarkdown.h"
#include "yetty/ygui/widgets/ymaze.h"
#include "yetty/ygui/widgets/ynode.h"
#include "yetty/ygui/widgets/ynodes.h"
#include "yetty/ygui/widgets/ypdf.h"
#include "yetty/ygui/widgets/yplot.h"
#include "yetty/ygui/widgets/yrich_view.h"
#include "yetty/ygui/widgets/yshadertoy.h"
#include "yetty/ygui/widgets/yvideo.h"
#include "yetty/ygui/widgets/yzoo.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_press: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            int button;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_press: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_press: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_press: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_press: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_press_fn)dispatch_impl_r.value)(obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_release: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_release: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            int button;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_release: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_release: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_release: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_release: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_release: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_release_fn)dispatch_impl_r.value)(obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_motion: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_motion: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_motion: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_motion: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_motion: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_motion_fn)dispatch_impl_r.value)(obj, x, y);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_widget_emit_body: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_emit_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_emit_body: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_body_fn)dispatch_impl_r.value)(obj, emit_ctx);
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui_constructor: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui_constructor: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_constructor: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui_constructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui_constructor: dispatch_lookup failed");
        return ((yetty_ygui_constructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_destructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui_destructor: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygui_destructor: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui_destructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui_destructor: dispatch_lookup failed");
        return ((yetty_ygui_destructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_scroll: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            float dx;
            float dy;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, dx, dy};
#pragma pack(pop)
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_scroll: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_scroll: remote impl returned error");
        }
        if (response_len != sizeof(resp_buf)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_scroll: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_scroll: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_scroll_fn)dispatch_impl_r.value)(obj, x, y, dx, dy);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_paint);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_paint: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_paint: dispatch_lookup failed");
    return ((yetty_ygui_widget_paint_fn)dispatch_impl_r.value)(obj, emit_ctx);
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_container(
    struct yetty_yclass_object *obj, struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_widget_emit_container: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_emit_container: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_emit_container: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_container_fn)dispatch_impl_r.value)(obj, emit_ctx);
}
