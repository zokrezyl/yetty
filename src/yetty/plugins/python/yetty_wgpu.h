#pragma once
//-----------------------------------------------------------------------------
// yetty_wgpu - C++ API header for Python WebGPU integration
//-----------------------------------------------------------------------------

#include <webgpu/webgpu.h>
#include <cstdint>

// Forward declare PyObject to avoid including Python.h
struct _object;
typedef _object PyObject;

namespace yetty {
class WebGPUContext;
}

extern "C" {

// Initialize with WebGPUContext (preferred method)
void yetty_wgpu_init(yetty::WebGPUContext* ctx);

// Set handles directly
void yetty_wgpu_set_handles(
    WGPUInstance instance,
    WGPUAdapter adapter,
    WGPUDevice device,
    WGPUQueue queue
);

// Create render texture for pygfx to render into
// Returns true on success
bool yetty_wgpu_create_render_texture(uint32_t width, uint32_t height);

// Get render texture handles (for C++ side rendering)
WGPUTexture yetty_wgpu_get_render_texture();
WGPUTextureView yetty_wgpu_get_render_texture_view();

// Cleanup resources
void yetty_wgpu_cleanup();

// Python module init function (called automatically by Python)
PyObject* PyInit_yetty_wgpu(void);

} // extern "C"
