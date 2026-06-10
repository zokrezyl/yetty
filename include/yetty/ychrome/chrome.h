/* GENERATED — do not edit. */
/* Public interface for regular class(es) `chrome` (module: ychrome).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YCHROME_CHROME_H
#define YETTY_YCLASSGEN_YCHROME_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void);

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_yui_event;

struct yetty_ycore_void_result yetty_ychrome_configure(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yclass_object *window_manager,
                                                       float caption_height, float edge_size,
                                                       uint32_t flags);
struct yetty_ycore_void_result yetty_ychrome_set_size(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float height);
struct yetty_ycore_void_result yetty_ychrome_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_edge_cursor_at(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj, float x,
                                                           float y);
struct yetty_ydraw_drawable_list_result yetty_ychrome_render(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_handle_event(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         const struct yetty_yui_event *event);

struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ychrome_register(void);

/* render() returns struct yetty_ydraw_drawable_list_result by value, so the
 * generated chrome.h (and the dispatch TU that includes it) needs the complete
 * type — pull its defining header into the public header. */
#include <yetty/ydraw-core/drawable-list.h>
/* Feature flags for configure(). OR them together; FLAG_ALL enables the lot.
 * Copied verbatim into the generated chrome.h. */
#define YETTY_YCHROME_FLAG_DRAG 0x1u     /* caption drag moves the window      */
#define YETTY_YCHROME_FLAG_RESIZE 0x2u   /* right/bottom edges resize          */
#define YETTY_YCHROME_FLAG_MAXIMIZE 0x4u /* caption double-click toggles max   */
#define YETTY_YCHROME_FLAG_ALL                                                                     \
    (YETTY_YCHROME_FLAG_DRAG | YETTY_YCHROME_FLAG_RESIZE | YETTY_YCHROME_FLAG_MAXIMIZE)
struct yetty_ycore_int_result yetty_ychrome_hover_button(struct yetty_yclass_object *obj);

#endif
