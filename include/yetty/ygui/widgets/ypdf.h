/* ygui-ypdf.h — PDF viewer. */
#ifndef YETTY_YGUI_WIDGETS_YPDF_H
#define YETTY_YGUI_WIDGETS_YPDF_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
struct yetty_yclass_ptr_result yetty_ygui_ypdf_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_ypdf_set_file(struct yetty_ygui_object *obj,
                                                        const char *path);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
