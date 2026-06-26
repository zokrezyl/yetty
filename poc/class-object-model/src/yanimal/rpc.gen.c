/* GENERATED — do not edit. */
#include "rpc.h"
#include "result.h"
#include "ytrace.h"
#include "yanimal/rpc.gen.h"
#include "yanimal/methods.gen.h"
#include "class.h"
#include "yanimal/animal.h"
#include "yanimal/cat.h"
#include "yanimal/dog.h"
#include "yanimal/pet.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yanimal_animal_ctor_skel(const void *_body, size_t _body_len, void *_resp,
                                       size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_ycore_void_result _r =
        yanimal_animal_ctor((struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yanimal_animal_ctor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yanimal_animal_dtor_skel(const void *_body, size_t _body_len, void *_resp,
                                       size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_ycore_void_result _r =
        yanimal_animal_dtor((struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yanimal_animal_dtor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yanimal_animal_breathe_skel(const void *_body, size_t _body_len, void *_resp,
                                          size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_ycore_void_result _r =
        yanimal_animal_breathe((struct object *)rpc_handle_resolve(_a.obj_handle));
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yanimal_animal_breathe", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yanimal_animal_speak_skel(const void *_body, size_t _body_len, void *_resp,
                                        size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        int volume;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct str_result _r =
        yanimal_animal_speak((struct object *)rpc_handle_resolve(_a.obj_handle), _a.volume);
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yanimal_animal_speak", _r.error);
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

static size_t yanimal_animal_eat_skel(const void *_body, size_t _body_len, void *_resp,
                                      size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float amount;
    } _a;
    if (_body_len < sizeof(_a)) {
        return 0;
    }
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_ycore_int_result _r =
        yanimal_animal_eat((struct object *)rpc_handle_resolve(_a.obj_handle), _a.amount);
    if (_resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yanimal_animal_eat", _r.error);
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

struct object_ptr_result yanimal_animal_create(struct ctx *ctx)
{
    ydebug("class=yanimal_animal");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yanimal_animal_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yanimal_animal_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_animal");

    uint64_t _h = 0;
    const char *_name = "yanimal_animal";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yanimal_animal_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yanimal_animal_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    _obj->session = ctx->session;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

struct object_ptr_result yanimal_cat_create(struct ctx *ctx)
{
    ydebug("class=yanimal_cat");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yanimal_cat_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yanimal_cat_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_cat");

    uint64_t _h = 0;
    const char *_name = "yanimal_cat";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yanimal_cat_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yanimal_cat_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    _obj->session = ctx->session;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

struct object_ptr_result yanimal_dog_create(struct ctx *ctx)
{
    ydebug("class=yanimal_dog");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = yanimal_dog_class_get();
    if (YETTY_IS_ERR(_kr)) {
        return YETTY_ERR(object_ptr, "yanimal_dog_create: class accessor failed", _kr);
    }
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        return object_alloc(_klass);
    }

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_dog");

    uint64_t _h = 0;
    const char *_name = "yanimal_dog";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name), &_h, sizeof(_h)) !=
            sizeof(_h) ||
        !_h) {
        return YETTY_ERR(object_ptr, "yanimal_dog_create: remote create failed");
    }

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) {
        return YETTY_ERR(object_ptr, "yanimal_dog_create: calloc(proxy) failed");
    }
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    _obj->session = ctx->session;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

/* ---- yanimal: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result yanimal_accessor_lookup(const char *name)
{
    if (strcmp(name, "yanimal_animal") == 0) {
        return yanimal_animal_class_get();
    }
    if (strcmp(name, "yanimal_cat") == 0) {
        return yanimal_cat_class_get();
    }
    if (strcmp(name, "yanimal_dog") == 0) {
        return yanimal_dog_class_get();
    }
    if (strcmp(name, "yanimal_pet") == 0) {
        return yanimal_pet_mixin_get();
    }
    /* "Not mine": OK with NULL value — class_by_name walks to next hook. */
    return YETTY_OK(class_ptr, NULL);
}

/* ---- yanimal: slot → skel, name-keyed static data --------------- */

struct yanimal_skel_row {
    const char *name;
    rpc_skel_fn fn;
};

static const struct yanimal_skel_row yanimal_skel_rows[] = {
    {"yanimal_animal_ctor", yanimal_animal_ctor_skel},
    {"yanimal_animal_dtor", yanimal_animal_dtor_skel},
    {"yanimal_animal_breathe", yanimal_animal_breathe_skel},
    {"yanimal_animal_speak", yanimal_animal_speak_skel},
    {"yanimal_animal_eat", yanimal_animal_eat_skel}};

static rpc_skel_fn yanimal_skel_lookup(method_slot slot)
{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) {
        yetty_ycore_error_destroy(nr.error);
        return NULL;
    }
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof(yanimal_skel_rows) / sizeof(yanimal_skel_rows[0]); ++i) {
        if (strcmp(yanimal_skel_rows[i].name, name) == 0) {
            return yanimal_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yanimal: install hooks before main ------------------------- */

__attribute__((constructor)) static void yanimal_install_hooks(void)
{
    struct yetty_ycore_void_result _ar = class_add_accessor_lookup(yanimal_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yanimal_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
    rpc_add_skel_lookup(yanimal_skel_lookup);
}
