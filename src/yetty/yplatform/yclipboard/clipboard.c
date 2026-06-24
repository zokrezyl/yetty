/*
 * yplatform/yclipboard/clipboard.c — system clipboard abstraction, base class.
 *
 * The clipboard is a system/global resource on every platform — X11/Wayland
 * CLIPBOARD (via GLFW), iOS/tvOS UIPasteboard, the browser's
 * navigator.clipboard — and is never owned by a window. This base introduces
 * the virtual contract; the per-platform subclasses (glfw_clipboard,
 * webasm_clipboard, ios_clipboard) implement it and hold their own plumbing.
 *
 * Model:
 *   - clipboard_set_text      push text to the system clipboard.
 *   - clipboard_request_paste ASYNCHRONOUS: ask for the current contents. The
 *                             text is delivered later as a YETTY_YCORE_PASTE
 *                             event on a response pipe the subclass holds (GLFW
 *                             needs a main-thread round trip; the browser read
 *                             is a Promise) — there is no synchronous getter.
 *   - clipboard_drain         service deferred main-thread work (GLFW). A no-op
 *                             where the platform needs none (iOS/tvOS, webasm).
 *
 * Every slot is local@ — a clipboard is a process-local object, never proxied
 * over RPC.
 *
 * NOT WIRED IN: no CMake entry and codegen has not been run, so the
 * clipboard.gen.c included at the foot does not exist yet. Review first.
 */

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* Base clipboard data slice. Abstract — holds no shared state; each subclass
 * keeps its own pipes (response channel, and on GLFW the marshalling bus). */
struct [[clang::annotate("class@yplatform:clipboard")]] yetty_yplatform_clipboard {
    char reserved;
};

/* Result wrapper + codegen accessor/downcast forward-decls. This TU does not
 * include its own generated header (a downstream artifact); the generated
 * clipboard.h publishes the identical declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_yplatform_clipboard_ptr, struct yetty_yplatform_clipboard *);
struct yetty_yclass_ptr_result yetty_yplatform_clipboard_class_get(void);
struct yetty_yplatform_clipboard_ptr_result yetty_yplatform_clipboard_from(
    struct yetty_yclass_object *obj);

/*===========================================================================
 * Virtual slots introduced by the base clipboard class. Each platform subclass
 * overrides set_text / request_paste; drain has a no-op default that platforms
 * with no deferred main-thread work inherit.
 *=========================================================================*/

[[clang::annotate("virtual@yplatform:clipboard:clipboard_set_text")]] [[clang::annotate(
    "local@yplatform:clipboard_set_text")]]
static struct yetty_ycore_void_result clipboard_default_set_text(struct yetty_yclass_object *obj,
                                                                 const char *text, size_t len)
{
    (void)obj;
    (void)text;
    (void)len;
    return YETTY_ERR(yetty_ycore_void,
                     "clipboard_set_text: not implemented by base clipboard class");
}

[[clang::annotate("virtual@yplatform:clipboard:clipboard_request_paste")]] [[clang::annotate(
    "local@yplatform:clipboard_request_paste")]]
static struct yetty_ycore_void_result clipboard_default_request_paste(
    struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_ERR(yetty_ycore_void,
                     "clipboard_request_paste: not implemented by base clipboard class");
}

[[clang::annotate("virtual@yplatform:clipboard:clipboard_drain")]] [[clang::annotate(
    "local@yplatform:clipboard_drain")]]
static struct yetty_ycore_void_result clipboard_default_drain(struct yetty_yclass_object *obj)
{
    /* Deferred main-thread work is platform-specific; platforms that need none
     * (iOS/tvOS, webasm) inherit this no-op. */
    (void)obj;
    return YETTY_OK_VOID();
}

#include "clipboard.gen.c"
