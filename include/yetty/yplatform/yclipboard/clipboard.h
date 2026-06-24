/* GENERATED — do not edit. */
/* Public interface for regular class(es) `clipboard` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YCLIPBOARD_CLIPBOARD_H
#define YETTY_YCLASSGEN_YPLATFORM_YCLIPBOARD_CLIPBOARD_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base clipboard data slice. Abstract — holds no shared state; each subclass
 * keeps its own pipes (response channel, and on GLFW the marshalling bus). */
struct yetty_yclass_ptr_result yetty_yplatform_clipboard_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_clipboard;
struct yetty_yplatform_clipboard_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_clipboard *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_clipboard_ptr_result yetty_yplatform_clipboard_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yplatform_clipboard_to(struct yetty_yplatform_clipboard *data);

struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object *obj,
                                                                  const char *text, size_t len);
struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_clipboard_drain(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_set_text_fn)(
    struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_request_paste_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_drain_fn)(
    struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yplatform_clipboard_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
