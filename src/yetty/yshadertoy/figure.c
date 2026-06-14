/*
 * figure.c — yshadertoy rendered as a yfigure_figure subclass.
 *
 * A standalone Shadertoy-style shader figure: one full-rect quad whose
 * fragment shader runs a user-supplied WGSL `mainImage(...)` with the
 * usual Shadertoy inputs (iResolution, iTime, iTimeDelta, iFrame,
 * iMouse) injected as uniforms. Unlike yterm's shader-glyph figure —
 * which scans the terminal's cell buffer and instances one quad per
 * PUA cell — this figure is decoupled from the text grid entirely: it
 * paints the whole figure rect and takes its shader text as an
 * argument.
 *
 * Two entry points share one implementation:
 *   - yetty_yshadertoy_figure_create(): direct in-process creation,
 *     mirroring shader-glyph's create(). The caller hands the shader
 *     text up front.
 *   - the registry factory (YETTY_YFIGURE_KIND_YSHADERTOY): minted by a
 *     container from a CREATE_CHILD wire record. The shader text arrives
 *     later as the record's init payload via process_bytes — this is the
 *     path the ygui yshadertoy widget drives.
 *
 * GPU plumbing matches shader-glyph: a self-owned gpu_resource_binder
 * compiled lazily on first render (so the figure can be created before
 * the GPU is fully wired). The binder prepends the `struct Uniforms`
 * block + bindings, so the assembled WGSL references the injected
 * fields as `uniforms.yshadertoy_<name>`.
 *
 * Animation timer: owned here, registered on the borrowed event loop,
 * ticking at the configured FPS while a shader is set. Each tick marks
 * the figure dirty and calls request_render so the next frame advances
 * iTime. Stopped on destroy.
 */
#include <yetty/yplatform/compat.h> /* clock_gettime shim on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <webgpu/webgpu.h>

#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>

/* This TU deliberately does NOT include its own generated header
 * `yetty/yshadertoy/figure.h` — that header is a downstream artifact for
 * other modules. The foundational types this TU and the appended
 * figure.gen.c need (yclass identity, Result, rectangle) are pulled in
 * directly above; the figure base type comes from the parent header
 * figure.h. This TU declares its own yetty_yshadertoy_figure_ptr_result
 * below (the same one the generated figure.h publishes for consumers). */

/* Uniform slots — order is arbitrary; the binder emits one struct field
 * per slot named `yshadertoy_<name>`. */
#define U_RESOLUTION 0 /* vec3: (width, height, 1.0)            */
#define U_TIME 1       /* f32:  seconds since first render      */
#define U_TIME_DELTA 2 /* f32:  seconds since previous frame    */
#define U_FRAME 3      /* i32:  frame counter                   */
#define U_MOUSE 4      /* vec4: (x, y, click_x, click_y) pixels */
#define U_COUNT 5

/* ===========================================================================
 * Figure struct
 * ========================================================================= */

