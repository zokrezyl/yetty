#ifndef YETTY_YFSVM_SHADER_RS_H
#define YETTY_YFSVM_SHADER_RS_H

/* yfsvm shader resource set — server-side bridge between the yfsvm compiler
 * and the ydraw GPU pipeline. Lives in yetty_yfsvm (not yetty_yfsvm_core)
 * because the embedded yfsvm.gen.wgsl shader is only meaningful when the
 * full GPU stack is available.
 */

#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Static shader-only resource set used by ydraw layer to include yfsvm_execute. */
const struct yetty_yrender_gpu_resource_set *yetty_yfsvm_get_shader_resource_set(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFSVM_SHADER_RS_H */
