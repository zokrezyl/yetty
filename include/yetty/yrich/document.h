/* GENERATED — do not edit. */
/* Public interface for regular class(es) `document` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YRICH_DOCUMENT_H
#define YETTY_YCLASSGEN_YRICH_DOCUMENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-operation.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_yrich_command;
struct yetty_yrich_operation;
struct yetty_yrich_selection;

struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_document;
struct yetty_yrich_document_ptr_result {
    int ok;
    union {
        struct yetty_yrich_document *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yrich_document_ptr_result yetty_yrich_document_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_document_to(struct yetty_yrich_document *data);

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object *obj,
                                                                float x, float y, uint32_t button,
                                                                uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_object *obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text);

typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_undo_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_redo_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_up_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);

struct yetty_yclass_object_ptr_result yetty_yrich_document_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

struct yetty_ycore_void_result yetty_yrich_super_void(struct yetty_yclass_object *obj,
                                                      const struct yetty_yclass *self_class,
                                                      yetty_yclass_method_id_t method_id);
struct yetty_ycore_void_result yetty_yrich_document_mark_dirty(struct yetty_yclass_object *obj);
int yetty_yrich_document_is_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_clear_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_set_buffer(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list);
struct yetty_ydraw_drawable_list *yetty_yrich_document_buffer(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_set_bg_color(struct yetty_yclass_object *obj,
                                                                 uint32_t color);
uint32_t yetty_yrich_document_bg_color(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_set_dirty_cb(struct yetty_yclass_object *obj,
                                                                 yetty_yrich_dirty_cb callback,
                                                                 void *userdata);
struct yetty_ycore_void_result yetty_yrich_document_set_sync_cb(struct yetty_yclass_object *obj,
                                                                yetty_yrich_sync_cb callback,
                                                                void *userdata);
struct yetty_yrich_element_id_result yetty_yrich_document_next_id(struct yetty_yclass_object *obj);
struct yetty_yrich_operation_ptr_result yetty_yrich_document_create_op(
    struct yetty_yclass_object *obj, uint32_t op_type);
struct yetty_ycore_void_result yetty_yrich_document_add_element(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *element_obj);
struct yetty_ycore_void_result yetty_yrich_document_remove_element(struct yetty_yclass_object *obj,
                                                                   yetty_yrich_element_id id);
struct yetty_yclass_object *yetty_yrich_document_find(struct yetty_yclass_object *obj,
                                                      yetty_yrich_element_id id);
struct yetty_yclass_object *yetty_yrich_document_element_at(struct yetty_yclass_object *obj,
                                                            float x, float y);
struct yetty_yrich_selection *yetty_yrich_document_selection(struct yetty_yclass_object *obj);
int yetty_yrich_document_is_selected(struct yetty_yclass_object *obj, yetty_yrich_element_id id);
struct yetty_ycore_void_result yetty_yrich_document_clear_selection(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_clear_history(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_execute(struct yetty_yclass_object *obj,
                                                            struct yetty_yrich_command *cmd);

#ifdef __cplusplus
}
#endif

#endif
