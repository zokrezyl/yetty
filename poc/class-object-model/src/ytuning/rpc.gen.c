/* GENERATED — do not edit. */
#include "rpc.h"
#include "ytuning/rpc.gen.h"
#include "ytuning/methods.gen.h"
#include "class.h"
#include "ytuning/tuned_sportscar.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct object *ytuning_tuned_sportscar_create(struct ctx *ctx)
{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = ytuning_tuned_sportscar_class_get();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "ytuning_tuned_sportscar");

    uint64_t _h = 0;
    const char *_name = "ytuning_tuned_sportscar";
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

/* ---- ytuning: class name → accessor (lazy) ---------------------- */

static const struct class *ytuning_accessor_lookup(const char *name)
{
    if (strcmp(name, "ytuning_tuned_sportscar") == 0) return ytuning_tuned_sportscar_class_get();
    return NULL;
}

/* ---- ytuning: install hooks before main ------------------------- */

__attribute__((constructor))
static void ytuning_install_hooks(void)
{
    class_add_accessor_lookup(ytuning_accessor_lookup);
}
