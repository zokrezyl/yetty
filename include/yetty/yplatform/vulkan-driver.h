/*
 * yplatform/vulkan-driver.h - bundled software Vulkan driver registration.
 *
 * Windows ships neither a Vulkan loader nor any Vulkan driver; its only
 * built-in software rasterizer is D3D12's WARP, which is far slower for
 * yetty's workload than Mesa lavapipe's LLVM JIT. The Windows build
 * therefore bundles lavapipe (vulkan_lvp.dll + lvp_icd.x86_64.json) and
 * the Khronos loader (vulkan-1.dll) next to each GPU-opening exe (see
 * build-tools/yetty/mesa-lavapipe.cmake), and yframework's adapter
 * recovery prefers it over WARP when no hardware adapter exists.
 *
 * Every other platform gets its Vulkan (or Metal) drivers from the OS,
 * so the non-Windows implementation is a no-op.
 */

#ifndef YETTY_YPLATFORM_VULKAN_DRIVER_H
#define YETTY_YPLATFORM_VULKAN_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the bundled software Vulkan driver (if any) with the platform's
 * Vulkan loader. Must run BEFORE the first WebGPU adapter request: the
 * loader latches its driver list when Dawn's Vulkan backend first
 * initializes it. Safe to call multiple times.
 *
 * Windows: if lvp_icd.x86_64.json sits next to the running exe and the
 * user has not configured the loader themselves (VK_ADD_DRIVER_FILES /
 * VK_DRIVER_FILES / VK_ICD_FILENAMES), export VK_ADD_DRIVER_FILES pointing
 * at it. ADD semantics: a hardware Vulkan ICD still enumerates and, being
 * non-CPU, outranks lavapipe in adapter selection — inert on machines with
 * working GPU drivers.
 *
 * Elsewhere: no-op. */
void yetty_yplatform_vulkan_register_bundled_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_VULKAN_DRIVER_H */
