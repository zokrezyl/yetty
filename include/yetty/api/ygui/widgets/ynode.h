/* GENERATED — do not edit. */
/* Object API for regular class(es) `ynode` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_YNODE_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_YNODE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_ynode;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YNODE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YNODE_PTR_RESULT
struct yetty_ygui_ynode_ptr_result {
    int ok;
    union {
        struct yetty_ygui_ynode *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_ynode_ptr_result yetty_ygui_ynode_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_ynode_to(struct yetty_ygui_ynode *data);

struct yetty_yclass_object_ptr_result yetty_ygui_ynode_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_ynode_reflow(struct yetty_yclass_object *node);
struct yetty_ycore_int_result yetty_ygui_ynode_pin_pos(const struct yetty_yclass_object *node,
                                                       int output, int index, float *x, float *y);
struct yetty_ycore_int_result yetty_ygui_ynode_pin_at(const struct yetty_yclass_object *node,
                                                      float x, float y, int *output, int *index);
/*-----------------------------------------------------------------------------
 * App-facing API.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_ynode_set_title(struct yetty_yclass_object *node,
                                                          const char *title);
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_pos(struct yetty_yclass_object *node,
                                                              float gx, float gy);
struct yetty_ycore_void_result yetty_ygui_ynode_graph_pos(const struct yetty_yclass_object *node,
                                                          float *gx, float *gy);
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_size(struct yetty_yclass_object *node,
                                                               float gw, float gh);
struct yetty_ycore_void_result yetty_ygui_ynode_graph_size(const struct yetty_yclass_object *node,
                                                           float *gw, float *gh);
struct uint32_result yetty_ygui_ynode_add_input(struct yetty_yclass_object *node, const char *name);
struct uint32_result yetty_ygui_ynode_add_output(struct yetty_yclass_object *node,
                                                 const char *name);
struct yetty_ycore_int_result yetty_ygui_ynode_input_count(const struct yetty_yclass_object *node);
struct yetty_ycore_int_result yetty_ygui_ynode_output_count(const struct yetty_yclass_object *node);

#ifdef __cplusplus
}
#endif

#endif
