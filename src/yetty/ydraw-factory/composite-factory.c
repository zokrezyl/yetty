// YDraw Complex Primitive Factory - Abstract Factory Implementation

#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONCRETE_FACTORIES 32

/* How many figures may hold live GPU allocations at once. Each figure's
 * binder holds a handful of allocator slots (storage + uniform buffers, and
 * for some types an atlas texture); the allocator caps total slots (see
 * MAX_ALLOCATIONS in gpu-allocator.c). A scrollback of thousands of lines can
 * anchor far more figures than that, so residency is bounded here instead:
 * only the BUDGET most-recently-rendered figures stay resident, the rest hand
 * their slots back and reacquire on their next render. Kept well below the
 * allocator budget (even at a few slots per figure) yet far above the handful
 * of figures visible in any one frame, so an on-screen figure is never evicted
 * mid-frame. */
#define YETTY_YDRAW_GPU_RESIDENCY_BUDGET 128u

//=============================================================================
// GPU residency — bounded LRU of GPU-resident figures (see the residency block
// in struct yetty_ydraw_composite). Owned by the abstract factory; every
// instance it mints back-points here. Not thread-safe: all mutation happens on
// the render/ingest thread that owns the factory.
//=============================================================================

struct yetty_ydraw_gpu_residency {
    struct yetty_ydraw_composite *mru; /* most recently used — list head */
    struct yetty_ydraw_composite *lru; /* least recently used — list tail */
    uint32_t count;
    uint32_t budget;
};

static void residency_init(struct yetty_ydraw_gpu_residency *residency, uint32_t budget)
{
    residency->mru = NULL;
    residency->lru = NULL;
    residency->count = 0;
    residency->budget = budget ? budget : YETTY_YDRAW_GPU_RESIDENCY_BUDGET;
}

/* Detach `composite` from the LRU list. Leaves its GPU resources untouched —
 * callers that also want the slots freed call residency_release_one(). */
static void residency_list_remove(struct yetty_ydraw_gpu_residency *residency,
                                  struct yetty_ydraw_composite *composite)
{
    if (!composite->resident) {
        return;
    }
    if (composite->res_prev) {
        composite->res_prev->res_next = composite->res_next;
    } else {
        residency->mru = composite->res_next;
    }
    if (composite->res_next) {
        composite->res_next->res_prev = composite->res_prev;
    } else {
        residency->lru = composite->res_prev;
    }
    composite->res_prev = NULL;
    composite->res_next = NULL;
    composite->resident = 0;
    if (residency->count > 0) {
        residency->count--;
    }
}

/* Push `composite` at the MRU (front). Caller guarantees it is not currently
 * linked. */
static void residency_list_push_front(struct yetty_ydraw_gpu_residency *residency,
                                      struct yetty_ydraw_composite *composite)
{
    composite->res_prev = NULL;
    composite->res_next = residency->mru;
    if (residency->mru) {
        residency->mru->res_prev = composite;
    }
    residency->mru = composite;
    if (!residency->lru) {
        residency->lru = composite;
    }
    composite->resident = 1;
    residency->count++;
}

/* Hand one figure's GPU allocations back to the allocator and drop it from the
 * LRU. The figure itself lives on (its wire record is retained); a later
 * render reacquires the slots via binder->finalize(). */
static void residency_release_one(struct yetty_ydraw_gpu_residency *residency,
                                  struct yetty_ydraw_composite *composite)
{
    if (composite->binder && composite->binder->ops->release_gpu) {
        struct yetty_ycore_void_result rr = composite->binder->ops->release_gpu(composite->binder);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error); /* best-effort — slot may already be gone */
        }
    }
    residency_list_remove(residency, composite);
}

/* Evict from the LRU tail until the resident set fits the budget. The tail is
 * always the least-recently-rendered figure, so with BUDGET far above the
 * per-frame visible count this never releases a figure needed this frame. */
static void residency_evict_to_budget(struct yetty_ydraw_gpu_residency *residency)
{
    while (residency->count > residency->budget && residency->lru) {
        residency_release_one(residency, residency->lru);
    }
}

/* Make `composite` the most-recently-used resident figure, ensuring its GPU
 * resources are live. Called on every render and on create (eager binders).
 * Returns an error only if reacquiring the GPU resources failed. */
