/* GENERATED — do not edit. */
#include "yanimal/methods.gen.h"
#include "rpc.h"
#include <stdint.h>
#include <string.h>

void animal_ctor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t)animal_ctor);

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

void animal_dtor(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t)animal_dtor);

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

void animal_breathe(struct ctx * ctx, struct object * obj)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t)animal_breathe);

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

struct str animal_speak(struct ctx * ctx, struct object * obj, int volume)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t)animal_speak);

    if (!obj) { return (struct str){0}; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return (struct str){0}; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            int volume;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), volume };
        struct str _r = {0};
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));
        return _r;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return (struct str){0};
        return ((struct str (*)(struct ctx *, struct object *, int))fn)(ctx, obj, volume);
    }
}

int animal_eat(struct ctx * ctx, struct object * obj, float amount)
{
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t)animal_eat);

    if (!obj) { return (int){0}; }

    struct ctx *_s = ctx;
    if (_s && _s->session) {
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) { return (int){0}; }
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float amount;
        } _a = { *(uint64_t *)((char *)obj + sizeof(*obj)), amount };
        int _r = {0};
        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));
        return _r;
    } else {
        impl_t fn = class_dispatch_lookup(object_class(obj), _slot);
        if (!fn) return (int){0};
        return ((int (*)(struct ctx *, struct object *, float))fn)(ctx, obj, amount);
    }
}

