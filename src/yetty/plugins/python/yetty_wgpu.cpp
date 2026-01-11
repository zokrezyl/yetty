//-----------------------------------------------------------------------------
// yetty_wgpu - Python extension module exposing yetty's WebGPU handles
//-----------------------------------------------------------------------------
// This module allows Python code (pygfx/fastplotlib) to use yetty's WebGPU
// device, queue, and textures directly.
//
// Supports multiple simultaneous widgets, each with its own render texture.
//-----------------------------------------------------------------------------

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <webgpu/webgpu.h>
#include <yetty/webgpu-context.h>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace {

//-----------------------------------------------------------------------------
// Per-widget texture state
//-----------------------------------------------------------------------------
struct WidgetTextureState {
    WGPUTexture renderTexture = nullptr;
    WGPUTextureView renderTextureView = nullptr;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
};

//-----------------------------------------------------------------------------
// Global state - shared WebGPU context (device/queue are shared)
//-----------------------------------------------------------------------------
struct YettyWGPUState {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    // Reference to WebGPUContext (if available)
    yetty::WebGPUContext* ctx = nullptr;

    // Per-widget texture states
    std::unordered_map<int, WidgetTextureState> widgetTextures;

    // Counter for generating unique widget IDs
    int nextWidgetId = 1;
};

static YettyWGPUState g_state;

//-----------------------------------------------------------------------------
// Helper to get widget state
//-----------------------------------------------------------------------------
static WidgetTextureState* getWidgetState(int widgetId) {
    auto it = g_state.widgetTextures.find(widgetId);
    if (it == g_state.widgetTextures.end()) {
        return nullptr;
    }
    return &it->second;
}

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

// Get render texture handle for a widget
static PyObject* get_render_texture_handle(PyObject* self, PyObject* args) {
    (void)self;
    int widgetId;
    if (!PyArg_ParseTuple(args, "i", &widgetId)) {
        return nullptr;
    }

    auto* ws = getWidgetState(widgetId);
    if (!ws || !ws->renderTexture) {
        PyErr_SetString(PyExc_RuntimeError, "Render texture not created for widget");
        return nullptr;
    }
    return PyLong_FromVoidPtr(ws->renderTexture);
}

// Get render texture view handle for a widget
static PyObject* get_render_texture_view_handle(PyObject* self, PyObject* args) {
    (void)self;
    int widgetId;
    if (!PyArg_ParseTuple(args, "i", &widgetId)) {
        return nullptr;
    }

    auto* ws = getWidgetState(widgetId);
    if (!ws || !ws->renderTextureView) {
        PyErr_SetString(PyExc_RuntimeError, "Render texture view not created for widget");
        return nullptr;
    }
    return PyLong_FromVoidPtr(ws->renderTextureView);
}

// Get render texture size for a widget
static PyObject* get_render_texture_size(PyObject* self, PyObject* args) {
    (void)self;
    int widgetId;
    if (!PyArg_ParseTuple(args, "i", &widgetId)) {
        return nullptr;
    }

    auto* ws = getWidgetState(widgetId);
    if (!ws) {
        PyErr_SetString(PyExc_RuntimeError, "Widget not found");
        return nullptr;
    }
    return Py_BuildValue("(II)", ws->textureWidth, ws->textureHeight);
}