static struct yetty_ycore_void_result residency_touch(struct yetty_ydraw_gpu_residency *residency,
                                                      struct yetty_ydraw_composite *composite)
{
    composite->residency = residency;

    /* Rebuild the binder's GPU resources if they were previously released
     * (off-screen). finalize() is a no-op when already finalized, so this is
     * cheap on the hot path where the figure stayed resident. */
    if (composite->binder && composite->binder->ops->finalize) {
        struct yetty_ycore_void_result fr = composite->binder->ops->finalize(composite->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "residency_touch: reacquire GPU resources");
    }

    if (composite->resident) {
        residency_list_remove(residency, composite);
    }
    residency_list_push_front(residency, composite);
    residency_evict_to_budget(residency);
    return YETTY_OK_VOID();
}

//=============================================================================
// Abstract factory internal structure
//=============================================================================

struct yetty_ydraw_composite_factory {
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;
    /* Event loop shared with the host terminal. Propagated to each
     * concrete factory at register time so instances can subscribe
     * to timers and other events. NULL is acceptable — concrete
     * factories that need it will gracefully degrade. */
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_ydraw_concrete_factory *factories[MAX_CONCRETE_FACTORIES];
    /* Deferred pipeline compilation: registration only records the concrete
     * factory; the (expensive — WGSL → SPIR-V → driver ISA) compile_pipeline
     * runs at the first create_instance of that type. A plain text session
     * never pays for figure shaders it doesn't use. Indexed like factories[]. */
    uint8_t pipeline_ready[MAX_CONCRETE_FACTORIES];
    uint32_t count;
    /* Bounds how many minted figures stay GPU-resident at once. Every
     * instance this factory creates back-points here for LRU bookkeeping. */
    struct yetty_ydraw_gpu_residency residency;
};

//=============================================================================
// Abstract factory lifecycle
//=============================================================================

struct yetty_ydraw_composite_factory_ptr_result yetty_ydraw_composite_factory_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat target_format,
    struct yetty_ydraw_gpu_allocator *allocator, struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_ydraw_composite_factory *factory =
        calloc(1, sizeof(struct yetty_ydraw_composite_factory));
    if (!factory) {
        return YETTY_ERR(yetty_ydraw_composite_factory_ptr, "allocation failed");
    }

    factory->device = device;
    factory->queue = queue;
    factory->target_format = target_format;
    factory->allocator = allocator;
    factory->event_loop = event_loop;
    residency_init(&factory->residency, YETTY_YDRAW_GPU_RESIDENCY_BUDGET);

    return YETTY_OK(yetty_ydraw_composite_factory_ptr, factory);
}

void yetty_ydraw_composite_factory_destroy(struct yetty_ydraw_composite_factory *factory)
{
    if (!factory) {
        return;
    }
    /* Registration transferred ownership of each concrete factory to us —
     * tear them down through their destroy op (frees the shared pipeline
     * and the factory allocation). */
    for (uint32_t i = 0; i < factory->count; i++) {
        struct yetty_ydraw_concrete_factory *concrete = factory->factories[i];
        if (concrete && concrete->destroy) {
            concrete->destroy(concrete);
        }
        factory->factories[i] = NULL;
    }
    free(factory);
}

//=============================================================================
// Abstract factory registration
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_composite_factory_register(
    struct yetty_ydraw_composite_factory *factory, struct yetty_ydraw_concrete_factory *concrete)
{
    if (!factory) {
        return YETTY_ERR(yetty_ycore_void, "factory is NULL");
    }
    if (!concrete) {
        return YETTY_ERR(yetty_ycore_void, "concrete is NULL");
    }
    if (factory->count >= MAX_CONCRETE_FACTORIES) {
        return YETTY_ERR(yetty_ycore_void, "max factories reached");
    }

    // Check for duplicate
    for (uint32_t i = 0; i < factory->count; i++) {
        if (factory->factories[i]->type_id == concrete->type_id) {
            return YETTY_ERR(yetty_ycore_void, "type already registered");
        }
    }

    // Propagate the registry's event_loop to the concrete factory so
    // its instances can subscribe to timers / mouse / etc.
    concrete->event_loop = factory->event_loop;