struct [[clang::annotate("class@yshadertoy:figure")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yshadertoy_figure {
    /* Borrowed. Held to mint the binder on first render (GPU handles),
     * to reach the event loop (anim timer + repaint nudge), and config. */
    const struct yetty_context *context;

    /* Final assembled WGSL: fixed template (quad VS + fs_main) with the
     * user's mainImage spliced in. Owned here. */
    char *shader_source;
    size_t shader_source_size;

    /* GPU plumbing. The binder owns pipeline + bind groups + buffers;
     * compiled lazily on first render. Rebuilt when the shader text
     * changes (binder torn down so finalize recompiles). */
    struct yetty_yrender_gpu_resource_set rs;
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    /* CPU-side animation clock. iTime = now - t0; iTimeDelta = now - t_last. */
    struct timespec t0;
    struct timespec t_last;
    uint32_t frame;
    int clock_started;

    /* Animation timer. Borrowed event loop; timer_id valid only when
     * timer_created. */
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    int timer_created;
    int timer_running;
    struct yetty_yevent_event_listener listener;
};

/* Result wrapper for this kind's handle. Declared here (not pulled from
 * figure.h, which this TU does not include) so the appended figure.gen.c —
 * which defines yetty_yshadertoy_figure_from() returning it — has the type
 * in scope. The public figure.h publishes the identical declaration for
 * other modules. */
YETTY_YRESULT_DECLARE(yetty_yshadertoy_figure_ptr, struct yetty_yshadertoy_figure *);

/* Defined in the appended figure.gen.c (foot of this TU). Forward-declared
 * here because this TU does not include its own generated header — the
 * class accessor and the obj→body downcast are used throughout. */
struct yetty_yclass_ptr_result yetty_yshadertoy_figure_class_get(void);
struct yetty_yshadertoy_figure_ptr_result yetty_yshadertoy_figure_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yshadertoy_figure_to(struct yetty_yshadertoy_figure *data);

/* This kind's own data slice (its fields sit after the figure
 * base slice in the shared yclass object). */
static struct yetty_yclass_void_ptr_result yshadertoy_figure_from_obj(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yshadertoy_figure_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "yshadertoy_figure_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/* Recover the owning yclass object header from a yshadertoy body slice.
 * The body has no stored back-pointer (none is kept): the slice sits at a
 * fixed offset after the object header (parent slices first), so the offset
 * yetty_yclass_object_data_offset reports for this class — relative to the
 * first data byte (obj + 1) — lets us step back to the header. Used only by
 * the anim-tick listener, which is handed the body via container_of and must
 * reach the figure base to mark it dirty. */
static struct yetty_yclass_object_ptr_result yshadertoy_obj_from_body(
    struct yetty_yshadertoy_figure *f)
{
    /* Recover the owning object from this body slice via the generated
     * inverse accessor. data-slice offsets are relative to the object
     * header, so `_to` is a single `(char *)data - offset` — no extra
     * header step (the previous hand-rolled `- 1` double-counted the
     * header and returned obj - sizeof(yclass_object), a bogus pointer). */
    struct yetty_yclass_object *obj = yetty_yshadertoy_figure_to(f);
    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yshadertoy_obj_from_body: figure_to failed");
    }
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* ===========================================================================
 * WGSL assembly
 *
 * The binder prepends `struct Uniforms { yshadertoy_iResolution: vec3<f32>,
 * ... }` + `@group(0) @binding(0) var<uniform> uniforms: Uniforms;` to
 * whatever we put in rs.shader (see yrender/pipeline.c). So the template
 * here only carries the fullscreen-quad vertex shader, the user's
 * mainImage, and an fs_main that reads the injected uniforms and calls it.
 *
 * Contract for the user shader text:
 *   fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
 *                iTime: f32, iMouse: vec4<f32>) -> vec4<f32> { ... }
 *
 * fragCoord is in pixels with origin at the bottom-left of the figure
 * rect (Shadertoy convention). iTimeDelta / iFrame are also available as
 * uniforms.yshadertoy_iTimeDelta / _iFrame for shaders that want them.
 * ========================================================================= */

static const char YSHADERTOY_VS[] =
    "struct VertexInput { @location(0) position: vec2<f32>, };\n"
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    @location(0) @interpolate(linear) uv: vec2<f32>,\n"
    "};\n"
    "@vertex\n"
    "fn vs_main(input: VertexInput) -> VertexOutput {\n"
    "    var out: VertexOutput;\n"
    "    out.position = vec4<f32>(input.position, 0.0, 1.0);\n"
    "    // Quad VB is (-1,-1)..(1,1); map to [0,1], y up (Shadertoy origin).\n"
    "    out.uv = input.position * 0.5 + 0.5;\n"
    "    return out;\n"
    "}\n";

static const char YSHADERTOY_FS[] = "@fragment\n"
                                    "fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {\n"
                                    "    let iResolution = uniforms.yshadertoy_iResolution;\n"
                                    "    let iTime = uniforms.yshadertoy_iTime;\n"
                                    "    let iMouse = uniforms.yshadertoy_iMouse;\n"
                                    "    let fragCoord = input.uv * iResolution.xy;\n"
                                    "    return mainImage(fragCoord, iResolution, iTime, iMouse);\n"
                                    "}\n";

/* Shown until a real shader is set (factory path mints before the wire
 * init payload arrives). A gentle animated gradient — never blank. */
static const char YSHADERTOY_DEFAULT_USER[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let uv = fragCoord / iResolution.xy;\n"
    "    let col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3<f32>(0.0, 2.0, 4.0));\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Build the full WGSL = VS + user shader + FS. Returns malloc'd buffer;
 * *out_size set to the byte length (excluding the NUL). */
static char *assemble_shader(const char *user_src, size_t user_len, size_t *out_size)
{
    if (!user_src || user_len == 0) {
        user_src = YSHADERTOY_DEFAULT_USER;
        user_len = sizeof(YSHADERTOY_DEFAULT_USER) - 1;
    }
    size_t vs_len = sizeof(YSHADERTOY_VS) - 1;
    size_t fs_len = sizeof(YSHADERTOY_FS) - 1;
    size_t total = vs_len + 1 + user_len + 1 + fs_len;
    char *out = malloc(total + 1);
    if (!out) {
        return NULL;
    }
    size_t off = 0;
    memcpy(out + off, YSHADERTOY_VS, vs_len);
    off += vs_len;
    out[off++] = '\n';
    memcpy(out + off, user_src, user_len);
    off += user_len;
    out[off++] = '\n';
    memcpy(out + off, YSHADERTOY_FS, fs_len);
    off += fs_len;
    out[off] = '\0';
    *out_size = off;
    return out;
}

/* ===========================================================================
 * Uniform helpers
 * ========================================================================= */

static void init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;
    rs->uniforms[U_RESOLUTION] =
        (struct yetty_yrender_uniform){"iResolution", YETTY_YRENDER_UNIFORM_VEC3};
    rs->uniforms[U_TIME] = (struct yetty_yrender_uniform){"iTime", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_TIME_DELTA] =
        (struct yetty_yrender_uniform){"iTimeDelta", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_FRAME] = (struct yetty_yrender_uniform){"iFrame", YETTY_YRENDER_UNIFORM_I32};
    rs->uniforms[U_MOUSE] = (struct yetty_yrender_uniform){"iMouse", YETTY_YRENDER_UNIFORM_VEC4};
}

static void set_resolution(struct yetty_yrender_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_RESOLUTION].vec3[0] = w;
    rs->uniforms[U_RESOLUTION].vec3[1] = h;
    rs->uniforms[U_RESOLUTION].vec3[2] = 1.0f;
}

/* ===========================================================================
 * Animation timer
 * ========================================================================= */

static inline struct yetty_yshadertoy_figure *figure_from_listener(
    struct yetty_yevent_event_listener *l)
{
    return (struct yetty_yshadertoy_figure *)((char *)l -
                                              offsetof(struct yetty_yshadertoy_figure, listener));
}

/* Nudge the host into a fresh render frame via the figure's own context
 * (no per-host callback needed — the event loop is reachable from the
 * mint-time context). Matches what terminal_request_render_callback does. */
static void figure_request_render(struct yetty_yshadertoy_figure *f)
{
    struct yetty_yevent_event_loop *el = f->context ? f->context->event_loop : NULL;
    if (el && el->ops && el->ops->request_render) {
        el->ops->request_render(el);
    }
}

static void anim_timer_stop(struct yetty_yshadertoy_figure *f)
{
    if (!f->timer_created || !f->timer_running) {
        return;
    }
    f->event_loop->ops->stop_timer(f->event_loop, f->timer_id);
    f->timer_running = 0;
}

static void anim_timer_start(struct yetty_yshadertoy_figure *f)
{
    if (!f->timer_created || f->timer_running) {
        return;
    }
    f->event_loop->ops->start_timer(f->event_loop, f->timer_id);
    f->timer_running = 1;
}

/* Anim tick — marks the figure dirty BEFORE asking for a render so the
 * next frame actually advances iTime (render bails on !dirty). */
static struct yetty_ycore_int_result on_anim_tick(struct yetty_yevent_event_listener *listener,
                                                  const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yshadertoy_figure *f = figure_from_listener(listener);
    struct yetty_yclass_object_ptr_result obj_r = yshadertoy_obj_from_body(f);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, obj_r, "on_anim_tick: obj_from_body");
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj_r.value, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    figure_request_render(f);
    return YETTY_OK(yetty_ycore_int, 0);
}

