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
 * adapter default"). The fields yetty actively cares about are requested
 * explicitly, and every requested value is clamped to the adapter-supported
 * maximum so the device request cannot be rejected for asking too much:
 *
 *   - maxTextureDimension2D          : min(wanted, adapter), default 16384
 *   - maxStorageBufferBindingSize    : min(wanted, adapter), default 512 MiB
 *   - maxBufferSize                  : min(wanted, adapter), default 1 GiB
 *   - maxStorageBuffersPerShaderStage: min(wanted, adapter), default 10
 *
 * The wanted values come from `config` via the YETTY_YCONFIG_KEY_GPU_* keys
 * declared in <yetty/yconfig/config.h> (sizes in MiB); a missing key keeps
 * the built-in default listed above. If the adapter limits cannot be
 * queried, all fields stay UNDEFINED (pure adapter defaults).
 */
void yetty_ywebgpu_fill_default_limits(WGPUAdapter adapter,
                                       const struct yetty_yconfig_config *config, WGPULimits *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWEBGPU_LIMITS_H */
