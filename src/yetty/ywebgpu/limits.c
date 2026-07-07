/*
 * ywebgpu/limits.c - default WGPULimits builder for yetty devices.
 */

#include <yetty/ywebgpu/limits.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/math.h>
#include <yetty/ytrace/ytrace.h>

void yetty_ywebgpu_fill_default_limits(WGPUAdapter adapter,
                                       const struct yetty_yconfig_config *config, WGPULimits *out)
{
    if (!out) {
        return;
    }

    /* Built-in defaults; each can be overridden by the YETTY_YCONFIG_KEY_GPU_*
     * config keys declared in <yetty/yconfig/config.h>. */
    uint32_t max_texture_dimension_2d = 16384u;
    uint64_t max_storage_binding_size = 512ULL * 1024 * 1024;
    uint64_t max_buffer_size = 1024ULL * 1024 * 1024;
    uint32_t max_storage_buffers_per_shader_stage = 10u;

    if (config && config->ops && config->ops->get_int) {
        const struct yetty_yconfig_config_ops *ops = config->ops;

        int texture_dimension_2d =
            ops->get_int(config, YETTY_YCONFIG_KEY_GPU_MAX_TEXTURE_DIMENSION_2D, 0);
        if (texture_dimension_2d > 0) {
            max_texture_dimension_2d = (uint32_t)texture_dimension_2d;
        }

        int storage_binding_size_mb =
            ops->get_int(config, YETTY_YCONFIG_KEY_GPU_MAX_STORAGE_BINDING_SIZE_MB, 0);
        if (storage_binding_size_mb > 0) {
            max_storage_binding_size = (uint64_t)storage_binding_size_mb * 1024 * 1024;
        }

        int buffer_size_mb = ops->get_int(config, YETTY_YCONFIG_KEY_GPU_MAX_BUFFER_SIZE_MB, 0);
        if (buffer_size_mb > 0) {
            max_buffer_size = (uint64_t)buffer_size_mb * 1024 * 1024;
        }

        int storage_buffers_per_shader_stage =
            ops->get_int(config, YETTY_YCONFIG_KEY_GPU_MAX_STORAGE_BUFFERS_PER_SHADER_STAGE, 0);
        if (storage_buffers_per_shader_stage > 0) {
            max_storage_buffers_per_shader_stage = (uint32_t)storage_buffers_per_shader_stage;
        }
    }

    WGPULimits limits = {0};
    limits.maxTextureDimension1D = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureDimension2D = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureDimension3D = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxTextureArrayLayers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroups = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindGroupsPlusVertexBuffers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBindingsPerBindGroup = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicUniformBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxDynamicStorageBuffersPerPipelineLayout = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSampledTexturesPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxSamplersPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageBuffersPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxStorageTexturesPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBuffersPerShaderStage = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxUniformBufferBindingSize = WGPU_LIMIT_U64_UNDEFINED;
    limits.maxStorageBufferBindingSize = WGPU_LIMIT_U64_UNDEFINED;
    limits.minUniformBufferOffsetAlignment = WGPU_LIMIT_U32_UNDEFINED;
    limits.minStorageBufferOffsetAlignment = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxVertexBuffers = WGPU_LIMIT_U32_UNDEFINED;
    limits.maxBufferSize = WGPU_LIMIT_U64_UNDEFINED;
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

    WGPULimits adapter_limits = {0};
    int have_adapter_limits =
        adapter && wgpuAdapterGetLimits(adapter, &adapter_limits) == WGPUStatus_Success;

    if (have_adapter_limits) {
        limits.maxTextureDimension2D =
            yetty_min_u32(max_texture_dimension_2d, adapter_limits.maxTextureDimension2D);
        limits.maxStorageBufferBindingSize =
            yetty_min_u64(max_storage_binding_size, adapter_limits.maxStorageBufferBindingSize);
        limits.maxBufferSize = yetty_min_u64(max_buffer_size, adapter_limits.maxBufferSize);
        limits.maxStorageBuffersPerShaderStage = yetty_min_u32(
            max_storage_buffers_per_shader_stage, adapter_limits.maxStorageBuffersPerShaderStage);

        ydebug("ywebgpu: requesting limits maxTextureDimension2D=%u "
               "maxStorageBufferBindingSize=%llu maxBufferSize=%llu "
               "maxStorageBuffersPerShaderStage=%u",
               limits.maxTextureDimension2D, (unsigned long long)limits.maxStorageBufferBindingSize,
               (unsigned long long)limits.maxBufferSize, limits.maxStorageBuffersPerShaderStage);
    } else {
        /* Without adapter maxima to clamp against, requesting concrete
         * values could exceed what the adapter supports and reject the
         * whole device request — leave everything at the adapter
         * default instead. */
        ywarn("ywebgpu: adapter limits unavailable; requesting default device limits");
    }

    *out = limits;
}
