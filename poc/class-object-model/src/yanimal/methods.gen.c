/* GENERATED — do not edit. */
#include "yanimal/methods.gen.h"
#include "result.h"
#include "rpc.h"
#include "ytrace.h"
#include <stdint.h>
#include <string.h>

struct yetty_ycore_void_result yanimal_animal_ctor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yanimal", (method_id_t)yanimal_animal_ctor);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: remote id unresolved");
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        uint8_t _wbuf[1];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a),
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_ctor: no impl on this class");
        return ((yanimal_animal_ctor_fn)fn)(ctx, obj);
    }
}

struct yetty_ycore_void_result yanimal_animal_dtor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yanimal", (method_id_t)yanimal_animal_dtor);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: remote id unresolved");
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        uint8_t _wbuf[1];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a),
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_dtor: no impl on this class");
        return ((yanimal_animal_dtor_fn)fn)(ctx, obj);
    }
}

struct yetty_ycore_void_result yanimal_animal_breathe(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yanimal", (method_id_t)yanimal_animal_breathe);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: remote id unresolved");
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        uint8_t _wbuf[1];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a),
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YETTY_ERR(yetty_ycore_void, "yanimal_animal_breathe: no impl on this class");
        return ((yanimal_animal_breathe_fn)fn)(ctx, obj);
    }
}

struct str_result yanimal_animal_speak(struct ctx * ctx, struct object * obj, int volume)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yanimal", (method_id_t)yanimal_animal_speak);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(str, "yanimal_animal_speak: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(str, "yanimal_animal_speak: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR(str, "yanimal_animal_speak: remote id unresolved");
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            int volume;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), volume };
        uint8_t _wbuf[1 + sizeof(struct str)];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a),
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR(str, "yanimal_animal_speak: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(str, "yanimal_animal_speak: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR(str, "yanimal_animal_speak: truncated RPC payload");
        struct str _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(str, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YETTY_ERR(str, "yanimal_animal_speak: no impl on this class");
        return ((yanimal_animal_speak_fn)fn)(ctx, obj, volume);
    }
}

struct yetty_ycore_int_result yanimal_animal_eat(struct ctx * ctx, struct object * obj, float amount)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yanimal", (method_id_t)yanimal_animal_eat);
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: method_slot_get failed", _sr);
        _slot = _sr.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: NULL object");

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: remote id unresolved");
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float amount;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), amount };
        uint8_t _wbuf[1 + sizeof(int)];
        size_t _wn = rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a),
                              _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: truncated RPC payload");
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return YETTY_ERR(yetty_ycore_int, "yanimal_animal_eat: no impl on this class");
        return ((yanimal_animal_eat_fn)fn)(ctx, obj, amount);
    }
}

