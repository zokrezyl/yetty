/*
 * prim.c — yshadertoy composite-prim factory (server side, GPU).
 *
 * Receives the wire record built by prim-wire.c and renders the user's
 * Shadertoy-style WGSL inside the scrolling content layer, cursor-anchored
 * like every other complex prim (yplot, yimage, yvideo).
 *
 * Departure from the sibling prim types: there is NO factory-shared
 * pipeline. The wire payload IS the fragment shader, so each instance
 * assembles its own WGSL (fullscreen-triangle VS + user mainImage +
 * bounds-clipping FS), compiles a per-instance yetty_yrender_pipeline from
 * it, and pairs it with a binder created via create_with_pipeline. The
 * factory only stashes the GPU handles handed to compile_pipeline at
 * registration time.
 *
 * Positioning follows yplot: the pipeline rasterizes the whole pane
 * viewport with a fullscreen triangle; the FS maps NDC → pane pixels via
 * the viewport_w/h uniforms, subtracts the bounds origin and discards
 * fragments outside the prim rect. That keeps scrolled-off positions
 * (negative canvas y) valid — no negative-viewport problem.
 *
 * Animation mirrors yplot-time.c: a factory-shared refcounted timer; every
 * instance subscribes its embedded listener at create time (a shadertoy is
 * presumed animated). Each tick marks the instance dirty and kicks a
 * render; the render itself advances iTime/iTimeDelta/iFrame from the
 * monotonic clock.
 */

#include <yetty/yshadertoy/prim.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/time.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/pipeline.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YSHADERTOY_ANIMATION_PERIOD_MS 16 /* ~60 Hz */

/* Wire payload words ahead of the WGSL text (must match prim-wire.c):
 * 4×f32 bounds + u32 flags + u32 wgsl_len. */
#define YSHADERTOY_PRIM_FIXED_WORDS 6u

/* Uniform slots — the binder emits one struct field per slot named
 * `yshadertoy_<name>` (namespace below). */
#define U_BOUNDS_X 0
#define U_BOUNDS_Y 1
#define U_BOUNDS_W 2
#define U_BOUNDS_H 3
#define U_VIEWPORT_W 4
#define U_VIEWPORT_H 5
#define U_TIME 6
#define U_TIME_DELTA 7
#define U_FRAME 8
#define U_MOUSE 9
#define U_COUNT 10

/*=============================================================================
 * WGSL assembly — same shape as the yshadertoy figure: the pipeline build
 * prepends the `struct Uniforms` block + binding, so the template below
 * references the injected fields as `uniforms.yshadertoy_<name>`.
 *
 * User-shader contract (identical to the figure's):
 *   fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,
 *                iTime: f32, iMouse: vec4<f32>) -> vec4<f32> { ... }
 *
 * fragCoord is in pixels, origin at the prim rect's bottom-left (Shadertoy
 * convention). iTimeDelta / iFrame remain reachable as
 * uniforms.yshadertoy_time_delta / _frame for shaders that want them.
 *===========================================================================*/

