/* GENERATED — do not edit. */
/* Object API for regular class(es) `canvas` (implementation module: ydummy).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YDUMMY_CANVAS_H
#define YETTY_YCLASSGEN_API_YDUMMY_CANVAS_H

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
struct yetty_ydummy_canvas;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDUMMY_CANVAS_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDUMMY_CANVAS_PTR_RESULT
struct yetty_ydummy_canvas_ptr_result {
    int ok;
    union {
        struct yetty_ydummy_canvas *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydummy_canvas_ptr_result yetty_ydummy_canvas_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydummy_canvas_to(struct yetty_ydummy_canvas *data);

/* set_shader: replace the user fragment WGSL. The buffer carries the raw
 * text of `fn ydummy_fragment(uv: vec2f, time: f32) -> vec4f`; an empty
 * buffer reverts to the renderer's built-in default. Wire shape: length-
 * prefixed byte payload (`struct yetty_ycore_buffer` by value). */
struct yetty_ycore_void_result yetty_ydummy_set_shader(struct yetty_yclass_object *obj,
                                                       struct yetty_ycore_buffer wgsl);
/* set_rect: placement in target pixels. Zero-area → cover the target. */
struct yetty_ycore_void_result yetty_ydummy_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                     float min_y, float max_x, float max_y);
/* set_time: advance the animation clock the fragment sees. */
struct yetty_ycore_void_result yetty_ydummy_set_time(struct yetty_yclass_object *obj,
                                                     float seconds);
/* destroy: release the owned WGSL copy and the object. */
struct yetty_ycore_void_result yetty_ydummy_destroy(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ydummy_canvas_create(struct yetty_yclass_ctx *ctx);

/* Current user fragment WGSL text, or NULL for "use the built-in default". */
struct yetty_ycore_const_char_ptr_result yetty_ydummy_canvas_shader_text(
    struct yetty_yclass_object *obj);
/* Byte length of the current user fragment WGSL (0 when default). */
struct yetty_ycore_size_result yetty_ydummy_canvas_shader_length(struct yetty_yclass_object *obj);
/* Monotonic shader generation — bumped by every set_shader. A renderer
 * rebuilds its pipeline when this differs from the generation it last
 * compiled. */
struct yetty_ycore_uint32_result yetty_ydummy_canvas_shader_generation(
    struct yetty_yclass_object *obj);
/* Placement rect in target pixels (zero-area → cover the whole target). */
struct yetty_ycore_rectangle_result yetty_ydummy_canvas_rect(struct yetty_yclass_object *obj);
/* Animation clock last stored by set_time. */
struct yetty_ycore_float_result yetty_ydummy_canvas_time(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
