/* GENERATED — do not edit. */
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/ymgui/rpc.h"
#include "yetty/ymgui/methods.h"
#include <yclass/class.h>
#include "yetty/ymgui/figure.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct yetty_yclass_object_ptr_result yetty_ymgui_figure_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ymgui_figure");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ymgui_figure_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymgui_figure_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return yetty_yclass_object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ymgui_figure");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ymgui_figure_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ymgui_figure";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymgui_figure_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ymgui_figure_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ymgui_figure_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

/* ---- ymgui: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ymgui_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ymgui_figure") == 0) return yetty_ymgui_figure_class_get();
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ymgui: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_ymgui_install_hooks(void)
{
    struct yetty_ycore_void_result _ar =
        yetty_yclass_add_accessor_lookup(yetty_ymgui_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yetty_ymgui_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
}
