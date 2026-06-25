/*
 * yplatform/yclipboard/ios-tvos.c — iOS / tvOS clipboard subclass.
 *
 * UIPasteboard.general is the system clipboard. UIKit access must happen on the
 * main thread, so the actual UIPasteboard calls live in a C-ABI Obj-C companion
 * (wired in later); this plain-C subclass forwards to it so the yclass generator
 * can parse the file as C. set_text is fire-and-forget; request_paste hands the
 * response pipe to the helper, which fetches the contents and posts the
 * YETTY_YCORE_PASTE result onto that pipe.
 *
 * No drain override — the base no-op suffices (the Obj-C helper does its own
 * main-thread dispatch).
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/platform-input-pipe.h>

/* UIPasteboard bridge (implemented in the Obj-C companion at wire time).
 * Return 0 on success. */
int yetty_yplatform_ios_clipboard_set_text(const char *text, size_t len);
int yetty_yplatform_ios_clipboard_request_paste(
    struct yetty_ycore_xthread_event_pipe *response_pipe);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_ios_clipboard_ptr, struct yetty_yplatform_ios_clipboard *);
struct yetty_yclass_ptr_result yetty_yplatform_ios_clipboard_class_get(void);
struct yetty_yplatform_ios_clipboard_ptr_result yetty_yplatform_ios_clipboard_from(
    struct yetty_yclass_object *obj);

/* Private subclass state: the pipe paste results are posted to (borrowed). */
struct YETTY_ANNOTATE("class@yplatform:ios_clipboard") YETTY_ANNOTATE("platform@ios")
    YETTY_ANNOTATE("parent@yplatform:clipboard") yetty_yplatform_ios_clipboard {
    struct yetty_ycore_xthread_event_pipe *response_pipe;
};

/* Bind the pipe that paste results are delivered on. Borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yplatform_ios_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *response_pipe)
{
    if (!response_pipe) {
        return YETTY_ERR(yetty_ycore_void, "ios_clipboard_configure: response_pipe is required");
    }
    struct yetty_yplatform_ios_clipboard_ptr_result data = yetty_yplatform_ios_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "ios_clipboard_configure: data_get");
    data.value->response_pipe = response_pipe;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_clipboard:clipboard_set_text")
static struct yetty_ycore_void_result ios_clipboard_set_text(struct yetty_yclass_object *obj,
                                                             const char *text, size_t len)
{
    (void)obj;
    if (yetty_yplatform_ios_clipboard_set_text(text, len) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: UIPasteboard write failed");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:ios_clipboard:clipboard_request_paste")
static struct yetty_ycore_void_result ios_clipboard_request_paste(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_ios_clipboard_ptr_result data = yetty_yplatform_ios_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "clipboard_request_paste: data_get");
    if (!data.value->response_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: not configured");
    }
    if (yetty_yplatform_ios_clipboard_request_paste(data.value->response_pipe) != 0) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: UIPasteboard read failed");
    }
    return YETTY_OK_VOID();
}

#include "ios-tvos.gen.c"
