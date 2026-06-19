/*
 * yplatform/yclipboard/webasm.c — WebAssembly clipboard subclass.
 *
 * The browser clipboard is navigator.clipboard. writeText/readText are async
 * (Promises), so the actual JS interop lives in emscripten helpers (wired in
 * later); this plain-C subclass forwards to them. set_text is fire-and-forget;
 * request_paste hands the response pipe to the helper, which resolves the read
 * Promise and posts the YETTY_YCORE_PASTE result onto that pipe.
 *
 * No drain override — the browser event loop services the Promise itself, so
 * the base no-op drain suffices.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/platform-input-pipe.h>

/* Emscripten clipboard bridge (implemented in the webasm platform layer at
 * wire time, using EM_ASM + navigator.clipboard). Return 0 on success. */
int yetty_yplatform_webasm_clipboard_set_text(const char *text, size_t len);
int yetty_yplatform_webasm_clipboard_request_paste(
    struct yetty_ycore_xthread_event_pipe *response_pipe);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_webasm_clipboard_ptr,
                      struct yetty_yplatform_webasm_clipboard *);
struct yetty_yclass_ptr_result yetty_yplatform_webasm_clipboard_class_get(void);
struct yetty_yplatform_webasm_clipboard_ptr_result yetty_yplatform_webasm_clipboard_from(
    struct yetty_yclass_object *obj);

/* Private subclass state: the pipe paste results are posted to (borrowed). */
struct [[clang::annotate("class@yplatform:webasm_clipboard")]] [[clang::annotate("platform@webasm")]]
[[clang::annotate("parent@yplatform:clipboard")]] yetty_yplatform_webasm_clipboard {
    struct yetty_ycore_xthread_event_pipe *response_pipe;
};

/* Bind the pipe that paste results are delivered on. Borrowed. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yplatform_webasm_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe)
{
    if (!response_pipe) {
        return YETTY_ERR(yetty_ycore_void, "webasm_clipboard_configure: response_pipe is required");
    }
    struct yetty_yplatform_webasm_clipboard_ptr_result data =
        yetty_yplatform_webasm_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "webasm_clipboard_configure: data_get");
    data.value->response_pipe = response_pipe;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_clipboard:clipboard_set_text")]]
static struct yetty_ycore_void_result webasm_clipboard_set_text(struct yetty_yclass_object *obj,
                                                                const char *text, size_t len)
{
    (void)obj;
    if (yetty_yplatform_webasm_clipboard_set_text(text, len) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: navigator.clipboard write failed");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:webasm_clipboard:clipboard_request_paste")]]
static struct yetty_ycore_void_result webasm_clipboard_request_paste(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_webasm_clipboard_ptr_result data =
        yetty_yplatform_webasm_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "clipboard_request_paste: data_get");
    if (!data.value->response_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: not configured");
    }
    if (yetty_yplatform_webasm_clipboard_request_paste(data.value->response_pipe) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: navigator.clipboard read failed");
    }
    return YETTY_OK_VOID();
}

#include "webasm.gen.c"
