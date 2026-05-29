/* GENERATED — do not edit. */
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/yfigure/rpc.h"
#include "yetty/yfigure/methods.h"
#include <yclass/class.h>
#include "yetty/yfigure/container.h"
#include "yetty/yfigure/figure.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yetty_yfigure_constructor_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_constructor: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_yfigure_constructor(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_constructor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_yfigure_add_child_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint64_t child_handle;
        uint32_t id;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_add_child: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_yclass_void_ptr_result _hr_child =
        yetty_yclass_rpc_handle_resolve(_a.child_handle);
    if (YETTY_IS_ERR(_hr_child)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_add_child: handle_resolve", _hr_child.error);
        yetty_ycore_error_destroy(_hr_child.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_yfigure_add_child(&_local, (struct yetty_yclass_object *)_hr_obj.value, (struct yetty_yfigure_figure *)_hr_child.value, _a.id);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_add_child", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_yfigure_remove_child_by_id_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t id;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_remove_child_by_id: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_yfigure_remove_child_by_id(&_local, (struct yetty_yclass_object *)_hr_obj.value, _a.id);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_remove_child_by_id", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_yfigure_raise_child_by_id_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t id;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_raise_child_by_id: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_yfigure_raise_child_by_id(&_local, (struct yetty_yclass_object *)_hr_obj.value, _a.id);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_raise_child_by_id", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_yfigure_process_records_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t bytes_len;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    if (_body_len != sizeof(_a) + (size_t)_a.bytes_len) return 0;
    size_t _bo = sizeof(_a);
    struct yetty_ycore_buffer _buf_bytes = {
        .data = (uint8_t *)((const uint8_t *)_body + _bo),
        .size = (size_t)_a.bytes_len,
        .capacity = (size_t)_a.bytes_len,
    };
    _bo += (size_t)_a.bytes_len;
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yfigure_process_records: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_yfigure_process_records(&_local, (struct yetty_yclass_object *)_hr_obj.value, _buf_bytes);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yfigure_process_records", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yfigure_container");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_yfigure_container_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_yfigure_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yfigure_container_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yfigure_container");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_yfigure_container_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_yfigure_container";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_container_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yfigure_container_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yfigure_figure");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_figure_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_yfigure_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yfigure_figure_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yfigure_figure");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_yfigure_figure_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_yfigure_figure";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_figure_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yfigure_figure_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yfigure_figure_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

/* ---- yfigure: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yfigure_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yfigure_container") == 0) return yetty_yfigure_container_class_get();
    if (strcmp(name, "yetty_yfigure_figure") == 0) return yetty_yfigure_figure_class_get();
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yfigure: slot → skel, name-keyed static data --------------- */

struct yetty_yfigure_skel_row { const char *name; yetty_yclass_rpc_skel_fn fn; };

static const struct yetty_yfigure_skel_row yetty_yfigure_skel_rows[] = {
    {"yetty_yfigure_constructor", yetty_yfigure_constructor_skel},
    {"yetty_yfigure_add_child", yetty_yfigure_add_child_skel},
    {"yetty_yfigure_remove_child_by_id", yetty_yfigure_remove_child_by_id_skel},
    {"yetty_yfigure_raise_child_by_id", yetty_yfigure_raise_child_by_id_skel},
    {"yetty_yfigure_process_records", yetty_yfigure_process_records_skel}
};

static yetty_yclass_rpc_skel_fn yetty_yfigure_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) { yetty_ycore_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0;
         i < sizeof(yetty_yfigure_skel_rows) / sizeof(yetty_yfigure_skel_rows[0]); ++i)
        if (strcmp(yetty_yfigure_skel_rows[i].name, name) == 0)
            return yetty_yfigure_skel_rows[i].fn;
    return NULL;
}

/* ---- yfigure: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_yfigure_install_hooks(void)
{
    struct yetty_ycore_void_result _ar =
        yetty_yclass_add_accessor_lookup(yetty_yfigure_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yetty_yfigure_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
    {
        struct yetty_ycore_void_result _sr =
            yetty_yclass_rpc_add_skel_lookup(yetty_yfigure_skel_lookup);
        if (YETTY_IS_ERR(_sr)) {
            yetty_ycore_error_print(stderr,
                "yetty_yfigure_install_hooks: rpc_add_skel_lookup", _sr.error);
            yetty_ycore_error_destroy(_sr.error);
            abort();
        }
    }
}
