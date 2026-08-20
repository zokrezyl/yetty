/* GENERATED — do not edit. */
/* Public interface for regular class(es) `mesh` (module: ycomplex2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YCOMPLEX2_MESH_H
#define YETTY_YCLASSGEN_YCOMPLEX2_MESH_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ycomplex2_mesh_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ycomplex2_mesh;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_MESH_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_MESH_PTR_RESULT
struct yetty_ycomplex2_mesh_ptr_result {
    int ok;
    union {
        struct yetty_ycomplex2_mesh *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ycomplex2_mesh_ptr_result yetty_ycomplex2_mesh_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_to(struct yetty_ycomplex2_mesh *data);
struct float_result yetty_ycomplex2_mesh_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_x_set(struct yetty_yclass_object *obj,
                                                          float value);
struct float_result yetty_ycomplex2_mesh_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_y_set(struct yetty_yclass_object *obj,
                                                          float value);
struct float_result yetty_ycomplex2_mesh_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_width_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ycomplex2_mesh_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_height_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ycomplex2_mesh_azimuth_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_azimuth_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ycomplex2_mesh_elevation_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_elevation_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ycomplex2_mesh_zoom_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_zoom_set(struct yetty_yclass_object *obj,
                                                             float value);
struct uint32_result yetty_ycomplex2_mesh_wireframe_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_wireframe_set(struct yetty_yclass_object *obj,
                                                                  uint32_t value);

/* set_glb: the glTF 2.0 binary file. */
struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *obj,
                                                       const char *path);

typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_glb_fn)(struct yetty_yclass_object *,
                                                                     const char *);

struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ycomplex2_register(void);

#ifdef __cplusplus
}
#endif

#endif
