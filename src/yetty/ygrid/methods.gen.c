/* GENERATED — do not edit. */
#include "yetty/ygrid/methods.gen.h"
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer record)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_add_record);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygrid_add_record: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint32_t record_len;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, (uint32_t)record.size };
        size_t _body_total = sizeof(_a) + (size_t)record.size;
        uint8_t *_body_buf = (uint8_t *)malloc(_body_total ? _body_total : 1);
        if (!_body_buf) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: body buf oom");
        memcpy(_body_buf, &_a, sizeof(_a));
        size_t _bo = sizeof(_a);
        memcpy(_body_buf + _bo, record.data, record.size);
        _bo += record.size;
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, _body_buf, _body_total,
            _wbuf, sizeof(_wbuf));
        free(_body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygrid_add_record: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygrid_add_record: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygrid_add_record: dispatch_lookup failed");
        return ((yetty_ygrid_add_record_fn)_ir.value)(ctx, obj, record);
    }
}

struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_clear);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygrid_clear: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygrid_clear: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygrid_clear: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygrid_clear: dispatch_lookup failed");
        return ((yetty_ygrid_clear_fn)_ir.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_destroy);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygrid_destroy: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygrid_destroy: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygrid_destroy: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygrid_destroy: dispatch_lookup failed");
        return ((yetty_ygrid_destroy_fn)_ir.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_ygrid_process_bytes(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ycore_buffer payload)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_process_bytes);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_process_bytes: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_process_bytes: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygrid_process_bytes: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint32_t payload_len;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, (uint32_t)payload.size };
        size_t _body_total = sizeof(_a) + (size_t)payload.size;
        uint8_t *_body_buf = (uint8_t *)malloc(_body_total ? _body_total : 1);
        if (!_body_buf) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_process_bytes: body buf oom");
        memcpy(_body_buf, &_a, sizeof(_a));
        size_t _bo = sizeof(_a);
        memcpy(_body_buf + _bo, payload.data, payload.size);
        _bo += payload.size;
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, _body_buf, _body_total,
            _wbuf, sizeof(_wbuf));
        free(_body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygrid_process_bytes: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_process_bytes: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_process_bytes: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygrid_process_bytes: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygrid_process_bytes: dispatch_lookup failed");
        return ((yetty_ygrid_process_bytes_fn)_ir.value)(ctx, obj, payload);
    }
}

struct yetty_ycore_void_result yetty_ygrid_reset_content(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_reset_content);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_reset_content: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_reset_content: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygrid_reset_content: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygrid_reset_content: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_reset_content: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_reset_content: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygrid_reset_content: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygrid_reset_content: dispatch_lookup failed");
        return ((yetty_ygrid_reset_content_fn)_ir.value)(ctx, obj);
    }
}

