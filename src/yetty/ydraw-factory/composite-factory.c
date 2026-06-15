// YDraw Complex Primitive Factory - Abstract Factory Implementation

#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONCRETE_FACTORIES 32

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
    uint32_t count;
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

    // Compile pipeline for this concrete factory
    if (concrete->compile_pipeline) {
        struct yetty_ycore_void_result res = concrete->compile_pipeline(
            concrete, factory->device, factory->queue, factory->target_format, factory->allocator);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "composite_factory: compile_pipeline failed");
    }

    factory->factories[factory->count++] = concrete;
    ydebug("composite_factory: registered type 0x%08x", concrete->type_id);
    return YETTY_OK_VOID();
}

//=============================================================================
// Abstract factory lookup
//=============================================================================

static struct yetty_ydraw_concrete_factory *composite_factory_get(
    struct yetty_ydraw_composite_factory *factory, uint32_t type_id)
{
    if (!factory) {
        return NULL;
    }

    for (uint32_t i = 0; i < factory->count; i++) {
        if (factory->factories[i]->type_id == type_id) {
            return factory->factories[i];
        }
    }
    return NULL;
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
    struct yetty_ydraw_concrete_factory *concrete = composite_factory_get(factory, type_id);
    if (!concrete) {
        return YETTY_ERR(yetty_ydraw_composite_ptr, "type not registered");
    }

    // Delegate to concrete factory
    return concrete->create_instance(concrete, buffer_data, size, rolling_row);
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
    return instance->render(instance, target, x, y);
}
