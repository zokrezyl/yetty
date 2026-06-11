/* GENERATED — do not edit. */
/* Public interface for regular class(es) `choicebox` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_CHOICEBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_CHOICEBOX_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_choicebox_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_choicebox;
YETTY_YRESULT_DECLARE(yetty_ygui_choicebox_ptr, struct yetty_ygui_choicebox *);
struct yetty_ygui_choicebox_ptr_result yetty_ygui_choicebox_from(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_choicebox_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_choicebox_add(struct yetty_yclass_object *obj,
                                                        const char *label);
struct yetty_ycore_int_result yetty_ygui_choicebox_is_selected(
    const struct yetty_yclass_object *obj, int idx);

#endif
