// YPaint Complex Primitive Factory - Abstract Factory Implementation

#include <yetty/ydraw-factory/complex-prim-factory.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONCRETE_FACTORIES 32

//=============================================================================
// Abstract factory internal structure
//=============================================================================

struct yetty_ydraw_core_complex_prim_factory {
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_core_gpu_allocator *allocator;
    struct yetty_ydraw_core_concrete_factory *factories[MAX_CONCRETE_FACTORIES];
    uint32_t count;
};

//=============================================================================
// Abstract factory lifecycle
//=============================================================================

struct yetty_ydraw_core_complex_prim_factory_ptr_result
yetty_ydraw_core_complex_prim_factory_create(WGPUDevice device, WGPUQueue queue,
                                              WGPUTextureFormat target_format,
                                              struct yetty_ydraw_core_gpu_allocator *allocator)
{
    struct yetty_ydraw_core_complex_prim_factory *factory =
        calloc(1, sizeof(struct yetty_ydraw_core_complex_prim_factory));
    if (!factory) {
        return YETTY_ERR(yetty_ydraw_core_complex_prim_factory_ptr, "allocation failed");
    }

    factory->device = device;
    factory->queue = queue;
    factory->target_format = target_format;
    factory->allocator = allocator;

    return YETTY_OK(yetty_ydraw_core_complex_prim_factory_ptr, factory);
}

void yetty_ydraw_core_complex_prim_factory_destroy(
    struct yetty_ydraw_core_complex_prim_factory *factory)
{
    if (!factory) {
        return;
    }
    // Concrete factories are not owned by abstract factory - they are static
    free(factory);
}

//=============================================================================
// Abstract factory registration
//=============================================================================

struct yetty_ycore_void_result yetty_ydraw_core_complex_prim_factory_register(
    struct yetty_ydraw_core_complex_prim_factory *factory,
    struct yetty_ydraw_core_concrete_factory *concrete)
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

    // Compile pipeline for this concrete factory
    if (concrete->compile_pipeline) {
        struct yetty_ycore_void_result res = concrete->compile_pipeline(
            concrete, factory->device, factory->queue, factory->target_format, factory->allocator);
        if (YETTY_IS_ERR(res)) {
            yerror("complex_prim_factory: failed to compile pipeline for type 0x%08x: %s",
                   concrete->type_id, res.error.msg);
            return res;
        }
    }

    factory->factories[factory->count++] = concrete;
    ydebug("complex_prim_factory: registered type 0x%08x", concrete->type_id);
    return YETTY_OK_VOID();
}

//=============================================================================
// Abstract factory lookup
//=============================================================================

static struct yetty_ydraw_core_concrete_factory *complex_prim_factory_get(
    struct yetty_ydraw_core_complex_prim_factory *factory, uint32_t type_id)
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

struct yetty_ydraw_core_complex_prim_instance_ptr_result
yetty_ydraw_core_complex_prim_factory_create_instance(
    struct yetty_ydraw_core_complex_prim_factory *factory, const void *buffer_data, size_t size,
    uint32_t rolling_row)
{
    if (!factory) {
        return YETTY_ERR(yetty_ydraw_core_complex_prim_instance_ptr, "factory is NULL");
    }
    if (!buffer_data || size < sizeof(struct yetty_ydraw_core_complex_prim)) {
        return YETTY_ERR(yetty_ydraw_core_complex_prim_instance_ptr, "invalid buffer data");
    }

    // Read type from buffer
    const struct yetty_ydraw_core_complex_prim *prim = buffer_data;
    uint32_t type_id = prim->type;

    // Get concrete factory
    struct yetty_ydraw_core_concrete_factory *concrete =
        complex_prim_factory_get(factory, type_id);
    if (!concrete) {
        return YETTY_ERR(yetty_ydraw_core_complex_prim_instance_ptr, "type not registered");
    }

    // Delegate to concrete factory
    return concrete->create_instance(concrete, buffer_data, size, rolling_row);
}

//=============================================================================
// Visual zoom fan-out — called by ypaint-layer when the visual zoom changes.
// Each concrete factory writes the scale/offsets into its own shared uniforms
// so its fragment shader can transform the incoming pixel at fs_main entry.
//=============================================================================

void yetty_ydraw_core_complex_prim_factory_set_visual_zoom(
    struct yetty_ydraw_core_complex_prim_factory *factory, float scale, float offset_x,
    float offset_y)
{
    if (!factory) {
        return;
    }
    for (uint32_t i = 0; i < factory->count; i++) {
        struct yetty_ydraw_core_concrete_factory *cf = factory->factories[i];
        if (cf && cf->set_visual_zoom) {
            cf->set_visual_zoom(cf, scale, offset_x, offset_y);
        }
    }
}

void yetty_ydraw_core_complex_prim_factory_set_cell_zoom(
    struct yetty_ydraw_core_complex_prim_factory *factory, float scale, float offset_x,
    float offset_y)
{
    if (!factory) {
        ydebug("complex_prim_factory_set_cell_zoom: factory is NULL");
        return;
    }
    ydebug("complex_prim_factory_set_cell_zoom: scale=%.3f off=(%.1f,%.1f) factories=%u", scale,
           offset_x, offset_y, factory->count);
    for (uint32_t i = 0; i < factory->count; i++) {
        struct yetty_ydraw_core_concrete_factory *cf = factory->factories[i];
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

void yetty_ydraw_core_complex_prim_instance_destroy(
    struct yetty_ydraw_core_complex_prim_instance *instance)
{
    if (!instance) {
        return;
    }

    if (instance->factory && instance->factory->destroy_instance) {
        instance->factory->destroy_instance(instance->factory, instance);
    } else {
        // Fallback if no concrete factory or no destroy_instance
        free(instance->buffer_data);
        free(instance);
    }
}
