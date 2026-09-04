#ifndef YETTY_YMSDF_WGSL_H
#define YETTY_YMSDF_WGSL_H

/*
 * ymsdf-wgsl - GPU MSDF glyph CDB generation via a WebGPU compute shader.
 *
 * Mirrors the API surface of ymsdf-gen (cpu) but routes glyph outline
 * decomposition (FreeType) → packed buffers → one compute dispatch over the
 * atlas → RGBA8 storage texture → readback → CDB writer. Output CDB layout is
 * byte-compatible with the CPU generator: per-glyph
 * `struct yetty_ymsdf_wgsl_glyph_header` followed by RGBA8 pixel data.
 *
 * The compute shader (shaders/msdf_gen.wgsl) handles per-pixel SDF
 * evaluation. Quality is WIP — straight-edge glyphs render cleanly,
 * curves still show artefacts. Trade-off accepted: GPU generation is
 * dramatically faster than the CPU msdfgen path for documents that embed
 * many fonts (PDFs commonly do), and slow CDB generation is a worse user
 * experience than glyph artefacts that we can fix later in the shader.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CDB value layout per glyph. Same byte layout as ymsdf-gen (cpu) so the
 * downstream msdf-font reader doesn't care which generator built the file. */
struct yetty_ymsdf_wgsl_glyph_header {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    float bearing_x;
    float bearing_y;
    float size_x;
    float size_y;
    float advance;
    /* followed by width * height * 4 bytes RGBA8 */
};

struct yetty_ymsdf_wgsl_config {
    const char *ttf_path; /* input TTF (required) */
    const char *cdb_path; /* output CDB full path (required) */
    float font_size;      /* default 32 px */
    float pixel_range;    /* default 4 px */

    /* WGPU resources to reuse. Both required. The generator submits to
     * `device`'s queue and blocks in wgpuInstanceWaitAny on `instance`
     * while waiting for completion + buffer mapping. `instance` must be
     * created with the WGPUInstanceFeatureName_TimedWaitAny feature. */
    void *device;   /* WGPUDevice — opaque to keep webgpu.h out */
    void *instance; /* WGPUInstance */

    /* Path to the compute shader (msdf_gen.wgsl). NULL ⇒ try
     * <exe_dir>/msdf_gen.wgsl, then <exe_dir>/shaders/msdf_gen.wgsl,
     * then "shaders/msdf_gen.wgsl". */
    const char *shader_path;
};

/* Generate a CDB at config->cdb_path. Walks every codepoint in the font's
 * cmap. Caller owns the WGPUDevice/Instance — they're not destroyed here.
 * Equivalent to the staged calls below, with a pipeline built for the call. */
struct yetty_ycore_void_result yetty_ymsdf_wgsl_config_generate(
    const struct yetty_ymsdf_wgsl_config *config);

/*
 * The compiled compute pipeline for one device. Building it costs tens of
 * milliseconds, so a caller generating several fonts keeps one and reuses
 * it. device/instance are borrowed; shader_path as in the config. Use only
 * from the device's thread.
 */
struct yetty_ymsdf_wgsl_pipeline;
YETTY_YRESULT_DECLARE(yetty_ymsdf_wgsl_pipeline_ptr, struct yetty_ymsdf_wgsl_pipeline *);

struct yetty_ymsdf_wgsl_pipeline_ptr_result yetty_ymsdf_wgsl_pipeline_create(
    void *device, void *instance, const char *shader_path);
void yetty_ymsdf_wgsl_pipeline_destroy(struct yetty_ymsdf_wgsl_pipeline *pipeline);

/*
 * Staged generation, for batches: the CPU stages of several fonts can run
 * concurrently on their own threads while only the device stages need the
 * device's thread — and submitting every font before reading any back lets
 * the GPU work on the next font while the CPU copies the previous one.
 *   prepare   CPU only, any thread. Reads the font, serialises every glyph
 *             outline, lays out the atlas. Copies the config's strings.
 *   submit    the device's thread: atlas texture, uploads, ONE compute
 *             dispatch over the atlas and the copy-out, submitted; returns
 *             before the GPU finishes.
 *   readback  the device's thread: waits for that font's copy-out and reads
 *             the atlas back, releasing the GPU objects.
 *   write     any thread: writes config->cdb_path.
 * render = submit + readback. A job is single-use; destroy it after write
 * (or after any failure).
 */
struct yetty_ymsdf_wgsl_job;
YETTY_YRESULT_DECLARE(yetty_ymsdf_wgsl_job_ptr, struct yetty_ymsdf_wgsl_job *);

struct yetty_ymsdf_wgsl_job_ptr_result yetty_ymsdf_wgsl_job_prepare(
    const struct yetty_ymsdf_wgsl_config *config);
struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_submit(
    struct yetty_ymsdf_wgsl_job *job, struct yetty_ymsdf_wgsl_pipeline *pipeline);
struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_readback(struct yetty_ymsdf_wgsl_job *job);
struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_render(
    struct yetty_ymsdf_wgsl_job *job, struct yetty_ymsdf_wgsl_pipeline *pipeline);
struct yetty_ycore_void_result yetty_ymsdf_wgsl_job_write(struct yetty_ymsdf_wgsl_job *job);
void yetty_ymsdf_wgsl_job_destroy(struct yetty_ymsdf_wgsl_job *job);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSDF_WGSL_H */
