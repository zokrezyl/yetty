//-----------------------------------------------------------------------------
// yetty_wgpu - Python extension module exposing yetty's WebGPU handles
//-----------------------------------------------------------------------------
// This module allows Python code (pygfx/fastplotlib) to use yetty's WebGPU
// device, queue, and textures directly.
//-----------------------------------------------------------------------------

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <webgpu/webgpu.h>
#include <yetty/webgpu-context.h>
#include <cstdint>

namespace {

// Global state - set by C++ before Python code runs
struct YettyWGPUState {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    // Render target texture (created for pygfx to render into)
    WGPUTexture renderTexture = nullptr;
    WGPUTextureView renderTextureView = nullptr;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;

    // Reference to WebGPUContext (if available)
    yetty::WebGPUContext* ctx = nullptr;
};

static YettyWGPUState g_state;

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
bool yetty_wgpu_create_render_texture(uint32_t width, uint32_t height);

//-----------------------------------------------------------------------------
// Python module functions
//-----------------------------------------------------------------------------

// Get device handle as integer (for wgpu-py _internal)
static PyObject* get_device_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.device) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU device not initialized");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.device);
}

// Get queue handle as integer
static PyObject* get_queue_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.queue) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU queue not initialized");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.queue);
}

// Get adapter handle as integer
static PyObject* get_adapter_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.adapter) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU adapter not initialized");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.adapter);
}

// Get instance handle as integer
static PyObject* get_instance_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.instance) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU instance not initialized");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.instance);
}

// Get render texture handle
static PyObject* get_render_texture_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.renderTexture) {
        PyErr_SetString(PyExc_RuntimeError, "Render texture not created");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.renderTexture);
}

// Get render texture view handle
static PyObject* get_render_texture_view_handle(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (!g_state.renderTextureView) {
        PyErr_SetString(PyExc_RuntimeError, "Render texture view not created");
        return nullptr;
    }
    return PyLong_FromVoidPtr(g_state.renderTextureView);
}

// Get render texture size
static PyObject* get_render_texture_size(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    return Py_BuildValue("(II)", g_state.textureWidth, g_state.textureHeight);
}

// Upload pixel data to render texture
static PyObject* upload_texture_data(PyObject* self, PyObject* args) {
    (void)self;

    Py_buffer buffer;
    uint32_t width, height;

    if (!PyArg_ParseTuple(args, "y*II", &buffer, &width, &height)) {
        return nullptr;
    }

    if (!g_state.device || !g_state.queue || !g_state.renderTexture) {
        PyBuffer_Release(&buffer);
        PyErr_SetString(PyExc_RuntimeError, "WebGPU not initialized or texture not created");
        return nullptr;
    }

    // Verify buffer size
    size_t expectedSize = width * height * 4;  // RGBA
    if ((size_t)buffer.len != expectedSize) {
        PyBuffer_Release(&buffer);
        PyErr_SetString(PyExc_ValueError, "Buffer size doesn't match width*height*4");
        return nullptr;
    }

    // Upload to texture
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = g_state.renderTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};

    wgpuQueueWriteTexture(g_state.queue, &dst, buffer.buf, buffer.len, &layout, &extent);

    PyBuffer_Release(&buffer);
    Py_RETURN_TRUE;
}

// Check if initialized
static PyObject* is_initialized(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    if (g_state.device && g_state.queue) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

// Set WebGPU handles from Python
static PyObject* set_handles(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    
    static char* kwlist[] = {(char*)"device", (char*)"queue", (char*)"adapter", (char*)"instance", nullptr};
    PyObject* device_obj = nullptr;
    PyObject* queue_obj = nullptr;
    PyObject* adapter_obj = Py_None;
    PyObject* instance_obj = Py_None;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|OO", kwlist,
                                     &device_obj, &queue_obj, &adapter_obj, &instance_obj)) {
        return nullptr;
    }
    
    // Convert Python ints (void pointers) to C pointers
    g_state.device = (WGPUDevice)PyLong_AsVoidPtr(device_obj);
    g_state.queue = (WGPUQueue)PyLong_AsVoidPtr(queue_obj);
    
    if (adapter_obj != Py_None) {
        g_state.adapter = (WGPUAdapter)PyLong_AsVoidPtr(adapter_obj);
    }
    if (instance_obj != Py_None) {
        g_state.instance = (WGPUInstance)PyLong_AsVoidPtr(instance_obj);
    }
    
    if (!g_state.device || !g_state.queue) {
        PyErr_SetString(PyExc_ValueError, "Invalid device or queue handle");
        return nullptr;
    }
    
    Py_RETURN_NONE;
}

