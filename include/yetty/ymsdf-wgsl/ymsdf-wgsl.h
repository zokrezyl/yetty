#ifndef YETTY_YMSDF_WGSL_H
#define YETTY_YMSDF_WGSL_H

/*
 * ymsdf-wgsl - GPU MSDF glyph CDB generation via a WebGPU compute shader.
 *
 * Mirrors the API surface of ymsdf-gen (cpu) but routes glyph outline
 * decomposition (FreeType) → packed buffers → compute shader → RGBA32Float
 * storage texture → readback → CDB writer. Output CDB layout is
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
     * `device`'s queue and pumps `instance` (via wgpuInstanceProcessEvents)
     * while waiting for completion + buffer mapping. */
    void *device;   /* WGPUDevice — opaque to keep webgpu.h out */
    void *instance; /* WGPUInstance */

    /* Path to the compute shader (msdf_gen.wgsl). NULL ⇒ try
     * <exe_dir>/msdf_gen.wgsl, then <exe_dir>/shaders/msdf_gen.wgsl,
     * then "shaders/msdf_gen.wgsl". */
    const char *shader_path;
};

/* Generate a CDB at config->cdb_path. Walks every codepoint in the font's
 * cmap. Caller owns the WGPUDevice/Instance — they're not destroyed here. */
struct yetty_ycore_void_result yetty_ymsdf_wgsl_config_generate(
    const struct yetty_ymsdf_wgsl_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSDF_WGSL_H */