static void anim_timer_init(struct yetty_yshadertoy_figure *f)
{
    f->event_loop = f->context ? f->context->event_loop : NULL;
    f->listener.handler = on_anim_tick;
    if (!f->event_loop || !f->event_loop->ops || !f->event_loop->ops->create_timer) {
        return;
    }
    int target_fps = 60;
    if (f->context->runtime && f->context->runtime->config) {
        target_fps = f->context->runtime->config->ops->get_int(
            f->context->runtime->config, "terminal/shader-glyph-layer/target-fps", 60);
    }
    if (target_fps < 1) {
        target_fps = 1;
    }
    if (target_fps > 1000) {
        target_fps = 1000;
    }
    int period_ms = 1000 / target_fps;
    if (period_ms < 1) {
        period_ms = 1;
    }
    struct yetty_yevent_timer_id_result tres = f->event_loop->ops->create_timer(f->event_loop);
    if (YETTY_IS_OK(tres)) {
        f->timer_id = tres.value;
        f->event_loop->ops->config_timer(f->event_loop, f->timer_id, period_ms);
        f->event_loop->ops->register_timer_listener(f->event_loop, f->timer_id, &f->listener);
        f->timer_created = 1;
        yinfo("yshadertoy figure: anim timer fps=%d period=%dms", target_fps, period_ms);
    } else {
        ywarn("yshadertoy figure: create_timer failed; shader will be static");
    }
}