// Create render texture
static PyObject* create_render_texture(PyObject* self, PyObject* args) {
    (void)self;
    
    uint32_t width, height;
    if (!PyArg_ParseTuple(args, "II", &width, &height)) {
        return nullptr;
    }
    
    if (!g_state.device) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU device not initialized");
        return nullptr;
    }

    // Release old texture if exists
    if (g_state.renderTextureView) {
        wgpuTextureViewRelease(g_state.renderTextureView);
        g_state.renderTextureView = nullptr;
    }
    if (g_state.renderTexture) {
        wgpuTextureDestroy(g_state.renderTexture);
        wgpuTextureRelease(g_state.renderTexture);
        g_state.renderTexture = nullptr;
    }

    // Create new texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = {.data = "yetty_pygfx_render_target", .length = WGPU_STRLEN};
    texDesc.size = {width, height, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment |
                    WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_CopySrc |
                    WGPUTextureUsage_CopyDst;

    g_state.renderTexture = wgpuDeviceCreateTexture(g_state.device, &texDesc);
    if (!g_state.renderTexture) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create render texture");
        return nullptr;
    }

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = {.data = "yetty_pygfx_render_target_view", .length = WGPU_STRLEN};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    g_state.renderTextureView = wgpuTextureCreateView(g_state.renderTexture, &viewDesc);
    if (!g_state.renderTextureView) {
        wgpuTextureDestroy(g_state.renderTexture);
        wgpuTextureRelease(g_state.renderTexture);
        g_state.renderTexture = nullptr;
        PyErr_SetString(PyExc_RuntimeError, "Failed to create texture view");
        return nullptr;
    }

    g_state.textureWidth = width;
    g_state.textureHeight = height;
    
    Py_RETURN_TRUE;
}

// Cleanup resources
static PyObject* cleanup(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    
    if (g_state.renderTextureView) {
        wgpuTextureViewRelease(g_state.renderTextureView);
        g_state.renderTextureView = nullptr;
    }
    if (g_state.renderTexture) {
        wgpuTextureRelease(g_state.renderTexture);
        g_state.renderTexture = nullptr;
    }
    
    g_state.textureWidth = 0;
    g_state.textureHeight = 0;
    
    Py_RETURN_NONE;
}

// Get device features (simplified - returns empty set for now)
static PyObject* get_device_features(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    // TODO: Query actual features from device
    return PySet_New(nullptr);
}

// Get device limits (simplified - returns default dict for now)
static PyObject* get_device_limits(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    // TODO: Query actual limits from device
    PyObject* limits = PyDict_New();
    // Add some common defaults
    PyDict_SetItemString(limits, "max_texture_dimension_2d", PyLong_FromLong(8192));
    PyDict_SetItemString(limits, "max_bind_groups", PyLong_FromLong(4));
    return limits;
}

//-----------------------------------------------------------------------------
// Module definition
//-----------------------------------------------------------------------------

