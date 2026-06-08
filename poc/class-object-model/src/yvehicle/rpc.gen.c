/* GENERATED — do not edit. */
#include "rpc.h"
#include "result.h"
#include "ytrace.h"
#include "yvehicle/rpc.gen.h"
#include "yvehicle/methods.gen.h"
#include "class.h"
#include "yvehicle/vehicle.h"
#include "yvehicle/motorbike.h"
#include "yvehicle/car.h"
#include "yvehicle/sportscar.h"
#include "yvehicle/electric.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yvehicle_vehicle_ctor_skel(const void *_body, size_t _body_len, void *_resp,
                                         size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct yetty_ycore_void_result _r =
        yvehicle_vehicle_ctor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_ctor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yvehicle_vehicle_dtor_skel(const void *_body, size_t _body_len, void *_resp,
                                         size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct yetty_ycore_void_result _r =
        yvehicle_vehicle_dtor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_dtor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yvehicle_vehicle_start_skel(const void *_body, size_t _body_len, void *_resp,
                                          size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct yetty_ycore_void_result _r =
        yvehicle_vehicle_start(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_start", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yvehicle_vehicle_accelerate_skel(const void *_body, size_t _body_len, void *_resp,
                                               size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float speed;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct yetty_ycore_int_result _r = yvehicle_vehicle_accelerate(
        &_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.speed);
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_accelerate", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) {
        return 0;
    }
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

static size_t yvehicle_vehicle_brake_skel(const void *_body, size_t _body_len, void *_resp,
                                          size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float intensity;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct yetty_ycore_int_result _r = yvehicle_vehicle_brake(
        &_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.intensity);
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_brake", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) {
        return 0;
    }
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

static size_t yvehicle_vehicle_describe_skel(const void *_body, size_t _body_len, void *_resp,
                                             size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float distance;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct str_result _r = yvehicle_vehicle_describe(
        &_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.distance);
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yvehicle_vehicle_describe", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) {
        return 0;
    }
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

struct object_ptr_result yvehicle_vehicle_create(struct ctx *ctx)
{
    ydebug("class=yvehicle_vehicle");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yvehicle_vehicle_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yvehicle_vehicle_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_vehicle");

    uint64_t _h = 0;
    const char *_name = "yvehicle_vehicle";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yvehicle_vehicle_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yvehicle_vehicle_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

struct object_ptr_result yvehicle_motorbike_create(struct ctx *ctx)
{
    ydebug("class=yvehicle_motorbike");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yvehicle_motorbike_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yvehicle_motorbike_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_motorbike");

    uint64_t _h = 0;
    const char *_name = "yvehicle_motorbike";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yvehicle_motorbike_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yvehicle_motorbike_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

struct object_ptr_result yvehicle_car_create(struct ctx *ctx)
{
    ydebug("class=yvehicle_car");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yvehicle_car_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yvehicle_car_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_car");

    uint64_t _h = 0;
    const char *_name = "yvehicle_car";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yvehicle_car_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yvehicle_car_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

struct object_ptr_result yvehicle_sportscar_create(struct ctx *ctx)
{
    ydebug("class=yvehicle_sportscar");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yvehicle_sportscar_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yvehicle_sportscar_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yvehicle_sportscar");

    uint64_t _h = 0;
    const char *_name = "yvehicle_sportscar";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yvehicle_sportscar_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yvehicle_sportscar_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

/* ---- yvehicle: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result yvehicle_accessor_lookup(const char *name)
{
    if (strcmp(name, "yvehicle_vehicle") == 0) {
        return yvehicle_vehicle_class_get();
    }
    if (strcmp(name, "yvehicle_motorbike") == 0) {
        return yvehicle_motorbike_class_get();
    }
    if (strcmp(name, "yvehicle_car") == 0) {
        return yvehicle_car_class_get();
    }
    if (strcmp(name, "yvehicle_sportscar") == 0) {
        return yvehicle_sportscar_class_get();
    }
    if (strcmp(name, "yvehicle_electric") == 0) {
        return yvehicle_electric_mixin_get();
    }
    /* "Not mine": OK with NULL value — class_by_name walks to next hook. */
    return YETTY_OK(class_ptr, NULL);
}

/* ---- yvehicle: slot → skel, name-keyed static data --------------- */

struct yvehicle_skel_row {
    const char *name;
    rpc_skel_fn fn;
};

static const struct yvehicle_skel_row yvehicle_skel_rows[] = {
    {"yvehicle_vehicle_ctor", yvehicle_vehicle_ctor_skel},
    {"yvehicle_vehicle_dtor", yvehicle_vehicle_dtor_skel},
    {"yvehicle_vehicle_start", yvehicle_vehicle_start_skel},
    {"yvehicle_vehicle_accelerate", yvehicle_vehicle_accelerate_skel},
    {"yvehicle_vehicle_brake", yvehicle_vehicle_brake_skel},
    {"yvehicle_vehicle_describe", yvehicle_vehicle_describe_skel}};

static rpc_skel_fn yvehicle_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) {
        yetty_ycore_error_destroy(nr.error);
        return NULL;
    }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(yvehicle_skel_rows) / sizeof(yvehicle_skel_rows[0]); ++i) {
        if (strcmp(yvehicle_skel_rows[i].name, name) == 0) {
            return yvehicle_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yvehicle: install hooks before main ------------------------- */

__attribute__((constructor)) static void yvehicle_install_hooks(void)
{
    struct yetty_ycore_void_result _ar = class_add_accessor_lookup(yvehicle_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yvehicle_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(yvehicle_skel_lookup);
}