static const char YSHADERTOY_PRIM_VS[] =
    "struct VertexOutput {\n"
    "    @builtin(position) position: vec4<f32>,\n"
    "    // Pane-local pixel coordinate (origin at the pane's top-left).\n"
    "    // The raw @builtin(position) is the FRAMEBUFFER pixel; mapping\n"
    "    // NDC -> pane pixels here keeps the FS independent of where the\n"
    "    // pane sits in the big surface (same trick as yplot.wgsl).\n"
    "    @location(0) @interpolate(linear) pane_pixel: vec2<f32>,\n"
    "};\n"
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {\n"
    "    var pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(\n"
    "        vec2<f32>(-1.0, -1.0),\n"
    "        vec2<f32>( 3.0, -1.0),\n"
    "        vec2<f32>(-1.0,  3.0)\n"
    "    );\n"
    "    let vp_w = uniforms.yshadertoy_viewport_w;\n"
    "    let vp_h = uniforms.yshadertoy_viewport_h;\n"
    "    var out: VertexOutput;\n"
    "    out.position = vec4<f32>(pos[vertex_index], 0.0, 1.0);\n"
    "    out.pane_pixel = vec2<f32>(\n"
    "        (pos[vertex_index].x * 0.5 + 0.5) * vp_w,\n"
    "        (0.5 - pos[vertex_index].y * 0.5) * vp_h\n"
    "    );\n"
    "    return out;\n"
    "}\n";

static const char YSHADERTOY_PRIM_FS[] =
    "@fragment\n"
    "fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {\n"
    "    let bounds_x = uniforms.yshadertoy_bounds_x;\n"
    "    let bounds_y = uniforms.yshadertoy_bounds_y;\n"
    "    let bounds_w = uniforms.yshadertoy_bounds_w;\n"
    "    let bounds_h = uniforms.yshadertoy_bounds_h;\n"
    "    let local_pos = in.pane_pixel - vec2<f32>(bounds_x, bounds_y);\n"
    "    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||\n"
    "        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {\n"
    "        discard;\n"
    "    }\n"
    "    // Shadertoy convention: fragCoord origin at the rect's bottom-left.\n"
    "    let frag_coord = vec2<f32>(local_pos.x, bounds_h - local_pos.y);\n"
    "    let resolution = vec3<f32>(bounds_w, bounds_h, 1.0);\n"
    "    return mainImage(frag_coord, resolution,\n"
    "                     uniforms.yshadertoy_time, uniforms.yshadertoy_mouse);\n"
    "}\n";

/* Shown when the wire carries no shader text — never blank. */
static const char YSHADERTOY_PRIM_DEFAULT_USER[] =
    "fn mainImage(fragCoord: vec2<f32>, iResolution: vec3<f32>,\n"
    "             iTime: f32, iMouse: vec4<f32>) -> vec4<f32> {\n"
    "    let uv = fragCoord / iResolution.xy;\n"
    "    let col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3<f32>(0.0, 2.0, 4.0));\n"
    "    return vec4<f32>(col, 1.0);\n"
    "}\n";

/* Build the full WGSL = VS + user shader + FS. Returns malloc'd buffer;
 * *out_size set to the byte length (excluding the NUL). */
static char *yshadertoy_prim_assemble_shader(const char *user_src, size_t user_len,
                                             size_t *out_size)
{
    if (!user_src || user_len == 0u) {
        user_src = YSHADERTOY_PRIM_DEFAULT_USER;
        user_len = sizeof(YSHADERTOY_PRIM_DEFAULT_USER) - 1u;
    }
    size_t vs_len = sizeof(YSHADERTOY_PRIM_VS) - 1u;
    size_t fs_len = sizeof(YSHADERTOY_PRIM_FS) - 1u;
    size_t total = vs_len + 1u + user_len + 1u + fs_len;
    char *out = malloc(total + 1u);
    if (!out) {
        return NULL;
    }
    size_t offset = 0u;
    memcpy(out + offset, YSHADERTOY_PRIM_VS, vs_len);
    offset += vs_len;
    out[offset++] = '\n';
    memcpy(out + offset, user_src, user_len);
    offset += user_len;
    out[offset++] = '\n';
    memcpy(out + offset, YSHADERTOY_PRIM_FS, fs_len);
    offset += fs_len;
    out[offset] = '\0';
    *out_size = offset;
    return out;
}

/*=============================================================================
 * Factory + per-instance state
 *===========================================================================*/

struct yetty_yshadertoy_prim_factory {
    struct yetty_ydraw_concrete_factory base;
    /* GPU handles stashed at compile_pipeline (registration) time; needed
     * later because every create_instance compiles its own pipeline. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;
};

static struct yetty_yshadertoy_prim_factory *yshadertoy_prim_factory_from_base(
    struct yetty_ydraw_concrete_factory *base)
{
    return (struct yetty_yshadertoy_prim_factory *)base;
}

/* Per-instance state, hung off instance->instance_data. */
struct yshadertoy_prim_instance_state {
    /* Per-instance pipeline compiled from the wire's WGSL. Owned here. */
    struct yetty_yrender_pipeline *pipeline;
    /* Assembled WGSL text — rs->shader references it, so it must outlive
     * the resource set. Owned here. */
    char *shader_source;
    /* Animation clock. iTime = now - start; iTimeDelta = now - last. */
    double start_monotonic_sec;
    double last_monotonic_sec;
    uint32_t frame;
    bool clock_started;
    bool timer_subscribed;
};

/* Factory-shared, refcounted animation timer (same pattern as
 * yplot-time.c), stashed in factory->hook_data. */
struct yshadertoy_prim_timer_state {
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id; /* valid only while subscribers > 0 */
    uint32_t subscribers;
};

/*=============================================================================
 * Resource set
 *===========================================================================*/

static void yshadertoy_prim_populate_rs(struct yetty_yrender_gpu_resource_set *rs,
                                        const char *shader_source, size_t shader_source_size)
{
    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "yshadertoy", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader, shader_source, shader_source_size);

    rs->uniforms[U_BOUNDS_X] =
        (struct yetty_yrender_uniform){"bounds_x", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_BOUNDS_Y] =
        (struct yetty_yrender_uniform){"bounds_y", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_BOUNDS_W] =
        (struct yetty_yrender_uniform){"bounds_w", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_BOUNDS_H] =
        (struct yetty_yrender_uniform){"bounds_h", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_VIEWPORT_W] =
        (struct yetty_yrender_uniform){"viewport_w", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_VIEWPORT_H] =
        (struct yetty_yrender_uniform){"viewport_h", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_TIME] = (struct yetty_yrender_uniform){"time", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_TIME_DELTA] =
        (struct yetty_yrender_uniform){"time_delta", YETTY_YRENDER_UNIFORM_F32, {0}};
    rs->uniforms[U_FRAME] = (struct yetty_yrender_uniform){"frame", YETTY_YRENDER_UNIFORM_I32, {0}};
    rs->uniforms[U_MOUSE] =
        (struct yetty_yrender_uniform){"mouse", YETTY_YRENDER_UNIFORM_VEC4, {0}};
    rs->uniform_count = U_COUNT;
}

/*=============================================================================
 * Shared animation timer
 *===========================================================================*/

static struct yshadertoy_prim_timer_state *yshadertoy_prim_timer_state_get_or_init(
    struct yetty_ydraw_concrete_factory *factory)
{
    if (!factory) {
        return NULL;
    }
    if (factory->hook_data) {
        return (struct yshadertoy_prim_timer_state *)factory->hook_data;
    }
    struct yshadertoy_prim_timer_state *timer_state = calloc(1, sizeof(*timer_state));
    if (!timer_state) {
        return NULL;
    }
    timer_state->event_loop = factory->event_loop;
    timer_state->timer_id = -1;
    factory->hook_data = timer_state;
    return timer_state;
}

static struct yetty_ycore_void_result yshadertoy_prim_timer_subscribe(
    struct yetty_ydraw_concrete_factory *factory, struct yetty_yevent_event_listener *listener)
{
    if (!factory || !factory->event_loop) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim timer: no event_loop on factory");
    }
    if (!listener || !listener->handler) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim timer: listener has no handler");
    }
    struct yshadertoy_prim_timer_state *timer_state =
        yshadertoy_prim_timer_state_get_or_init(factory);
    if (!timer_state) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim timer: state alloc failed");
    }
    struct yetty_yevent_event_loop *loop = timer_state->event_loop;

    if (timer_state->subscribers == 0u) {
        struct yetty_yevent_timer_id_result timer_res = loop->ops->create_timer(loop);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, timer_res, "yshadertoy prim timer: create_timer");
        timer_state->timer_id = timer_res.value;
        loop->ops->config_timer(loop, timer_state->timer_id, YSHADERTOY_ANIMATION_PERIOD_MS);
        loop->ops->start_timer(loop, timer_state->timer_id);
        yinfo("yshadertoy prim: shared timer started (period=%d ms)",
              YSHADERTOY_ANIMATION_PERIOD_MS);
    }
    struct yetty_ycore_void_result register_res =
        loop->ops->register_timer_listener(loop, timer_state->timer_id, listener);
    if (YETTY_IS_ERR(register_res)) {
        if (timer_state->subscribers == 0u) {
            loop->ops->stop_timer(loop, timer_state->timer_id);
            loop->ops->destroy_timer(loop, timer_state->timer_id);
            timer_state->timer_id = -1;
        }
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim timer: register_timer_listener",
                         register_res);
    }
    timer_state->subscribers++;
    return YETTY_OK_VOID();
}