/* ===========================================================================
 * Shader source apply — (re)build the assembled WGSL and force a binder
 * rebuild so the new source is compiled on the next render.
 * ========================================================================= */

static struct yetty_ycore_void_result figure_apply_shader(struct yetty_yclass_object *obj,
                                                          const char *user_src, size_t user_len)
{
    struct yetty_yclass_void_ptr_result figure_r = yshadertoy_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "figure_apply_shader: from_obj");
    struct yetty_yshadertoy_figure *f = figure_r.value;
    size_t size = 0;
    char *assembled = assemble_shader(user_src, user_len, &size);
    if (!assembled) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy: assemble shader oom");
    }
    free(f->shader_source);
    f->shader_source = assembled;
    f->shader_source_size = size;
    yetty_yrender_shader_code_set(&f->rs.shader, f->shader_source, f->shader_source_size);

    /* Tear down the binder so it recompiles the new source on next render. */
    if (f->binder) {
        f->binder->ops->destroy(f->binder);
        f->binder = NULL;
    }
    f->binder_finalized = 0;
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Figure ops — render
 * ========================================================================= */

static struct yetty_ycore_void_result figure_ensure_binder(struct yetty_yshadertoy_figure *f)
{
    if (f->binder) {
        return YETTY_OK_VOID();
    }
    struct yetty_yframework_gpu_context *gpu = &f->context->runtime->gpu;
    struct yetty_yrender_gpu_resource_binder_result br = yetty_yrender_gpu_resource_binder_create(
        gpu->device, gpu->queue, gpu->surface_format, gpu->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "yshadertoy figure: binder create failed");
    f->binder = br.value;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result figure_render(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target)
{
    struct yetty_yclass_void_ptr_result figure_r = yshadertoy_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "yshadertoy figure_render: from_obj");
    struct yetty_yshadertoy_figure *f = figure_r.value;

