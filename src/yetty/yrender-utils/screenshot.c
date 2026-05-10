/*
 * screenshot.c - GPU texture readback to a binary PPM (P6) file.
 *
 * Mirrors the readback half of tile-diff.c: encode CopyTextureToBuffer into
 * a row-aligned mappable buffer, queue submit, await the buffer map inside
 * a coroutine, then walk the rows and dump RGB triplets to a file.
 *
 * Format choice: PPM keeps the module free of external image-codec deps.
 * We swizzle BGRA -> RGB on the fly while writing rows.
 */

#include <yetty/yrender-utils/screenshot.h>

#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct screenshot_args {
    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_yplatform_wgpu *wgpu;
    WGPUTexture texture;
    char *path;
};

/* Returns 0 = unsupported; 1 = swizzle BGRA->RGB; 2 = pass through RGBA->RGB. */
static int format_kind(WGPUTextureFormat fmt)
{
    switch (fmt) {
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
        return 1;
    case WGPUTextureFormat_RGBA8Unorm:
    case WGPUTextureFormat_RGBA8UnormSrgb:
        return 2;
    default:
        return 0;
    }
}

static struct yetty_ycore_void_result write_ppm(const char *path, const uint8_t *pixels,
                                                uint32_t width, uint32_t height,
                                                uint32_t aligned_bytes_per_row, int kind)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "screenshot: fopen failed");
    }

    if (fprintf(f, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "screenshot: header write failed");
    }

    /* Row buffer: 3 bytes per pixel after swizzle. */
    uint8_t *row = malloc((size_t)width * 3);
    if (!row) {
        fclose(f);
        return YETTY_ERR(yetty_ycore_void, "screenshot: row alloc failed");
    }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *src = pixels + (size_t)y * aligned_bytes_per_row;
        if (kind == 1) {
            /* BGRA -> RGB */
            for (uint32_t x = 0; x < width; x++) {
                row[x * 3 + 0] = src[x * 4 + 2];
                row[x * 3 + 1] = src[x * 4 + 1];
                row[x * 3 + 2] = src[x * 4 + 0];
            }
        } else {
            /* RGBA -> RGB */
            for (uint32_t x = 0; x < width; x++) {
                row[x * 3 + 0] = src[x * 4 + 0];
                row[x * 3 + 1] = src[x * 4 + 1];
                row[x * 3 + 2] = src[x * 4 + 2];
            }
        }
        if (fwrite(row, 1, (size_t)width * 3, f) != (size_t)width * 3) {
            free(row);
            fclose(f);
            return YETTY_ERR(yetty_ycore_void, "screenshot: row write failed");
        }
    }

    free(row);
    if (fclose(f) != 0) {
        return YETTY_ERR(yetty_ycore_void, "screenshot: fclose failed");
    }
    return YETTY_OK_VOID();
}

static void screenshot_coro_entry(void *arg)
{
    struct screenshot_args *args = arg;
    WGPUDevice device = args->device;
    WGPUQueue queue = args->queue;
    struct yetty_yplatform_wgpu *wgpu = args->wgpu;
    WGPUTexture texture = args->texture;
    char *path = args->path;
    free(args);

    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    WGPUTextureFormat format = wgpuTextureGetFormat(texture);

    int kind = format_kind(format);
    if (!kind) {
        ywarn("screenshot: unsupported texture format %d, skipping %s", (int)format, path);
        free(path);
        return;
    }

    /* WebGPU requires bytesPerRow to be a multiple of 256. */
    uint32_t aligned_bytes_per_row = (width * 4 + 255) & ~255u;
    uint64_t buf_size = (uint64_t)aligned_bytes_per_row * height;

    WGPUBufferDescriptor buf_desc = {0};
    buf_desc.size = buf_size;
    buf_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(device, &buf_desc);
    if (!readback) {
        ywarn("screenshot: failed to create readback buffer");
        free(path);
        return;
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    if (!encoder) {
        wgpuBufferRelease(readback);
        ywarn("screenshot: failed to create command encoder");
        free(path);
        return;
    }

    WGPUTexelCopyTextureInfo src = {0};
    src.texture = texture;
    WGPUTexelCopyBufferInfo dst = {0};
    dst.buffer = readback;
    dst.layout.bytesPerRow = aligned_bytes_per_row;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D extent = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    struct yetty_ycore_void_result map_res =
        yetty_yplatform_wgpu_buffer_map_await(wgpu, readback, WGPUMapMode_Read, 0, buf_size);
    if (!YETTY_IS_OK(map_res)) {
        ywarn("screenshot: buffer map failed: %s", map_res.error.msg);
        yetty_ycore_error_destroy(map_res.error);
        wgpuBufferRelease(readback);
        free(path);
        return;
    }

    const uint8_t *pixels = wgpuBufferGetConstMappedRange(readback, 0, buf_size);
    if (!pixels) {
        ywarn("screenshot: GetConstMappedRange returned NULL");
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        free(path);
        return;
    }

    struct yetty_ycore_void_result wr =
        write_ppm(path, pixels, width, height, aligned_bytes_per_row, kind);
    if (!YETTY_IS_OK(wr)) {
        ywarn("screenshot: write failed: %s (path=%s)", wr.error.msg, path);
        yetty_ycore_error_destroy(wr.error);
    } else {
        yinfo("screenshot: saved %ux%u to %s", width, height, path);
    }

    wgpuBufferUnmap(readback);
    wgpuBufferRelease(readback);
    free(path);
}

struct yetty_ycore_void_result yetty_yrender_utils_screenshot_capture(
    WGPUDevice device, WGPUQueue queue, struct yetty_yplatform_wgpu *wgpu, WGPUTexture texture,
    const char *path)
{
    if (!device || !queue || !wgpu || !texture || !path || !*path) {
        return YETTY_ERR(yetty_ycore_void, "screenshot: invalid arguments");
    }

    if (!format_kind(wgpuTextureGetFormat(texture))) {
        return YETTY_ERR(yetty_ycore_void, "screenshot: unsupported texture format");
    }

    struct screenshot_args *args = malloc(sizeof(*args));
    if (!args) {
        return YETTY_ERR(yetty_ycore_void, "screenshot: malloc failed");
    }
    args->device = device;
    args->queue = queue;
    args->wgpu = wgpu;
    args->texture = texture;
    size_t path_len = strlen(path);
    args->path = malloc(path_len + 1);
    if (!args->path) {
        free(args);
        return YETTY_ERR(yetty_ycore_void, "screenshot: path alloc failed");
    }
    memcpy(args->path, path, path_len + 1);

    struct yplatform_coro_ptr_result coro_res =
        yetty_yplatform_coro_spawn(screenshot_coro_entry, args, 0, "screenshot");
    if (!YETTY_IS_OK(coro_res)) {
        free(args->path);
        free(args);
        return YETTY_ERR(yetty_ycore_void, "screenshot: coro spawn failed", coro_res);
    }

    yetty_yplatform_coro_resume(coro_res.value);
    if (yetty_yplatform_coro_is_finished(coro_res.value)) {
        yetty_yplatform_coro_destroy(coro_res.value);
    }
    /* Otherwise the coro yielded into the GPU await; the await machinery
     * destroys it once it finishes. Same pattern as tile-diff_engine_submit. */

    return YETTY_OK_VOID();
}
