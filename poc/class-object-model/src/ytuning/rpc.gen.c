/* GENERATED — do not edit. */
#include "rpc.h"
#include "result.h"
#include "ytrace.h"
#include "ytuning/rpc.gen.h"
#include "ytuning/methods.gen.h"
#include "class.h"
#include "ytuning/tuned_sportscar.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct object_ptr_result ytuning_tuned_sportscar_create(struct ctx *ctx)
{
    ydebug("class=ytuning_tuned_sportscar");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct class_ptr_result _kr = ytuning_tuned_sportscar_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(object_ptr, "ytuning_tuned_sportscar_create: class accessor failed", _kr);
    const struct class *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "ytuning_tuned_sportscar");

    uint64_t _h = 0;
    const char *_name = "ytuning_tuned_sportscar";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return YETTY_ERR(object_ptr, "ytuning_tuned_sportscar_create: remote create failed");

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem)
        return YETTY_ERR(object_ptr, "ytuning_tuned_sportscar_create: calloc(proxy) failed");
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    _obj->session = ctx->session;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(object_ptr, _obj);
}

/* ---- ytuning: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result ytuning_accessor_lookup(const char *name)
{
    if (strcmp(name, "ytuning_tuned_sportscar") == 0) return ytuning_tuned_sportscar_class_get();
    /* "Not mine": OK with NULL value — class_by_name walks to next hook. */
    return YETTY_OK(class_ptr, NULL);
}

/* ---- ytuning: install hooks before main ------------------------- */

__attribute__((constructor))
static void ytuning_install_hooks(void)
{
    struct yetty_ycore_void_result _ar = class_add_accessor_lookup(ytuning_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "ytuning_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
}