    float x = yetty_yfigure_figure_rect_get(obj).value.min.x;
    float y = yetty_yfigure_figure_rect_get(obj).value.min.y;
    float w = yetty_yfigure_figure_rect_get(obj).value.max.x -
              yetty_yfigure_figure_rect_get(obj).value.min.x;
    float h = yetty_yfigure_figure_rect_get(obj).value.max.y -
              yetty_yfigure_figure_rect_get(obj).value.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* The figure rect is in logical pixels (ygui chrome lays widgets out in
     * logical space). Render at physical resolution for HiDPI: scaling the
     * rect here makes iResolution, pixel_size, the viewport and the scissor
     * below all physical. content_scale is 1.0 outside a HiDPI host. */
    {
        float content_scale = (f->context && f->context->runtime &&
                               f->context->runtime->gpu.app_gpu_context.content_scale > 0.0f)
                                  ? f->context->runtime->gpu.app_gpu_context.content_scale
                                  : 1.0f;
        x *= content_scale;
        y *= content_scale;
        w *= content_scale;
        h *= content_scale;
    }

    /* A live shadertoy is assumed animated — keep ticking while visible. */
    anim_timer_start(f);

    struct yetty_ycore_void_result ebr = figure_ensure_binder(f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ebr, "yshadertoy figure: ensure_binder");

    /* Advance the animation clock. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!f->clock_started) {
        f->t0 = now;
        f->t_last = now;
        f->clock_started = 1;
    }
    float t = (float)(now.tv_sec - f->t0.tv_sec) + (float)(now.tv_nsec - f->t0.tv_nsec) * 1e-9f;
    float dt =
        (float)(now.tv_sec - f->t_last.tv_sec) + (float)(now.tv_nsec - f->t_last.tv_nsec) * 1e-9f;
    f->t_last = now;

    set_resolution(&f->rs, w, h);
    f->rs.uniforms[U_TIME].f32 = t;
    f->rs.uniforms[U_TIME_DELTA].f32 = dt;
    f->rs.uniforms[U_FRAME].i32 = (int32_t)f->frame++;
    f->rs.pixel_size.width = w;
    f->rs.pixel_size.height = h;

    /* Push to the binder: submit collects layout, finalize compiles the
     * shader on first call, update queues the uniform writes. */
    struct yetty_ycore_void_result sr = f->binder->ops->submit(f->binder, &f->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yshadertoy figure: binder submit");
    if (!f->binder_finalized) {
        struct yetty_ycore_void_result fr = f->binder->ops->finalize(f->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "yshadertoy figure: binder finalize");
        f->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = f->binder->ops->update(f->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "yshadertoy figure: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        ywarn("yshadertoy figure: render target has no view");
        return YETTY_OK_VOID();
    }
    WGPURenderPipeline pipeline = f->binder->ops->get_pipeline(f->binder);
    WGPUBuffer quad_vb = f->binder->ops->get_quad_vertex_buffer(f->binder);
    if (!pipeline || !quad_vb) {
        return YETTY_OK_VOID();
    }

    /* Confine the fullscreen NDC quad to the figure rect via viewport +
     * scissor. The scissor must lie inside the render area, so clamp it
     * against the target viewport (the figure rect may exceed the pane). */
    float target_w = target->viewport.w;
    float target_h = target->viewport.h;
    float scissor_x = x < 0.0f ? 0.0f : x;
    float scissor_y = y < 0.0f ? 0.0f : y;
    float scissor_right = x + w > target_w ? target_w : x + w;
    float scissor_bottom = y + h > target_h ? target_h : y + h;
    if (scissor_right <= scissor_x || scissor_bottom <= scissor_y) {
        return YETTY_OK_VOID();
    }

    struct yetty_yframework_gpu_context *gpu = &f->context->runtime->gpu;
    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy figure: command encoder create failed");
    }

    /* LoadOp_Load → paint on top of whatever lower layers already drew. */
    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0.0, 0.0, 0.0, 0.0};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "yshadertoy figure: render pass begin failed");
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    f->binder->ops->bind(f->binder, pass, 0);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetViewport(pass, x, y, w, h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)scissor_x, (uint32_t)scissor_y,
                                        (uint32_t)(scissor_right - scissor_x),
                                        (uint32_t)(scissor_bottom - scissor_y));
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cb_desc);
    wgpuQueueSubmit(gpu->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(encoder);
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Figure ops — process_bytes (wire init payload = raw WGSL shader text)
 * ========================================================================= */

static struct yetty_ycore_void_result figure_process_bytes(struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len)
{
    struct yetty_yclass_void_ptr_result figure_r = yshadertoy_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "yshadertoy figure_process_bytes: from_obj");
    struct yetty_yshadertoy_figure *f = figure_r.value;
    struct yetty_ycore_void_result r = figure_apply_shader(obj, (const char *)bytes, bytes_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yshadertoy figure: process_bytes apply");
    figure_request_render(f);
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Figure ops — destroy
 * ========================================================================= */

static struct yetty_ycore_void_result figure_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result figure_r = yshadertoy_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_r, "yshadertoy figure_destroy: from_obj");
    struct yetty_yshadertoy_figure *f = figure_r.value;
    if (f->timer_created && f->event_loop && f->event_loop->ops) {
        if (f->timer_running && f->event_loop->ops->stop_timer) {
            f->event_loop->ops->stop_timer(f->event_loop, f->timer_id);
        }
        if (f->event_loop->ops->destroy_timer) {
            f->event_loop->ops->destroy_timer(f->event_loop, f->timer_id);
        }
    }
    if (f->binder) {
        f->binder->ops->destroy(f->binder);
        f->binder = NULL;
    }
    free(f->shader_source);
    /* Free the yclass allocation (header + every slice). */
    return yetty_yclass_object_free(obj);
}

