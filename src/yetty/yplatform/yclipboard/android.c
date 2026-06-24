/*
 * yplatform/yclipboard/android.c — Android clipboard subclass.
 *
 * The Android system clipboard is the ClipboardManager service, reached over
 * JNI. UI-thread access lives in a C-ABI bridge (implemented in the NDK glue at
 * wire time); this plain-C subclass forwards to it. set_text is fire-and-forget;
 * request_paste hands the response pipe to the bridge, which fetches the
 * contents and posts the YETTY_YCORE_PASTE result onto that pipe.
 *
 * No drain override — the base no-op suffices (the bridge does its own thread
 * dispatch).
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/platform-input-pipe.h>

/* ClipboardManager bridge (implemented in the NDK glue at wire time).
 * Return 0 on success. */
int yetty_yplatform_android_clipboard_set_text(const char *text, size_t len);
int yetty_yplatform_android_clipboard_request_paste(
    struct yetty_ycore_xthread_event_pipe *response_pipe);

YETTY_YRESULT_DECLARE(yetty_yplatform_android_clipboard_ptr,
                      struct yetty_yplatform_android_clipboard *);
struct yetty_yclass_ptr_result yetty_yplatform_android_clipboard_class_get(void);
struct yetty_yplatform_android_clipboard_ptr_result yetty_yplatform_android_clipboard_from(
    struct yetty_yclass_object *obj);

/* Private subclass state: the pipe paste results are posted to (borrowed). */
struct [[clang::annotate("class@yplatform:android_clipboard")]] [[clang::annotate(
    "platform@android")]] [[clang::annotate("parent@yplatform:clipboard")]]
yetty_yplatform_android_clipboard {
    struct yetty_ycore_xthread_event_pipe *response_pipe;
};

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yplatform_android_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe)
{
    if (!response_pipe) {
        return YETTY_ERR(yetty_ycore_void,
                         "android_clipboard_configure: response_pipe is required");
    }
    struct yetty_yplatform_android_clipboard_ptr_result data =
        yetty_yplatform_android_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "android_clipboard_configure: data_get");
    data.value->response_pipe = response_pipe;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:android_clipboard:clipboard_set_text")]]
static struct yetty_ycore_void_result android_clipboard_set_text(struct yetty_yclass_object *obj,
                                                                 const char *text, size_t len)
{
    (void)obj;
    if (yetty_yplatform_android_clipboard_set_text(text, len) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: ClipboardManager write failed");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:android_clipboard:clipboard_request_paste")]]
static struct yetty_ycore_void_result android_clipboard_request_paste(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_android_clipboard_ptr_result data =
        yetty_yplatform_android_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "clipboard_request_paste: data_get");
    if (!data.value->response_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: not configured");
    }
    if (yetty_yplatform_android_clipboard_request_paste(data.value->response_pipe) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: ClipboardManager read failed");
    }
    return YETTY_OK_VOID();
}

#include "android.gen.c"
