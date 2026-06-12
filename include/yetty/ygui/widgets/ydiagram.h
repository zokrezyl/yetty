/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydiagram` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_ydiagram;
YETTY_YRESULT_DECLARE(yetty_ygui_ydiagram_ptr, struct yetty_ygui_ydiagram *);
struct yetty_ygui_ydiagram_ptr_result yetty_ygui_ydiagram_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_ydiagram_to(struct yetty_ygui_ydiagram *data);

struct yetty_yclass_object_ptr_result yetty_ygui_ydiagram_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_ydiagram_set_source(struct yetty_yclass_object *obj, const char *source);
struct yetty_ycore_const_char_ptr_result yetty_ygui_ydiagram_get_source(const struct yetty_yclass_object *obj);

#endif
