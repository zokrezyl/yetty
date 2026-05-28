/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, float x, float y, int button)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: NULL object");

    struct yetty_yclass_ctx *_s = _yc_ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _rr, "yetty_ygui_widget_on_press: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t _yc_obj_handle;
            float x;
            float y;
            int button;
        } _a = { container_of((struct yetty_yclass_object *)_yc_obj, struct yetty_yclass_proxy, header)->handle, x, y, button };
        uint8_t _wbuf[1 + sizeof(int)];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _wr, "yetty_ygui_widget_on_press: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(_yc_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _cr_local, "yetty_ygui_widget_on_press: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _ir, "yetty_ygui_widget_on_press: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_press_fn)_ir.value)(_yc_ctx, _yc_obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, float x, float y, int button)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: NULL object");

    struct yetty_yclass_ctx *_s = _yc_ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _rr, "yetty_ygui_widget_on_release: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t _yc_obj_handle;
            float x;
            float y;
            int button;
        } _a = { container_of((struct yetty_yclass_object *)_yc_obj, struct yetty_yclass_proxy, header)->handle, x, y, button };
        uint8_t _wbuf[1 + sizeof(int)];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _wr, "yetty_ygui_widget_on_release: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(_yc_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _cr_local, "yetty_ygui_widget_on_release: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _ir, "yetty_ygui_widget_on_release: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_release_fn)_ir.value)(_yc_ctx, _yc_obj, x, y, button);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, struct yetty_ygui_emit_ctx * ctx)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: NULL object");

    struct yetty_yclass_ctx *_s = _yc_ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygui_widget_emit_body: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t _yc_obj_handle;
            uint64_t ctx_handle;
        } _a = { container_of((struct yetty_yclass_object *)_yc_obj, struct yetty_yclass_proxy, header)->handle, container_of((struct yetty_yclass_object *)ctx, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygui_widget_emit_body: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(_yc_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygui_widget_emit_body: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygui_widget_emit_body: dispatch_lookup failed");
        return ((yetty_ygui_widget_emit_body_fn)_ir.value)(_yc_ctx, _yc_obj, ctx);
    }
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_constructor);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygui_constructor: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygui_constructor: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygui_constructor: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygui_constructor: dispatch_lookup failed");
        return ((yetty_ygui_constructor_fn)_ir.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_destructor);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygui_destructor: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygui_destructor: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygui_destructor: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygui_destructor: dispatch_lookup failed");
        return ((yetty_ygui_destructor_fn)_ir.value)(ctx, obj);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _rr, "yetty_ygui_widget_on_motion: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float x;
            float y;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y };
        uint8_t _wbuf[1 + sizeof(int)];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _wr, "yetty_ygui_widget_on_motion: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _cr_local, "yetty_ygui_widget_on_motion: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, _ir, "yetty_ygui_widget_on_motion: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_motion_fn)_ir.value)(ctx, obj, x, y);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_paint);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygui_widget_paint: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint64_t emit_ctx_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, container_of((struct yetty_yclass_object *)emit_ctx, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygui_widget_paint: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygui_widget_paint: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygui_widget_paint: dispatch_lookup failed");
        return ((yetty_ygui_widget_paint_fn)_ir.value)(ctx, obj, emit_ctx);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx)
{
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: NULL object");

    struct yetty_yclass_ctx *_s = ctx;
    if (_s && _s->session) {
        struct uint32_result _rr =
            yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _rr, "yetty_ygui_widget_emit_container: ensure_remote_id failed");
        uint32_t _rid = _rr.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            uint64_t emit_ctx_handle;
        } _a = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, container_of((struct yetty_yclass_object *)emit_ctx, struct yetty_yclass_proxy, header)->handle };
        uint8_t _wbuf[1];
        struct yetty_ycore_size_result _wr = yetty_yclass_rpc_call(
            _s->session, YETTY_YCLASS_RPC_OP_CALL, _rid, &_a, sizeof(_a),
            _wbuf, sizeof(_wbuf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _wr, "yetty_ygui_widget_emit_container: RPC call failed");
        size_t _wn = _wr.value;
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result _cr_local =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _cr_local, "yetty_ygui_widget_emit_container: object_class failed");
        struct yetty_yclass_impl_t_result _ir =
            yetty_yclass_dispatch_lookup(_cr_local.value, _slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, _ir, "yetty_ygui_widget_emit_container: dispatch_lookup failed");
        return ((yetty_ygui_widget_emit_container_fn)_ir.value)(ctx, obj, emit_ctx);
    }
}