    /* Pipeline compilation is deferred to the first create_instance of this
     * type (see composite_factory_ensure_pipeline). Compiling here would make
     * every startup pay for every figure shader, used or not. */
    factory->pipeline_ready[factory->count] = concrete->compile_pipeline ? 0 : 1;
    factory->factories[factory->count++] = concrete;
    ydebug("composite_factory: registered type 0x%08x", concrete->type_id);
    return YETTY_OK_VOID();
}

//=============================================================================
// Abstract factory lookup
//=============================================================================

/* Returns the factories[] slot for `type_id`, or -1 if not registered. The
 * slot (not the pointer) is what lookups hand out so callers can reach the
 * parallel pipeline_ready[] flag. */
static int composite_factory_get_slot(struct yetty_ydraw_composite_factory *factory,
                                      uint32_t type_id)
{
    if (!factory) {
        return -1;
    }

    for (uint32_t i = 0; i < factory->count; i++) {
        if (factory->factories[i]->type_id == type_id) {
            return (int)i;
        }
    }
    return -1;
}

/* Run the deferred compile_pipeline for the factory in `slot` if it hasn't run
 * yet. Idempotent; called on every create_instance, a boolean test after the
 * first time. */
static struct yetty_ycore_void_result composite_factory_ensure_pipeline(
    struct yetty_ydraw_composite_factory *factory, uint32_t slot)
{
    struct yetty_ydraw_concrete_factory *concrete = factory->factories[slot];
    if (factory->pipeline_ready[slot]) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result compile_res = concrete->compile_pipeline(
        concrete, factory->device, factory->queue, factory->target_format, factory->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, compile_res,
                        "composite_factory: deferred compile_pipeline failed");
    factory->pipeline_ready[slot] = 1;
    return YETTY_OK_VOID();
}

//=============================================================================
// Abstract factory instance creation
//=============================================================================

struct yetty_ydraw_composite_ptr_result yetty_ydraw_composite_factory_create_instance(
    struct yetty_ydraw_composite_factory *factory, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!factory) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "factory is NULL");
    }
    if (!buffer_data || size < sizeof(struct yetty_ydraw_composite_record)) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "invalid buffer data");
    }

    // Read type from buffer
    const struct yetty_ydraw_composite_record *prim = buffer_data;
    uint32_t type_id = prim->header.type;

    // Get concrete factory
    int slot = composite_factory_get_slot(factory, type_id);
    if (slot < 0) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "type not registered");
    }
    struct yetty_ydraw_concrete_factory *concrete = factory->factories[slot];

    /* First instance of this type: run the deferred pipeline compile. */
    struct yetty_ycore_void_result ensure_res =
        composite_factory_ensure_pipeline(factory, (uint32_t)slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_composite_ptr, ensure_res,
                        "composite_factory: pipeline compile on first use failed");

    // Delegate to concrete factory
    struct yetty_ydraw_composite_ptr_result inst_res =
        concrete->create_instance(concrete, buffer_data, size, rolling_row);
    if (YETTY_IS_ERR(inst_res)) {
        return inst_res;
    }

    /* Register the fresh instance with the GPU residency LRU. create_instance
     * finalises the binder eagerly, so a burst of ingests (a scrollback full
     * of plots) would otherwise pin every figure's allocator slots at once.
     * Registering here evicts the least-recently-used figures back down to the
     * budget, bounding live GPU allocations regardless of scrollback depth. */
    if (inst_res.value) {
        struct yetty_ycore_void_result tr = residency_touch(&factory->residency, inst_res.value);
        if (YETTY_IS_ERR(tr)) {
            /* The binder was just finalised by create_instance, so the touch's
             * reacquire is a no-op that cannot fail here; guard anyway and keep
             * the instance — it reacquires on its next render. */
            yetty_ycore_error_destroy(tr.error);
        }
    }
    return inst_res;
}

//=============================================================================
// Visual zoom fan-out — called by ydraw-layer when the visual zoom changes.
// Each concrete factory writes the scale/offsets into its own shared uniforms
// so its fragment shader can transform the incoming pixel at fs_main entry.
//=============================================================================

void yetty_ydraw_composite_factory_set_visual_zoom(struct yetty_ydraw_composite_factory *factory,
                                                   float scale, float offset_x, float offset_y)
{
    if (!factory) {
        return;
    }
    for (uint32_t i = 0; i < factory->count; i++) {
        struct yetty_ydraw_concrete_factory *cf = factory->factories[i];
        if (cf && cf->set_visual_zoom) {
            cf->set_visual_zoom(cf, scale, offset_x, offset_y);
        }
    }
}

