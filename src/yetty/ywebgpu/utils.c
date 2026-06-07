/*
 * ywebgpu/utils.c - WebGPU helper utilities.
 */

#include <yetty/ywebgpu/utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *backend_type_name(WGPUBackendType t)
{
    switch (t) {
    case WGPUBackendType_Undefined:
        return "Undefined";
    case WGPUBackendType_Null:
        return "Null";
    case WGPUBackendType_WebGPU:
        return "WebGPU";
    case WGPUBackendType_D3D11:
        return "D3D11";
    case WGPUBackendType_D3D12:
        return "D3D12";
    case WGPUBackendType_Metal:
        return "Metal";
    case WGPUBackendType_Vulkan:
        return "Vulkan";
    case WGPUBackendType_OpenGL:
        return "OpenGL";
    case WGPUBackendType_OpenGLES:
        return "OpenGLES";
    default:
        return "Unknown";
    }
}

static const char *adapter_type_name(WGPUAdapterType t)
{
    switch (t) {
    case WGPUAdapterType_DiscreteGPU:
        return "DiscreteGPU";
    case WGPUAdapterType_IntegratedGPU:
        return "IntegratedGPU";
    case WGPUAdapterType_CPU:
        return "CPU";
    case WGPUAdapterType_Unknown:
        return "Unknown";
    default:
        return "Unknown";
    }
}

/* Human-readable name for the texture formats we expect on a surface.
 * Unknown values fall through to a hex repr so the log line never lies
 * about an enum value it can't translate. */
static const char *texture_format_name(WGPUTextureFormat f, char *hex_buf, size_t hex_buf_size)
{
    switch (f) {
    case WGPUTextureFormat_BGRA8Unorm:
        return "BGRA8Unorm";
    case WGPUTextureFormat_BGRA8UnormSrgb:
        return "BGRA8UnormSrgb";
    case WGPUTextureFormat_RGBA8Unorm:
        return "RGBA8Unorm";
    case WGPUTextureFormat_RGBA8UnormSrgb:
        return "RGBA8UnormSrgb";
    case WGPUTextureFormat_RGBA16Float:
        return "RGBA16Float";
    case WGPUTextureFormat_RGB10A2Unorm:
        return "RGB10A2Unorm";
    default:
        snprintf(hex_buf, hex_buf_size, "0x%x", (unsigned)f);
        return hex_buf;
    }
}

static const char *present_mode_name(WGPUPresentMode m)
{
    switch (m) {
    case WGPUPresentMode_Fifo:
        return "Fifo";
    case WGPUPresentMode_FifoRelaxed:
        return "FifoRelaxed";
    case WGPUPresentMode_Mailbox:
        return "Mailbox";
    case WGPUPresentMode_Immediate:
        return "Immediate";
    default:
        return "Unknown";
    }
}

