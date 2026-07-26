/* GENERATED — do not edit. */
#include <yetty/api/ygit/repo.h>

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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ygit_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ygit_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygit_destructor_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygit_destructor: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ygit_destructor");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ygit_destructor: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ygit_destructor", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygit_destructor: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_ygit",
                                             (yetty_yclass_method_id_t)yetty_ygit_destructor);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_ygit_destructor: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygit_destructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygit_destructor: dispatch_lookup failed");
        return ((yetty_ygit_destructor_fn)dispatch_impl_r.value)(obj);
    }
}