static PyMethodDef YettyWgpuMethods[] = {
    {"set_handles", (PyCFunction)set_handles, METH_VARARGS | METH_KEYWORDS,
     "Set WebGPU handles (device, queue, adapter, instance)"},
    {"create_render_texture", create_render_texture, METH_VARARGS,
     "Create render texture (width, height)"},
    {"cleanup", cleanup, METH_NOARGS,
     "Cleanup WebGPU resources"},
    {"get_device_handle", get_device_handle, METH_NOARGS,
     "Get the WGPUDevice handle as an integer"},
    {"get_queue_handle", get_queue_handle, METH_NOARGS,
     "Get the WGPUQueue handle as an integer"},
    {"get_adapter_handle", get_adapter_handle, METH_NOARGS,
     "Get the WGPUAdapter handle as an integer"},
    {"get_instance_handle", get_instance_handle, METH_NOARGS,
     "Get the WGPUInstance handle as an integer"},
    {"get_render_texture_handle", get_render_texture_handle, METH_NOARGS,
     "Get the render target WGPUTexture handle"},
    {"get_render_texture_view_handle", get_render_texture_view_handle, METH_NOARGS,
     "Get the render target WGPUTextureView handle"},
    {"get_render_texture_size", get_render_texture_size, METH_NOARGS,
     "Get the render texture size as (width, height)"},
    {"upload_texture_data", upload_texture_data, METH_VARARGS,
     "Upload RGBA pixel data to render texture (bytes, width, height)"},
    {"is_initialized", is_initialized, METH_NOARGS,
     "Check if WebGPU handles are initialized"},
    {"get_device_features", get_device_features, METH_NOARGS,
     "Get device features as a set"},
    {"get_device_limits", get_device_limits, METH_NOARGS,
     "Get device limits as a dict"},
    {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef yetty_wgpu_module = {
    PyModuleDef_HEAD_INIT,
    "yetty_wgpu",
    "Yetty WebGPU handle exposure for Python graphics libraries",
    -1,
    YettyWgpuMethods
};

} // anonymous namespace

//-----------------------------------------------------------------------------
// Module initialization (called by Python)
//-----------------------------------------------------------------------------

PyMODINIT_FUNC PyInit_yetty_wgpu(void) {
    return PyModule_Create(&yetty_wgpu_module);
}

//-----------------------------------------------------------------------------
// C++ API - called from PythonPlugin to set up state
//-----------------------------------------------------------------------------

extern "C" {

// Initialize the module with WebGPU context
void yetty_wgpu_init(yetty::WebGPUContext* ctx) {
    if (!ctx) return;

    g_state.ctx = ctx;
    g_state.device = ctx->getDevice();
    g_state.queue = ctx->getQueue();
    // Note: adapter and instance need to be exposed from WebGPUContext
    // For now they may be null
}

// Set handles directly (alternative to using WebGPUContext)
void yetty_wgpu_set_handles(
    WGPUInstance instance,
    WGPUAdapter adapter,
    WGPUDevice device,
    WGPUQueue queue
) {
    g_state.instance = instance;
    g_state.adapter = adapter;
    g_state.device = device;
    g_state.queue = queue;
}

// Create render texture for pygfx to render into
bool yetty_wgpu_create_render_texture(uint32_t width, uint32_t height) {
    if (!g_state.device) return false;

    // Release old texture if exists
    if (g_state.renderTextureView) {
        wgpuTextureViewRelease(g_state.renderTextureView);
        g_state.renderTextureView = nullptr;
    }
    if (g_state.renderTexture) {
        wgpuTextureDestroy(g_state.renderTexture);
        wgpuTextureRelease(g_state.renderTexture);
        g_state.renderTexture = nullptr;
    }

    // Create new texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = {.data = "yetty_pygfx_render_target", .length = WGPU_STRLEN};
    texDesc.size = {width, height, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment |
                    WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_CopySrc |
                    WGPUTextureUsage_CopyDst;

    g_state.renderTexture = wgpuDeviceCreateTexture(g_state.device, &texDesc);
    if (!g_state.renderTexture) return false;

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = {.data = "yetty_pygfx_render_target_view", .length = WGPU_STRLEN};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    g_state.renderTextureView = wgpuTextureCreateView(g_state.renderTexture, &viewDesc);
    if (!g_state.renderTextureView) {
        wgpuTextureDestroy(g_state.renderTexture);
        wgpuTextureRelease(g_state.renderTexture);
        g_state.renderTexture = nullptr;
        return false;
    }

    g_state.textureWidth = width;
    g_state.textureHeight = height;

    return true;
}

// Get render texture for use in C++ rendering
WGPUTexture yetty_wgpu_get_render_texture() {
    return g_state.renderTexture;
}

WGPUTextureView yetty_wgpu_get_render_texture_view() {
    return g_state.renderTextureView;
}

// Cleanup
// Note: We don't destroy the texture here because wgpu-py may have already
// claimed ownership via wrapped handles and destroyed it during Python cleanup.
// Trying to destroy it again causes a panic in wgpu-native.
void yetty_wgpu_cleanup() {
    // Just null out references, don't destroy
    // The texture will be destroyed when the wgpu device is destroyed
    g_state.renderTextureView = nullptr;
    g_state.renderTexture = nullptr;
    g_state = YettyWGPUState{};
}

} // extern "C"