static void yshadertoy_prim_timer_unsubscribe(struct yetty_ydraw_concrete_factory *factory,
                                              struct yetty_yevent_event_listener *listener)
{
    if (!factory) {
        return;
    }
    struct yshadertoy_prim_timer_state *timer_state =
        (struct yshadertoy_prim_timer_state *)factory->hook_data;
    if (!timer_state || !timer_state->event_loop || timer_state->timer_id < 0 ||
        timer_state->subscribers == 0u) {
        return;
    }
    struct yetty_yevent_event_loop *loop = timer_state->event_loop;
    if (loop->ops->deregister_timer_listener) {
        loop->ops->deregister_timer_listener(loop, timer_state->timer_id, listener);
    }
    timer_state->subscribers--;
    if (timer_state->subscribers == 0u) {
        loop->ops->stop_timer(loop, timer_state->timer_id);
        loop->ops->destroy_timer(loop, timer_state->timer_id);
        timer_state->timer_id = -1;
        yinfo("yshadertoy prim: shared timer stopped (last subscriber gone)");
    }
}

/* Tick handler — marks the instance dirty BEFORE asking for a render so
 * the next frame actually re-runs the shader (the figure loop only calls
 * inst->render iff inst->dirty || force). Time itself advances inside
 * render from the monotonic clock. */
