/* GENERATED — do not edit. */
#include "rpc.h"
#include "yvehicle/rpc.gen.h"
#include "yvehicle/methods.gen.h"
#include "class.h"
#include "yvehicle/vehicle.h"
#include "yvehicle/motorbike.h"
#include "yvehicle/car.h"
#include "yvehicle/sportscar.h"
#include "yvehicle/electric.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t vehicle_ctor_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    vehicle_ctor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t vehicle_dtor_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    vehicle_dtor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t vehicle_start_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    vehicle_start(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t vehicle_accelerate_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float speed;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    int _r = vehicle_accelerate(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.speed);
    if (_resp_max < sizeof(_r)) return 0;
    memcpy(_resp, &_r, sizeof(_r));
    return sizeof(_r);
}

static size_t vehicle_brake_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float intensity;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    int _r = vehicle_brake(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.intensity);
    if (_resp_max < sizeof(_r)) return 0;
    memcpy(_resp, &_r, sizeof(_r));
    return sizeof(_r);
}

static size_t vehicle_describe_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float distance;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct str _r = vehicle_describe(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.distance);
    if (_resp_max < sizeof(_r)) return 0;
    memcpy(_resp, &_r, sizeof(_r));
    return sizeof(_r);
}

struct object *vehicle_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = vehicle_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_vehicle");

    uint64_t _h = 0;
    const char *_name = "yvehicle_vehicle";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return NULL;

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) return NULL;
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return _obj;
}

struct object *motorbike_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = motorbike_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_motorbike");

    uint64_t _h = 0;
    const char *_name = "yvehicle_motorbike";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return NULL;

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) return NULL;
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return _obj;
}

struct object *car_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = car_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_car");

    uint64_t _h = 0;
    const char *_name = "yvehicle_car";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return NULL;

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) return NULL;
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return _obj;
}

struct object *sportscar_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = sportscar_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_sportscar");

    uint64_t _h = 0;
    const char *_name = "yvehicle_sportscar";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return NULL;

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) return NULL;
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return _obj;
}

/* ---- yvehicle: class name → accessor (lazy) ---------------------- */

static const struct class *yvehicle_accessor_lookup(const char *name)
{
    if (strcmp(name, "yvehicle_vehicle") == 0) return vehicle_class_get();
    if (strcmp(name, "yvehicle_motorbike") == 0) return motorbike_class_get();
    if (strcmp(name, "yvehicle_car") == 0) return car_class_get();
    if (strcmp(name, "yvehicle_sportscar") == 0) return sportscar_class_get();
    if (strcmp(name, "yvehicle_electric") == 0) return electric_mixin_get();
    return NULL;
}

/* ---- yvehicle: slot → skel, name-keyed static data --------------- */

struct yvehicle_skel_row { const char *name; rpc_skel_fn fn; };

static const struct yvehicle_skel_row yvehicle_skel_rows[] = {
    {"yvehicle_vehicle_ctor", vehicle_ctor_skel},
    {"yvehicle_vehicle_dtor", vehicle_dtor_skel},
    {"yvehicle_vehicle_start", vehicle_start_skel},
    {"yvehicle_vehicle_accelerate", vehicle_accelerate_skel},
    {"yvehicle_vehicle_brake", vehicle_brake_skel},
    {"yvehicle_vehicle_describe", vehicle_describe_skel}
};

static rpc_skel_fn yvehicle_skel_lookup(method_slot slot)
{
    const char *name = method_slot_name(slot);
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(yvehicle_skel_rows) / sizeof(yvehicle_skel_rows[0]); ++i)
        if (strcmp(yvehicle_skel_rows[i].name, name) == 0)
            return yvehicle_skel_rows[i].fn;
    return NULL;
}

/* ---- yvehicle: install hooks before main ------------------------- */

__attribute__((constructor))
static void yvehicle_install_hooks(void)
{
    class_add_accessor_lookup(yvehicle_accessor_lookup);
    rpc_add_skel_lookup(yvehicle_skel_lookup);
}
