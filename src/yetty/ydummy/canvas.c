/*
 * canvas.c — yclass class `ydummy:canvas`: pilot for the codegen
 * client/server split.
 *
 * The class is pure contract + state: a shader is a piece of TEXT, a rect
 * is four floats, time is a float. There is no GPU code, no GPU type and
 * no GPU include anywhere in this module — rendering the state is the
 * embedding program's business (the standalone ydummy server today, a
 * hosting yetty later), which reads it back through the exposed
 * accessors below.
 *
 *   constructor    lifecycle slot (never remotely callable)
 *   set_shader     wire method, `struct yetty_ycore_buffer` payload
 *   set_rect       wire method, scalar args
 *   set_time       wire method, scalar arg
 *   destroy        wire method, no args beyond obj
 *
 * The user shader contract (assembled and compiled by the renderer, not
 * here): the text defines
 *
 *     fn ydummy_fragment(uv: vec2f, time: f32) -> vec4f
 *
 * `uv` is 0..1 across the rect, `time` is whatever set_time last stored.
 * No shader set (NULL text) means "renderer's built-in default". A
 * zero-area rect means "cover the whole target".
 *
 * Role-split generated layout (the directory is the role boundary):
 *   include/yetty/api/ydummy/canvas.h    the object API (one public header)
 *   src/yetty/gen/api/ydummy/canvas.c    typed API stubs
 *   src/yetty/gen/impl/ydummy/canvas.c  impl glue, #included at the foot
 *
 * This TU deliberately does NOT include its own generated API header —
 * that header is a downstream artifact for consumers. The foundational
 * types this TU and the appended impl glue need are pulled in directly
 * here, and this TU declares its own `yetty_ydummy_canvas_ptr_result`
 * (after the class struct).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

struct YETTY_ANNOTATE("class@ydummy:canvas") yetty_ydummy_canvas {
    /* Owned copy of the user fragment WGSL (NUL-terminated). NULL means
     * "renderer's built-in default". */
    char *fragment_wgsl;
    size_t fragment_wgsl_len;
    /* Bumped on every set_shader — the renderer compares it against the
     * generation it last compiled to know when to rebuild its pipeline. */
    uint32_t shader_generation;

    /* Placement in target pixels. Zero-area → cover the whole target. */
    struct yetty_ycore_rectangle rect;
    float time_seconds;
};

/* Result wrapper for the canvas slice. Declared here (not pulled from a
 * header) so the appended impl glue — which defines
 * yetty_ydummy_canvas_from() returning it — has the type in scope. */
YETTY_YRESULT_DECLARE(yetty_ydummy_canvas_ptr, struct yetty_ydummy_canvas *);

/* Defined in the appended generated impl glue (foot of this TU). */
struct yetty_yclass_ptr_result yetty_ydummy_canvas_class_get(void);

/* Resolve the object's canvas slice, preserving the class_get / object_data
 * error chain (returned as a void-ptr result; callers cast .value). */
static struct yetty_yclass_void_ptr_result canvas_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ydummy_canvas_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "ydummy canvas_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "ydummy canvas_from_obj: object_data");
    return slice_res;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* constructor: creation-time defaults. Every sanctioned creation path must
 * run this exactly once (the creation-contract pilot assertion). */
YETTY_ANNOTATE("virtual@ydummy:canvas:constructor")
static struct yetty_ycore_void_result canvas_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, canvas_res, "ydummy constructor: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;

    /* calloc zero-fill covers the pointers; make the contract explicit. */
    canvas->shader_generation = 0;
    canvas->time_seconds = 0.0f;
    return YETTY_OK_VOID();
}

/* set_shader: replace the user fragment WGSL. The buffer carries the raw
 * text of `fn ydummy_fragment(uv: vec2f, time: f32) -> vec4f`; an empty
 * buffer reverts to the renderer's built-in default. Wire shape: length-
 * prefixed byte payload (`struct yetty_ycore_buffer` by value). */
