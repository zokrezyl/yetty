/* WebAssembly surface.c - WebGPU surface from HTML canvas
 *
 * Creates WGPUSurface from the #canvas HTML element.
 */

#include <webgpu/webgpu.h>
#include <yetty/ytrace.h>
#include <emscripten/html5.h>

WGPUSurface yetty_yplatform_webasm_create_surface(WGPUInstance instance)
{
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source = {0};
    WGPUSurfaceDescriptor surface_desc = {0};
    WGPUSurface surface;

    if (!instance) {
        yerror("create_surface: null instance");
        return NULL;
    }

    canvas_source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas_source.selector.data = "#canvas";
    canvas_source.selector.length = 7;

    surface_desc.nextInChain = &canvas_source.chain;

    surface = wgpuInstanceCreateSurface(instance, &surface_desc);
    if (!surface) {
        yerror("create_surface: Failed to create surface from #canvas");
        return NULL;
    }

    ydebug("create_surface: Surface created from #canvas");
    return surface;
}
