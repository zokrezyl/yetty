/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ynodes` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ynodes_class_get(void);

struct yetty_ygui_object;
struct nodes_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ynodes_data_ptr, struct nodes_data *);
struct yetty_ygui_ynodes_data_ptr_result yetty_ygui_ynodes_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

#include <yetty/ycore/result.h>
#include <yetty/ygui/object.h>
typedef struct yetty_ycore_void_result (*yetty_ygui_ynodes_link_cb)(
    struct yetty_ygui_object *editor, struct yetty_ygui_object *from, int out_idx,
    struct yetty_ygui_object *to, int in_idx, void *userdata);
void yetty_ygui_ynodes_view(const struct yetty_ygui_object *editor, float *pan_x, float *pan_y, float *zoom);
float yetty_ygui_ynodes_zoom(const struct yetty_ygui_object *editor);
struct yetty_ycore_void_result yetty_ygui_ynodes_reflow(struct yetty_ygui_object *editor);
struct yetty_ycore_void_result yetty_ygui_ynodes_set_view(struct yetty_ygui_object *editor, float pan_x, float pan_y, float zoom);
struct yetty_ycore_void_result yetty_ygui_ynodes_link(struct yetty_ygui_object *editor, struct yetty_ygui_object *from, int out_idx, struct yetty_ygui_object *to, int in_idx);
struct yetty_ycore_void_result yetty_ygui_ynodes_drop_links_for(struct yetty_ygui_object *editor, struct yetty_ygui_object *node);
struct yetty_ycore_void_result yetty_ygui_ynodes_on_link_set(struct yetty_ygui_object *editor, yetty_ygui_ynodes_link_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_ynodes_begin_link(struct yetty_ygui_object *editor, struct yetty_ygui_object *from, int pin, int output, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_update_link(struct yetty_ygui_object *editor, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_end_link(struct yetty_ygui_object *editor, float x, float y);
struct yetty_ygui_object_ptr_result yetty_ygui_ynodes_add_node(struct yetty_ygui_object *editor, float gx, float gy);
struct yetty_ycore_void_result yetty_ygui_ynodes_register_widget(struct yetty_ygui_object *editor, const char *label, const struct yetty_yclass *cls);
struct yetty_ycore_void_result yetty_ygui_ynodes_open_canvas_menu(struct yetty_ygui_object *editor, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_open_node_menu(struct yetty_ygui_object *editor, struct yetty_ygui_object *node, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_close_menu(struct yetty_ygui_object *editor);
int yetty_ygui_ynodes_menu_is_open(const struct yetty_ygui_object *editor);

#endif