void yetty_ydraw_composite_factory_set_cell_zoom(struct yetty_ydraw_composite_factory *factory,
                                                 float scale, float offset_x, float offset_y)
{
    if (!factory) {
        ydebug("composite_factory_set_cell_zoom: factory is NULL");
        return;
    }
    ydebug("composite_factory_set_cell_zoom: scale=%.3f off=(%.1f,%.1f) factories=%u", scale,
           offset_x, offset_y, factory->count);
    for (uint32_t i = 0; i < factory->count; i++) {
        struct yetty_ydraw_concrete_factory *cf = factory->factories[i];
        if (!cf) {
            continue;
        }
        ydebug("  factory[%u] type=0x%08x set_cell_zoom=%p", i, cf->type_id,
               (void *)(uintptr_t)cf->set_cell_zoom);
        if (cf->set_cell_zoom) {
            cf->set_cell_zoom(cf, scale, offset_x, offset_y);
        }
    }
}

//=============================================================================
// Instance destruction (uses back-pointer)
//=============================================================================

void yetty_ydraw_composite_destroy(struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return;
    }

    /* Drop out of the GPU residency LRU before teardown so the manager never
     * retains a dangling pointer. The binder (and its GPU slots, resident or
     * released) is freed by the per-instance destroy below. */
    if (instance->residency) {
        residency_list_remove(instance->residency, instance);
        instance->residency = NULL;
    }

    /* Preferred path: dispatch through the per-instance vtable. The
     * factory is out of the runtime call path. */
    if (instance->ops && instance->ops->destroy) {
        instance->ops->destroy(instance);
        return;
    }

    /* Legacy fallback — only reached if a figure type was created
     * without wiring its ops table (would be a bug; every concrete
     * factory should set instance->ops in create_instance). */
    if (instance->factory && instance->factory->destroy_instance) {
        instance->factory->destroy_instance(instance->factory, instance);
        return;
    }

    free(instance->buffer_data);
    free(instance);
}

/* Render one composite instance into `target` at (x, y). Thin public wrapper
 * over the per-instance `render` op (the host grid supplies the position for
 * its scrolling/anchored placement). A figure type with no render op simply
 * doesn't paint — not an error. */
struct yetty_ycore_void_result yetty_ydraw_composite_render(struct yetty_ydraw_composite *instance,
                                                            struct yetty_ydraw_target *target,
                                                            float x, float y)
{
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydraw_composite_render: NULL instance");
    }
    if (!instance->render) {
        return YETTY_OK_VOID();
    }

    /* This figure is on-screen: make it the most-recently-used resident and
     * ensure its GPU resources are live (reacquired if it was released while
     * off-screen). A figure that dropped off the LRU while scrolled away lands
     * back here the moment it re-enters the pane, rebuilding its binder from
     * its retained wire record. Skip for figures minted outside a residency
     * (none today, but keep the wrapper self-contained). */
    if (instance->residency) {
        struct yetty_ycore_void_result tr = residency_touch(instance->residency, instance);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "composite_render: residency acquire");
    }

    return instance->render(instance, target, x, y);
}

float yetty_ydraw_composite_pixel_height(const struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return 0.0f;
    }
    float height = instance->bounds.max.y - instance->bounds.min.y;
    return height > 0.0f ? height : 0.0f;
}

/* Bottom edge of the figure's wire bounds (envelope-local px). Culling
 * must use THIS, not the height: a multi-figure envelope's record can sit
 * deep inside its block (origin at bounds.min), so the visible extent
 * below the block anchor is max.y, not max.y - min.y. */
float yetty_ydraw_composite_pixel_bottom(const struct yetty_ydraw_composite *instance)
{
    if (!instance) {
        return 0.0f;
    }
    return instance->bounds.max.y > 0.0f ? instance->bounds.max.y : 0.0f;
}

void yetty_ydraw_composite_set_content_scale(struct yetty_ydraw_composite *instance, float scale)
{
    if (!instance) {
        return;
    }
    instance->content_scale = scale > 0.0f ? scale : 1.0f;
}
