/* GENERATED — do not edit. */
#include <yetty/api/yterminal/terminal.h>

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

struct yetty_yclass_object_ptr_result;
struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(struct yetty_yclass_object * obj);
typedef struct yetty_yclass_object_ptr_result (*yetty_yterminal_figure_root_container_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yterminal_figure_root_container: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yterminal_figure_root_container");
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, remote_id_r, "yetty_yterminal_figure_root_container: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, rpc_call_r, "yetty_yterminal_figure_root_container: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yterminal_figure_root_container: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_yclass_object_ptr_result remote_error =
                YETTY_ERR(yetty_yclass_object_ptr, "yetty_yterminal_figure_root_container: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(uint64_t)) {
            free(resp_buf);
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yterminal_figure_root_container: truncated RPC payload");
        }
        uint64_t remote_handle;
        memcpy(&remote_handle, resp_buf + 1, sizeof(remote_handle));
        free(resp_buf);
        if (remote_handle == 0) {
            return YETTY_OK(yetty_yclass_object_ptr, NULL);
        }
        return yetty_yclass_object_proxy_create(obj->session, remote_handle, NULL);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yterminal",
                                             (yetty_yclass_method_id_t)yetty_yterminal_figure_root_container);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yterminal_figure_root_container: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_yterminal_figure_root_container: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_yterminal_figure_root_container: dispatch_lookup failed");
        return ((yetty_yterminal_figure_root_container_fn)dispatch_impl_r.value)(obj);
    }
}

