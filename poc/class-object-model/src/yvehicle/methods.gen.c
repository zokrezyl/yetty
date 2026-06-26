/* GENERATED — do not edit. */
#include "yvehicle/methods.gen.h"
#include "result.h"
#include "rpc.h"
#include "ytrace.h"
#include <stdint.h>
#include <string.h>

struct yetty_ycore_void_result yvehicle_vehicle_ctor(struct object *obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_ctor);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: method_slot_get failed",
                             _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj))};
        uint8_t _wbuf[1];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_ctor: no impl on this class");
        }
        return ((yvehicle_vehicle_ctor_fn)fn)(obj);
    }
}

struct yetty_ycore_void_result yvehicle_vehicle_dtor(struct object *obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_dtor);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: method_slot_get failed",
                             _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj))};
        uint8_t _wbuf[1];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_dtor: no impl on this class");
        }
        return ((yvehicle_vehicle_dtor_fn)fn)(obj);
    }
}

struct yetty_ycore_void_result yvehicle_vehicle_start(struct object *obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_start);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_start: method_slot_get failed",
                             _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_start: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_start: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj))};
        uint8_t _wbuf[1];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_start: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yvehicle_vehicle_start: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(yetty_ycore_void, "yvehicle_vehicle_start: no impl on this class");
        }
        return ((yvehicle_vehicle_start_fn)fn)(obj);
    }
}

struct yetty_ycore_int_result yvehicle_vehicle_accelerate(struct object *obj, float speed)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_accelerate);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: method_slot_get failed",
                             _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float speed;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj)), speed};
        uint8_t _wbuf[1 + sizeof(int)];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int,
                             "yvehicle_vehicle_accelerate: remote impl returned error");
        }
        if (_wn != sizeof(_wbuf)) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: truncated RPC payload");
        }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_accelerate: no impl on this class");
        }
        return ((yvehicle_vehicle_accelerate_fn)fn)(obj, speed);
    }
}

struct yetty_ycore_int_result yvehicle_vehicle_brake(struct object *obj, float intensity)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_brake);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: method_slot_get failed",
                             _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float intensity;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj)), intensity};
        uint8_t _wbuf[1 + sizeof(int)];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: remote impl returned error");
        }
        if (_wn != sizeof(_wbuf)) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: truncated RPC payload");
        }
        int _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(yetty_ycore_int, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(yetty_ycore_int, "yvehicle_vehicle_brake: no impl on this class");
        }
        return ((yvehicle_vehicle_brake_fn)fn)(obj, intensity);
    }
}

struct str_result yvehicle_vehicle_describe(struct object *obj, float distance)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {
        struct method_slot_result _sr =
            method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_describe);
        if (YETTY_IS_ERR(_sr)) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: method_slot_get failed", _sr);
        }
        _slot = _sr.value;
    }

    if (!obj) {
        return YETTY_ERR(str, "yvehicle_vehicle_describe: NULL object");
    }

    if (obj->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(obj->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: remote id unresolved");
        }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float distance;
        } _a = {*(uint64_t *)((char *)obj + sizeof(*obj)), distance};
        uint8_t _wbuf[1 + sizeof(struct str)];
        size_t _wn =
            rpc_call(obj->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: short RPC response");
        }
        if (_wbuf[0] != 0) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: remote impl returned error");
        }
        if (_wn != sizeof(_wbuf)) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: truncated RPC payload");
        }
        struct str _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK(str, _v);
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) {
            return YETTY_ERR(str, "yvehicle_vehicle_describe: no impl on this class");
        }
        return ((yvehicle_vehicle_describe_fn)fn)(obj, distance);
    }
}
