#pragma once
//-----------------------------------------------------------------------------
// yetty_wgpu - C++ API header for Python WebGPU integration
//-----------------------------------------------------------------------------
// Supports multiple simultaneous widgets, each with its own render texture.
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

// Allocate a new widget ID (returns unique ID for this widget)
int yetty_wgpu_allocate_widget_id();

// Create render texture for a specific widget
// Returns true on success
bool yetty_wgpu_create_render_texture(int widgetId, uint32_t width, uint32_t height);

// Get render texture handles for a specific widget (for C++ side rendering)
WGPUTexture yetty_wgpu_get_render_texture(int widgetId);
WGPUTextureView yetty_wgpu_get_render_texture_view(int widgetId);

// Cleanup resources for a specific widget
void yetty_wgpu_cleanup_widget(int widgetId);

// Cleanup ALL widget resources (called on plugin shutdown)
void yetty_wgpu_cleanup();

// Python module init function (called automatically by Python)
PyObject* PyInit_yetty_wgpu(void);

} // extern "C"