static struct yetty_ycore_int_result yshadertoy_prim_on_tick(
    struct yetty_yevent_event_listener *listener, const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ydraw_composite *instance =
        container_of(listener, struct yetty_ydraw_composite, listener);
    if (!instance || !instance->factory || !instance->factory->event_loop) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    instance->dirty = 1;
    struct yetty_yevent_event_loop *loop = instance->factory->event_loop;
    loop->ops->request_render(loop);
    return YETTY_OK(yetty_ycore_int, 0);
}

/*=============================================================================
 * Instance render
 *===========================================================================*/

static struct yetty_ycore_void_result yshadertoy_prim_instance_render(
    struct yetty_ydraw_composite *self, struct yetty_ydraw_target *target, float x, float y)
{
    if (!self || !self->factory || !self->resource_set || !self->binder) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim render: invalid instance");
    }
    struct yshadertoy_prim_instance_state *state =
        (struct yshadertoy_prim_instance_state *)self->instance_data;
    if (!state || !state->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim render: instance not finalised");
    }
    struct yetty_yshadertoy_prim_factory *factory =
        yshadertoy_prim_factory_from_base(self->factory);

    /* Advance the animation clock. */
    double now = yetty_yplatform_ytime_monotonic_sec();
    if (!state->clock_started) {
        state->start_monotonic_sec = now;
        state->last_monotonic_sec = now;
        state->clock_started = true;
    }
    float elapsed = (float)(now - state->start_monotonic_sec);
    float delta = (float)(now - state->last_monotonic_sec);
    state->last_monotonic_sec = now;

    struct yetty_yrender_gpu_resource_set *rs = self->resource_set;
    rs->uniforms[U_BOUNDS_X].f32 = x;
    rs->uniforms[U_BOUNDS_Y].f32 = y;
    rs->uniforms[U_BOUNDS_W].f32 = self->bounds.max.x - self->bounds.min.x;
    rs->uniforms[U_BOUNDS_H].f32 = self->bounds.max.y - self->bounds.min.y;
    rs->uniforms[U_VIEWPORT_W].f32 = target->viewport.w;
    rs->uniforms[U_VIEWPORT_H].f32 = target->viewport.h;
    rs->uniforms[U_TIME].f32 = elapsed;
    rs->uniforms[U_TIME_DELTA].f32 = delta;
    rs->uniforms[U_FRAME].i32 = (int32_t)state->frame++;

    struct yetty_ycore_void_result update_res = self->binder->ops->update(self->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, update_res, "yshadertoy prim render: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim render: target has no view");
    }

    WGPUCommandEncoderDescriptor encoder_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(factory->device, &encoder_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim render: encoder create failed");
    }

    /* LoadOp_Load → paint on top of whatever lower content already drew. */
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "yshadertoy prim render: render pass begin failed");
    }

    /* Confine raster + pane_pixel mapping to the pane's rect inside the
     * big surface — same reasoning as yplot_instance_render. */
    wgpuRenderPassEncoderSetViewport(pass, target->viewport.x, target->viewport.y,
                                     target->viewport.w, target->viewport.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)target->viewport.x,
                                        (uint32_t)target->viewport.y, (uint32_t)target->viewport.w,
                                        (uint32_t)target->viewport.h);

    yetty_yrender_pipeline_bind(state->pipeline, pass);
    self->binder->ops->bind(self->binder, pass, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0); /* fullscreen triangle */

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(factory->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Instance destroy
 *===========================================================================*/

static void yshadertoy_prim_instance_destroy(struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return;
    }
    struct yshadertoy_prim_instance_state *state =
        (struct yshadertoy_prim_instance_state *)instance->instance_data;
    if (state && state->timer_subscribed && instance->factory) {
        yshadertoy_prim_timer_unsubscribe(instance->factory, &instance->listener);
        state->timer_subscribed = false;
    }
    if (instance->binder) {
        instance->binder->ops->destroy(instance->binder);
    }
    if (state) {
        if (state->pipeline) {
            yetty_yrender_pipeline_destroy(state->pipeline);
        }
        free(state->shader_source);
        free(state);
    }
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}

