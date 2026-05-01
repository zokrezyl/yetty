/*
 * ywebgpu/limits.c - default WGPULimits builder for yetty devices.
 */

#include <yetty/ywebgpu/limits.h>
#include <yetty/ycore/math.h>

void yetty_ywebgpu_fill_default_limits(WGPUAdapter adapter,
                                       const struct yetty_yconfig_config *config,
                                       WGPULimits *out)
{
    /* config is intentionally unused for now; see issue #138
     * (https://github.com/zokrezyl/yetty/issues/138). */
    (void)config;

    if (!out) {
        return;
    }

    WGPULimits adapter_limits = {0};
    if (adapter) {
        wgpuAdapterGetLimits(adapter, &adapter_limits);
    }

    WGPULimits limits = {0};
    limits.maxTextureDimension1D = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureDimension2D = yetty_min_u32(16384u, adapter_limits.maxTextureDimension2D);
    limits.maxTextureDimension3D = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureArrayLayers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroups = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroupsPlusVertexBuffers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindingsPerBindGroup = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicUniformBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicStorageBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSampledTexturesPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSamplersPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageBuffersPerShaderStage = 10;
    limits.maxStorageTexturesPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBuffersPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBufferBindingSize = WGPU_LIMIT_U64_UNDEFINED;
    limits.maxStorageBufferBindingSize =
        yetty_min_u64(512ULL * 1024 * 1024, adapter_limits.maxStorageBufferBindingSize);
    limits.minUniformBufferOffsetAlignment = WGPU_LIMIT_U32_UNDEFINED;
    limits.minStorageBufferOffsetAlignment = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBuffers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBufferSize = yetty_min_u64(1024ULL * 1024 * 1024, adapter_limits.maxBufferSize);
    limits.maxVertexAttributes = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBufferArrayStride = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxInterStageShaderVariables = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxColorAttachments = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxColorAttachmentBytesPerSample = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupStorageSize = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeInvocationsPerWorkgroup = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeX = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeY = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupSizeZ = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxComputeWorkgroupsPerDimension = WGPU_LIMIT_U32_UNDEFINED;

    *out = limits;
}
