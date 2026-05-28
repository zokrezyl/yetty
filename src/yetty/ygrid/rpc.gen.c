/* GENERATED — do not edit. */
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/ygrid/rpc.h"
#include "yetty/ygrid/methods.h"
#include <yclass/class.h>
#include "yetty/ygrid/grid.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yetty_ygrid_add_record_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t record_len;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    if (_body_len != sizeof(_a) + (size_t)_a.record_len) return 0;
    size_t _bo = sizeof(_a);
    struct yetty_ycore_buffer _buf_record = {
        .data = (uint8_t *)((const uint8_t *)_body + _bo),
        .size = (size_t)_a.record_len,
        .capacity = (size_t)_a.record_len,
    };
    _bo += (size_t)_a.record_len;
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_add_record: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygrid_add_record(&_local, (struct yetty_yclass_object *)_hr_obj.value, _buf_record);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_add_record", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_clear_skel(const void *_body, size_t _body_len,
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
            "[skel] yetty_ygrid_clear: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygrid_clear(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_clear", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_destroy_skel(const void *_body, size_t _body_len,
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
            "[skel] yetty_ygrid_destroy: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygrid_destroy(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_destroy", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_process_bytes_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        uint32_t payload_len;
    } _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    if (_body_len != sizeof(_a) + (size_t)_a.payload_len) return 0;
    size_t _bo = sizeof(_a);
    struct yetty_ycore_buffer _buf_payload = {
        .data = (uint8_t *)((const uint8_t *)_body + _bo),
        .size = (size_t)_a.payload_len,
        .capacity = (size_t)_a.payload_len,
    };
    _bo += (size_t)_a.payload_len;
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr_obj =
        yetty_yclass_rpc_handle_resolve(_a.obj_handle);
    if (YETTY_IS_ERR(_hr_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygrid_process_bytes: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygrid_process_bytes(&_local, (struct yetty_yclass_object *)_hr_obj.value, _buf_payload);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_process_bytes", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygrid_reset_content_skel(const void *_body, size_t _body_len,
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
            "[skel] yetty_ygrid_reset_content: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygrid_reset_content(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygrid_reset_content", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

struct yetty_yclass_object_ptr_result yetty_ygrid_grid_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygrid_grid");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygrid_grid_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygrid_grid");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygrid_grid_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygrid_grid";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygrid_grid_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygrid_grid_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

/* ---- ygrid: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygrid_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygrid_grid") == 0) return yetty_ygrid_grid_class_get();
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygrid: slot → skel, name-keyed static data --------------- */

struct yetty_ygrid_skel_row { const char *name; yetty_yclass_rpc_skel_fn fn; };

static const struct yetty_ygrid_skel_row yetty_ygrid_skel_rows[] = {
    {"yetty_ygrid_add_record", yetty_ygrid_add_record_skel},
    {"yetty_ygrid_clear", yetty_ygrid_clear_skel},
    {"yetty_ygrid_destroy", yetty_ygrid_destroy_skel},
    {"yetty_ygrid_process_bytes", yetty_ygrid_process_bytes_skel},
    {"yetty_ygrid_reset_content", yetty_ygrid_reset_content_skel}
};

static yetty_yclass_rpc_skel_fn yetty_ygrid_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) { yetty_ycore_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0;
         i < sizeof(yetty_ygrid_skel_rows) / sizeof(yetty_ygrid_skel_rows[0]); ++i)
        if (strcmp(yetty_ygrid_skel_rows[i].name, name) == 0)
            return yetty_ygrid_skel_rows[i].fn;
    return NULL;
}

/* ---- ygrid: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_ygrid_install_hooks(void)
{
    struct yetty_ycore_void_result _ar =
        yetty_yclass_add_accessor_lookup(yetty_ygrid_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yetty_ygrid_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
    {
        struct yetty_ycore_void_result _sr =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygrid_skel_lookup);
        if (YETTY_IS_ERR(_sr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygrid_install_hooks: rpc_add_skel_lookup", _sr.error);
            yetty_ycore_error_destroy(_sr.error);
            abort();
        }
    }
}