/* Vtable installed on every yshadertoy prim instance. No incremental
 * updates — the canvas drops CMD_UPDATE records addressed to us. */
static const struct yetty_ydraw_composite_ops yshadertoy_prim_instance_ops = {
    .destroy = yshadertoy_prim_instance_destroy,
    .update = NULL,
};

/*=============================================================================
 * Factory ops
 *===========================================================================*/

static struct yetty_ycore_void_result yshadertoy_prim_compile_pipeline(
    struct yetty_ydraw_concrete_factory *self, WGPUDevice device, WGPUQueue queue,
    WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator)
{
    /* No shared pipeline for this type — the shader arrives per instance.
     * Just stash the GPU handles every create_instance will need. */
    struct yetty_yshadertoy_prim_factory *factory = yshadertoy_prim_factory_from_base(self);
    factory->device = device;
    factory->queue = queue;
    factory->target_format = target_format;
    factory->allocator = allocator;
    return YETTY_OK_VOID();
}

static WGPURenderPipeline yshadertoy_prim_get_pipeline(struct yetty_ydraw_concrete_factory *self)
{
    (void)self;
    return NULL; /* per-instance pipelines only */
}

static struct yetty_yrender_gpu_resource_set *yshadertoy_prim_get_shared_rs(
    struct yetty_ydraw_concrete_factory *self)
{
    (void)self;
    return NULL; /* no shared RS — every instance owns its own */
}

