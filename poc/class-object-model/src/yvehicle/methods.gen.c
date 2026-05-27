/* GENERATED — do not edit. */
#include "yvehicle/methods.gen.h"
#include "rpc.h"
#include <stdint.h>
#include <string.h>

void yvehicle_vehicle_ctor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_ctor);

    if (!obj) { return; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), NULL, 0);
        return;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (fn) ((void (*)(struct ctx *, struct object *))fn)(ctx, obj);
        return;
    }
}

void yvehicle_vehicle_dtor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_dtor);

    if (!obj) { return; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), NULL, 0);
        return;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (fn) ((void (*)(struct ctx *, struct object *))fn)(ctx, obj);
        return;
    }
}

void yvehicle_vehicle_start(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_start);

    if (!obj) { return; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)) };
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), NULL, 0);
        return;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (fn) ((void (*)(struct ctx *, struct object *))fn)(ctx, obj);
        return;
    }
}

int yvehicle_vehicle_accelerate(struct ctx * ctx, struct object * obj, float speed)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_accelerate);

    if (!obj) { return (int){0}; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return (int){0}; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float speed;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), speed };
        int _r = {0};
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));
        return _r;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return (int){0};
        return ((int (*)(struct ctx *, struct object *, float))fn)(ctx, obj, speed);
    }
}

int yvehicle_vehicle_brake(struct ctx * ctx, struct object * obj, float intensity)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_brake);

    if (!obj) { return (int){0}; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return (int){0}; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float intensity;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), intensity };
        int _r = {0};
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));
        return _r;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return (int){0};
        return ((int (*)(struct ctx *, struct object *, float))fn)(ctx, obj, intensity);
    }
}

struct str yvehicle_vehicle_describe(struct ctx * ctx, struct object * obj, float distance)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get("yvehicle", (method_id_t)yvehicle_vehicle_describe);

    if (!obj) { return (struct str){0}; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return (struct str){0}; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float distance;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), distance };
        struct str _r = {0};
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));
        return _r;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return (struct str){0};
        return ((struct str (*)(struct ctx *, struct object *, float))fn)(ctx, obj, distance);
    }
}

