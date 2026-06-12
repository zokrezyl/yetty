/* GENERATED — do not edit. */
/* Public interface for regular class(es) `editor` (module: yai).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YAI_EDITOR_H
#define YETTY_YCLASSGEN_YAI_EDITOR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yai_editor_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yai_editor;
YETTY_YRESULT_DECLARE(yetty_yai_editor_ptr, struct yetty_yai_editor *);
struct yetty_yai_editor_ptr_result yetty_yai_editor_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yai_editor_to(struct yetty_yai_editor *data);

struct yai_app;
struct yetty_ycore_int_result;

struct yetty_ycore_int_result yetty_yai_feed_byte(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  struct yai_app *app, int byte);

typedef struct yetty_ycore_int_result (*yetty_yai_feed_byte_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                struct yai_app *, int);

struct yetty_yclass_object_ptr_result yetty_yai_editor_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yai_register(void);

#endif
