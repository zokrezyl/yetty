/* GENERATED — do not edit. */
#include "rpc.h"
#include "yanimal/rpc.gen.h"
#include "yanimal/methods.gen.h"
#include "class.h"
#include "yanimal/animal.h"
#include "yanimal/cat.h"
#include "yanimal/dog.h"
#include "yanimal/pet.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t animal_ctor_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    animal_ctor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t animal_dtor_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    animal_dtor(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t animal_breathe_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    animal_breathe(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle));
    (void)_resp; (void)_resp_max;
    return 0;
}

static size_t animal_speak_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        int volume;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    struct str _r = animal_speak(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.volume);
    if (_resp_max < sizeof(_r)) return 0;
    memcpy(_resp, &_r, sizeof(_r));
    return sizeof(_r);
}

static size_t animal_eat_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float amount;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {0};
    int _r = animal_eat(&_local, (struct object *)rpc_handle_resolve(_a.obj_handle), _a.amount);
    if (_resp_max < sizeof(_r)) return 0;
    memcpy(_resp, &_r, sizeof(_r));
    return sizeof(_r);
}

struct object *animal_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = animal_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_animal");

    uint64_t _h = 0;
    const char *_name = "yanimal_animal";
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

struct object *cat_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = cat_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_cat");

    uint64_t _h = 0;
    const char *_name = "yanimal_cat";
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

struct object *dog_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = dog_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "yanimal_dog");

    uint64_t _h = 0;
    const char *_name = "yanimal_dog";
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

/* ---- yanimal: class name → accessor (lazy) ---------------------- */

static const struct class *yanimal_accessor_lookup(const char *name)
{
    if (strcmp(name, "yanimal_animal") == 0) return animal_class_get();
    if (strcmp(name, "yanimal_cat") == 0) return cat_class_get();
    if (strcmp(name, "yanimal_dog") == 0) return dog_class_get();
    if (strcmp(name, "yanimal_pet") == 0) return pet_mixin_get();
    return NULL;
}

/* ---- yanimal: slot → skel, name-keyed static data --------------- */

struct yanimal_skel_row { const char *name; rpc_skel_fn fn; };

static const struct yanimal_skel_row yanimal_skel_rows[] = {
    {"yanimal_animal_ctor", animal_ctor_skel},
    {"yanimal_animal_dtor", animal_dtor_skel},
    {"yanimal_animal_breathe", animal_breathe_skel},
    {"yanimal_animal_speak", animal_speak_skel},
    {"yanimal_animal_eat", animal_eat_skel}
};

static rpc_skel_fn yanimal_skel_lookup(method_slot slot)
{
    const char *name = method_slot_name(slot);
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(yanimal_skel_rows) / sizeof(yanimal_skel_rows[0]); ++i)
        if (strcmp(yanimal_skel_rows[i].name, name) == 0)
            return yanimal_skel_rows[i].fn;
    return NULL;
}

/* ---- yanimal: install hooks before main ------------------------- */

__attribute__((constructor))
static void yanimal_install_hooks(void)
{
    class_add_accessor_lookup(yanimal_accessor_lookup);
    rpc_add_skel_lookup(yanimal_skel_lookup);
}
