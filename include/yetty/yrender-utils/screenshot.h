/*
 * screenshot.h - Capture a rendered WebGPU texture to a file on disk.
 *
 * One-shot helper: spawns a coroutine that issues CopyTextureToBuffer for
 * the supplied texture, awaits the readback through the shared wgpu await
 * machinery, and writes the pixels out as a binary PPM (P6) file. PPM is
 * used so the module stays free of external image-codec dependencies.
 *
 * Triggered today by a YETTY_YCORE_SCREENSHOT event-loop event; later the
 * same path will be wired to a keyboard shortcut.
 */

#ifndef YETTY_YRENDER_UTILS_SCREENSHOT_H
#define YETTY_YRENDER_UTILS_SCREENSHOT_H

#include <yetty/ycore/result.h>
#include <yetty/yplatform/ywebgpu.h>

#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Capture `texture` (must have been created with TextureUsage_CopySrc, which
 * the standard render target sets) and write it as a binary PPM (P6) file to
 * `path`. Spawns a coroutine that runs the GPU readback asynchronously; this
 * call returns as soon as the GPU work has been submitted. The actual file
 * write happens once the buffer map completes on the loop thread.
 *
 * Borrows `device`, `queue`, `wgpu`. `path` is copied internally.
 *
 * Supported texture formats: BGRA8Unorm[Srgb], RGBA8Unorm[Srgb]. Anything
 * else returns an error.
 */
struct yetty_ycore_void_result yetty_yrender_utils_screenshot_capture(
    WGPUDevice device, WGPUQueue queue, struct yetty_yplatform_wgpu *wgpu, WGPUTexture texture,
    const char *path);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_UTILS_SCREENSHOT_H */
