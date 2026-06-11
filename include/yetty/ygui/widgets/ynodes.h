/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ynodes` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_ynodes_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_ynodes;
YETTY_YRESULT_DECLARE(yetty_ygui_ynodes_ptr, struct yetty_ygui_ynodes *);
struct yetty_ygui_ynodes_ptr_result yetty_ygui_ynodes_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_ynodes_to(struct yetty_ygui_ynodes *data);

struct yetty_yclass_object_ptr_result yetty_ygui_ynodes_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

#include <yetty/ycore/result.h>
#include <yetty/ygui/widget.h>
typedef struct yetty_ycore_void_result (*yetty_ygui_ynodes_link_cb)(
    struct yetty_yclass_object *editor, struct yetty_yclass_object *from, int out_idx,
    struct yetty_yclass_object *to, int in_idx, void *userdata);
void yetty_ygui_ynodes_view(const struct yetty_yclass_object *editor, float *pan_x, float *pan_y, float *zoom);
float yetty_ygui_ynodes_zoom(const struct yetty_yclass_object *editor);
struct yetty_ycore_void_result yetty_ygui_ynodes_reflow(struct yetty_yclass_object *editor);
struct yetty_ycore_void_result yetty_ygui_ynodes_set_view(struct yetty_yclass_object *editor, float pan_x, float pan_y, float zoom);
struct yetty_ycore_void_result yetty_ygui_ynodes_link(struct yetty_yclass_object *editor, struct yetty_yclass_object *from, int out_idx, struct yetty_yclass_object *to, int in_idx);
struct yetty_ycore_void_result yetty_ygui_ynodes_drop_links_for(struct yetty_yclass_object *editor, struct yetty_yclass_object *node);
struct yetty_ycore_void_result yetty_ygui_ynodes_on_link_set(struct yetty_yclass_object *editor, yetty_ygui_ynodes_link_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_ynodes_begin_link(struct yetty_yclass_object *editor, struct yetty_yclass_object *from, int pin, int output, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_update_link(struct yetty_yclass_object *editor, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_end_link(struct yetty_yclass_object *editor, float x, float y);
struct yetty_yclass_object_ptr_result yetty_ygui_ynodes_add_node(struct yetty_yclass_object *editor, float gx, float gy);
struct yetty_ycore_void_result yetty_ygui_ynodes_register_widget(struct yetty_yclass_object *editor, const char *label, const struct yetty_yclass *cls);
struct yetty_ycore_void_result yetty_ygui_ynodes_open_canvas_menu(struct yetty_yclass_object *editor, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_open_node_menu(struct yetty_yclass_object *editor, struct yetty_yclass_object *node, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_close_menu(struct yetty_yclass_object *editor);
struct yetty_ycore_int_result yetty_ygui_ynodes_menu_is_open(const struct yetty_yclass_object *editor);

#endif
