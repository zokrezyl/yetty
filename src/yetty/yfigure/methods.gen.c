/* GENERATED — do not edit. */
#include "yetty/yfigure/methods.gen.h"
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_constructor);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_yfigure_constructor: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_yfigure_constructor: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_constructor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_yfigure_constructor: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_yfigure_constructor: dispatch_lookup failed");
        return ((yetty_yfigure_constructor_fn)_ir.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_yfigure_figure * child, uint32_t id)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_add_child);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_yfigure_add_child: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint64_t child_handle;
            uint32_t id;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, container_of((struct yetty_yclass_object *)child, struct yetty_yclass_proxy, header)->handle, id };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_yfigure_add_child: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_yfigure_add_child: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_yfigure_add_child: dispatch_lookup failed");
        return ((yetty_yfigure_add_child_fn)_ir.value)(ctx, obj, child, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_yfigure_remove_child_by_id: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint32_t id;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, id };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_yfigure_remove_child_by_id: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_yfigure_remove_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_yfigure_remove_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_remove_child_by_id_fn)_ir.value)(ctx, obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, uint32_t id)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_yfigure_raise_child_by_id: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint32_t id;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, id };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_yfigure_raise_child_by_id: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_yfigure_raise_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_yfigure_raise_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_raise_child_by_id_fn)_ir.value)(ctx, obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer bytes)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_process_records);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_yfigure_process_records: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint32_t bytes_len;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, (uint32_t)bytes.size };
        size_t _body_total = sizeof(_a) + (size_t)bytes.size;
        uint8_t *_body_buf = (uint8_t *)malloc(_body_total ? _body_total : 1);
        if (!_body_buf) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: body buf oom");
        memcpy(_body_buf, &_a, sizeof(_a));
        size_t _bo = sizeof(_a);
        memcpy(_body_buf + _bo, bytes.data, bytes.size);
        _bo += bytes.size;
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, _body_buf, _body_total,
            _wbuf, sizeof(_wbuf));
        free(_body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_yfigure_process_records: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_process_records: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_yfigure_process_records: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_yfigure_process_records: dispatch_lookup failed");
        return ((yetty_yfigure_process_records_fn)_ir.value)(ctx, obj, bytes);
    }
}