/* ===========================================================================
 * yclass slot overrides — every slot takes the object handle.
 * ========================================================================= */

[[clang::annotate("override@yshadertoy:figure:yfigure:render")]]
static struct yetty_ycore_void_result figure_render_slot(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         struct yetty_ydraw_target *target)
{
    (void)ctx;
    return figure_render(obj, target);
}

[[clang::annotate("override@yshadertoy:figure:yfigure:destroy")]]
static struct yetty_ycore_void_result figure_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj)
{
    (void)ctx;
    return figure_destroy(obj);
}

[[clang::annotate("override@yshadertoy:figure:yfigure:process_bytes")]]
static struct yetty_ycore_void_result figure_process_bytes_slot(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                const uint8_t *bytes,
                                                                size_t bytes_len)
{
    (void)ctx;
    return figure_process_bytes(obj, bytes, bytes_len);
}

/* ===========================================================================
 * Construction — shared by create() and the registry factory.
 * ========================================================================= */

static struct yetty_yclass_object_ptr_result figure_construct(struct yetty_ycore_rectangle rect,
                                                              const char *shader_src,
                                                              size_t shader_len,
                                                              const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yshadertoy figure: context is NULL");
    }

    struct yetty_yclass_ptr_result class_r = yetty_yshadertoy_figure_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yshadertoy figure: class", class_r);
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(class_r.value);
    if (YETTY_IS_ERR(obj_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yshadertoy figure: object_alloc", obj_r);
    }
    struct yetty_yclass_object *obj = obj_r.value;
    struct yetty_yclass_void_ptr_result figure_r = yshadertoy_figure_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, figure_r, "yshadertoy figure: from_obj");
    struct yetty_yshadertoy_figure *f = figure_r.value;

    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(obj, rect);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, set_r, "drop: yetty_yfigure_figure_rect_set");
    }
    {
        struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, set_r, "drop: yetty_yfigure_figure_dirty_set");
    }

    f->context = context;

    strncpy(f->rs.namespace, "yshadertoy", YETTY_YRENDER_NAME_MAX - 1);
    init_uniforms(&f->rs);
    set_resolution(&f->rs, rect.max.x - rect.min.x, rect.max.y - rect.min.y);
    f->rs.instance_count = 1;
    f->rs.pixel_size.width = rect.max.x - rect.min.x;
    f->rs.pixel_size.height = rect.max.y - rect.min.y;

    struct yetty_ycore_void_result ar = figure_apply_shader(obj, shader_src, shader_len);
    if (YETTY_IS_ERR(ar)) {
        (void)yetty_yclass_object_free(obj);
        return YETTY_ERR(yetty_yclass_object_ptr, "yshadertoy figure: apply shader", ar);
    }

    anim_timer_init(f);

    ydebug("yshadertoy figure: created rect=(%.0f,%.0f,%.0fx%.0f)", rect.min.x, rect.min.y,
           rect.max.x - rect.min.x, rect.max.y - rect.min.y);
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* ===========================================================================
 * Public API — all object-keyed: callers hold the yclass object handle.
 * ========================================================================= */

