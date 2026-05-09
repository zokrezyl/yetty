/* iOS surface - WebGPU surface from CAMetalLayer */

#include <webgpu/webgpu.h>
#import <QuartzCore/CAMetalLayer.h>

WGPUSurface yetty_yplatform_create_surface_from_layer(WGPUInstance instance, CAMetalLayer *layer)
{
    WGPUSurfaceSourceMetalLayer metal_source = {0};
    WGPUSurfaceDescriptor surface_desc = {0};

    if (!instance || !layer)
        return NULL;

    metal_source.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metal_source.layer = (__bridge void *)layer;

    surface_desc.nextInChain = &metal_source.chain;

    return wgpuInstanceCreateSurface(instance, &surface_desc);
}
