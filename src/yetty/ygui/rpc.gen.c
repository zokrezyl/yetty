/* GENERATED — do not edit. */
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/ygui/rpc.h"
#include "yetty/ygui/methods.h"
#include <yclass/class.h>
#include "yetty/ygui/mixins/clickable.h"
#include "yetty/ygui/primitive-widget.h"
#include "yetty/ygui/widget.h"
#include "yetty/ygui/widgets/breadcrumbs.h"
#include "yetty/ygui/widgets/button.h"
#include "yetty/ygui/widgets/checkbox.h"
#include "yetty/ygui/widgets/chip.h"
#include "yetty/ygui/widgets/choicebox.h"
#include "yetty/ygui/widgets/collapsing_header.h"
#include "yetty/ygui/widgets/colorpicker.h"
#include "yetty/ygui/widgets/combobox.h"
#include "yetty/ygui/widgets/dialog.h"
#include "yetty/ygui/widgets/dropdown.h"
#include "yetty/ygui/widgets/hbox.h"
#include "yetty/ygui/widgets/label.h"
#include "yetty/ygui/widgets/list.h"
#include "yetty/ygui/widgets/menubar.h"
#include "yetty/ygui/widgets/panel.h"
#include "yetty/ygui/widgets/popup_menu.h"
#include "yetty/ygui/widgets/progress.h"
#include "yetty/ygui/widgets/radio.h"
#include "yetty/ygui/widgets/rich.h"
#include "yetty/ygui/widgets/scrollarea.h"
#include "yetty/ygui/widgets/selectable.h"
#include "yetty/ygui/widgets/separator.h"
#include "yetty/ygui/widgets/slider.h"
#include "yetty/ygui/widgets/spinner.h"
#include "yetty/ygui/widgets/splitter.h"
#include "yetty/ygui/widgets/statusbar.h"
#include "yetty/ygui/widgets/stepper.h"
#include "yetty/ygui/widgets/tabbar.h"
#include "yetty/ygui/widgets/table.h"
#include "yetty/ygui/widgets/textarea.h"
#include "yetty/ygui/widgets/textinput.h"
#include "yetty/ygui/widgets/toggle.h"
#include "yetty/ygui/widgets/tooltip.h"
#include "yetty/ygui/widgets/tree_node.h"
#include "yetty/ygui/widgets/vbox.h"
#include "yetty/ygui/widgets/ybrowser.h"
#include "yetty/ygui/widgets/ydraw_embed.h"
#include "yetty/ygui/widgets/yimage.h"
#include "yetty/ygui/widgets/yjungle.h"
#include "yetty/ygui/widgets/ymarkdown.h"
#include "yetty/ygui/widgets/yplot.h"
#include "yetty/ygui/widgets/yvideo.h"
#include "yetty/ygui/widgets/yzoo.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t yetty_ygui_widget_on_press_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t _yc_obj_handle;
        float x;
        float y;
        int button;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr__yc_obj =
        yetty_yclass_rpc_handle_resolve(_a._yc_obj_handle);
    if (YETTY_IS_ERR(_hr__yc_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygui_widget_on_press: handle_resolve", _hr__yc_obj.error);
        yetty_ycore_error_destroy(_hr__yc_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result _r = yetty_ygui_widget_on_press(&_local, (struct yetty_yclass_object *)_hr__yc_obj.value, _a.x, _a.y, _a.button);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_press", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

static size_t yetty_ygui_widget_on_release_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t _yc_obj_handle;
        float x;
        float y;
        int button;
    } _a;
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (_body_len != sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {0};
    struct yetty_yclass_void_ptr_result _hr__yc_obj =
        yetty_yclass_rpc_handle_resolve(_a._yc_obj_handle);
    if (YETTY_IS_ERR(_hr__yc_obj)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_ygui_widget_on_release: handle_resolve", _hr__yc_obj.error);
        yetty_ycore_error_destroy(_hr__yc_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result _r = yetty_ygui_widget_on_release(&_local, (struct yetty_yclass_object *)_hr__yc_obj.value, _a.x, _a.y, _a.button);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_release", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

static size_t yetty_ygui_constructor_skel(const void *_body, size_t _body_len,
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
            "[skel] yetty_ygui_constructor: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygui_constructor(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_constructor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygui_destructor_skel(const void *_body, size_t _body_len,
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
            "[skel] yetty_ygui_destructor: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result _r = yetty_ygui_destructor(&_local, (struct yetty_yclass_object *)_hr_obj.value);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_destructor", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)_resp)[0] = 0;
    return 1;
}

static size_t yetty_ygui_widget_on_motion_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{
    struct __attribute__((packed)) {
        uint64_t obj_handle;
        float x;
        float y;
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
            "[skel] yetty_ygui_widget_on_motion: handle_resolve", _hr_obj.error);
        yetty_ycore_error_destroy(_hr_obj.error);
        if (_resp_max < 1) return 0;
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_int_result _r = yetty_ygui_widget_on_motion(&_local, (struct yetty_yclass_object *)_hr_obj.value, _a.x, _a.y);
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_motion", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ygui_primitive_widget_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_primitive_widget");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_primitive_widget_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_primitive_widget_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_primitive_widget");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_primitive_widget_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_primitive_widget";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_primitive_widget_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_primitive_widget_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_primitive_widget_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_widget");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_widget_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_widget");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_widget_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_widget";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_breadcrumbs");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_breadcrumbs_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_breadcrumbs_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_breadcrumbs");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_breadcrumbs_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_breadcrumbs";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_breadcrumbs_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_breadcrumbs_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_button_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_button");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_button_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_button_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_button_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_button");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_button_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_button";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_button_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_button_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_button_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_checkbox_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_checkbox");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_checkbox_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_checkbox_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_checkbox_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_checkbox");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_checkbox_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_checkbox";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_checkbox_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_checkbox_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_checkbox_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_chip_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_chip");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_chip_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_chip_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_chip_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_chip");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_chip_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_chip";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_chip_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_chip_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_chip_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_choicebox_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_choicebox");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_choicebox_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_choicebox_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_choicebox_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_choicebox");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_choicebox_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_choicebox";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_choicebox_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_choicebox_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_choicebox_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_collapsing_header_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_collapsing_header");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_collapsing_header_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_collapsing_header_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_collapsing_header_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_collapsing_header");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_collapsing_header_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_collapsing_header";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_collapsing_header_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_collapsing_header_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_collapsing_header_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_colorpicker_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_colorpicker");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_colorpicker_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_colorpicker_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_colorpicker_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_colorpicker");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_colorpicker_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_colorpicker";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_colorpicker_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_colorpicker_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_colorpicker_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_combobox_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_combobox");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_combobox_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_combobox_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_combobox_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_combobox");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_combobox_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_combobox";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_combobox_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_combobox_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_combobox_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_dialog_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_dialog");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_dialog_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dialog_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_dialog_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_dialog");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_dialog_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_dialog";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dialog_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dialog_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_dialog_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_dropdown_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_dropdown");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_dropdown_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dropdown_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_dropdown_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_dropdown");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_dropdown_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_dropdown";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dropdown_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_dropdown_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_dropdown_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_hbox_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_hbox");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_hbox_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_hbox_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_hbox_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_hbox");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_hbox_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_hbox";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_hbox_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_hbox_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_hbox_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_label_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_label");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_label_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_label_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_label_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_label");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_label_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_label";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_label_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_label_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_label_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_list_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_list");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_list_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_list_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_list_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_list");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_list_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_list";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_list_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_list_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_list_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_menubar_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_menubar");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_menubar_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_menubar_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_menubar_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_menubar");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_menubar_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_menubar";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_menubar_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_menubar_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_menubar_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_panel_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_panel");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_panel_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_panel_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_panel_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_panel");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_panel_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_panel";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_panel_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_panel_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_panel_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_popup_menu_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_popup_menu");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_popup_menu_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_popup_menu_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_popup_menu_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_popup_menu");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_popup_menu_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_popup_menu";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_popup_menu_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_popup_menu_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_popup_menu_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_progress_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_progress");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_progress_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_progress_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_progress_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_progress");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_progress_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_progress";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_progress_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_progress_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_progress_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_radio_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_radio");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_radio_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_radio_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_radio_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_radio");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_radio_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_radio";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_radio_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_radio_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_radio_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_rich_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_rich");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_rich_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_rich_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_rich_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_rich");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_rich_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_rich";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_rich_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_rich_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_rich_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_scrollarea_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_scrollarea");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_scrollarea_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_scrollarea_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_scrollarea_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_scrollarea");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_scrollarea_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_scrollarea";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_scrollarea_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_scrollarea_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_scrollarea_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_selectable_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_selectable");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_selectable_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_selectable_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_selectable_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_selectable");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_selectable_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_selectable";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_selectable_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_selectable_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_selectable_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_separator_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_separator");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_separator_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_separator_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_separator_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_separator");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_separator_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_separator";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_separator_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_separator_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_separator_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_slider_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_slider");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_slider_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_slider_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_slider_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_slider");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_slider_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_slider";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_slider_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_slider_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_slider_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_spinner_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_spinner");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_spinner_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_spinner_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_spinner_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_spinner");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_spinner_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_spinner";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_spinner_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_spinner_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_spinner_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_splitter_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_splitter");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_splitter_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_splitter_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_splitter_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_splitter");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_splitter_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_splitter";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_splitter_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_splitter_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_splitter_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_statusbar_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_statusbar");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_statusbar_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_statusbar_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_statusbar_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_statusbar");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_statusbar_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_statusbar";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_statusbar_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_statusbar_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_statusbar_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_stepper_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_stepper");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_stepper_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_stepper_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_stepper_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_stepper");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_stepper_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_stepper";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_stepper_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_stepper_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_stepper_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_tabbar");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_tabbar_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tabbar_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_tabbar_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_tabbar");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_tabbar_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_tabbar";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tabbar_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tabbar_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_tabbar_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_table_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_table");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_table_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_table_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_table_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_table");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_table_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_table";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_table_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_table_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_table_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_textarea_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_textarea");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_textarea_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textarea_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_textarea_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_textarea");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_textarea_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_textarea";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textarea_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textarea_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_textarea_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_textinput_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_textinput");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_textinput_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textinput_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_textinput_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_textinput");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_textinput_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_textinput";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textinput_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_textinput_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_textinput_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_toggle_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_toggle");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_toggle_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_toggle_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_toggle_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_toggle");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_toggle_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_toggle";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_toggle_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_toggle_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_toggle_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_tooltip_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_tooltip");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_tooltip_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tooltip_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_tooltip_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_tooltip");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_tooltip_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_tooltip";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tooltip_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tooltip_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_tooltip_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_tree_node_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_tree_node");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_tree_node_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tree_node_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_tree_node_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_tree_node");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_tree_node_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_tree_node";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tree_node_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_tree_node_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_tree_node_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_vbox_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_vbox");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_vbox_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_vbox_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_vbox_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_vbox");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_vbox_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_vbox";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_vbox_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_vbox_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_vbox_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_ybrowser_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_ybrowser");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_ybrowser_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ybrowser_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_ybrowser_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_ybrowser");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_ybrowser_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_ybrowser";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ybrowser_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ybrowser_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_ybrowser_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_ydraw_embed_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_ydraw_embed");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ydraw_embed_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_ydraw_embed_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_ydraw_embed");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_ydraw_embed_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_ydraw_embed";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ydraw_embed_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ydraw_embed_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_ydraw_embed_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yimage_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yimage");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_yimage_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yimage_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_yimage_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_yimage");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_yimage_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_yimage";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yimage_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yimage_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_yimage_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yjungle_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yjungle");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_yjungle_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yjungle_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_yjungle_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_yjungle");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_yjungle_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_yjungle";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yjungle_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yjungle_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_yjungle_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_ymarkdown_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_ymarkdown");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_ymarkdown_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ymarkdown_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_ymarkdown_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_ymarkdown");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_ymarkdown_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_ymarkdown";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ymarkdown_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_ymarkdown_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_ymarkdown_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yplot_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yplot");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_yplot_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yplot_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_yplot_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_yplot");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_yplot_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_yplot";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yplot_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yplot_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_yplot_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yvideo_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yvideo");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_yvideo_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yvideo_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_yvideo_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_yvideo");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_yvideo_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_yvideo";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yvideo_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yvideo_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_yvideo_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yzoo_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yzoo");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = yetty_ygui_yzoo_class_get();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yzoo_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result _alloc =
            yetty_yclass_object_alloc(_klass);
        if (YETTY_IS_ERR(_alloc)) return _alloc;
        struct yetty_ycore_void_result _ct =
            yetty_ygui_constructor(ctx, _alloc.value);
        if (YETTY_IS_ERR(_ct)) {
            struct yetty_ycore_void_result _fr =
                yetty_yclass_object_free(_alloc.value);
            if (YETTY_IS_ERR(_fr)) yetty_ycore_error_destroy(_fr.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_yzoo_create: constructor failed", _ct);
        }
        return _alloc;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result _tr =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_yzoo");
        if (YETTY_IS_ERR(_tr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_yzoo_create: translate_class (degraded — will lazy-resolve)",
                _tr.error);
            yetty_ycore_error_destroy(_tr.error);
        }
    }

    uint64_t _h = 0;
    const char *_name = "yetty_ygui_yzoo";
    struct yetty_ycore_size_result _cr = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name, strlen(_name), &_h,
        sizeof(_h));
    if (YETTY_IS_ERR(_cr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yzoo_create: CREATE call failed", _cr);
    if (_cr.value != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yzoo_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *_proxy = calloc(1, sizeof(*_proxy));
    if (!_proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_yzoo_create: calloc(proxy) failed");
    _proxy->header.klass = _klass;
    _proxy->handle = _h;
    return YETTY_OK(yetty_yclass_object_ptr, &_proxy->header);
}

/* ---- ygui: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_clickable") == 0) return yetty_ygui_clickable_mixin_get();
    if (strcmp(name, "yetty_ygui_primitive_widget") == 0) return yetty_ygui_primitive_widget_class_get();
    if (strcmp(name, "yetty_ygui_widget") == 0) return yetty_ygui_widget_class_get();
    if (strcmp(name, "yetty_ygui_breadcrumbs") == 0) return yetty_ygui_breadcrumbs_class_get();
    if (strcmp(name, "yetty_ygui_button") == 0) return yetty_ygui_button_class_get();
    if (strcmp(name, "yetty_ygui_checkbox") == 0) return yetty_ygui_checkbox_class_get();
    if (strcmp(name, "yetty_ygui_chip") == 0) return yetty_ygui_chip_class_get();
    if (strcmp(name, "yetty_ygui_choicebox") == 0) return yetty_ygui_choicebox_class_get();
    if (strcmp(name, "yetty_ygui_collapsing_header") == 0) return yetty_ygui_collapsing_header_class_get();
    if (strcmp(name, "yetty_ygui_colorpicker") == 0) return yetty_ygui_colorpicker_class_get();
    if (strcmp(name, "yetty_ygui_combobox") == 0) return yetty_ygui_combobox_class_get();
    if (strcmp(name, "yetty_ygui_dialog") == 0) return yetty_ygui_dialog_class_get();
    if (strcmp(name, "yetty_ygui_dropdown") == 0) return yetty_ygui_dropdown_class_get();
    if (strcmp(name, "yetty_ygui_hbox") == 0) return yetty_ygui_hbox_class_get();
    if (strcmp(name, "yetty_ygui_label") == 0) return yetty_ygui_label_class_get();
    if (strcmp(name, "yetty_ygui_list") == 0) return yetty_ygui_list_class_get();
    if (strcmp(name, "yetty_ygui_menubar") == 0) return yetty_ygui_menubar_class_get();
    if (strcmp(name, "yetty_ygui_panel") == 0) return yetty_ygui_panel_class_get();
    if (strcmp(name, "yetty_ygui_popup_menu") == 0) return yetty_ygui_popup_menu_class_get();
    if (strcmp(name, "yetty_ygui_progress") == 0) return yetty_ygui_progress_class_get();
    if (strcmp(name, "yetty_ygui_radio") == 0) return yetty_ygui_radio_class_get();
    if (strcmp(name, "yetty_ygui_rich") == 0) return yetty_ygui_rich_class_get();
    if (strcmp(name, "yetty_ygui_scrollarea") == 0) return yetty_ygui_scrollarea_class_get();
    if (strcmp(name, "yetty_ygui_selectable") == 0) return yetty_ygui_selectable_class_get();
    if (strcmp(name, "yetty_ygui_separator") == 0) return yetty_ygui_separator_class_get();
    if (strcmp(name, "yetty_ygui_slider") == 0) return yetty_ygui_slider_class_get();
    if (strcmp(name, "yetty_ygui_spinner") == 0) return yetty_ygui_spinner_class_get();
    if (strcmp(name, "yetty_ygui_splitter") == 0) return yetty_ygui_splitter_class_get();
    if (strcmp(name, "yetty_ygui_statusbar") == 0) return yetty_ygui_statusbar_class_get();
    if (strcmp(name, "yetty_ygui_stepper") == 0) return yetty_ygui_stepper_class_get();
    if (strcmp(name, "yetty_ygui_tabbar") == 0) return yetty_ygui_tabbar_class_get();
    if (strcmp(name, "yetty_ygui_table") == 0) return yetty_ygui_table_class_get();
    if (strcmp(name, "yetty_ygui_textarea") == 0) return yetty_ygui_textarea_class_get();
    if (strcmp(name, "yetty_ygui_textinput") == 0) return yetty_ygui_textinput_class_get();
    if (strcmp(name, "yetty_ygui_toggle") == 0) return yetty_ygui_toggle_class_get();
    if (strcmp(name, "yetty_ygui_tooltip") == 0) return yetty_ygui_tooltip_class_get();
    if (strcmp(name, "yetty_ygui_tree_node") == 0) return yetty_ygui_tree_node_class_get();
    if (strcmp(name, "yetty_ygui_vbox") == 0) return yetty_ygui_vbox_class_get();
    if (strcmp(name, "yetty_ygui_ybrowser") == 0) return yetty_ygui_ybrowser_class_get();
    if (strcmp(name, "yetty_ygui_ydraw_embed") == 0) return yetty_ygui_ydraw_embed_class_get();
    if (strcmp(name, "yetty_ygui_yimage") == 0) return yetty_ygui_yimage_class_get();
    if (strcmp(name, "yetty_ygui_yjungle") == 0) return yetty_ygui_yjungle_class_get();
    if (strcmp(name, "yetty_ygui_ymarkdown") == 0) return yetty_ygui_ymarkdown_class_get();
    if (strcmp(name, "yetty_ygui_yplot") == 0) return yetty_ygui_yplot_class_get();
    if (strcmp(name, "yetty_ygui_yvideo") == 0) return yetty_ygui_yvideo_class_get();
    if (strcmp(name, "yetty_ygui_yzoo") == 0) return yetty_ygui_yzoo_class_get();
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui: slot → skel, name-keyed static data --------------- */

struct yetty_ygui_skel_row { const char *name; yetty_yclass_rpc_skel_fn fn; };

static const struct yetty_ygui_skel_row yetty_ygui_skel_rows[] = {
    {"yetty_ygui_widget_on_press", yetty_ygui_widget_on_press_skel},
    {"yetty_ygui_widget_on_release", yetty_ygui_widget_on_release_skel},
    {"yetty_ygui_constructor", yetty_ygui_constructor_skel},
    {"yetty_ygui_destructor", yetty_ygui_destructor_skel},
    {"yetty_ygui_widget_on_motion", yetty_ygui_widget_on_motion_skel}
};

static yetty_yclass_rpc_skel_fn yetty_ygui_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) { yetty_ycore_error_destroy(nr.error); return NULL; }
    const char *name = nr.value;
    for (size_t i = 0;
         i < sizeof(yetty_ygui_skel_rows) / sizeof(yetty_ygui_skel_rows[0]); ++i)
        if (strcmp(yetty_ygui_skel_rows[i].name, name) == 0)
            return yetty_ygui_skel_rows[i].fn;
    return NULL;
}

/* ---- ygui: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_ygui_install_hooks(void)
{
    struct yetty_ycore_void_result _ar =
        yetty_yclass_add_accessor_lookup(yetty_ygui_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {
        yetty_ycore_error_print(stderr, "yetty_ygui_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }
    {
        struct yetty_ycore_void_result _sr =
            yetty_yclass_rpc_add_skel_lookup(yetty_ygui_skel_lookup);
        if (YETTY_IS_ERR(_sr)) {
            yetty_ycore_error_print(stderr,
                "yetty_ygui_install_hooks: rpc_add_skel_lookup", _sr.error);
            yetty_ycore_error_destroy(_sr.error);
            abort();
        }
    }
}
