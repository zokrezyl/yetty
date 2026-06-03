/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yimage` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yimage_class_get(void);

struct yetty_ygui_object;
struct yimage_data;
YETTY_YRESULT_DECLARE(yetty_ygui_yimage_data_ptr, struct yimage_data *);
struct yetty_ygui_yimage_data_ptr_result yetty_ygui_yimage_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj, const uint8_t *bytes, size_t len);
const uint8_t *yetty_ygui_yimage_bytes(const struct yetty_ygui_object *obj);
size_t yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj);

#endif