// Upload pixel data to render texture for a widget
static PyObject* upload_texture_data(PyObject* self, PyObject* args) {
    (void)self;

    int widgetId;
    Py_buffer buffer;
    uint32_t width, height;

    if (!PyArg_ParseTuple(args, "iy*II", &widgetId, &buffer, &width, &height)) {
        return nullptr;
    }

    auto* ws = getWidgetState(widgetId);
    if (!g_state.device || !g_state.queue || !ws || !ws->renderTexture) {
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
    dst.texture = ws->renderTexture;
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

// Allocate a new widget ID and return it
static PyObject* allocate_widget_id(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    int widgetId = g_state.nextWidgetId++;
    g_state.widgetTextures[widgetId] = WidgetTextureState{};
    return PyLong_FromLong(widgetId);
}

// Create render texture for a specific widget
static PyObject* create_render_texture(PyObject* self, PyObject* args) {
    (void)self;

    int widgetId;
    uint32_t width, height;
    if (!PyArg_ParseTuple(args, "iII", &widgetId, &width, &height)) {
        return nullptr;
    }

    if (!g_state.device) {
        PyErr_SetString(PyExc_RuntimeError, "WebGPU device not initialized");
        return nullptr;
    }

    auto* ws = getWidgetState(widgetId);
    if (!ws) {
        PyErr_SetString(PyExc_RuntimeError, "Widget ID not found - call allocate_widget_id first");
        return nullptr;
    }

    // Release old texture if exists
    if (ws->renderTextureView) {
        wgpuTextureViewRelease(ws->renderTextureView);
        ws->renderTextureView = nullptr;
    }
    if (ws->renderTexture) {
        wgpuTextureDestroy(ws->renderTexture);
        wgpuTextureRelease(ws->renderTexture);
        ws->renderTexture = nullptr;
    }

    // Create label with widget ID
    std::string label = "yetty_pygfx_render_target_" + std::to_string(widgetId);

    // Create new texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = {.data = label.c_str(), .length = label.size()};
    texDesc.size = {width, height, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment |
                    WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_CopySrc |
                    WGPUTextureUsage_CopyDst;

    ws->renderTexture = wgpuDeviceCreateTexture(g_state.device, &texDesc);
    if (!ws->renderTexture) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create render texture");
        return nullptr;
    }

    // Create view
    std::string viewLabel = "yetty_pygfx_render_target_view_" + std::to_string(widgetId);
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = {.data = viewLabel.c_str(), .length = viewLabel.size()};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    ws->renderTextureView = wgpuTextureCreateView(ws->renderTexture, &viewDesc);
    if (!ws->renderTextureView) {
        wgpuTextureDestroy(ws->renderTexture);
        wgpuTextureRelease(ws->renderTexture);
        ws->renderTexture = nullptr;
        PyErr_SetString(PyExc_RuntimeError, "Failed to create texture view");
        return nullptr;
    }

    ws->textureWidth = width;
    ws->textureHeight = height;

    Py_RETURN_TRUE;
}

// Cleanup resources for a specific widget
static PyObject* cleanup_widget(PyObject* self, PyObject* args) {
    (void)self;

    int widgetId;
    if (!PyArg_ParseTuple(args, "i", &widgetId)) {
        return nullptr;
    }

    auto it = g_state.widgetTextures.find(widgetId);
    if (it == g_state.widgetTextures.end()) {
        // Widget not found, nothing to cleanup
        Py_RETURN_NONE;
    }

    auto& ws = it->second;
    if (ws.renderTextureView) {
        wgpuTextureViewRelease(ws.renderTextureView);
        ws.renderTextureView = nullptr;
    }
    if (ws.renderTexture) {
        wgpuTextureDestroy(ws.renderTexture);
        wgpuTextureRelease(ws.renderTexture);
        ws.renderTexture = nullptr;
    }

    // Remove from map
    g_state.widgetTextures.erase(it);

    Py_RETURN_NONE;
}

// Cleanup ALL resources (called on plugin shutdown)
static PyObject* cleanup_all(PyObject* self, PyObject* args) {
    (void)self; (void)args;

    for (auto& [id, ws] : g_state.widgetTextures) {
        if (ws.renderTextureView) {
            wgpuTextureViewRelease(ws.renderTextureView);
        }
        if (ws.renderTexture) {
            wgpuTextureDestroy(ws.renderTexture);
            wgpuTextureRelease(ws.renderTexture);
        }
    }
    g_state.widgetTextures.clear();

    Py_RETURN_NONE;
}

// Get list of active widget IDs
static PyObject* get_widget_ids(PyObject* self, PyObject* args) {
    (void)self; (void)args;

    PyObject* list = PyList_New(g_state.widgetTextures.size());
    if (!list) return nullptr;

    Py_ssize_t i = 0;
    for (const auto& [id, ws] : g_state.widgetTextures) {
        PyList_SET_ITEM(list, i++, PyLong_FromLong(id));
    }
    return list;
}

// Get device features (simplified - returns empty set for now)
static PyObject* get_device_features(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    return PySet_New(nullptr);
}

// Get device limits (simplified - returns default dict for now)
static PyObject* get_device_limits(PyObject* self, PyObject* args) {
    (void)self; (void)args;
    PyObject* limits = PyDict_New();
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
    {"allocate_widget_id", allocate_widget_id, METH_NOARGS,
     "Allocate a new widget ID"},
    {"create_render_texture", create_render_texture, METH_VARARGS,
     "Create render texture (widget_id, width, height)"},
    {"cleanup_widget", cleanup_widget, METH_VARARGS,
     "Cleanup resources for a specific widget (widget_id)"},
    {"cleanup_all", cleanup_all, METH_NOARGS,
     "Cleanup all widget resources"},
    {"get_widget_ids", get_widget_ids, METH_NOARGS,
     "Get list of active widget IDs"},
    {"get_device_handle", get_device_handle, METH_NOARGS,
     "Get the WGPUDevice handle as an integer"},
    {"get_queue_handle", get_queue_handle, METH_NOARGS,
     "Get the WGPUQueue handle as an integer"},
    {"get_adapter_handle", get_adapter_handle, METH_NOARGS,
     "Get the WGPUAdapter handle as an integer"},
    {"get_instance_handle", get_instance_handle, METH_NOARGS,
     "Get the WGPUInstance handle as an integer"},
    {"get_render_texture_handle", get_render_texture_handle, METH_VARARGS,
     "Get the render target WGPUTexture handle (widget_id)"},
    {"get_render_texture_view_handle", get_render_texture_view_handle, METH_VARARGS,
     "Get the render target WGPUTextureView handle (widget_id)"},
    {"get_render_texture_size", get_render_texture_size, METH_VARARGS,
     "Get the render texture size as (width, height) (widget_id)"},
    {"upload_texture_data", upload_texture_data, METH_VARARGS,
     "Upload RGBA pixel data to render texture (widget_id, bytes, width, height)"},
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
    "Yetty WebGPU handle exposure for Python graphics libraries (multi-widget)",
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

// Allocate a new widget ID (C++ API)
int yetty_wgpu_allocate_widget_id() {
    int widgetId = g_state.nextWidgetId++;
    g_state.widgetTextures[widgetId] = WidgetTextureState{};
    return widgetId;
}

// Create render texture for a specific widget (C++ API)
bool yetty_wgpu_create_render_texture(int widgetId, uint32_t width, uint32_t height) {
    if (!g_state.device) return false;

    auto* ws = getWidgetState(widgetId);
    if (!ws) return false;

    // Release old texture if exists
    if (ws->renderTextureView) {
        wgpuTextureViewRelease(ws->renderTextureView);
        ws->renderTextureView = nullptr;
    }
    if (ws->renderTexture) {
        wgpuTextureDestroy(ws->renderTexture);
        wgpuTextureRelease(ws->renderTexture);
        ws->renderTexture = nullptr;
    }

    std::string label = "yetty_pygfx_render_target_" + std::to_string(widgetId);

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = {.data = label.c_str(), .length = label.size()};
    texDesc.size = {width, height, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment |
                    WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_CopySrc |
                    WGPUTextureUsage_CopyDst;

    ws->renderTexture = wgpuDeviceCreateTexture(g_state.device, &texDesc);
    if (!ws->renderTexture) return false;

    std::string viewLabel = "yetty_pygfx_render_target_view_" + std::to_string(widgetId);
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = {.data = viewLabel.c_str(), .length = viewLabel.size()};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    ws->renderTextureView = wgpuTextureCreateView(ws->renderTexture, &viewDesc);
    if (!ws->renderTextureView) {
        wgpuTextureDestroy(ws->renderTexture);
        wgpuTextureRelease(ws->renderTexture);
        ws->renderTexture = nullptr;
        return false;
    }

    ws->textureWidth = width;
    ws->textureHeight = height;

    return true;
}

// Get render texture for a specific widget (C++ API)
WGPUTexture yetty_wgpu_get_render_texture(int widgetId) {
    auto* ws = getWidgetState(widgetId);
    return ws ? ws->renderTexture : nullptr;
}

WGPUTextureView yetty_wgpu_get_render_texture_view(int widgetId) {
    auto* ws = getWidgetState(widgetId);
    return ws ? ws->renderTextureView : nullptr;
}

// Cleanup a specific widget (C++ API)
void yetty_wgpu_cleanup_widget(int widgetId) {
    auto it = g_state.widgetTextures.find(widgetId);
    if (it == g_state.widgetTextures.end()) return;

    auto& ws = it->second;
    if (ws.renderTextureView) {
        wgpuTextureViewRelease(ws.renderTextureView);
    }
    if (ws.renderTexture) {
        wgpuTextureDestroy(ws.renderTexture);
        wgpuTextureRelease(ws.renderTexture);
    }
    g_state.widgetTextures.erase(it);
}

// Cleanup all widgets (C++ API)
void yetty_wgpu_cleanup() {
    for (auto& [id, ws] : g_state.widgetTextures) {
        if (ws.renderTextureView) {
            wgpuTextureViewRelease(ws.renderTextureView);
        }
        if (ws.renderTexture) {
            wgpuTextureDestroy(ws.renderTexture);
            wgpuTextureRelease(ws.renderTexture);
        }
    }
    g_state.widgetTextures.clear();
    g_state = YettyWGPUState{};
}

} // extern "C"