/* Create a yshadertoy figure directly (in-process). `shader_src` is the user
 * WGSL mainImage body; pass NULL to start with the default gradient (set the
 * real source later via set_source). `rect` is absolute pixel space within the
 * parent container. The figure reaches the GPU, event loop (anim timer +
 * repaint nudge) and config through `context`. Returns the yclass object
 * handle — pass it to set_source / as_figure. */
[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yshadertoy_create(struct yetty_ycore_rectangle rect,
                                                              const char *shader_src,
                                                              size_t shader_len,
                                                              const struct yetty_context *context)
{
    return figure_construct(rect, shader_src, shader_len, context);
}

/* Upcast to the figure base handle (a Result — the object may not be of this
 * class). */
[[clang::annotate("expose")]]
struct yetty_yfigure_figure_ptr_result yetty_yshadertoy_as_figure(struct yetty_yclass_object *obj)
{
    return yetty_yfigure_figure_from(obj);
}

/* Replace the shader text and force a recompile on the next render. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yshadertoy_set_source(struct yetty_yclass_object *obj,
                                                           const char *shader_src,
                                                           size_t shader_len)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy figure set_source: NULL object");
    }
    return figure_apply_shader(obj, shader_src, shader_len);
}

/* ===========================================================================
 * Registry factory — minted by a container from a CREATE_CHILD record.
 * The registry hands us the host's context at mint time (everything the
 * figure needs — GPU, event loop, config — hangs off it), so no
 * registration-time args bundle is required. The shader text arrives
 * later via process_bytes (the record's init payload).
 * ========================================================================= */

static struct yetty_yfigure_figure_ptr_result yshadertoy_factory(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context, void *user)
{
    (void)user;
    if (!context) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yshadertoy_factory: no context");
    }
    /* No shader yet — the default gradient stands in until the init
     * payload (process_bytes) supplies the real source. */
    struct yetty_yclass_object_ptr_result obj_r = figure_construct(rect, NULL, 0, context);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, obj_r, "yshadertoy_factory: construct");
    struct yetty_yfigure_figure_ptr_result base_r = yetty_yfigure_figure_from(obj_r.value);
    YETTY_RETURN_IF_ERR(yetty_yfigure_figure_ptr, base_r, "yshadertoy_factory: base from_obj");
    return YETTY_OK(yetty_yfigure_figure_ptr, base_r.value);
}

/* Register the yshadertoy factory under YETTY_YFIGURE_KIND_YSHADERTOY.
 * No args bundle: the registry hands the host context to the factory at
 * mint time. Call from yframework's register_figure_factories (the same
 * place ymgui/yrdawn register), once per host registry. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yshadertoy_register_factory(
    struct yetty_yfigure_registry *registry)
{
    if (!registry) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yshadertoy_register_factory: NULL registry");
    }
    return yetty_yfigure_registry_register(registry, YETTY_YFIGURE_KIND_YSHADERTOY,
                                           yshadertoy_factory, NULL);
}

/* yclass class accessor (yetty_yshadertoy_figure_class_get) + registration. */
#include "figure.gen.c"