YETTY_ANNOTATE("virtual@ydummy:canvas:set_shader")
static struct yetty_ycore_void_result canvas_set_shader(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_buffer wgsl)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, canvas_res, "ydummy set_shader: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;

    char *copy = NULL;
    if (wgsl.size > 0 && wgsl.data) {
        copy = malloc(wgsl.size + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "ydummy set_shader: wgsl copy alloc failed");
        }
        memcpy(copy, wgsl.data, wgsl.size);
        copy[wgsl.size] = '\0';
    }
    free(canvas->fragment_wgsl);
    canvas->fragment_wgsl = copy;
    canvas->fragment_wgsl_len = copy ? wgsl.size : 0;
    canvas->shader_generation++;
    return YETTY_OK_VOID();
}

/* set_rect: placement in target pixels. Zero-area → cover the target. */
YETTY_ANNOTATE("virtual@ydummy:canvas:set_rect")
static struct yetty_ycore_void_result canvas_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                      float min_y, float max_x, float max_y)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, canvas_res, "ydummy set_rect: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;

    canvas->rect = (struct yetty_ycore_rectangle){.min = {.x = min_x, .y = min_y},
                                                  .max = {.x = max_x, .y = max_y}};
    return YETTY_OK_VOID();
}

/* set_time: advance the animation clock the fragment sees. */
YETTY_ANNOTATE("virtual@ydummy:canvas:set_time")
static struct yetty_ycore_void_result canvas_set_time(struct yetty_yclass_object *obj,
                                                      float seconds)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, canvas_res, "ydummy set_time: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;

    canvas->time_seconds = seconds;
    return YETTY_OK_VOID();
}

/* destroy: release the owned WGSL copy and the object. */
YETTY_ANNOTATE("virtual@ydummy:canvas:destroy")
static struct yetty_ycore_void_result canvas_destroy(struct yetty_yclass_object *obj)
{
    /* Best-effort teardown: even if the slice can't be resolved we still
     * free the object, stashing the resolve error as the first error. */
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    struct yetty_ydummy_canvas *canvas =
        YETTY_IS_OK(canvas_res) ? (struct yetty_ydummy_canvas *)canvas_res.value : NULL;
    struct yetty_ycore_void_result result =
        YETTY_IS_ERR(canvas_res) ? YETTY_ERR(yetty_ycore_void, "ydummy destroy: object", canvas_res)
                                 : YETTY_OK_VOID();
    if (canvas) {
        free(canvas->fragment_wgsl);
        canvas->fragment_wgsl = NULL;
    }
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(free_res)) {
        return YETTY_ERR(yetty_ycore_void, "ydummy destroy: object_free", free_res);
    }
    if (YETTY_IS_ERR(free_res)) {
        yetty_ycore_error_destroy(free_res.error);
    }
    return result;
}

/*=============================================================================
 * Exposed read accessors — how a renderer (server program / hosting yetty)
 * observes the state the wire methods mutate. Plain object API: no data
 * slice crosses, no GPU; valid only on a real local object (a proxy errs
 * cleanly at object_data).
 *===========================================================================*/

/* Current user fragment WGSL text, or NULL for "use the built-in default". */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_ydummy_canvas_shader_text(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, canvas_res, "ydummy shader_text: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, canvas->fragment_wgsl);
}

/* Byte length of the current user fragment WGSL (0 when default). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_ydummy_canvas_shader_length(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, canvas_res, "ydummy shader_length: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;
    return YETTY_OK(yetty_ycore_size, canvas->fragment_wgsl_len);
}

/* Monotonic shader generation — bumped by every set_shader. A renderer
 * rebuilds its pipeline when this differs from the generation it last
 * compiled. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ydummy_canvas_shader_generation(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, canvas_res, "ydummy shader_generation: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;
    return YETTY_OK(yetty_ycore_uint32, canvas->shader_generation);
}

/* Placement rect in target pixels (zero-area → cover the whole target). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_rectangle_result yetty_ydummy_canvas_rect(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_rectangle, canvas_res, "ydummy rect: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;
    return YETTY_OK(yetty_ycore_rectangle, canvas->rect);
}

/* Animation clock last stored by set_time. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_ydummy_canvas_time(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result canvas_res = canvas_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, canvas_res, "ydummy time: object");
    struct yetty_ydummy_canvas *canvas = (struct yetty_ydummy_canvas *)canvas_res.value;
    return YETTY_OK(yetty_ycore_float, canvas->time_seconds);
}

#include "yetty/gen/impl/ydummy/canvas.c"
