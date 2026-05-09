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

char *yetty_ywebgpu_get_webgpu_description(WGPUAdapter adapter)
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

    if (n < 0 || n_limits < 0) {
        wgpuAdapterInfoFreeMembers(info);
        return NULL;
    }

    size_t total = (size_t)n + (size_t)n_limits + 1;
    char *out = (char *)malloc(total);
    if (!out) {
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
        snprintf(out + w, total - (size_t)w,
                 "GPU limits:      maxTextureDimension2D=%u  "
                 "maxBufferSize=%llu  "
                 "maxStorageBufferBindingSize=%llu  "
                 "maxUniformBufferBindingSize=%llu\n",
                 limits.maxTextureDimension2D, (unsigned long long)limits.maxBufferSize,
                 (unsigned long long)limits.maxStorageBufferBindingSize,
                 (unsigned long long)limits.maxUniformBufferBindingSize);
    }

    wgpuAdapterInfoFreeMembers(info);
    return out;
}
