/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tabbar` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_tabbar_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_tabbar;
YETTY_YRESULT_DECLARE(yetty_ygui_tabbar_ptr, struct yetty_ygui_tabbar *);
struct yetty_ygui_tabbar_ptr_result yetty_ygui_tabbar_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_tabbar_to(struct yetty_ygui_tabbar *data);

struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_yclass_object;
typedef void (*yetty_ygui_tab_close_cb)(struct yetty_yclass_object *tabbar, int index,
                                        void *userdata);
typedef void (*yetty_ygui_tab_new_cb)(struct yetty_yclass_object *tabbar, void *userdata);
struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_yclass_object *tabbar,
                                                              const char *label);
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_yclass_object *tabbar,
                                                            int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_label(struct yetty_yclass_object *tabbar,
                                                           int index, const char *label);
int yetty_ygui_tabbar_count(const struct yetty_yclass_object *tabbar);
struct yetty_ycore_int_result yetty_ygui_tabbar_active(const struct yetty_yclass_object *tabbar);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_yclass_object *tabbar,
                                                            int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_yclass_object *tabbar,
                                                              yetty_ygui_tab_close_cb cb,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_yclass_object *tabbar,
                                                                yetty_ygui_tab_new_cb cb,
                                                                void *userdata);

#endif