static struct yetty_ydraw_composite_ptr_result yshadertoy_prim_create_instance(
    struct yetty_ydraw_concrete_factory *self, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ydraw_composite_header) +
                                   YSHADERTOY_PRIM_FIXED_WORDS * sizeof(uint32_t)) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: wire record truncated");
    }
    struct yetty_yshadertoy_prim_factory *factory = yshadertoy_prim_factory_from_base(self);
    if (!factory->device) {
        return YETTY_ERR(yetty_ydraw_composite_ptr,
                         "yshadertoy prim: factory pipeline handles not initialised");
    }

    const uint32_t *words = (const uint32_t *)buffer_data;
    uint32_t payload_size = words[1];
    uint32_t wgsl_len = words[7];
    if (payload_size < YSHADERTOY_PRIM_FIXED_WORDS * sizeof(uint32_t) ||
        wgsl_len > payload_size - YSHADERTOY_PRIM_FIXED_WORDS * sizeof(uint32_t) ||
        sizeof(struct yetty_ydraw_composite_header) + payload_size > size) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: wire lengths inconsistent");
    }
    const char *wgsl_source = (const char *)&words[2u + YSHADERTOY_PRIM_FIXED_WORDS];

    struct yetty_ydraw_composite *instance = calloc(1, sizeof(struct yetty_ydraw_composite));
    if (!instance) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: instance alloc failed");
    }
    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: wire copy alloc failed");
    }
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_YSHADERTOY_PRIM_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = yshadertoy_prim_instance_render;
    instance->ops = &yshadertoy_prim_instance_ops;

    struct rectangle_result aabb_res = yetty_ydraw_composite_record_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res)) {
        instance->bounds = aabb_res.value;
    }

    struct yshadertoy_prim_instance_state *state = calloc(1, sizeof(*state));
    if (!state) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: state alloc failed");
    }
    instance->instance_data = state;

    size_t shader_source_size = 0u;
    state->shader_source =
        yshadertoy_prim_assemble_shader(wgsl_source, wgsl_len, &shader_source_size);
    if (!state->shader_source) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: shader assemble failed");
    }

    instance->resource_set = malloc(sizeof(struct yetty_yrender_gpu_resource_set));
    if (!instance->resource_set) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: rs alloc failed");
    }
    yshadertoy_prim_populate_rs(instance->resource_set, state->shader_source, shader_source_size);

    /* Per-instance pipeline compiled from the assembled WGSL. A broken
     * user shader fails here — the envelope is rejected with the error,
     * nothing is leaked. */
    struct yetty_yrender_pipeline_ptr_result pipeline_res = yetty_yrender_pipeline_create(
        factory->device, factory->target_format, factory->allocator, instance->resource_set);
    if (YETTY_IS_ERR(pipeline_res)) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: pipeline create",
                         pipeline_res);
    }
    state->pipeline = pipeline_res.value;

    struct yetty_yrender_gpu_resource_binder_result binder_res =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(factory->device, factory->queue,
                                                               factory->allocator, state->pipeline);
    if (YETTY_IS_ERR(binder_res)) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: binder create", binder_res);
    }
    instance->binder = binder_res.value;

    struct yetty_ycore_void_result submit_res =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(submit_res)) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: binder submit", submit_res);
    }
    struct yetty_ycore_void_result finalize_res = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(finalize_res)) {
        yshadertoy_prim_instance_destroy(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "yshadertoy prim: binder finalize",
                         finalize_res);
    }

    /* A shadertoy is presumed animated — subscribe unconditionally.
     * Failure is non-fatal: the shader still renders, frozen at t=0. */
    if (self->event_loop) {
        instance->listener.handler = yshadertoy_prim_on_tick;
        struct yetty_ycore_void_result subscribe_res =
            yshadertoy_prim_timer_subscribe(self, &instance->listener);
        if (YETTY_IS_OK(subscribe_res)) {
            state->timer_subscribed = true;
        } else {
            ywarn("yshadertoy prim: timer subscribe failed: %s", subscribe_res.error.msg);
            yetty_ycore_error_destroy(subscribe_res.error);
            instance->listener.handler = NULL;
        }
    }

    ydebug("yshadertoy prim: instance created bounds=(%.0f,%.0f %.0fx%.0f) wgsl_len=%u",
           instance->bounds.min.x, instance->bounds.min.y,
           instance->bounds.max.x - instance->bounds.min.x,
           instance->bounds.max.y - instance->bounds.min.y, wgsl_len);
    return YETTY_OK(yetty_ydraw_composite_ptr, instance);
}

static void yshadertoy_prim_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                             struct yetty_ydraw_composite *instance)
{
    (void)self;
    yshadertoy_prim_instance_destroy(instance);
}

/*=============================================================================
 * Factory lifecycle
 *===========================================================================*/

struct yetty_ydraw_concrete_factory *yetty_yshadertoy_prim_factory_create(void)
{
    struct yetty_yshadertoy_prim_factory *factory = calloc(1, sizeof(*factory));
    if (!factory) {
        return NULL;
    }
    factory->base.type_id = YETTY_YSHADERTOY_PRIM_TYPE_ID;
    factory->base.compile_pipeline = yshadertoy_prim_compile_pipeline;
    factory->base.get_pipeline = yshadertoy_prim_get_pipeline;
    factory->base.create_instance = yshadertoy_prim_create_instance;
    factory->base.destroy_instance = yshadertoy_prim_destroy_instance;
    factory->base.update_instance = NULL;
    factory->base.get_shared_rs = yshadertoy_prim_get_shared_rs;
    factory->base.set_visual_zoom = NULL;
    factory->base.set_cell_zoom = NULL;
    return &factory->base;
}

void yetty_yshadertoy_prim_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{
    if (!self) {
        return;
    }
    struct yetty_yshadertoy_prim_factory *factory = yshadertoy_prim_factory_from_base(self);
    /* The shared timer is torn down when the last instance unsubscribes;
     * by destroy time only the bookkeeping struct can remain. */
    free(self->hook_data);
    free(factory);
}
