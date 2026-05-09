#ifndef YETTY_YWEBGPU_LIMITS_H
#define YETTY_YWEBGPU_LIMITS_H

#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;

/*
 * Fill `out` with a sensible default WGPULimits descriptor for a yetty device.
 *
 * Most fields are set to WGPU_LIMIT_U{32,64}_UNDEFINED ("don't care, take the
 * adapter default"). The fields yetty actively cares about are clamped down
 * to whatever the adapter actually supports:
 *
 *   - maxTextureDimension2D          : min(16384, adapter)
 *   - maxStorageBufferBindingSize    : min(512 MiB, adapter)
 *   - maxBufferSize                  : min(1 GiB, adapter)
 *   - maxStorageBuffersPerShaderStage: 10
 *
 * NOTE: `config` is currently IGNORED. The plan is to source these knobs from
 * the yetty config file instead of hardcoding them here.
 *   See: https://github.com/zokrezyl/yetty/issues/138
 */
void yetty_ywebgpu_fill_default_limits(WGPUAdapter adapter,
                                       const struct yetty_yconfig_config *config, WGPULimits *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_LIMITS_H */
