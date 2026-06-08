/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ynode` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YNODE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YNODE_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ynode_class_get(void);

struct yetty_ygui_object;
struct node_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ynode_data_ptr, struct node_data *);
struct yetty_ygui_ynode_data_ptr_result yetty_ygui_ynode_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ynode_reflow(struct yetty_ygui_object *node);
int yetty_ygui_ynode_pin_pos(const struct yetty_ygui_object *node, int output, int index, float *x,
                             float *y);
int yetty_ygui_ynode_pin_at(const struct yetty_ygui_object *node, float x, float y, int *output,
                            int *index);
struct yetty_ycore_void_result yetty_ygui_ynode_set_title(struct yetty_ygui_object *node,
                                                          const char *title);
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_pos(struct yetty_ygui_object *node,
                                                              float gx, float gy);
void yetty_ygui_ynode_graph_pos(const struct yetty_ygui_object *node, float *gx, float *gy);
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_size(struct yetty_ygui_object *node,
                                                               float gw, float gh);
void yetty_ygui_ynode_graph_size(const struct yetty_ygui_object *node, float *gw, float *gh);
struct uint32_result yetty_ygui_ynode_add_input(struct yetty_ygui_object *node, const char *name);
struct uint32_result yetty_ygui_ynode_add_output(struct yetty_ygui_object *node, const char *name);
int yetty_ygui_ynode_input_count(const struct yetty_ygui_object *node);
int yetty_ygui_ynode_output_count(const struct yetty_ygui_object *node);

#endif
