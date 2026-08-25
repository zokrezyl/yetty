/* GENERATED — do not edit. */
/* Object API for regular class(es) `video` (implementation module: ycomplex2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YCOMPLEX2_VIDEO_H
#define YETTY_YCLASSGEN_API_YCOMPLEX2_VIDEO_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ycomplex2_video_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ycomplex2_video;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_VIDEO_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_VIDEO_PTR_RESULT
struct yetty_ycomplex2_video_ptr_result {
    int ok;
    union {
        struct yetty_ycomplex2_video *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ycomplex2_video_ptr_result yetty_ycomplex2_video_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_video_to(struct yetty_ycomplex2_video *data);
struct float_result yetty_ycomplex2_video_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_x_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ycomplex2_video_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_y_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ycomplex2_video_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_width_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ycomplex2_video_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_height_set(struct yetty_yclass_object *obj,
                                                                float value);
struct uint32_result yetty_ycomplex2_video_id_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_id_set(struct yetty_yclass_object *obj,
                                                            uint32_t value);
struct uint32_result yetty_ycomplex2_video_video_w_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_video_w_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value);
struct uint32_result yetty_ycomplex2_video_video_h_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_video_h_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value);
struct float_result yetty_ycomplex2_video_fps_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ycomplex2_video_fps_set(struct yetty_yclass_object *obj,
                                                             float value);

/* set_h264: the raw Annex-B H.264 byte stream file (.h264/.264). */
struct yetty_ycore_void_result yetty_ycomplex2_set_h264(struct yetty_yclass_object *obj,
                                                        const char *path);

struct yetty_yclass_object_ptr_result yetty_ycomplex2_video_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
