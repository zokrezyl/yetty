/* Android surface - WebGPU surface from ANativeWindow */

#include <webgpu/webgpu.h>
#include <android/native_window.h>

WGPUSurface yetty_yplatform_create_surface_from_window(WGPUInstance instance, ANativeWindow *window)
{
    WGPUSurfaceSourceAndroidNativeWindow android_source = {0};
    WGPUSurfaceDescriptor surface_desc = {0};

    if (!instance || !window) {
        return NULL;
    }

    android_source.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
    android_source.window = window;

    surface_desc.nextInChain = &android_source.chain;

    return wgpuInstanceCreateSurface(instance, &surface_desc);
}