char *yetty_ywebgpu_get_webgpu_description(WGPUAdapter adapter, WGPUSurface surface)
{
    if (!adapter) {
        return NULL;
    }

    WGPUAdapterInfo info = {0};
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        return NULL;
    }

    WGPULimits limits = {0};
    int has_limits = (wgpuAdapterGetLimits(adapter, &limits) == WGPUStatus_Success);

    /* Surface caps — formats[0] is the platform's "preferred" format. The
     * difference between BGRA8Unorm (linear, returned first on Vulkan) and
     * BGRA8UnormSrgb (gamma-encoded, returned first by Metal) is exactly
     * the reason the chrome rendered correctly on Linux but crushed to
     * black on macOS for a while — dumping the list here makes that kind
     * of asymmetry visible at startup without needing to attach a
     * debugger. */
    WGPUSurfaceCapabilities caps = {0};
    int has_caps = 0;
    if (surface) {
        has_caps = (wgpuSurfaceGetCapabilities(surface, adapter, &caps) == WGPUStatus_Success);
    }

    /* Two-pass: measure with snprintf(NULL), then allocate and format. */
    int n = snprintf(
        NULL, 0,
        "GPU backend:     %s\n"
        "GPU adapter:     %s\n"
        "GPU vendor:      %.*s (vendorID=0x%04x)\n"
        "GPU device:      %.*s (deviceID=0x%04x)\n"
        "GPU arch:        %.*s\n"
        "GPU description: %.*s\n",
        backend_type_name(info.backendType), adapter_type_name(info.adapterType),
        (int)info.vendor.length, info.vendor.data ? info.vendor.data : "", info.vendorID,
        (int)info.device.length, info.device.data ? info.device.data : "", info.deviceID,
        (int)info.architecture.length, info.architecture.data ? info.architecture.data : "",
        (int)info.description.length, info.description.data ? info.description.data : "");

    int n_limits = 0;
    if (has_limits) {
        n_limits = snprintf(NULL, 0,
                            "GPU limits:      maxTextureDimension2D=%u  "
                            "maxBufferSize=%llu  "
                            "maxStorageBufferBindingSize=%llu  "
                            "maxUniformBufferBindingSize=%llu\n",
                            limits.maxTextureDimension2D, (unsigned long long)limits.maxBufferSize,
                            (unsigned long long)limits.maxStorageBufferBindingSize,
                            (unsigned long long)limits.maxUniformBufferBindingSize);
    }

    /* Worst-case per-entry length for caps lines: longest format name is
     * "BGRA8UnormSrgb" (14 chars); add "Surface fmt[%zu]: %s\n" framing
     * (worst-case index ~3 digits) → well under 64 bytes per entry. Same
     * order of magnitude for present modes. Add a fixed header line. */
    int n_caps = 0;
    if (has_caps) {
        n_caps = 64; /* header line */
        n_caps += 64 * (int)caps.formatCount;
        n_caps += 64 * (int)caps.presentModeCount;
    }

    if (n < 0 || n_limits < 0) {
        if (has_caps) {
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        wgpuAdapterInfoFreeMembers(info);
        return NULL;
    }

    size_t total = (size_t)n + (size_t)n_limits + (size_t)n_caps + 1;
    char *out = (char *)malloc(total);
    if (!out) {
        if (has_caps) {
            wgpuSurfaceCapabilitiesFreeMembers(caps);
        }
        wgpuAdapterInfoFreeMembers(info);
        return NULL;
    }

    int w = snprintf(
        out, total,
        "GPU backend:     %s\n"
        "GPU adapter:     %s\n"
        "GPU vendor:      %.*s (vendorID=0x%04x)\n"
        "GPU device:      %.*s (deviceID=0x%04x)\n"
        "GPU arch:        %.*s\n"
        "GPU description: %.*s\n",
        backend_type_name(info.backendType), adapter_type_name(info.adapterType),
        (int)info.vendor.length, info.vendor.data ? info.vendor.data : "", info.vendorID,
        (int)info.device.length, info.device.data ? info.device.data : "", info.deviceID,
        (int)info.architecture.length, info.architecture.data ? info.architecture.data : "",
        (int)info.description.length, info.description.data ? info.description.data : "");

    if (has_limits && w >= 0) {
        int m = snprintf(out + w, total - (size_t)w,
                         "GPU limits:      maxTextureDimension2D=%u  "
                         "maxBufferSize=%llu  "
                         "maxStorageBufferBindingSize=%llu  "
                         "maxUniformBufferBindingSize=%llu\n",
                         limits.maxTextureDimension2D, (unsigned long long)limits.maxBufferSize,
                         (unsigned long long)limits.maxStorageBufferBindingSize,
                         (unsigned long long)limits.maxUniformBufferBindingSize);
        if (m > 0) {
            w += m;
        }
    }

    if (has_caps && w >= 0) {
        int m = snprintf(out + w, total - (size_t)w, "Surface formats (%zu, preferred first):\n",
                         (size_t)caps.formatCount);
        if (m > 0) {
            w += m;
        }
        for (size_t i = 0; i < caps.formatCount && w >= 0; i++) {
            char hex_buf[16];
            const char *fname = texture_format_name(caps.formats[i], hex_buf, sizeof(hex_buf));
            m = snprintf(out + w, total - (size_t)w, "  [%zu] %s\n", i, fname);
            if (m > 0) {
                w += m;
            }
        }
        m = snprintf(out + w, total - (size_t)w, "Surface present modes (%zu):\n",
                     (size_t)caps.presentModeCount);
        if (m > 0) {
            w += m;
        }
        for (size_t i = 0; i < caps.presentModeCount && w >= 0; i++) {
            m = snprintf(out + w, total - (size_t)w, "  [%zu] %s\n", i,
                         present_mode_name(caps.presentModes[i]));
            if (m > 0) {
                w += m;
            }
        }
    }

    if (has_caps) {
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }
    wgpuAdapterInfoFreeMembers(info);
    return out;
}
