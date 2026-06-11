/* GENERATED — do not edit. */
/* Public interface for regular class(es) `breadcrumbs` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BREADCRUMBS_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BREADCRUMBS_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_breadcrumbs;
YETTY_YRESULT_DECLARE(yetty_ygui_breadcrumbs_ptr, struct yetty_ygui_breadcrumbs *);
struct yetty_ygui_breadcrumbs_ptr_result yetty_ygui_breadcrumbs_from(
    struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_breadcrumbs_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_add(struct yetty_yclass_object *obj,
                                                          const char *text);
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_clear(struct yetty_yclass_object *obj);

#endif
